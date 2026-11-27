#include "nav/grid_surface.hpp"

#include "entity/nav_grid.hpp"
#include "spatial/obstacle_field.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace avgen::nav {

const char* navBackendName(NavBackend backend) {
    switch (backend) {
    case NavBackend::Grid: return "grid";
    case NavBackend::NavMesh: return "navmesh";
    }
    return "?";
}

const char* navStatusName(NavStatus status) {
    switch (status) {
    case NavStatus::Ok: return "ok";
    case NavStatus::AlreadyThere: return "alreadyThere";
    case NavStatus::NoSurface: return "noSurface";
    case NavStatus::NoStart: return "noStart";
    case NavStatus::NoGoal: return "noGoal";
    case NavStatus::Unreachable: return "unreachable";
    case NavStatus::Exhausted: return "exhausted";
    }
    return "?";
}

namespace {

std::uint64_t fnv(std::uint64_t h, const void* data, std::size_t n) {
    const auto* b = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < n; ++i) {
        h ^= b[i];
        h *= 1099511628211ull;
    }
    return h;
}

NavStatus fromPathStatus(entity::PathStatus s) {
    switch (s) {
    case entity::PathStatus::Ok: return NavStatus::Ok;
    case entity::PathStatus::AlreadyThere: return NavStatus::AlreadyThere;
    case entity::PathStatus::NoGraph: return NavStatus::NoSurface;
    case entity::PathStatus::NoStart: return NavStatus::NoStart;
    case entity::PathStatus::NoGoal: return NavStatus::NoGoal;
    case entity::PathStatus::Unreachable: return NavStatus::Unreachable;
    case entity::PathStatus::SearchExhausted: return NavStatus::Exhausted;
    }
    return NavStatus::NoSurface;
}

// Whether a cell's ground is standable whatever stands on it (`NavTerrain`, ADR-295).
bool terrainOk(const entity::NavGrid& g, glm::ivec2 c) {
    return g.inside(c) && (g.at(c).flags & entity::NavTerrain) != 0;
}

} // namespace

GridNavSurface::GridNavSurface(entity::Navigator navigator) : nav_(std::move(navigator)) {
    std::uint64_t h = 1469598103934665603ull;
    if (const entity::NavGrid* g = nav_.grid()) {
        const auto cells = g->cells();
        h = fnv(h, cells.data(), cells.size_bytes());
        const float cs = g->cellSize();
        h = fnv(h, &cs, sizeof cs);
        const glm::vec2 o = g->centerOf({0, 0});
        h = fnv(h, &o, sizeof o);
    }
    if (const spatial::ObstacleField* f = nav_.obstacles()) {
        const std::uint64_t oh = f->contentHash();
        h = fnv(h, &oh, sizeof oh);
    }
    key_ = h;
}

bool GridNavSurface::valid() const { return nav_.valid() && nav_.grid() != nullptr && nav_.grid()->valid(); }

NavPath GridNavSurface::findPath(glm::vec2 from, glm::vec2 to, float tolerance) const {
    NavPath out;
    const entity::NavGrid* g = nav_.grid();
    if (g == nullptr || !g->valid()) {
        out.status = NavStatus::NoSurface;
        return out;
    }
    entity::PathRequest request;
    request.from = from;
    request.to = to;
    request.goalTolerance = tolerance;
    entity::PathResult r = g->path(request);
    out.status = fromPathStatus(r.status);
    out.points = std::move(r.waypoints);
    out.goal = r.goal;
    out.length = r.length;
    out.expansions = static_cast<std::uint32_t>(r.expansions);
    return out;
}

bool GridNavSurface::ground(glm::vec2 p, float& y) const {
    if (!nav_.valid()) {
        return false;
    }
    if (const entity::NavGrid* g = nav_.grid(); g != nullptr && g->valid()) {
        y = g->groundAt(p);
        return true;
    }
    y = nav_.groundHeight(p);
    return true;
}

void GridNavSurface::staticObstacles(glm::vec2 p, float range, std::vector<NavDisc>& discs,
                                     std::vector<NavSegment>& segments) const {
    if (const spatial::ObstacleField* f = nav_.obstacles(); f != nullptr && !f->empty()) {
        hits_.clear();
        f->query(p, range, hits_);
        std::sort(hits_.begin(), hits_.end()); // a fixed order, whatever the index returned
        for (const std::uint32_t i : hits_) {
            const spatial::NavigationObstacle& o = f->obstacles()[i];
            if (!o.blocking() || o.height < nav_.settings().stepOver) {
                continue;
            }
            discs.push_back(NavDisc{o.center, o.radius});
        }
    }
    const entity::NavGrid* g = nav_.grid();
    if (g == nullptr || !g->valid()) {
        return;
    }
    const float cs = g->cellSize();
    const glm::ivec2 lo = g->cellOf(p - glm::vec2(range));
    const glm::ivec2 hi = g->cellOf(p + glm::vec2(range));
    const glm::ivec2 dir[4] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (int z = lo.y; z <= hi.y; ++z) {
        for (int x = lo.x; x <= hi.x; ++x) {
            const glm::ivec2 c{x, z};
            if (!terrainOk(*g, c)) {
                continue;
            }
            const glm::vec2 m = g->centerOf(c);
            for (const glm::ivec2& d : dir) {
                if (terrainOk(*g, c + d)) {
                    continue;
                }
                // The face of this standable cell toward the unstandable one.
                const glm::vec2 n(d);
                const glm::vec2 t(-n.y, n.x);
                const glm::vec2 mid = m + n * (0.5f * cs);
                segments.push_back(NavSegment{mid - t * (0.5f * cs), mid + t * (0.5f * cs)});
            }
        }
    }
}

bool GridNavSurface::lineClear(glm::vec2 a, glm::vec2 b, float radius) const {
    if (const spatial::ObstacleField* f = nav_.obstacles(); f != nullptr && !f->empty()) {
        spatial::ObstacleFilter filter = nav_.filter(0.0f);
        filter.bodyRadius = radius;
        filter.footY = 0.0f;
        filter.headHeight = 0.0f; // every blocking solid counts, whatever its base
        if (f->segmentBlocked(a, b, filter)) {
            return false;
        }
    }
    const entity::NavGrid* g = nav_.grid();
    if (g == nullptr || !g->valid()) {
        return true;
    }
    const float len = glm::length(b - a);
    const float step = std::max(0.05f, 0.5f * g->cellSize());
    const int n = std::max(1, static_cast<int>(std::ceil(len / step)));
    for (int i = 0; i <= n; ++i) {
        const glm::vec2 q = a + (b - a) * (static_cast<float>(i) / static_cast<float>(n));
        if (!terrainOk(*g, g->cellOf(q))) {
            return false;
        }
    }
    return true;
}

} // namespace avgen::nav
