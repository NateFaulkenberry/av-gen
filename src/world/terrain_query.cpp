#include "world/terrain_query.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace avgen::world {
namespace {

constexpr float kTwoPi = 6.2831853071795864769f;
// The golden angle. Offsetting each ring of the spiral search by it means successive rings do not
// stack their samples along the same spokes, which is what a fixed offset does and why a fixed
// offset misses a corridor that happens to lie between two spokes.
constexpr float kGoldenAngle = 2.3999632297286533f;

} // namespace

const char* terrainRejectName(TerrainReject reason) {
    switch (reason) {
    case TerrainReject::None: return "none";
    case TerrainReject::OutOfBounds: return "out of bounds";
    case TerrainReject::TooSteep: return "too steep";
    case TerrainReject::Submerged: return "submerged";
    case TerrainReject::NoHeadroom: return "no headroom";
    case TerrainReject::InsideHero: return "inside a hero";
    case TerrainReject::Obstructed: return "obstructed";
    }
    return "unknown";
}

float TerrainQuery::heightAt(glm::vec2 p) const {
    // Zero rather than a failure: a scene with no terrain is a legitimate scene and its ground is
    // the y = 0 plane. Refusing to answer would make every caller write the same fallback.
    return map != nullptr ? map->height(p) : 0.0f;
}

glm::vec3 TerrainQuery::normalAt(glm::vec2 p) const {
    return map != nullptr ? map->normal(p, epsilon) : glm::vec3(0.0f, 1.0f, 0.0f);
}

float TerrainQuery::slopeAt(glm::vec2 p) const {
    return glm::clamp(1.0f - normalAt(p).y, 0.0f, 1.0f);
}

bool TerrainQuery::isWater(glm::vec2 p) const {
    return waterDepthAt(p) > 0.0f;
}

float TerrainQuery::waterDepthAt(glm::vec2 p) const {
    if (map == nullptr) {
        return 0.0f;
    }
    const float surface = map->waterSurface(p);
    if (!std::isfinite(surface)) {
        return 0.0f;
    }
    return std::max(surface - map->height(p), 0.0f);
}

float TerrainQuery::canopyHeightAt(glm::vec2 p) const {
    return clearance.map != nullptr ? clearance.canopyHeight(p) : 0.0f;
}

float TerrainQuery::canopyHeightAt(glm::vec2 p, const Sample& sample) const {
    return clearance.map != nullptr ? clearance.canopyHeight(p, sample) : 0.0f;
}

bool TerrainQuery::inBounds(glm::vec2 p, float margin) const {
    if (map == nullptr) {
        return true;
    }
    const glm::vec2 lo = map->min() + margin;
    const glm::vec2 hi = map->max() - margin;
    return p.x >= lo.x && p.x <= hi.x && p.y >= lo.y && p.y <= hi.y;
}

TerrainPoint TerrainQuery::at(glm::vec2 p) const {
    TerrainPoint out;
    out.position = p;
    if (map == nullptr) {
        out.waterSurface = -std::numeric_limits<float>::infinity();
        out.walkable = true;
        return out;
    }
    const Sample s = map->sample(p, epsilon);
    out.height = s.height;
    out.normal = s.normal;
    out.slope = s.slope;
    out.waterSurface = s.waterSurface;
    out.water = s.submerged;
    out.waterDepth = std::isfinite(s.waterSurface) ? std::max(s.waterSurface - s.height, 0.0f) : 0.0f;
    out.moisture = s.moisture;
    out.altitude = s.altitude;
    // The sample above, not a second one. `epsilon` is this query's rather than the hardcoded 0.5
    // the one-argument form uses -- identical for every caller in this repository, none of which
    // moves `epsilon`, and the consistent answer where one does: a point should not be asked about
    // at two resolutions inside one call.
    out.canopy = canopyHeightAt(p, s);

    // The order is the order the rules fire in, cheapest and most decisive first, and it is the
    // same order `entity::Navigator::sample` uses -- these are one set of rules, written once.
    if (!inBounds(p, rules.boundaryMargin)) {
        out.reject = TerrainReject::OutOfBounds;
        return out;
    }
    if (s.slope > rules.maxSlope) {
        out.reject = TerrainReject::TooSteep;
        return out;
    }
    // `waterSurface` is -infinity on dry ground, so this whole block is a no-op there.
    //
    // Two rules, and which one applies is decided by the body rather than by the water. With
    // `wadeDepth` at its default of 0 this is the rule that was always here: the margin is the
    // difference between standing on a bank and standing ankle-deep in the river, and anything
    // inside it is refused. With a wade band declared, the margin is *replaced* rather than added
    // to -- a body that will cross a half-metre ford and still refuses to stand on a bank 0.3 m
    // above the water is describing nothing -- and what is refused is water deeper than the band.
    //
    // `out.waterDepth` above is the same number, from the same sample: 0 on the dry side of the
    // margin, so the freeboard shelf is walkable the moment wading is on at all.
    if (std::isfinite(s.waterSurface)) {
        const bool refused = rules.wadeDepth > 0.0f ? out.waterDepth > rules.wadeDepth
                                                    : s.height < s.waterSurface + rules.waterMargin;
        if (refused) {
            out.reject = TerrainReject::Submerged;
            return out;
        }
    }
    if (out.canopy > rules.walkableVegetation && out.canopy < rules.headroom) {
        // Tall enough to stop a mover and too low to duck under: a thicket. Either side of that
        // band is passable -- grass is waded through, and the floor under a fourteen-metre canopy
        // is the most walkable ground there is.
        out.reject = TerrainReject::NoHeadroom;
        return out;
    }
    const glm::vec3 stand(p.x, s.height + rules.heroMargin, p.y);
    if (clearance.heroPenetration(stand) > 0.0f) {
        out.reject = TerrainReject::InsideHero;
        return out;
    }
    out.walkable = true;
    return out;
}

bool TerrainQuery::isWalkable(glm::vec2 p) const {
    return at(p).walkable;
}

TerrainReject TerrainQuery::rejectAt(glm::vec2 p) const {
    return at(p).reject;
}

bool TerrainQuery::isOccupied(glm::vec2 p, float radius) const {
    const float r = std::max(radius, 0.0f);
    // The world's edge is solid: nothing may stand half outside the map, and a caller that has to
    // remember to bounds-check separately is a caller that forgets to.
    if (!inBounds(p, r)) {
        return true;
    }
    // Heroes are exact -- a handful of capsules with real radii -- so they are answered here rather
    // than being left to an obstacle set that may not exist.
    for (const HeroPoint& h : clearance.heroes) {
        const float planar = glm::length(p - glm::vec2(h.position.x, h.position.z));
        if (planar < h.radius + r + rules.heroMargin) {
            return true;
        }
    }
    // Everything else is §5's to answer. Note what is *not* consulted: the canopy. It is a
    // statistical model (ADR-080) and cannot say whether a particular disc contains a trunk, so
    // folding it in here would turn "is something standing here" into "does something grow nearby",
    // which is a different and much less useful question.
    return obstacles != nullptr && obstacles->occupied(p, r);
}

std::optional<glm::vec2> TerrainQuery::nearestValidPoint(glm::vec2 p, float radius,
                                                         float searchRadius) const {
    const auto acceptable = [&](glm::vec2 q) { return isWalkable(q) && !isOccupied(q, radius); };
    if (acceptable(p)) {
        return p;
    }
    const float reach = std::max(searchRadius, 0.0f);
    if (reach <= 0.0f) {
        return std::nullopt;
    }
    // Ring spacing is the mover's own size, floored so a point query does not walk out in
    // millimetre steps and ceiled so a big mover does not step over the only clearing there is.
    const float step = glm::clamp(std::max(radius, 0.5f) * 2.0f, 1.0f, 8.0f);
    const int rings = std::max(1, static_cast<int>(std::ceil(reach / step)));
    for (int ring = 1; ring <= rings; ++ring) {
        const float r = std::min(static_cast<float>(ring) * step, reach);
        // Enough samples that consecutive ones are about `step` apart along the ring, so the search
        // has the same resolution near and far rather than thinning out as it widens.
        const int count = std::clamp(static_cast<int>(std::ceil(kTwoPi * r / step)), 8, 256);
        const float offset = kGoldenAngle * static_cast<float>(ring);
        for (int i = 0; i < count; ++i) {
            const float angle = offset + kTwoPi * static_cast<float>(i) / static_cast<float>(count);
            const glm::vec2 q = p + glm::vec2(std::cos(angle), std::sin(angle)) * r;
            if (acceptable(q)) {
                return q;
            }
        }
    }
    return std::nullopt;
}

glm::vec3 TerrainQuery::groundPoint(glm::vec2 p) const {
    return glm::vec3(p.x, heightAt(p), p.y);
}

float TerrainQuery::surfaceAt(glm::vec2 p) const {
    if (map == nullptr) {
        return 0.0f;
    }
    const float surface = map->waterSurface(p);
    const float ground = map->height(p);
    return std::isfinite(surface) ? std::max(ground, surface) : ground;
}

TerrainQuery terrainQuery(const WorldMap& map, const Ecology* ecology,
                          std::span<const HeroPoint> heroes) {
    TerrainQuery q;
    q.map = &map;
    q.clearance.map = &map;
    q.clearance.ecology = ecology;
    q.clearance.heroes = heroes;
    // A thing that stands on the ground is not a camera: it wants no headroom cleared above the
    // surface and its personal space is its own width, not a near plane. `ClearanceField`'s own
    // defaults are the camera's, and inheriting them here is how a walker ends up hovering.
    q.clearance.cameraRadius = 0.0f;
    q.clearance.groundClearance = 0.0f;
    return q;
}

} // namespace avgen::world
