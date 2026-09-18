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
    // From the same `TerrainPoint` as everything above it: `TerrainQuery::at` derives depth from
    // the one `WorldMap::sample` it already took, so this line adds no evaluation of the world.
    out.waterDepth = t.waterDepth;
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
    f.jumpOver = settings_.jumpOver;
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

PathResult Navigator::requestPath(const PathRequest& request) const {
    if (grid_ != nullptr && grid_->valid()) {
        return grid_->path(request);
    }
    // No graph. A straight line is the only thing that can be checked, and saying so by name is
    // what lets a caller distinguish "this world has no navigation" from "there is no way there".
    PathResult out;
    out.goal = request.to;
    if (glm::length(request.to - request.from) <= std::max(request.goalTolerance, 0.01f)) {
        out.status = PathStatus::AlreadyThere;
        return out;
    }
    if (!pathClear(request.from, request.to)) {
        out.status = PathStatus::NoGraph;
        return out;
    }
    out.status = PathStatus::Ok;
    out.waypoints.push_back(request.to);
    out.length = glm::length(request.to - request.from);
    return out;
}

bool Navigator::pathValid(glm::vec2 from, std::span<const glm::vec2> waypoints,
                          std::size_t nextLeg) const {
    if (waypoints.empty() || nextLeg >= waypoints.size()) {
        return false;
    }
    glm::vec2 previous = from;
    for (std::size_t i = nextLeg; i < waypoints.size(); ++i) {
        if (!pathClear(previous, waypoints[i])) {
            return false;
        }
        previous = waypoints[i];
    }
    return true;
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
    const float step = std::max(spacing, 0.25f);
    // Then the grid, where the grid can prove it (ADR-295). The solids are already settled above,
    // exactly and continuously; what is left is the terrain, and the terrain is a smooth height
    // field whose rejected ground the grid maps at four metres. `segmentTerrainClear` answers only
    // when the line runs `gridTrustCells` cells clear of any of it and the cell-to-cell gradient
    // cannot trip the step test below -- and answers false, meaning "ask the world", for everything
    // else. It can skip work; it can never decide against it, which is why this is an early accept
    // and not a branch.
    //
    // `stepHeight` is a rise between two samples `step` metres apart, so the gradient that matters
    // is `stepHeight / step`. Two thirds of it, because the grid knows the ground at cell centres
    // and the fine check looks between them: the margin is what pays for the difference, and it was
    // calibrated against the world rather than chosen -- see the `agree` arm of tools/charai_probe.
    if (settings_.gridTrustMetres > 0.0f && grid_ != nullptr && grid_->valid() && grid_->vouches() &&
        grid_->segmentTerrainClear(a, b, gridTrustCells(*grid_),
                                   settings_.stepHeight / step * 0.667f,
                                   gridSlopeGate(step))) {
        // The terrain is settled; the solids are not quite. The swept test above ran with the
        // walker's foot at `a`'s height for the whole line, and a solid is stepped over or ducked
        // under according to where the foot *is*: over forty metres of Glowmere the ground moves by
        // metres and the two disagree. Measured, when this test was not here: of 876 segments in
        // 20,000 where the grid said clear and the world said blocked, **807 were this** -- and
        // only 47 were the step test and 13 the slope. The terrain was never the problem.
        //
        // So the point test runs, at the same spacing the fine loop uses, on the grid's own
        // interpolated ground rather than on a fresh `WorldMap::height`: a foot height feeds a
        // threshold against a solid's base and top, and inside terrain this smooth the two agree to
        // centimetres. 0.013 us a point against 5.6.
        const float distance = glm::length(b - a);
        const int steps = std::max(1, static_cast<int>(std::ceil(distance / step)));
        bool clear = true;
        for (int i = 1; i <= steps && clear; ++i) {
            const glm::vec2 p = a + (b - a) * (static_cast<float>(i) / static_cast<float>(steps));
            clear = !obstructed(p, grid_->groundAt(p));
        }
        if (clear) {
            return true;
        }
    }
    const float distance = glm::length(b - a);
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

int Navigator::gridTrustCells(const NavGrid& grid) const {
    // At least one cell whatever the margin asks for: a rule that trusted a cell sitting against
    // unstandable ground would be the naive substitution, and that one is measured and refused.
    const float cell = std::max(grid.cellSize(), 1e-3f);
    return std::max(1, static_cast<int>(std::ceil(settings_.gridTrustMetres / cell)));
}

std::uint8_t Navigator::gridSlopeGate(float spacing) const {
    // The steepest ground the grid may vouch for, in the cell's own quantised slope units.
    //
    // Slope and the step test are two different limits and the step is far the tighter one: a cell
    // at the walkable limit of 0.55 is a 63-degree face, a gradient of 1.99, and 2.5 m of it rises
    // five metres against a step height of 1.4. So a route over ground that is *walkable* can fail
    // the fine check on every second sample, and the grid must not vouch for any of it. The gate is
    // the slope at which a straight `spacing` of ground exactly consumes the step height --
    // gradient `stepHeight / spacing`, converted back through `slope = 1 - 1/sqrt(1 + g^2)` -- and
    // then quantised the way the cell was.
    const float gradient = settings_.stepHeight / std::max(spacing, 0.25f);
    const float slope = 1.0f - 1.0f / std::sqrt(1.0f + gradient * gradient);
    const float range = std::max(settings_.maxSlope, 1e-3f);
    return static_cast<std::uint8_t>(
        std::clamp(slope / range, 0.0f, 1.0f) * 255.0f + 0.5f);
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
