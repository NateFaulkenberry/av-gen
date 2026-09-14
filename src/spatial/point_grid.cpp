#include "spatial/point_grid.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::spatial {
namespace {

// Cell coordinates are signed and unbounded, so they are mixed into a table slot rather than used
// as an array index. Three large odd multipliers, which is the usual spatial hash; the table is a
// power of two so the mask is the modulo.
std::size_t hashCell(std::int32_t x, std::int32_t y, std::int32_t z) {
    const auto ux = static_cast<std::uint64_t>(static_cast<std::uint32_t>(x));
    const auto uy = static_cast<std::uint64_t>(static_cast<std::uint32_t>(y));
    const auto uz = static_cast<std::uint64_t>(static_cast<std::uint32_t>(z));
    std::uint64_t h = ux * 0x9E3779B97F4A7C15ULL;
    h ^= uy * 0xC2B2AE3D27D4EB4FULL;
    h ^= uz * 0x165667B19E3779F9ULL;
    h ^= h >> 29;
    h *= 0xBF58476D1CE4E5B9ULL;
    h ^= h >> 32;
    return static_cast<std::size_t>(h);
}

std::size_t roundUpPowerOfTwo(std::size_t n) {
    std::size_t p = 16;
    while (p < n) {
        p <<= 1U;
    }
    return p;
}

} // namespace

glm::ivec3 PointGrid::cellOf(const glm::vec3& p) const {
    return {static_cast<std::int32_t>(std::floor(p.x * invCell_)),
            static_cast<std::int32_t>(std::floor(p.y * invCell_)),
            static_cast<std::int32_t>(std::floor(p.z * invCell_))};
}

std::size_t PointGrid::slotOf(std::int32_t x, std::int32_t y, std::int32_t z) const {
    std::size_t slot = hashCell(x, y, z) & mask_;
    // Linear probing. The table is at least twice the cell count, so this terminates.
    while (cells_[slot].used && (cells_[slot].x != x || cells_[slot].y != y || cells_[slot].z != z)) {
        slot = (slot + 1) & mask_;
    }
    return slot;
}

void PointGrid::build(std::span<const glm::vec3> points, float cellSize) {
    cellSize_ = std::max(cellSize, 1e-4f);
    invCell_ = 1.0f / cellSize_;
    points_.assign(points.begin(), points.end());
    order_.clear();
    cells_.clear();
    mask_ = 0;
    if (points_.empty()) {
        return;
    }

    // Table sized for the worst case of one cell per point, at load factor 0.5 or better. Sizing on
    // the point count rather than the distinct cell count costs memory and buys a single pass: the
    // distinct count is not known until the points have been hashed, and hashing them twice to find
    // out is the more expensive half.
    cells_.assign(roundUpPowerOfTwo(points_.size() * 2), Cell{});
    mask_ = cells_.size() - 1;

    // Pass 1: claim a slot per distinct cell and count its members in `end`.
    for (const glm::vec3& p : points_) {
        const glm::ivec3 c = cellOf(p);
        const std::size_t slot = slotOf(c.x, c.y, c.z);
        Cell& cell = cells_[slot];
        if (!cell.used) {
            cell.used = true;
            cell.x = c.x;
            cell.y = c.y;
            cell.z = c.z;
        }
        ++cell.end;
    }

    // Pass 2: prefix-sum the counts into ranges. The scan runs over the table in slot order, which
    // is deterministic for a given point set, so the layout is too.
    std::uint32_t running = 0;
    for (Cell& cell : cells_) {
        if (!cell.used) {
            continue;
        }
        const std::uint32_t count = cell.end;
        cell.begin = running;
        cell.end = running; // becomes the write cursor, and ends up as the true end
        running += count;
    }

    // Pass 3: scatter the point indices. Within a cell they land in `points` order.
    order_.resize(points_.size());
    for (std::uint32_t i = 0; i < points_.size(); ++i) {
        const glm::ivec3 c = cellOf(points_[i]);
        Cell& cell = cells_[slotOf(c.x, c.y, c.z)];
        order_[cell.end++] = i;
    }
}

void PointGrid::query(const glm::vec3& p, float radius, std::vector<std::uint32_t>& out) const {
    out.clear();
    if (points_.empty() || radius <= 0.0f) {
        return;
    }
    const float r2 = radius * radius;
    const glm::ivec3 lo = cellOf(p - glm::vec3(radius));
    const glm::ivec3 hi = cellOf(p + glm::vec3(radius));
    for (std::int32_t z = lo.z; z <= hi.z; ++z) {
        for (std::int32_t y = lo.y; y <= hi.y; ++y) {
            for (std::int32_t x = lo.x; x <= hi.x; ++x) {
                const Cell& cell = cells_[slotOf(x, y, z)];
                if (!cell.used) {
                    continue;
                }
                for (std::uint32_t k = cell.begin; k < cell.end; ++k) {
                    const std::uint32_t index = order_[k];
                    const glm::vec3 d = points_[index] - p;
                    if (glm::dot(d, d) <= r2) {
                        out.push_back(index);
                    }
                }
            }
        }
    }
}

} // namespace avgen::spatial
