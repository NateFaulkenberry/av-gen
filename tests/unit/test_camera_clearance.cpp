// Keeping a directed camera out of the scenery (ADR-080). The failure this guards is specific and
// was real: on Glowmere, 21 of 32 baked camera keys were inside the terrain, inside the canopy or
// inside a hero, because shot geometry is orbit points at a distance in radii and knows nothing
// about what is in the way.

#include "world/camera_clearance.hpp"
#include "world/world_composer.hpp"

#include <string_view>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace avgen;

namespace {
// A real generated terrain rather than a flat plane. A plane has uniform altitude, slope and
// moisture, so every point resolves to the same biome and a test that needs to find forest cannot.
// The composer's own terrain is varied by construction and its biome coverage is known.
world::WorldMap flatMap() {
    world::WorldRecipe recipe;
    recipe.world = "clearance";
    recipe.seed = 20260910u;
    recipe.extent = 400.0f;
    return world::terrainFor(recipe);
}

// Somewhere `name` is unambiguously present, searched over the map. Returns false when the terrain
// happens not to contain any, which is a legitimate answer for a small map and is better than a
// test that quietly asserts nothing.
bool findBiome(const world::WorldMap& map, std::string_view name, float minWeight, glm::vec2& out) {
    for (int j = 0; j < 40; ++j) {
        for (int i = 0; i < 40; ++i) {
            const glm::vec2 p = map.min() + map.size * glm::vec2((i + 0.5f) / 40.0f, (j + 0.5f) / 40.0f);
            const world::Sample s = map.sample(p, 0.5f);
            const world::BiomeWeights w = map.biomes.at(s.altitude, s.slope, s.moisture, p);
            for (std::size_t b = 0; b < map.biomes.biomes.size() && static_cast<int>(b) < w.count; ++b) {
                if (map.biomes.biomes[b].name == name && w.weights[b] >= minWeight) {
                    out = p;
                    return true;
                }
            }
        }
    }
    return false;
}

// One tall layer that grows in forest, one short one everywhere.
world::Ecology twoLayerEcology() {
    world::Ecology e;
    world::ScatterLayer canopy;
    canopy.name = "canopy";
    canopy.category = "flora";
    canopy.height = 14.0f;
    canopy.densities = {{"forest", 0.01f}};
    world::ScatterLayer grass;
    grass.name = "grass";
    grass.category = "flora";
    grass.height = 0.5f;
    grass.densities = {{"forest", 0.4f}, {"meadow", 0.4f}, {"marsh", 0.4f}};
    e.layers = {canopy, grass};
    return e;
}
} // namespace

TEST_CASE("A camera under the ground is lifted onto it", "[world][clearance]") {
    const world::WorldMap map = flatMap();
    world::ClearanceField field;
    field.map = &map;

    const glm::vec2 where(10.0f, -20.0f);
    const float ground = map.sample(where, 0.5f).height;
    const world::ClearanceAdjustment out =
        world::clearPoint(field, glm::vec3(where.x, ground - 30.0f, where.y));
    CHECK(out.lifted > 29.0f);
    CHECK(out.position.y >= ground + field.groundClearance);
    // Only the height moves. Sliding a camera sideways changes which way the shot faces and what is
    // in frame; rising changes the shot least.
    CHECK_THAT(out.position.x, Catch::Matchers::WithinAbs(where.x, 1e-5));
    CHECK_THAT(out.position.z, Catch::Matchers::WithinAbs(where.y, 1e-5));
}

TEST_CASE("A camera already clear is not moved at all", "[world][clearance]") {
    const world::WorldMap map = flatMap();
    world::ClearanceField field;
    field.map = &map;
    field.ecology = nullptr;

    const glm::vec2 where(5.0f, 5.0f);
    const glm::vec3 high(where.x, map.sample(where, 0.5f).height + 200.0f, where.y);
    const world::ClearanceAdjustment out = world::clearPoint(field, high);
    CHECK_THAT(out.lifted, Catch::Matchers::WithinAbs(0.0, 1e-6));
    CHECK_THAT(glm::length(out.position - high), Catch::Matchers::WithinAbs(0.0, 1e-6));
}

TEST_CASE("The canopy is an obstacle where its biome is actually present", "[world][clearance]") {
    const world::WorldMap map = flatMap();
    const world::Ecology ecology = twoLayerEcology();
    world::ClearanceField field;
    field.map = &map;
    field.ecology = &ecology;

    // Somewhere forest is present, the tall layer counts; the canopy figure must be the tall one
    // rather than an average, because a camera flying at the average height hits everything taller.
    glm::vec2 wooded(0.0f);
    REQUIRE(findBiome(map, "forest", 0.5f, wooded));
    INFO("forest at " << wooded.x << ", " << wooded.y);
    // The tall layer, not an average: a camera flying at the average height hits everything taller.
    CHECK(field.canopyHeight(wooded) > 13.0f);
    // And the floor is above the canopy, not merely above the ground.
    CHECK(field.minimumHeight(wooded) > map.sample(wooded, 0.5f).height + 13.0f);
}

TEST_CASE("Cleared ground grows nothing, so a corridor stays flyable", "[world][clearance]") {
    // The point of the negative-space corridor (ADR-067) is that the camera can travel down it. If
    // the canopy field ignored clearances it would lift the camera out of the very lane the
    // composer cut for it.
    const world::WorldMap map = flatMap();
    world::Ecology ecology = twoLayerEcology();

    // Find a spot where the canopy does register, then clear it.
    glm::vec2 wooded(0.0f);
    REQUIRE(findBiome(map, "forest", 0.5f, wooded));

    world::ScatterClearance lane;
    lane.center = wooded;
    lane.radius = 25.0f;
    lane.softness = 5.0f;
    lane.strength = 1.0f;
    lane.minHeight = 1.2f;   // canopy-only, as the composer's corridor is
    ecology.clearances.push_back(lane);

    world::ClearanceField field;
    field.map = &map;
    field.ecology = &ecology;
    INFO("canopy in the lane: " << field.canopyHeight(wooded));
    CHECK(field.canopyHeight(wooded) < 1.0f);
}

TEST_CASE("A camera inside a hero is pushed out of it, not over it", "[world][clearance]") {
    const world::WorldMap map = flatMap();
    world::HeroPoint hero;
    hero.name = "elder";
    hero.assetId = "a";
    hero.position = glm::vec3(0.0f, map.sample(glm::vec2(0.0f), 0.5f).height, 0.0f);
    hero.radius = 8.0f;
    hero.height = 16.0f;
    const std::vector<world::HeroPoint> heroes{hero};

    world::ClearanceField field;
    field.map = &map;
    field.heroes = heroes;

    // Dead centre, halfway up: inside the trunk.
    //
    // This test asserted the opposite until the Auto-director gained a continuous-shot mode. Lifting
    // is right for a cut-based film -- being inside a hero means the camera came too close on its way
    // past -- and exactly wrong for a take that dollies toward a subject and orbits it, where it
    // carries the camera *over* the thing it is circling. The contract changed deliberately and this
    // is the assertion that changed with it.
    const glm::vec3 inside(0.0f, hero.position.y + 8.0f, 0.0f);
    CHECK(field.heroPenetration(inside) > 8.0f);
    const world::ClearanceAdjustment out = world::clearPoint(field, inside);
    CHECK(out.insideHero);
    CHECK(out.pushed > 8.0f);
    // Out, not up: the height is unchanged and the planar distance now clears the capsule.
    CHECK_THAT(out.position.y, Catch::Matchers::WithinAbs(inside.y, 1e-4));
    CHECK(glm::length(glm::vec2(out.position.x, out.position.z)) >= hero.radius + field.cameraRadius - 1e-3f);
    CHECK_THAT(field.heroPenetration(out.position), Catch::Matchers::WithinAbs(0.0, 1e-3));

    // Well outside its radius at the same height: untouched by the hero.
    const glm::vec3 beside(40.0f, hero.position.y + 8.0f, 0.0f);
    CHECK_THAT(field.heroPenetration(beside), Catch::Matchers::WithinAbs(0.0, 1e-6));

    // Above it entirely: also untouched, or a camera could never fly over a hero.
    const glm::vec3 over(0.0f, hero.position.y + hero.height + 20.0f, 0.0f);
    CHECK_THAT(field.heroPenetration(over), Catch::Matchers::WithinAbs(0.0, 1e-6));
}

TEST_CASE("Clearing a path smooths the correction without undoing it", "[world][clearance]") {
    const world::WorldMap map = flatMap();
    const world::Ecology ecology = twoLayerEcology();
    world::ClearanceField field;
    field.map = &map;
    field.ecology = &ecology;

    // A path that dives through the ground in the middle.
    std::vector<glm::vec3> path;
    for (int i = 0; i < 21; ++i) {
        const float x = static_cast<float>(i - 10) * 8.0f;
        const float ground = map.sample(glm::vec2(x, 0.0f), 0.5f).height;
        const float dip = i > 6 && i < 14 ? -25.0f : 60.0f;
        path.emplace_back(x, ground + dip, 0.0f);
    }
    std::vector<glm::vec3> before = path;
    const std::size_t raised = world::clearPath(field, path);
    CHECK(raised > 0);

    // Every point ends up clear. Smoothing that dragged a corrected point back into the ground
    // would be the obvious way to write this and is exactly wrong -- the smoothing removes the
    // kink, not the clearance.
    for (const glm::vec3& p : path) {
        INFO("at " << p.x << ", " << p.y);
        CHECK(p.y >= field.minimumHeight(glm::vec2(p.x, p.z)) - 1e-3f);
    }
    // And the points that were already clear were not dragged down to meet the corrected ones.
    CHECK(path.front().y > before.front().y - 25.0f);
    // Horizontal position is never touched.
    for (std::size_t i = 0; i < path.size(); ++i) {
        CHECK_THAT(path[i].x, Catch::Matchers::WithinAbs(before[i].x, 1e-5));
        CHECK_THAT(path[i].z, Catch::Matchers::WithinAbs(before[i].z, 1e-5));
    }
}

TEST_CASE("A path that is already clear is returned untouched", "[world][clearance]") {
    const world::WorldMap map = flatMap();
    world::ClearanceField field;
    field.map = &map;

    std::vector<glm::vec3> path;
    for (int i = 0; i < 10; ++i) {
        const float x = static_cast<float>(i) * 10.0f;
        path.emplace_back(x, map.sample(glm::vec2(x, 0.0f), 0.5f).height + 300.0f, 0.0f);
    }
    const std::vector<glm::vec3> before = path;
    CHECK(world::clearPath(field, path) == 0);
    for (std::size_t i = 0; i < path.size(); ++i) {
        CHECK_THAT(glm::length(path[i] - before[i]), Catch::Matchers::WithinAbs(0.0, 1e-6));
    }
}

// ---- lateral clearance: an orbit that stays an orbit (section 9) --------------------------------

TEST_CASE("A path through a hero goes around it rather than over it", "[world][clearance][lateral]") {
    const world::WorldMap map = flatMap();
    // `flatMap()` is a *generated* terrain, not a plane -- the name is older than the fixture. So the
    // path is flown above the highest ground along its own length, which makes the hero the only
    // thing in the way. The first version of this test put the path at a constant height above the
    // ground at the origin, and its endpoints were underground: the 3.3 m "rise" it reported was the
    // terrain lift working correctly on a path that was inside a hill.
    float ceiling = -1.0e9f;
    for (int i = 0; i <= 40; ++i) {
        const float x = glm::mix(-40.0f, 40.0f, static_cast<float>(i) / 40.0f);
        ceiling = std::max(ceiling, map.sample(glm::vec2(x, 0.0f), 0.5f).height);
    }
    const float ground = map.sample(glm::vec2(0.0f), 0.5f).height;
    const float flyY = ceiling + 6.0f;

    world::HeroPoint hero;
    hero.name = "elder-2";
    hero.assetId = "a";
    hero.position = glm::vec3(0.0f, ground, 0.0f);
    hero.radius = 8.0f;
    hero.height = (flyY - ground) + 8.0f; // tall enough that its capsule reaches the dolly
    const std::vector<world::HeroPoint> heroes{hero};

    world::ClearanceField field;
    field.map = &map;
    field.heroes = heroes;

    // A dolly straight at the subject and out the other side -- the move the continuous shot is for,
    // and the one the old vertical-only correction turned into a fly-over.
    std::vector<glm::vec3> path;
    for (int i = 0; i <= 40; ++i) {
        const float t = static_cast<float>(i) / 40.0f;
        path.push_back(glm::vec3(glm::mix(-40.0f, 40.0f, t), flyY, 0.0f));
    }
    const std::vector<glm::vec3> authored = path;
    const std::size_t moved = world::clearPath(field, path, 2);
    REQUIRE(moved > 0);
    REQUIRE(path.size() == authored.size());

    SECTION("every point ends up outside the hero") {
        for (const glm::vec3& p : path) {
            INFO("point (" << p.x << ", " << p.y << ", " << p.z << ")");
            REQUIRE(field.heroPenetration(p) < 0.05f);
        }
    }

    SECTION("it went around: the path deviates sideways and barely rises") {
        float maxSide = 0.0f;
        float maxRise = 0.0f;
        for (std::size_t i = 0; i < path.size(); ++i) {
            maxSide = std::max(maxSide, std::fabs(path[i].z - authored[i].z) +
                                            std::fabs(path[i].x - authored[i].x));
            maxRise = std::max(maxRise, path[i].y - authored[i].y);
        }
        INFO("max lateral " << maxSide << " m, max rise " << maxRise << " m");
        REQUIRE(maxSide > 4.0f);
        // The whole point. Under the old correction this was the penetration depth, about 9 m.
        REQUIRE(maxRise < 0.5f);
    }

    SECTION("the ends are untouched, because they were always clear") {
        REQUIRE(glm::distance(path.front(), authored.front()) < 1e-3f);
        REQUIRE(glm::distance(path.back(), authored.back()) < 1e-3f);
    }

    SECTION("it is deterministic") {
        std::vector<glm::vec3> again = authored;
        REQUIRE(world::clearPath(field, again, 2) == moved);
        for (std::size_t i = 0; i < path.size(); ++i) {
            REQUIRE(glm::distance(again[i], path[i]) < 1e-5f);
        }
    }
}

TEST_CASE("terrain still lifts, and a clear path is still untouched", "[world][clearance][lateral]") {
    const world::WorldMap map = flatMap();
    const float ground = map.sample(glm::vec2(0.0f), 0.5f).height;
    world::ClearanceField field;
    field.map = &map;

    SECTION("a path underground is raised, not shoved sideways") {
        // The negative control for the change: with no heroes in the field, nothing is pushed and the
        // correction is exactly what it was before lateral clearance existed.
        std::vector<glm::vec3> path;
        for (int i = 0; i <= 10; ++i) {
            path.push_back(glm::vec3(static_cast<float>(i) * 4.0f, ground - 3.0f, 0.0f));
        }
        const std::vector<glm::vec3> authored = path;
        REQUIRE(world::clearPath(field, path, 2) == path.size());
        for (std::size_t i = 0; i < path.size(); ++i) {
            REQUIRE(path[i].y > authored[i].y);
            REQUIRE_THAT(path[i].x, Catch::Matchers::WithinAbs(authored[i].x, 1e-5));
            REQUIRE_THAT(path[i].z, Catch::Matchers::WithinAbs(authored[i].z, 1e-5));
        }
    }

    SECTION("a path with nothing in the way is returned byte for byte") {
        std::vector<glm::vec3> path{{-20.0f, ground + 60.0f, 0.0f}, {0.0f, ground + 60.0f, 0.0f}};
        const std::vector<glm::vec3> authored = path;
        REQUIRE(world::clearPath(field, path, 2) == 0);
        REQUIRE(path == authored);
    }
}

TEST_CASE("a camera on a hero's axis is pushed somewhere deterministic", "[world][clearance][lateral]") {
    // No direction exists there, so the answer is a convention rather than a truth. What matters is
    // that it is finite, reproducible, and actually clear -- an unseeded or NaN direction would make
    // a bake non-reproducible, which is the one thing a baked camera may not be.
    const world::WorldMap map = flatMap();
    const float ground = map.sample(glm::vec2(0.0f), 0.5f).height;
    world::HeroPoint hero;
    hero.name = "h";
    hero.assetId = "a";
    hero.position = glm::vec3(0.0f, ground, 0.0f);
    hero.radius = 5.0f;
    hero.height = 10.0f;
    const std::vector<world::HeroPoint> heroes{hero};
    world::ClearanceField field;
    field.map = &map;
    field.heroes = heroes;

    const glm::vec3 onAxis(0.0f, ground + 5.0f, 0.0f);
    const world::ClearanceAdjustment a = world::clearPoint(field, onAxis);
    const world::ClearanceAdjustment b = world::clearPoint(field, onAxis);
    REQUIRE(std::isfinite(a.position.x));
    REQUIRE(std::isfinite(a.position.z));
    REQUIRE(glm::distance(a.position, b.position) < 1e-6f);
    REQUIRE(field.heroPenetration(a.position) < 0.05f);
}
