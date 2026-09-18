// What the navigation grid may answer for, and what the walkable ground is divided into
// (ADR-295, ADR-296).
//
// Two claims are under test and each one has a control beside it, because an arm that cannot fail
// proves nothing (ADR-182):
//
//   * The grid answers the *terrain* half of a walkability query only where it has proved it can,
//     and where it does the answer is the world's. The control is the naive substitution -- trust
//     any walkable cell -- which must disagree with the world, and a rough world, for which the
//     grid must refuse to answer at all.
//   * The reachability the grid has always computed is now reported. The control is a world whose
//     ground is one piece, which must report nothing stranded.

#include "entity/behavior.hpp"
#include "entity/entity.hpp"
#include "entity/nav_grid.hpp"
#include "entity/navigation.hpp"
#include "params/parameter_set.hpp"
#include "spatial/obstacle_field.hpp"
#include "world/camera_clearance.hpp"
#include "world/ecology.hpp"
#include "world/terrain_query.hpp"
#include "world/world_map.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace avgen;

namespace {

spatial::NavigationObstacle solid(float x, float z, float radius, float height) {
    spatial::NavigationObstacle o;
    o.center = glm::vec2(x, z);
    o.radius = radius;
    o.base = 0.0f;
    o.height = height;
    o.type = spatial::ObstacleType::Rock;
    return o;
}

// A flat world with a lake cut into one corner, which is the shape both claims need: flat so the
// grid can be exact about the terrain, and a lake so there is terrain it must refuse.
struct Flat {
    world::WorldMap map;
    world::Ecology ecology;
    world::ClearanceField clearance;

    Flat() {
        map.name = "flat";
        map.size = glm::vec2(240.0f, 240.0f);
        map.baseHeight = 0.0f;
        world::Feature lake;
        lake.kind = world::FeatureKind::Flat;
        lake.water = true;
        lake.waterDepth = 2.0f;
        lake.amplitude = 3.0f;
        lake.width = 26.0f;
        lake.falloff = 1.0f;
        lake.path = {glm::vec3(-70.0f, -4.0f, -70.0f), glm::vec3(-70.0f, -4.0f, 40.0f)};
        map.features.push_back(lake);
        map.prepare();
        clearance.map = &map;
        clearance.ecology = &ecology;
        clearance.cameraRadius = 0.6f;
        clearance.groundClearance = 0.0f;
    }
};

} // namespace

// ---- the sample that was taken twice ------------------------------------------------------------

TEST_CASE("the canopy from a sample already taken is the canopy from a fresh one",
          "[navigation][terrain]") {
    // The whole safety argument for the largest unconditional win in ADR-295 is that
    // `canopyHeight(p, s)` is `canopyHeight(p)` with the sample handed over instead of retaken. If
    // that is ever not true, every walkable set in the engine moves and nothing else would notice.
    world::WorldMap map = world::defaultWorld();
    world::Ecology ecology;
    world::ScatterLayer trees;
    trees.height = 9.0f;
    trees.category = "tree";
    // One biome, not all of them. A layer present in every biome answers `layer.height` wherever any
    // biome is present, which is everywhere -- and then the canopy is a constant and the section
    // below cannot fail however wrong the function is. The first version of this test did that.
    REQUIRE(map.biomes.biomes.size() > 1);
    world::BiomeDensity d;
    d.biome = map.biomes.biomes.front().name;
    d.density = 1.0f;
    trees.densities.push_back(d);
    ecology.layers.push_back(trees);
    world::ClearanceField field;
    field.map = &map;
    field.ecology = &ecology;

    const glm::vec2 lo = map.min() + 20.0f;
    const glm::vec2 hi = map.max() - 20.0f;
    int agreed = 0;
    int grew = 0;
    for (int i = 0; i < 2048; ++i) {
        const float u = static_cast<float>(i % 64) / 64.0f;
        const float v = static_cast<float>(i / 64) / 32.0f;
        const glm::vec2 p = lo + (hi - lo) * glm::vec2(u, v);
        const float fresh = field.canopyHeight(p);
        const float handed = field.canopyHeight(p, map.sample(p, 0.5f));
        REQUIRE(fresh == handed);
        ++agreed;
        grew += fresh > 0.0f ? 1 : 0;
    }
    CHECK(agreed == 2048);
    // The control. A canopy that is zero everywhere agrees with anything, and the first version of
    // this test had no ecology and proved exactly that.
    INFO("points where anything grows: " << grew);
    CHECK(grew > 0);

    SECTION("and a sample from somewhere else does not, which is what makes the above a check") {
        const glm::vec2 a = lo + (hi - lo) * glm::vec2(0.2f, 0.2f);
        const glm::vec2 b = lo + (hi - lo) * glm::vec2(0.8f, 0.8f);
        // Not an assertion that these two differ -- they may legitimately not -- but that the
        // function reads the sample it is given rather than ignoring it. Slope, altitude and
        // moisture from another place must be able to change the answer somewhere in the world.
        int differed = 0;
        for (int i = 0; i < 256; ++i) {
            const float t = static_cast<float>(i) / 256.0f;
            const glm::vec2 p = a + (b - a) * t;
            if (field.canopyHeight(p, map.sample(b, 0.5f)) != field.canopyHeight(p)) {
                ++differed;
            }
        }
        INFO("points where a foreign sample changed the canopy: " << differed);
        CHECK(differed > 0);
    }
}

// ---- what the grid will and will not answer for -------------------------------------------------

TEST_CASE("a flat world's grid vouches for its terrain and a rough one's does not",
          "[navigation][grid][trust]") {
    Flat flat;
    entity::Navigator nav(&flat.map, flat.clearance);
    nav.buildGrid(4.0f);
    REQUIRE(nav.grid() != nullptr);
    const entity::NavGridStats& stats = nav.grid()->stats();
    INFO("flat: " << stats.trustChecks << " walks checked, " << stats.trustFailures << " wrong");
    REQUIRE(stats.trustChecks > 0); // an untested grid must never vouch, and a vacuous pass is one
    CHECK(stats.trustFailures == 0);
    CHECK(nav.grid()->vouches());

    SECTION("the control: ground with structure finer than a cell is refused") {
        // The mechanism is worth nothing if it says yes to everything, and this is the shape it has
        // to say no to: an octave whose period is 2.9 m on a lattice whose cells are 4 m, with
        // enough amplitude that the slope crosses the walkable limit within one cell. Every cell
        // centre can land on gentle ground while the metre between two of them is a wall, and no
        // margin in cells fixes that because the information is not in the lattice.
        //
        // Measured while this control was being found, and worth recording because it was not
        // predicted: the shipped `defaultWorld` **with no ecology vouches**, over 1,237 checked
        // walks. Five octaves of terrain noise alone are smooth enough at four metres for the slope
        // gate and the rise test to cover. What defeats the grid on the real Glowmere scene is the
        // combination measured in ADR-295 -- `too steep`, `no headroom` and the step test together
        // -- and the canopy half of it is a thresholded biome weight, which is a step function and
        // is why no resolution is sound rather than merely why four metres is not.
        world::WorldMap rough;
        rough.name = "rough";
        rough.size = glm::vec2(240.0f, 240.0f);
        rough.baseHeight = 0.0f;
        rough.seaLevel = -1000.0f;
        // 3 m of relief on a 2.9 m period. Every cell of the grid is still walkable -- the refusal
        // is about resolution and nothing else, which is what makes it the control for this
        // mechanism rather than for obstacle handling.
        rough.layers = {{0.34f, 3.0f, 0.0f, 0.0f}};
        rough.prepare();
        world::Ecology none;
        world::ClearanceField clearance;
        clearance.map = &rough;
        clearance.ecology = &none;
        clearance.cameraRadius = 0.6f;
        clearance.groundClearance = 0.0f;
        entity::Navigator wild(&rough, clearance);
        wild.buildGrid(4.0f);
        REQUIRE(wild.grid() != nullptr);
        const entity::NavGridStats& s = wild.grid()->stats();
        INFO("rough: " << s.walkable << " walkable, " << s.trustChecks << " walks checked, "
                       << s.trustFailures << " wrong");
        // The control's own control: a world with nothing walkable in it refuses for the wrong
        // reason, and would pass this section while proving nothing.
        REQUIRE(s.walkable == s.cells);
        REQUIRE(s.trustChecks > 0);
        CHECK_FALSE(wild.grid()->vouches());
        CHECK(s.trustFailures == 1); // it stops at the first, by design
    }
}

TEST_CASE("where the grid vouches, it gives the world's answer", "[navigation][grid][trust]") {
    Flat flat;
    auto obstacles = std::make_shared<spatial::ObstacleField>();
    for (int i = 0; i < 40; ++i) {
        const float a = static_cast<float>(i) * 0.9973f;
        obstacles->add(solid(std::cos(a) * 60.0f, std::sin(a) * 60.0f, 1.1f, 7.0f));
    }
    obstacles->build();
    entity::Navigator nav(&flat.map, flat.clearance);
    nav.setObstacles(obstacles);
    nav.buildGrid(4.0f);
    REQUIRE(nav.grid()->vouches());

    entity::Navigator analytic = nav;
    entity::NavSettings off = analytic.settings();
    off.gridTrustMetres = 0.0f; // the control arm: the world, exactly as it was asked before
    analytic.setSettings(off);

    int compared = 0;
    int clear = 0;
    int blocked = 0;
    int disagreed = 0;
    std::uint32_t state = 12345u;
    const auto next01 = [&state]() {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(state >> 8u) / 16777216.0f;
    };
    const glm::vec2 lo = nav.worldMin();
    const glm::vec2 hi = nav.worldMax();
    while (compared < 4000) {
        const glm::vec2 a = lo + (hi - lo) * glm::vec2(next01(), next01());
        if (!analytic.sample(a).navigable) {
            continue;
        }
        const float angle = next01() * 6.2831853f;
        const float reach = next01() < 0.5f ? 6.0f : 40.0f;
        const glm::vec2 b = a + glm::vec2(std::cos(angle), std::sin(angle)) * reach;
        const bool truth = analytic.pathClear(a, b);
        const bool fast = nav.pathClear(a, b);
        ++compared;
        clear += truth ? 1 : 0;
        blocked += truth ? 0 : 1;
        disagreed += truth == fast ? 0 : 1;
    }
    INFO(compared << " segments: " << clear << " clear, " << blocked << " blocked, " << disagreed
                  << " disagreements");
    CHECK(disagreed == 0);
    // Both controls. A run in which nothing was ever blocked would agree trivially, and so would one
    // in which nothing was ever clear.
    CHECK(clear > 0);
    CHECK(blocked > 0);
}

TEST_CASE("the terrain-room field is a distance to unstandable ground", "[navigation][grid]") {
    Flat flat;
    entity::Navigator nav(&flat.map, flat.clearance);
    nav.buildGrid(4.0f);
    const entity::NavGrid& grid = *nav.grid();

    // Deep in the open, far from the lake and the world's margin.
    const std::uint8_t open = grid.terrainRoom(glm::vec2(60.0f, 60.0f));
    // In the lake, which is not standable ground at all.
    const std::uint8_t wet = grid.terrainRoom(glm::vec2(-70.0f, 0.0f));
    INFO("open " << static_cast<int>(open) << ", wet " << static_cast<int>(wet));
    CHECK(wet == 0);
    CHECK(open > 4);

    SECTION("and it falls off towards the shore rather than being a boolean") {
        // A row of samples marching at the water. Room must decrease and reach zero; a field that
        // was merely "walkable or not" would step from its maximum straight to nothing.
        std::vector<int> room;
        for (float x = -10.0f; x >= -90.0f; x -= 4.0f) {
            room.push_back(grid.terrainRoom(glm::vec2(x, 0.0f)));
        }
        REQUIRE(room.size() > 8);
        CHECK(room.front() > 0);
        CHECK(room.back() == 0);
        int descents = 0;
        for (std::size_t i = 1; i < room.size(); ++i) {
            descents += room[i] < room[i - 1] ? 1 : 0;
            // A Chebyshev distance transform cannot fall by more than one per cell of travel.
            CHECK(room[i - 1] - room[i] <= 1);
        }
        INFO("room along the row falls " << descents << " times");
        CHECK(descents > 2);
    }

    SECTION("a segment that crosses the lake is one the grid will not answer for") {
        // The control is the same length of line in the open, which it will.
        const std::uint8_t gate = nav.gridSlopeGate(2.5f);
        const int cells = nav.gridTrustCells(grid);
        CHECK_FALSE(grid.segmentTerrainClear(glm::vec2(-40.0f, 0.0f), glm::vec2(-90.0f, 0.0f), cells,
                                             0.374f, gate));
        CHECK(grid.segmentTerrainClear(glm::vec2(40.0f, 0.0f), glm::vec2(90.0f, 0.0f), cells, 0.374f,
                                       gate));
    }
}

// ---- reachability, reported ---------------------------------------------------------------------

TEST_CASE("the grid reports how much ground is on the wrong side of it", "[navigation][grid][reach]") {
    Flat flat;
    // An island: a closed ring of solids with nothing walkable inside but the middle.
    auto obstacles = std::make_shared<spatial::ObstacleField>();
    for (int i = 0; i < 90; ++i) {
        const float a = static_cast<float>(i) / 90.0f * 6.2831853f;
        obstacles->add(solid(60.0f + std::cos(a) * 14.0f, 60.0f + std::sin(a) * 14.0f, 2.2f, 8.0f));
    }
    obstacles->build();
    entity::Navigator nav(&flat.map, flat.clearance);
    nav.setObstacles(obstacles);
    nav.buildGrid(4.0f);
    const entity::NavGrid& grid = *nav.grid();
    const entity::NavGridStats& stats = grid.stats();
    INFO("regions " << stats.regions << ", largest " << stats.largestRegion << ", stranded "
                    << stats.stranded << " of " << stats.walkable);

    REQUIRE(stats.regions > 1);
    CHECK(stats.stranded > 0);
    // The accounting has to close, or the number an author is shown is not a number.
    CHECK(stats.stranded + stats.largestRegion == stats.walkable);
    CHECK(grid.regionSizes().size() == stats.regions + 1); // [0] is the unwalkable set
    CHECK(grid.regionSize(grid.largestRegionId()) == stats.largestRegion);

    const glm::vec2 inside(60.0f, 60.0f);
    const glm::vec2 outside(0.0f, 0.0f);
    REQUIRE(grid.regionAt(inside) != 0);
    REQUIRE(grid.regionAt(outside) != 0);
    CHECK(grid.regionAt(inside) != grid.regionAt(outside));
    CHECK_FALSE(grid.connected(inside, outside));
    // `connected` is a lookup and `path` is a search, and the whole value of the first is that it
    // never disagrees with the second.
    entity::PathRequest request;
    request.from = inside;
    request.to = outside;
    const entity::PathResult route = grid.path(request);
    CHECK(route.status == entity::PathStatus::Unreachable);
    CHECK(route.waypoints.empty());

    SECTION("the control: ground in one piece strands nothing") {
        Flat open;
        entity::Navigator plain(&open.map, open.clearance);
        plain.buildGrid(4.0f);
        const entity::NavGridStats& s = plain.grid()->stats();
        INFO("open world: " << s.regions << " region(s), " << s.stranded << " stranded");
        REQUIRE(s.walkable > 0);
        CHECK(s.regions == 1);
        CHECK(s.stranded == 0);
        CHECK(plain.grid()->connected(glm::vec2(40.0f, 40.0f), glm::vec2(-20.0f, 60.0f)));
    }
}

// ---- being stuck and being idle are no longer the same frame ------------------------------------

TEST_CASE("a body with nowhere to go says so, and one between errands does not",
          "[navigation][reach][confined]") {
    // The Character Intelligence Lab measured this and could only write it down: a character sealed
    // inside a ring of stones never reaches `requestPath` at all -- every destination it could pick
    // is outside the wall, `pickDestination` rejects all of them, and the `PathStatus` it publishes
    // is still the `Ok` of whatever it did before it was penned. **Being stuck and being idle
    // produced the same frame and the same log** (ADR-296).
    //
    // The arm is the penned body; the control is an identical body twenty metres away with the same
    // behaviour, the same seed and no wall, which must report nothing.
    Flat flat;
    auto obstacles = std::make_shared<spatial::ObstacleField>();
    // A thick sealed wall: forty 3 m stones on a 12 m ring, overlapping heavily, so the band from
    // about 9 m to 15 m out is solid. The thickness is the point and it took two wrong pens to learn
    // why. A *thin* ring leaves the annulus a stroll is drawn from on open ground outside the wall,
    // and the body picks a destination there, plans, and is told `Unreachable` -- which is the case
    // that already worked. A ring thinner than a grid cell is worse: at 4 m cells the graph does not
    // see the wall at all, A* routes a line straight through it, the status reads `ok`, and the body
    // walks into stone for two minutes. This wall is thicker than the annulus, so every candidate
    // lands inside a solid and the body never gets as far as asking for a path.
    for (int i = 0; i < 40; ++i) {
        const float a = static_cast<float>(i) / 40.0f * 6.2831853f;
        obstacles->add(solid(std::cos(a) * 12.0f, std::sin(a) * 12.0f, 3.0f, 6.0f));
    }
    obstacles->build();
    entity::Navigator nav(&flat.map, flat.clearance);
    nav.setObstacles(obstacles);
    nav.buildGrid(4.0f);

    entity::EntityWorld world;
    params::ParameterSet params;
    std::vector<entity::EntityDesc> descs;
    std::vector<entity::NodeBinding> bindings;
    const glm::vec2 places[2] = {glm::vec2(0.0f, 0.0f), glm::vec2(80.0f, 20.0f)};
    const char* names[2] = {"penned", "free"};
    for (int i = 0; i < 2; ++i) {
        entity::EntityDesc d;
        d.name = names[i];
        d.node = d.name;
        entity::BehaviorDesc b;
        b.kind = "explore";
        // `homeRadius` is what makes this the lab's pen rather than a merely awkward one, and the
        // difference is the whole finding. With a home of 14 m every interest point in the world is
        // filtered out before the plan stage, so the penned body never reaches `requestPath` and
        // never publishes an `Unreachable` -- it falls through to a stroll, `pickDestination`
        // refuses every candidate in the annulus because they are all outside the wall, and it
        // stands there. Without the home radius it *does* plan, gets `Unreachable`, and is reported
        // honestly by machinery that already existed; that case is not the hole.
        b.settings = {{"kind", "explore"},
                      {"speed", 1.6},
                      {"minRange", 10.0},
                      {"maxRange", 120.0},
                      {"homeRadius", 14.0}};
        d.behaviors.push_back(b);
        d.gait.walkSpeed = 1.6f;
        descs.push_back(std::move(d));

        entity::NodeBinding nb;
        nb.node = names[i];
        nb.exists = true;
        nb.transformPrefix = std::string("nodes/") + names[i] + "/";
        nb.anchor = glm::vec3(places[i].x, nav.groundHeight(places[i]), places[i].y);
        params.add(params::ParamDesc<glm::vec3>{.path = nb.transformPrefix + "position",
                                                .defaultValue = nb.anchor,
                                                .hardMin = glm::vec3(-1e4f),
                                                .hardMax = glm::vec3(1e4f)});
        params.add(params::ParamDesc<glm::vec3>{.path = nb.transformPrefix + "rotation",
                                                .defaultValue = glm::vec3(0.0f),
                                                .hardMin = glm::vec3(-360.0f),
                                                .hardMax = glm::vec3(360.0f)});
        params.add(params::ParamDesc<glm::vec3>{.path = nb.transformPrefix + "scale",
                                                .defaultValue = glm::vec3(1.0f),
                                                .hardMin = glm::vec3(0.001f),
                                                .hardMax = glm::vec3(100.0f)});
        bindings.push_back(std::move(nb));
    }
    world.setNavigator(nav);
    world.setBindings(bindings);
    world.setEntities(std::move(descs), 4242u);
    world.registerParameters(params, "entity/");
    world.bind(params, "entity/");

    entity::EntityUpdate u;
    u.dt = 1.0 / 60.0;
    u.distanceDetail = false;
    for (int f = 1; f <= 60 * 20; ++f) {
        u.time = static_cast<double>(f) / 60.0;
        world.update(u, params);
    }

    const auto report = [&](const char* name) {
        entity::NavDebug out;
        for (const auto& e : world.entities()) {
            if (e->name() != name) {
                continue;
            }
            for (const auto& b : e->behaviors()) {
                if (b->navDebug(out)) {
                    return std::pair<entity::NavDebug, double>{
                        out, static_cast<double>(glm::length(e->state().travel))};
                }
            }
        }
        FAIL("no navigating behaviour on " << name);
        return std::pair<entity::NavDebug, double>{out, 0.0};
    };

    const auto [penned, pennedTravel] = report("penned");
    const auto [free, freeTravel] = report("free");
    INFO("penned: confined " << penned.confinedFor << " s, status "
                             << entity::pathStatusName(penned.status) << ", travelled "
                             << pennedTravel << " m");
    INFO("free:   confined " << free.confinedFor << " s, status "
                             << entity::pathStatusName(free.status) << ", travelled " << freeTravel
                             << " m");

    // The pen contains it. Without this the rest measures a body that simply walked away.
    CHECK(pennedTravel < 8.0);
    CHECK(freeTravel > 8.0);
    // And the engine now says which of the two it is looking at.
    CHECK(penned.confinedFor > 5.0f);
    CHECK(free.confinedFor == 0.0f);
    // The half that was already true and was never enough on its own: both bodies publish a path
    // status, and the penned one's is indistinguishable from a body that is simply between errands.
    // That is the reason `confinedFor` exists rather than another `PathStatus` value.
    CHECK(penned.status == free.status);
}
