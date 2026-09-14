#include "spatial/obstacle_field.hpp"

#include <bit>
#include <cmath>
#include <limits>

namespace avgen::spatial {
namespace {

// Squared distance from `p` to the segment ab, and where along it the closest point fell. The
// closed-form version rather than a sampled one: sampling a segment against a disc is exactly the
// mistake that let a walker pass through a tree between two samples.
float distanceToSegmentSq(glm::vec2 p, glm::vec2 a, glm::vec2 b) {
    const glm::vec2 ab = b - a;
    const float lenSq = glm::dot(ab, ab);
    if (lenSq < 1e-12f) {
        const glm::vec2 d = p - a;
        return glm::dot(d, d);
    }
    float t = glm::dot(p - a, ab) / lenSq;
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    const glm::vec2 d = p - (a + ab * t);
    return glm::dot(d, d);
}

float segmentParam(glm::vec2 p, glm::vec2 a, glm::vec2 b) {
    const glm::vec2 ab = b - a;
    const float lenSq = glm::dot(ab, ab);
    if (lenSq < 1e-12f) {
        return 0.0f;
    }
    const float t = glm::dot(p - a, ab) / lenSq;
    return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
}

// FNV-1a over a bit pattern, so -0 and +0 hash alike and the hash is stable across machines.
void hashFloat(std::uint64_t& h, float v) {
    const float n = v == 0.0f ? 0.0f : v; // -0 and +0 are the same value and must hash alike
    const auto bits = std::bit_cast<std::uint32_t>(n);
    for (int i = 0; i < 4; ++i) {
        h = (h ^ static_cast<std::uint8_t>((bits >> (8 * i)) & 0xFFu)) * 0x100000001b3ULL;
    }
}

} // namespace

const char* traversalName(Traversal traversal) {
    switch (traversal) {
    case Traversal::Passable: return "passable";
    case Traversal::StepOver: return "step-over";
    case Traversal::Jumpable: return "jumpable";
    case Traversal::Blocking: return "blocking";
    }
    return "unknown";
}

const char* obstacleTypeName(ObstacleType type) {
    switch (type) {
    case ObstacleType::Vegetation: return "vegetation";
    case ObstacleType::Trunk: return "trunk";
    case ObstacleType::Rock: return "rock";
    case ObstacleType::Structure: return "structure";
    case ObstacleType::Creature: return "creature";
    case ObstacleType::Custom: return "custom";
    }
    return "unknown";
}

void ObstacleField::clear() {
    obstacles_.clear();
    starts_.clear();
    entries_.clear();
    blocking_ = 0;
    maxRadius_ = 0.0f;
    indexed_ = false;
    cellSize_ = 0.0f;
    origin_ = glm::ivec2(0);
    dims_ = glm::ivec2(0);
}

void ObstacleField::reserve(std::size_t count) { obstacles_.reserve(count); }

void ObstacleField::add(const NavigationObstacle& obstacle) {
    obstacles_.push_back(obstacle);
    if (obstacle.blocking()) {
        ++blocking_;
    }
    maxRadius_ = std::max(maxRadius_, obstacle.radius);
    // The index no longer describes the set. Queries fall back to a linear scan until `build()`
    // runs again, which is slow and correct rather than fast and wrong.
    indexed_ = false;
}

void ObstacleField::build(float cellSize) {
    starts_.clear();
    entries_.clear();
    indexed_ = false;
    if (obstacles_.empty()) {
        return;
    }
    // A cell wide enough that an obstacle spans at most two of them on an axis, and never so fine
    // that a sparse world pays for a million empty cells. Derived from the obstacles themselves
    // rather than configured, because the right answer is a property of what was put in.
    float cell = cellSize;
    if (cell <= 0.0f) {
        cell = std::max(4.0f, maxRadius_ * 2.5f);
    }
    glm::vec2 lo(std::numeric_limits<float>::max());
    glm::vec2 hi(std::numeric_limits<float>::lowest());
    for (const NavigationObstacle& o : obstacles_) {
        lo = glm::min(lo, o.center - o.radius);
        hi = glm::max(hi, o.center + o.radius);
    }
    cellSize_ = cell;
    origin_ = glm::ivec2(static_cast<int>(std::floor(lo.x / cell)), static_cast<int>(std::floor(lo.y / cell)));
    const glm::ivec2 last(static_cast<int>(std::floor(hi.x / cell)), static_cast<int>(std::floor(hi.y / cell)));
    dims_ = last - origin_ + glm::ivec2(1);
    // A degenerate set (one obstacle, or a world of unreasonable extent) must not allocate wildly.
    constexpr std::size_t kMaxCells = 4u << 20; // 4M cells, 16 MB of offsets: far past any real world
    const auto cells = static_cast<std::size_t>(dims_.x) * static_cast<std::size_t>(dims_.y);
    if (dims_.x <= 0 || dims_.y <= 0 || cells > kMaxCells) {
        return; // stays unindexed: correct, just linear
    }

    // Counting sort into CSR, one entry per obstacle, in the cell its *centre* falls in.
    //
    // Binning an obstacle into every cell its footprint touches is the obvious thing and it is
    // wrong here: a query would then be handed the same obstacle once per cell, and `resolve`
    // sums its overlaps -- so a body next to one rock was pushed out of it four times and shot
    // across the valley. Single-cell binning makes every query duplicate-free by construction
    // rather than by each caller remembering to dedupe. The cost is that a query must pad its
    // search box by `maxRadius_`, which it does, and which is exact: an obstacle overlapping a
    // disc of radius r at p has its centre within r + maxRadius_ of p.
    starts_.assign(cells + 1, 0u);
    const auto cellFor = [&](const NavigationObstacle& o) {
        const glm::ivec2 c = glm::clamp(cellOf(o.center), glm::ivec2(0), dims_ - 1);
        return static_cast<std::size_t>(c.y) * static_cast<std::size_t>(dims_.x) +
               static_cast<std::size_t>(c.x);
    };
    for (const NavigationObstacle& o : obstacles_) {
        ++starts_[cellFor(o) + 1];
    }
    for (std::size_t i = 1; i <= cells; ++i) {
        starts_[i] += starts_[i - 1];
    }
    entries_.resize(starts_[cells]);
    std::vector<std::uint32_t> cursor(starts_.begin(), starts_.end() - 1);
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(obstacles_.size()); ++i) {
        entries_[cursor[cellFor(obstacles_[i])]++] = i;
    }
    indexed_ = true;
}

glm::ivec2 ObstacleField::cellOf(glm::vec2 p) const {
    return glm::ivec2(static_cast<int>(std::floor(p.x / cellSize_)),
                      static_cast<int>(std::floor(p.y / cellSize_))) -
           origin_;
}

namespace {

// The one ladder. `traversalFor` and `relevant` are both this function, which is why it takes the
// class as an argument rather than reading it off the obstacle: `relevant` has to be able to ask
// "and what would this take if it *were* solid", which is what `includeNonBlocking` means.
Traversal traversalOf(Traversal cls, float base, float height, const ObstacleFilter& filter) {
    const float top = base + height;
    if (top < filter.footY + filter.stepOver) {
        return Traversal::StepOver; // short enough to step over, whatever it is
    }
    if (filter.headHeight > 0.0f && base > filter.footY + filter.headHeight) {
        return Traversal::Passable; // its foot is above the body's head: it ducks under
    }
    if (cls == Traversal::Jumpable && height <= filter.jumpOver) {
        return Traversal::Jumpable; // this body can clear one this tall
    }
    if (cls == Traversal::Passable) {
        return Traversal::Passable;
    }
    return Traversal::Blocking;
}

// Whether this obstacle is in the way of a walker described by `filter`. Separated out because
// every query below asks exactly this question and getting it subtly different in one of them is
// how "it avoids trees except when pathfinding" happens.
bool relevant(const NavigationObstacle& o, const ObstacleFilter& filter) {
    if (!o.blocking() && !filter.includeNonBlocking) {
        return false;
    }
    // A caller that asked for the non-solids wants the ones that are actually in its way, so they
    // are put through the ladder as though they were solid. Their own class already answered "no"
    // once; asking it twice would make `includeNonBlocking` return nothing.
    const Traversal cls = o.blocking() ? o.traversal : Traversal::Blocking;
    return traversalOf(cls, o.base, o.height, filter) == Traversal::Blocking;
}

} // namespace

Traversal traversalFor(const NavigationObstacle& o, const ObstacleFilter& filter) {
    return traversalOf(o.traversal, o.base, o.height, filter);
}

bool ObstacleField::isOccupied(float x, float z, float radius) const {
    const glm::vec2 p(x, z);
    const float pad = std::max(radius, 0.0f) + maxRadius_;
    bool hit = false;
    forEachNear(p - pad, p + pad, [&](std::uint32_t i) {
        if (hit) {
            return;
        }
        const NavigationObstacle& o = obstacles_[i];
        if (!o.blocking()) {
            return;
        }
        const glm::vec2 d = o.center - p;
        const float reach = o.radius + std::max(radius, 0.0f);
        if (glm::dot(d, d) < reach * reach) {
            hit = true;
        }
    });
    return hit;
}

bool ObstacleField::blocker(glm::vec2 p, const ObstacleFilter& filter, ObstacleHit& out) const {
    const float pad = filter.bodyRadius + maxRadius_;
    float deepest = 0.0f;
    bool found = false;
    forEachNear(p - pad, p + pad, [&](std::uint32_t i) {
        const NavigationObstacle& o = obstacles_[i];
        if (!relevant(o, filter)) {
            return;
        }
        const float reach = o.radius + filter.bodyRadius;
        const glm::vec2 d = o.center - p;
        const float distSq = glm::dot(d, d);
        if (distSq >= reach * reach) {
            return;
        }
        const float penetration = reach - std::sqrt(distSq);
        // Strictly greater, so the lowest index wins a tie and the answer never depends on the
        // order the grid happened to visit the cells in.
        if (!found || penetration > deepest) {
            found = true;
            deepest = penetration;
            out.index = i;
            out.penetration = penetration;
        }
    });
    return found;
}

Traversal ObstacleField::traversalAt(glm::vec2 p, const ObstacleFilter& filter,
                                     ObstacleHit& out) const {
    const float pad = filter.bodyRadius + maxRadius_;
    Traversal worst = Traversal::Passable;
    float deepest = 0.0f;
    out.index = static_cast<std::uint32_t>(obstacles_.size());
    out.penetration = 0.0f;
    forEachNear(p - pad, p + pad, [&](std::uint32_t i) {
        const NavigationObstacle& o = obstacles_[i];
        const float reach = o.radius + filter.bodyRadius;
        const glm::vec2 d = o.center - p;
        const float distSq = glm::dot(d, d);
        if (distSq >= reach * reach) {
            return;
        }
        const Traversal t = traversalFor(o, filter);
        if (t == Traversal::Passable) {
            return; // nothing to do about it, so it is not the answer
        }
        const float penetration = reach - std::sqrt(distSq);
        // Harder wins; within a class the deeper overlap wins; strictly greater, so a tie goes to
        // the lowest index and never to the order the grid visited the cells in.
        if (t > worst || (t == worst && penetration > deepest)) {
            worst = t;
            deepest = penetration;
            out.index = i;
            out.penetration = penetration;
        }
    });
    return worst;
}

float ObstacleField::clearance(glm::vec2 p, const ObstacleFilter& filter, float maxRange) const {
    const float range = std::max(maxRange, 0.0f);
    const float pad = filter.bodyRadius + maxRadius_ + range;
    float best = range;
    forEachNear(p - pad, p + pad, [&](std::uint32_t i) {
        const NavigationObstacle& o = obstacles_[i];
        if (!relevant(o, filter)) {
            return;
        }
        const float gap = glm::length(o.center - p) - (o.radius + filter.bodyRadius);
        best = std::min(best, gap);
    });
    return best;
}

bool ObstacleField::segmentBlocked(glm::vec2 a, glm::vec2 b, const ObstacleFilter& filter) const {
    ObstacleHit ignored;
    return segmentHit(a, b, filter, ignored);
}

bool ObstacleField::segmentHit(glm::vec2 a, glm::vec2 b, const ObstacleFilter& filter,
                               ObstacleHit& out) const {
    const float pad = filter.bodyRadius + maxRadius_;
    const glm::vec2 lo = glm::min(a, b) - pad;
    const glm::vec2 hi = glm::max(a, b) + pad;
    float nearest = std::numeric_limits<float>::max();
    bool found = false;
    // The segment's own bounding box rather than a walk along it. A diagonal crossing of the world
    // over-selects cells, but a walker's lookahead is a handful of metres and a planner's
    // string-pull candidate is tens, so the box is a couple of cells wide in every case that
    // happens -- and the alternative, a DDA with a dedupe set, costs more than it saves at that
    // size and adds mutable state to a query that is otherwise thread-safe.
    forEachNear(lo, hi, [&](std::uint32_t i) {
        const NavigationObstacle& o = obstacles_[i];
        if (!relevant(o, filter)) {
            return;
        }
        const float reach = o.radius + filter.bodyRadius;
        const float distSq = distanceToSegmentSq(o.center, a, b);
        if (distSq >= reach * reach) {
            return;
        }
        const float t = segmentParam(o.center, a, b);
        if (!found || t < nearest) {
            found = true;
            nearest = t;
            out.index = i;
            out.penetration = reach - std::sqrt(distSq);
        }
    });
    return found;
}

glm::vec2 ObstacleField::resolve(glm::vec2 p, const ObstacleFilter& filter) const {
    const float pad = filter.bodyRadius + maxRadius_;
    glm::vec2 push(0.0f);
    forEachNear(p - pad, p + pad, [&](std::uint32_t i) {
        const NavigationObstacle& o = obstacles_[i];
        if (!relevant(o, filter)) {
            return;
        }
        const float reach = o.radius + filter.bodyRadius;
        glm::vec2 d = p - o.center;
        const float distSq = glm::dot(d, d);
        if (distSq >= reach * reach) {
            return;
        }
        if (distSq < 1e-8f) {
            // Exactly on the axis. Any direction is as good as any other and none of them is
            // derivable from the geometry, so take one from the obstacle's own index: arbitrary,
            // but the same arbitrary answer every time this world is built.
            const float angle = static_cast<float>(i % 97u) * 0.06479f;
            d = glm::vec2(std::cos(angle), std::sin(angle));
            push += d * reach;
            return;
        }
        const float dist = std::sqrt(distSq);
        push += (d / dist) * (reach - dist);
    });
    return push;
}

void ObstacleField::query(glm::vec2 p, float radius, std::vector<std::uint32_t>& out,
                          bool includeNonBlocking) const {
    out.clear();
    const float pad = std::max(radius, 0.0f) + maxRadius_;
    forEachNear(p - pad, p + pad, [&](std::uint32_t i) {
        const NavigationObstacle& o = obstacles_[i];
        if (!o.blocking() && !includeNonBlocking) {
            return;
        }
        const glm::vec2 d = o.center - p;
        const float reach = o.radius + std::max(radius, 0.0f);
        if (glm::dot(d, d) < reach * reach) {
            out.push_back(i);
        }
    });
    // Already a set, and already in index order: obstacles are binned once each, and the traversal
    // visits cells in index order within a row. Sorted so the order is the obstacles' own rather
    // than the grid's, which is what makes two builds of a world agree.
    std::sort(out.begin(), out.end());
}

std::uint64_t ObstacleField::contentHash() const {
    std::uint64_t h = 0xcbf29ce484222325ULL;
    for (const NavigationObstacle& o : obstacles_) {
        hashFloat(h, o.center.x);
        hashFloat(h, o.center.y);
        hashFloat(h, o.radius);
        hashFloat(h, o.base);
        hashFloat(h, o.height);
        h = (h ^ static_cast<std::uint8_t>(o.type)) * 0x100000001b3ULL;
        // The traversal class, not merely whether it is solid: two worlds that agree on every
        // cylinder and disagree on which of them can be vaulted answer a jumper's queries
        // differently, and a hash that said they were the same world would be wrong for the one
        // caller -- a cache -- that this exists for.
        h = (h ^ static_cast<std::uint8_t>(o.traversal)) * 0x100000001b3ULL;
    }
    return h;
}

} // namespace avgen::spatial
