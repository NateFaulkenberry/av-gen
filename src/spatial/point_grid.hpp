#pragma once

// A uniform 3D hash grid for radius queries over a point set.
//
// Why this exists: nothing in the repository answers "which of these 40,000 points lie within r of
// p" in three dimensions. `ObstacleField` is the same shape but XZ-only and models everything as a
// vertical cylinder, and `FilterDistance` in spatial_ops is a single-pivot O(n) scan. The space
// colonization step in the tree generator asks that question once per bud per iteration, which is
// O(buds * markers) without an index and finishes in a blink with one.
//
// It is deliberately the same shape as `ObstacleField`: build once from a snapshot, then query.
// There is no removal API because the caller that needs one (markers die as the tree colonises
// them) is better served by rebuilding from the surviving set -- rebuilding is two linear passes,
// and an index that supports deletion has to answer awkward questions about whether a query made
// mid-deletion sees a consistent set.
//
// Determinism: cells live in a fixed-size open-addressed table keyed by the integer cell
// coordinate, and the point indices are laid out by a counting sort, so a query visits points in
// an order that is a pure function of (points, cellSize). No hash-table iteration order is ever
// observable, which is the property that makes a generator built on this reproducible.

#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace avgen::spatial {

class PointGrid {
public:
    // Indexes `points` in cells of `cellSize`. A query radius no larger than `cellSize` touches at
    // most 27 cells, which is the case this is tuned for; larger radii still work and cost more.
    // `cellSize` is clamped to a small positive value so a degenerate call cannot divide by zero.
    void build(std::span<const glm::vec3> points, float cellSize);

    // Replaces `out` with the indices of every indexed point within `radius` of `p`, in cell order
    // and then in the order the points were passed to `build`.
    void query(const glm::vec3& p, float radius, std::vector<std::uint32_t>& out) const;

    [[nodiscard]] std::size_t size() const { return points_.size(); }
    [[nodiscard]] bool empty() const { return points_.empty(); }
    [[nodiscard]] float cellSize() const { return cellSize_; }

private:
    struct Cell {
        std::int32_t x = 0, y = 0, z = 0;
        std::uint32_t begin = 0; // range in order_
        std::uint32_t end = 0;
        bool used = false;
    };

    [[nodiscard]] glm::ivec3 cellOf(const glm::vec3& p) const;
    // Index of the table slot holding (x, y, z), or the slot it would occupy. Linear probing.
    [[nodiscard]] std::size_t slotOf(std::int32_t x, std::int32_t y, std::int32_t z) const;

    std::vector<glm::vec3> points_;
    std::vector<std::uint32_t> order_; // point indices grouped by cell
    std::vector<Cell> cells_;          // open-addressed, power-of-two sized, load factor <= 0.5
    std::size_t mask_ = 0;
    float cellSize_ = 1.0f;
    float invCell_ = 1.0f;
};

} // namespace avgen::spatial
