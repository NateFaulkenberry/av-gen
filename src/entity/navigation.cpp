#include "entity/navigation.hpp"

#include "entity/nav_grid.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace avgen::entity {
namespace {

constexpr float kTwoPi = 6.283185307179586f;

} // namespace

const char* navRejectName(NavReject reason) {
    switch (reason) {
    case NavReject::None: return "navigable";
    case NavReject::OutOfBounds: return "outside the world";
    case NavReject::TooSteep: return "too steep";
    case NavReject::Submerged: return "under water";
    case NavReject::NoHeadroom: return "no headroom";
    case NavReject::InsideHero: return "inside a hero";
    case NavReject::Step: return "step too high";
    case NavReject::Obstructed: return "blocked by a solid";
    }
    return "unknown";
}

Navigator::Navigator(const world::WorldMap* map, world::ClearanceField field, NavSettings settings)
    : map_(map), field_(field), settings_(settings) {
    query_.map = map;
    query_.clearance = field;
    query_.rules = settings_.walkRules();
}

glm::vec2 Navigator::worldMin() const {
    return map_ != nullptr ? map_->min() + settings_.boundaryMargin : glm::vec2(-64.0f);
}

glm::vec2 Navigator::worldMax() const {
    return map_ != nullptr ? map_->max() - settings_.boundaryMargin : glm::vec2(64.0f);
}

float Navigator::groundHeight(glm::vec2 p) const {
    return map_ != nullptr ? map_->height(p) : 0.0f;
}

glm::vec3 Navigator::groundNormal(glm::vec2 p, float epsilon) const {
    return map_ != nullptr ? map_->normal(p, std::max(epsilon, 0.05f)) : glm::vec3(0.0f, 1.0f, 0.0f);
}

float Navigator::canopyHeight(glm::vec2 p) const {
    return field_.canopyHeight(p);
}

NavSample Navigator::sample(glm::vec2 p) const {
    // One set of rules, asked once (§3, ADR-090). This used to be a second copy of the walkability
    // ladder -- bounds, slope, water, thicket, hero -- sitting beside `TerrainQuery::at` with the
    // same numbers and no mechanism keeping them in step. Now it is a translation: terrain answers
    // the ground, and navigation adds the one thing terrain deliberately cannot answer.
    const world::TerrainPoint t = query_.at(p);
    NavSample out;
    out.ground = t.height;
    out.slope = t.slope;
    out.normal = t.normal;
    out.waterSurface = t.waterSurface;
    out.canopy = t.canopy;
    switch (t.reject) {
    case world::TerrainReject::None: break;
    case world::TerrainReject::OutOfBounds: out.reject = NavReject::OutOfBounds; return out;
    case world::TerrainReject::TooSteep: out.reject = NavReject::TooSteep; return out;
    case world::TerrainReject::Submerged: out.reject = NavReject::Submerged; return out;
    case world::TerrainReject::NoHeadroom: out.reject = NavReject::NoHeadroom; return out;
    case world::TerrainReject::InsideHero: out.reject = NavReject::InsideHero; return out;
    case world::TerrainReject::Obstructed: out.reject = NavReject::Obstructed; return out;
    }
    // The per-instance test the canopy model cannot make. `canopy` above says "trees about fourteen
    // metres tall grow around here"; this says "and one of them is at this spot". Asked here rather
    // than through `TerrainQuery::isOccupied` because a walker has a body: it knows its own radius,
    // what it can step over and how much headroom it needs, and the shared query takes a radius and
    // nothing else.
    if (obstructed(p, t.height)) {
        out.reject = NavReject::Obstructed;
        return out;
    }
    out.navigable = true;
    return out;
}

spatial::ObstacleFilter Navigator::filter(float footY) const {
    spatial::ObstacleFilter f;
    f.bodyRadius = settings_.bodyRadius;
    f.stepOver = settings_.stepOver;
    f.footY = footY;
    f.headHeight = settings_.headroom;
    return f;
}

bool Navigator::obstructed(glm::vec2 p, float ground) const {
    if (obstacles_ == nullptr) {
        return false;
    }
    spatial::ObstacleHit hit;
    return obstacles_->blocker(p, filter(ground), hit);
}

float Navigator::clearanceAt(glm::vec2 p, float ground, float maxRange) const {
    if (obstacles_ == nullptr) {
        return maxRange;
    }
    return obstacles_->clearance(p, filter(ground), maxRange);
}

glm::vec2 Navigator::resolvePenetration(glm::vec2 p, float ground) const {
    if (obstacles_ == nullptr) {
        return glm::vec2(0.0f);
    }
    return obstacles_->resolve(p, filter(ground));
}

void Navigator::buildGrid(float cellSize) {
    auto grid = std::make_shared<NavGrid>();
    grid->build(*this, cellSize);
    grid_ = std::move(grid);
}

bool Navigator::findPath(glm::vec2 from, glm::vec2 to, std::vector<glm::vec2>& out) const {
    out.clear();
    if (grid_ != nullptr && grid_->valid()) {
        return grid_->findPath(from, to, out);
    }
    // No graph. Answer the only question a straight line can: is the straight line clear. Saying
    // "yes, one waypoint" here rather than "no path" is what keeps a scene that never built a grid
    // -- a test, a scene with no terrain -- behaving exactly as it did before this existed.
    if (!pathClear(from, to)) {
        return false;
    }
    out.push_back(to);
    return true;
}

bool Navigator::pickDestination(Rng& rng, glm::vec2 from, float minRadius, float maxRadius,
                                glm::vec2& out, int attempts) const {
    const float lo = std::max(0.0f, std::min(minRadius, maxRadius));
    const float hi = std::max(lo, maxRadius);
    for (int i = 0; i < std::max(attempts, 1); ++i) {
        const float angle = rng.range(0.0f, kTwoPi);
        // sqrt of the radius fraction, so points are uniform over the annulus rather than bunched
        // at its inner edge -- a wanderer that always chose the near ring would pace, not wander.
        const float t = rng.nextFloat();
        const float radius = std::sqrt(lo * lo + t * (hi * hi - lo * lo));
        const glm::vec2 candidate = from + glm::vec2(std::cos(angle), std::sin(angle)) * radius;
        if (sample(candidate).navigable) {
            out = candidate;
            return true;
        }
    }
    return false;
}

bool Navigator::pathClear(glm::vec2 a, glm::vec2 b, float spacing) const {
    // Solids first, and swept rather than sampled. A point test every couple of metres walks
    // straight through a half-metre trunk, which is precisely the defect this pass exists to fix:
    // the old `pathClear` could only ever have caught an obstacle it happened to land on.
    if (obstacles_ != nullptr) {
        const float ground = map_ != nullptr ? map_->height(a) : 0.0f;
        if (obstacles_->segmentBlocked(a, b, filter(ground))) {
            return false;
        }
    }
    const float distance = glm::length(b - a);
    const float step = std::max(spacing, 0.25f);
    const int steps = std::max(1, static_cast<int>(std::ceil(distance / step)));
    float previousGround = sample(a).ground;
    for (int i = 1; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const NavSample s = sample(a + (b - a) * t);
        if (!s.navigable) {
            return false;
        }
        if (std::abs(s.ground - previousGround) > settings_.stepHeight) {
            return false;
        }
        previousGround = s.ground;
    }
    return true;
}

glm::vec2 Navigator::steer(glm::vec2 from, glm::vec2 to, float lookahead) const {
    const glm::vec2 delta = to - from;
    const float distance = glm::length(delta);
    if (distance < 1e-4f) {
        return glm::vec2(0.0f);
    }
    const glm::vec2 direction = delta / distance;
    const float reach = std::min(std::max(lookahead, 0.5f), distance);
    if (pathClear(from, from + direction * reach)) {
        return direction;
    }
    // A fan either side, widening. The first clear deviation wins, and the sides alternate so a
    // walker does not develop a preference for turning one way into every obstacle it meets.
    //
    // Tie-broken by room rather than by order once a deviation is found: two adjacent trunks both
    // offer a clear line, and the gap a walker should take is the wider one. Without this a walker
    // shaves every trunk it passes, which reads as clipping even when it never quite does.
    constexpr std::array<float, 6> kFan{0.3f, 0.6f, 0.9f, 1.25f, 1.6f, 2.1f};
    const float ground = map_ != nullptr ? map_->height(from) : 0.0f;
    for (const float spread : kFan) {
        glm::vec2 best(0.0f);
        float bestRoom = -1.0f;
        for (const float sign : {-1.0f, 1.0f}) {
            const float angle = spread * sign;
            const float c = std::cos(angle);
            const float s = std::sin(angle);
            const glm::vec2 candidate(direction.x * c - direction.y * s, direction.x * s + direction.y * c);
            if (!pathClear(from, from + candidate * reach)) {
                continue;
            }
            const float room = clearanceAt(from + candidate * reach, ground, 6.0f);
            if (room > bestRoom) {
                bestRoom = room;
                best = candidate;
            }
        }
        if (bestRoom >= 0.0f) {
            return best;
        }
    }
    return glm::vec2(0.0f);
}

} // namespace avgen::entity
