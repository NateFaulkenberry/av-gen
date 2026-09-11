// The world's spatial-query surface (§3, ADR-090).
//
// §3's instruction is "do not implement separate terrain logic for the alien, water placement and
// editor placement", so what these check is mostly agreement: that the join answers what the parts
// answer, that its walkability rules are the ones the navigator already uses, and that the one
// question nothing could answer -- isOccupied -- is honest about what it does and does not know.

#include "world/terrain_gen.hpp"
#include "world/terrain_query.hpp"
#include "world/world_map.hpp"

#include <optional>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace avgen;
using Catch::Matchers::WithinAbs;

namespace {

world::WorldMap testMap() {
    world::TerrainParams p = world::terrainPreset(world::TerrainStyle::Valley, 420.0f);
    p.seed = 20260911u;
    return world::generateTerrain(p);
}

// A disc of obstruction, standing in for §5's set. The point of the seam is that terrain needs to
// know nothing more than this about it.
class OneRock final : public world::ObstacleField {
public:
    OneRock(glm::vec2 at, float radius) : at_(at), radius_(radius) {}
    [[nodiscard]] bool occupied(glm::vec2 p, float radius) const override {
        return glm::distance(p, at_) < radius_ + radius;
    }

private:
    glm::vec2 at_;
    float radius_;
};

} // namespace

TEST_CASE("the query answers what the map answers", "[terrain][query]") {
    const world::WorldMap map = testMap();
    const world::TerrainQuery q = world::terrainQuery(map);
    REQUIRE(q.valid());
    for (int i = 0; i < 36; ++i) {
        const glm::vec2 uv((i % 6 + 0.5f) / 6.0f, (i / 6 + 0.5f) / 6.0f);
        const glm::vec2 p = map.min() + map.size * uv;
        const world::Sample s = map.sample(p, q.epsilon);
        CHECK_THAT(q.heightAt(p), WithinAbs(s.height, 1e-4f));
        CHECK_THAT(q.slopeAt(p), WithinAbs(s.slope, 1e-4f));
        CHECK_THAT(q.normalAt(p).y, WithinAbs(s.normal.y, 1e-4f));
        CHECK(q.groundPoint(p) == glm::vec3(p.x, s.height, p.y));
        // `at` exists so a caller that needs more than one scalar pays for one set of evaluations
        // rather than three. It must agree with the scalars exactly or it is a second answer.
        const world::TerrainPoint tp = q.at(p);
        CHECK_THAT(tp.height, WithinAbs(s.height, 1e-5f));
        CHECK_THAT(tp.slope, WithinAbs(s.slope, 1e-5f));
        CHECK(tp.water == s.submerged);
    }
}

TEST_CASE("water depth is metres over the bed, and zero on dry land", "[terrain][query]") {
    const world::WorldMap map = testMap();
    const world::TerrainQuery q = world::terrainQuery(map);
    int wet = 0;
    for (int j = 0; j < 48; ++j) {
        for (int i = 0; i < 48; ++i) {
            const glm::vec2 p = map.min() + map.size * glm::vec2((i + 0.5f) / 48.0f, (j + 0.5f) / 48.0f);
            const float depth = q.waterDepthAt(p);
            CHECK(depth >= 0.0f);
            CHECK(q.isWater(p) == (depth > 0.0f));
            // The surface a thing rests on is the water when there is any and the ground otherwise.
            CHECK_THAT(q.surfaceAt(p), WithinAbs(q.heightAt(p) + depth, 1e-3f));
            if (depth > 0.0f) {
                ++wet;
            }
        }
    }
    CHECK(wet > 0);
}

TEST_CASE("walkability rejects the four things that make ground unstandable", "[terrain][query]") {
    const world::WorldMap map = testMap();
    world::TerrainQuery q = world::terrainQuery(map);

    // Outside the map, with the boundary margin.
    const glm::vec2 outside = map.max() - q.rules.boundaryMargin * 0.5f;
    CHECK_FALSE(q.isWalkable(outside));
    CHECK(q.rejectAt(outside) == world::TerrainReject::OutOfBounds);

    // Under water.
    bool foundWater = false;
    for (int j = 0; j < 64 && !foundWater; ++j) {
        for (int i = 0; i < 64 && !foundWater; ++i) {
            const glm::vec2 p = map.min() + map.size * glm::vec2((i + 0.5f) / 64.0f, (j + 0.5f) / 64.0f);
            if (q.waterDepthAt(p) > 1.0f && q.inBounds(p, q.rules.boundaryMargin)) {
                foundWater = true;
                CHECK_FALSE(q.isWalkable(p));
                CHECK(q.rejectAt(p) == world::TerrainReject::Submerged);
            }
        }
    }
    CHECK(foundWater);

    // Too steep: the rule, rather than a place that happens to be steep.
    world::TerrainQuery strict = q;
    strict.rules.maxSlope = 0.0f;
    bool foundSlope = false;
    for (int j = 0; j < 32 && !foundSlope; ++j) {
        for (int i = 0; i < 32 && !foundSlope; ++i) {
            const glm::vec2 p = map.min() + map.size * glm::vec2((i + 0.5f) / 32.0f, (j + 0.5f) / 32.0f);
            if (strict.slopeAt(p) > 0.02f && strict.inBounds(p, strict.rules.boundaryMargin) &&
                strict.waterDepthAt(p) == 0.0f) {
                foundSlope = true;
                CHECK(strict.rejectAt(p) == world::TerrainReject::TooSteep);
            }
        }
    }
    CHECK(foundSlope);

    // And somewhere is walkable, or the rules reject everything and nothing above proved anything.
    int walkable = 0;
    for (int j = 0; j < 32; ++j) {
        for (int i = 0; i < 32; ++i) {
            if (q.isWalkable(map.min() + map.size * glm::vec2((i + 0.5f) / 32.0f, (j + 0.5f) / 32.0f))) {
                ++walkable;
            }
        }
    }
    CHECK(walkable > 200);
}

TEST_CASE("a scene with no terrain still answers", "[terrain][query]") {
    // A scene with no ground is a legitimate scene, and a query that refused to answer would make
    // every caller write the same fallback.
    const world::TerrainQuery q;
    CHECK_FALSE(q.valid());
    CHECK(q.heightAt(glm::vec2(12.0f, -4.0f)) == 0.0f);
    CHECK(q.normalAt(glm::vec2(0.0f)) == glm::vec3(0.0f, 1.0f, 0.0f));
    CHECK(q.isWalkable(glm::vec2(900.0f, 900.0f)));
    CHECK_FALSE(q.isWater(glm::vec2(0.0f)));
    CHECK(q.waterDepthAt(glm::vec2(0.0f)) == 0.0f);
}

TEST_CASE("isOccupied answers heroes, the world's edge, and §5's obstacles", "[terrain][query]") {
    const world::WorldMap map = testMap();
    world::HeroPoint hero;
    hero.name = "monolith";
    hero.position = glm::vec3(30.0f, map.height(glm::vec2(30.0f, -12.0f)), -12.0f);
    hero.radius = 6.0f;
    hero.height = 20.0f;
    const std::vector<world::HeroPoint> heroes{hero};

    world::TerrainQuery q = world::terrainQuery(map, nullptr, heroes);
    // Heroes are exact -- a handful of capsules with real radii -- so they are answered without any
    // obstacle set at all.
    CHECK_FALSE(q.hasObstacles());
    CHECK(q.isOccupied(glm::vec2(30.0f, -12.0f), 0.5f));
    CHECK(q.isOccupied(glm::vec2(30.0f + hero.radius + q.rules.heroMargin - 0.5f, -12.0f), 0.0f));
    CHECK_FALSE(q.isOccupied(glm::vec2(30.0f + hero.radius + q.rules.heroMargin + 2.0f, -12.0f), 0.0f));
    // A larger mover overlaps from further away. This is the whole reason the query takes a radius.
    CHECK(q.isOccupied(glm::vec2(30.0f + hero.radius + q.rules.heroMargin + 2.0f, -12.0f), 4.0f));

    // The world's edge is solid: nothing may stand half outside the map.
    CHECK(q.isOccupied(map.max() - glm::vec2(1.0f), 4.0f));
    CHECK_FALSE(q.isOccupied(glm::vec2(0.0f), 1.0f));

    // Everything else is §5's to answer, through a one-method interface.
    const OneRock rock(glm::vec2(-40.0f, 60.0f), 3.0f);
    q.obstacles = &rock;
    CHECK(q.hasObstacles());
    CHECK(q.isOccupied(glm::vec2(-40.0f, 60.0f), 0.5f));
    CHECK(q.isOccupied(glm::vec2(-36.0f, 60.0f), 1.5f));
    CHECK_FALSE(q.isOccupied(glm::vec2(-20.0f, 60.0f), 0.5f));
    // The default penetration is derived from `occupied`, so an implementation needs only one method.
    CHECK(rock.penetration(glm::vec2(-40.0f, 60.0f), 0.5f) > 0.0f);
    CHECK(rock.penetration(glm::vec2(-20.0f, 60.0f), 0.5f) < 0.0f);
}

TEST_CASE("nearestValidPoint returns the point when it is already valid", "[terrain][query]") {
    const world::WorldMap map = testMap();
    const world::TerrainQuery q = world::terrainQuery(map);
    glm::vec2 good(0.0f);
    bool any = false;
    for (int j = 0; j < 32 && !any; ++j) {
        for (int i = 0; i < 32 && !any; ++i) {
            const glm::vec2 p = map.min() + map.size * glm::vec2((i + 0.5f) / 32.0f, (j + 0.5f) / 32.0f);
            if (q.isWalkable(p) && !q.isOccupied(p, 1.0f)) {
                good = p;
                any = true;
            }
        }
    }
    REQUIRE(any);
    const auto same = q.nearestValidPoint(good, 1.0f);
    REQUIRE(same);
    CHECK(*same == good);
}

TEST_CASE("nearestValidPoint escapes water and is deterministic", "[terrain][query]") {
    const world::WorldMap map = testMap();
    const world::TerrainQuery q = world::terrainQuery(map);
    glm::vec2 wet(0.0f);
    bool found = false;
    for (int j = 0; j < 64 && !found; ++j) {
        for (int i = 0; i < 64 && !found; ++i) {
            const glm::vec2 p = map.min() + map.size * glm::vec2((i + 0.5f) / 64.0f, (j + 0.5f) / 64.0f);
            if (q.waterDepthAt(p) > 1.0f && q.inBounds(p, 60.0f)) {
                wet = p;
                found = true;
            }
        }
    }
    REQUIRE(found);
    REQUIRE_FALSE(q.isWalkable(wet));
    const auto dry = q.nearestValidPoint(wet, 0.6f, 90.0f);
    REQUIRE(dry);
    CHECK(q.isWalkable(*dry));
    CHECK(glm::distance(*dry, wet) <= 90.0f);
    // The spiral is a fixed sequence, not a sample: two callers asking the same question of the same
    // world get the same answer, which is what lets a generator use it.
    const auto again = q.nearestValidPoint(wet, 0.6f, 90.0f);
    REQUIRE(again);
    CHECK(*again == *dry);
}

TEST_CASE("nearestValidPoint gives up rather than lying", "[terrain][query]") {
    // Silently returning an invalid point is how an entity ends up inside a cliff.
    const world::WorldMap map = testMap();
    world::TerrainQuery q = world::terrainQuery(map);
    q.rules.maxSlope = -1.0f;   // nothing anywhere is walkable
    CHECK_FALSE(q.nearestValidPoint(glm::vec2(0.0f), 1.0f, 40.0f));
}

TEST_CASE("the canopy is not consulted for occupancy", "[terrain][query]") {
    // ADR-080's canopy says "trees about nine metres tall grow around here", never "there is a trunk
    // at this spot". Folding it into isOccupied would turn "is something standing here" into "does
    // something grow nearby", which is a different and much less useful question -- and it would put
    // the whole of a meadow permanently out of bounds.
    const world::WorldMap map = testMap();
    const world::TerrainQuery q = world::terrainQuery(map);
    for (int i = 0; i < 25; ++i) {
        const glm::vec2 p = map.min() + map.size * glm::vec2((i % 5 + 0.5f) / 5.0f, (i / 5 + 0.5f) / 5.0f);
        if (q.inBounds(p, 40.0f)) {
            CHECK(q.isOccupied(p, 0.5f) == false);
        }
    }
}
