// Terrain generation from artistic parameters (§29-§32, ADR-090).
//
// What these guard, in the order they were found to be wrong: that a style produces the landform it
// names rather than the same dune field five times; that Elevation min/max is a metre figure and not
// a hint; that a generated river descends along its whole course, sits in a depression with banks
// above it, and is connected rather than a set of disconnected puddles; and that the whole thing is
// a pure function of its parameters, which is what §32 requires and what every byte-identical
// capture in this repository depends on.

#include "world/terrain_gen.hpp"
#include "world/terrain_query.hpp"
#include "world/terrain_water.hpp"
#include "world/world_map.hpp"
#include "world/world_recipe.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace avgen;
using Catch::Matchers::WithinAbs;

namespace {

world::TerrainParams paramsFor(world::TerrainStyle style, std::uint32_t seed = 20260911u) {
    world::TerrainParams p = world::terrainPreset(style, 420.0f);
    p.seed = seed;
    p.name = world::terrainStyleName(style);
    return p;
}

// Every height on a fixed lattice. The cheapest possible statement of "the same world": a hash can
// collide and a spot check can miss, and this is 4225 floats compared bit for bit.
std::vector<float> lattice(const world::WorldMap& map, int side = 65) {
    std::vector<float> out;
    out.reserve(static_cast<std::size_t>(side) * side);
    for (int j = 0; j < side; ++j) {
        for (int i = 0; i < side; ++i) {
            const glm::vec2 uv((i + 0.5f) / side, (j + 0.5f) / side);
            out.push_back(map.height(map.min() + map.size * uv));
        }
    }
    return out;
}

float relief(const world::WorldMap& map) { return map.sampledMaxHeight - map.sampledMinHeight; }

} // namespace

TEST_CASE("every terrain style generates a valid, prepared world", "[terrain][gen]") {
    for (const world::TerrainStyle style : world::terrainStyles()) {
        const world::TerrainParams p = paramsFor(style);
        REQUIRE(p.validate());
        const world::WorldMap map = world::generateTerrain(p);
        INFO("style " << world::terrainStyleName(style));
        REQUIRE(map.validate());
        CHECK(map.size == glm::vec2(420.0f, 420.0f));
        CHECK(map.seed == p.seed);
        // Prepared: every feature's smoothed curve and bounds are filled in, and the height range is
        // measured. An unprepared map samples features with the wrong path and normalises altitude
        // against [0, 1], which turns every altitude-banded biome rule into a no-op.
        CHECK(relief(map) > 1.0f);
        for (const world::Feature& f : map.features) {
            CHECK(f.boundsMax.x >= f.boundsMin.x);
        }
        // A generator that installed biomes would be the second place the composer's five-biome
        // vocabulary could be written down, and the second is always the one that is out of date.
        CHECK(map.biomes.empty());
    }
}

TEST_CASE("the same parameters generate the same world, bit for bit", "[terrain][gen][determinism]") {
    // §32. The repository verifies determinism with byte-identical captures, so this is the level
    // the terrain has to hold itself to: not "looks the same", the same floats.
    for (const world::TerrainStyle style : world::terrainStyles()) {
        const world::TerrainParams p = paramsFor(style);
        const world::WorldMap a = world::generateTerrain(p);
        const world::WorldMap b = world::generateTerrain(p);
        INFO("style " << world::terrainStyleName(style));
        CHECK(a.structuralHash() == b.structuralHash());
        CHECK(lattice(a) == lattice(b));
        CHECK(a.features.size() == b.features.size());
    }
}

TEST_CASE("a different seed generates a different world", "[terrain][gen][determinism]") {
    const world::WorldMap a = world::generateTerrain(paramsFor(world::TerrainStyle::Valley, 1u));
    const world::WorldMap b = world::generateTerrain(paramsFor(world::TerrainStyle::Valley, 2u));
    CHECK(a.structuralHash() != b.structuralHash());
    CHECK(lattice(a) != lattice(b));
}

TEST_CASE("a different parameter generates a different world", "[terrain][gen][determinism]") {
    world::TerrainParams a = paramsFor(world::TerrainStyle::RollingHills);
    world::TerrainParams b = a;
    b.ridgeStrength = a.ridgeStrength + 0.3f;
    CHECK(a.structuralHash() != b.structuralHash());
    CHECK(lattice(world::generateTerrain(a)) != lattice(world::generateTerrain(b)));
}

TEST_CASE("elevation min and max are metres, not a hint", "[terrain][gen]") {
    // The fit is applied twice: once to the bare octave stack and again once the landform features
    // are stamped. Without the second, a style that lays down five ridge lines overshoots the range
    // it was given by more than the range itself -- the mountainous preset measured 396 m of relief
    // for a requested 210.
    for (const world::TerrainStyle style : world::terrainStyles()) {
        world::TerrainParams p = paramsFor(style);
        p.elevationMin = 0.0f;
        p.elevationMax = 60.0f;
        const world::WorldMap map = world::generateTerrain(p);
        INFO("style " << world::terrainStyleName(style) << " relief " << relief(map));
        // Water cuts below the floor by its own depth and terraces can sit at the ceiling, so this
        // is a tolerance rather than an equality -- but a factor of two is not a tolerance.
        CHECK(relief(map) > 40.0f);
        CHECK(relief(map) < 95.0f);
    }
}

TEST_CASE("relief scales with the world's extent", "[terrain][gen]") {
    // The density benchmark rungs are the same world at 420 m and 840 m. A preset whose elevation
    // range is an absolute metre figure gives the larger rung half the slope of the smaller one,
    // which is a plane, and every slope rule in the ecology reads it as one.
    const auto hills = [](float extent) {
        return world::generateTerrain(world::terrainPreset(world::TerrainStyle::RollingHills, extent));
    };
    const world::WorldMap small = hills(420.0f);
    const world::WorldMap large = hills(840.0f);
    CHECK(relief(large) > relief(small) * 1.5f);
}

TEST_CASE("generated rivers descend along their whole course", "[terrain][gen][water]") {
    // Water runs downhill. A course whose levels are not monotonic renders as a staircase of pools
    // and, where the level rises above the ground beside it, as a wall of water over the floodplain.
    for (const world::TerrainStyle style : world::terrainStyles()) {
        const world::WorldMap map = world::generateTerrain(paramsFor(style));
        for (const world::WaterCourse& c : world::waterCourses(map)) {
            if (c.kind != world::WaterKind::River) {
                continue;
            }
            INFO("style " << world::terrainStyleName(style) << " course " << c.name);
            REQUIRE(c.centreline.size() >= 3);
            for (std::size_t i = 1; i < c.centreline.size(); ++i) {
                CHECK(c.centreline[i].y <= c.centreline[i - 1].y + 1e-3f);
            }
            CHECK(c.descent >= 0.0f);
            CHECK(c.length > 0.0f);
        }
    }
}

TEST_CASE("a generated river occupies a depression with banks above it", "[terrain][gen][water]") {
    // §31: not a water polygon over random terrain. Along the centreline the bed must be under the
    // surface, and a few channel widths to either side the ground must be above it -- which is the
    // difference between a river and a canal painted on a hillside.
    const world::WorldMap map = world::generateTerrain(paramsFor(world::TerrainStyle::Valley));
    const std::vector<world::WaterCourse> courses = world::waterCourses(map);
    const auto river = std::find_if(courses.begin(), courses.end(),
                                    [](const world::WaterCourse& c) {
                                        return c.kind == world::WaterKind::River;
                                    });
    REQUIRE(river != courses.end());

    const world::TerrainQuery q = world::terrainQuery(map);
    int wet = 0;
    int banked = 0;
    int tested = 0;
    for (std::size_t i = 2; i + 2 < river->centreline.size(); ++i) {
        const glm::vec2 p(river->centreline[i].x, river->centreline[i].z);
        if (!q.inBounds(p, river->halfWidth * 3.0f)) {
            continue;
        }
        const glm::vec2 flow = river->flowAt(p);
        const glm::vec2 across(-flow.y, flow.x);
        const float surface = river->surfaceAt(p);
        // Where the bank is itself under water the course is running into a lake, and a lake is not
        // a failed bank. Skipped rather than counted either way, so the fraction below means what it
        // says about the channel rather than being diluted by the body it empties into.
        if (q.isWater(p + across * river->halfWidth * 1.5f) ||
            q.isWater(p - across * river->halfWidth * 1.5f)) {
            continue;
        }
        ++tested;
        if (q.waterDepthAt(p) > 0.05f) {
            ++wet;
        }
        // Inside the reach the generator undertakes to keep above the water line: each node's level
        // is taken from the lowest ground across two half-widths of channel. Measured at 1.5 rather
        // than at 2, because the undertaking is enforced at the traced points and this samples the
        // smoothed curve through them, which wanders a metre or two either side -- and on a 20%
        // hillside a metre sideways is a fifth of a metre of height.
        const float left = q.heightAt(p + across * river->halfWidth * 1.5f);
        const float right = q.heightAt(p - across * river->halfWidth * 1.5f);
        if (left > surface && right > surface) {
            ++banked;
        }
    }
    REQUIRE(tested > 4);
    INFO(wet << " of " << tested << " wet, " << banked << " banked");
    // Not every node: a course crossing a col has one bank lower than the other for a few metres,
    // and demanding perfection here would be demanding a generator that never cuts a water gap.
    CHECK(wet * 10 >= tested * 9);
    // Measured at 79% on this seed. The undertaking is enforced at the traced points; this samples
    // the smoothed curve through them, and the remaining fifth is where that curve wanders far
    // enough sideways on a steep flank to leave the lower bank under the line. Three quarters is the
    // floor below which the channel has stopped being a channel, not a target.
    CHECK(banked * 4 >= tested * 3);
}

TEST_CASE("standing water sits below its own surface", "[terrain][gen][water]") {
    // A pond whose bed is above its water line is a pond that renders as nothing at all, and the
    // rim has to stand above the water or it drains.
    bool found = false;
    for (const world::TerrainStyle style : world::terrainStyles()) {
        const world::WorldMap map = world::generateTerrain(paramsFor(style));
        const world::TerrainQuery q = world::terrainQuery(map);
        for (const world::WaterCourse& c : world::waterCourses(map)) {
            if (c.kind != world::WaterKind::Pond && c.kind != world::WaterKind::Lake) {
                continue;
            }
            found = true;
            const glm::vec2 centre(c.centreline.front().x, c.centreline.front().z);
            INFO("style " << world::terrainStyleName(style) << " " << c.name);
            CHECK(q.waterDepthAt(centre) > 0.2f);
            CHECK(q.isWater(centre));
        }
    }
    CHECK(found);
}

TEST_CASE("water amount at zero makes a dry world", "[terrain][gen][water]") {
    world::TerrainParams p = paramsFor(world::TerrainStyle::Basin);
    p.waterAmount = 0.0f;
    const world::WorldMap map = world::generateTerrain(p);
    CHECK(world::waterCourses(map).empty());
    const world::TerrainQuery q = world::terrainQuery(map);
    for (int i = 0; i < 64; ++i) {
        const glm::vec2 uv((i % 8 + 0.5f) / 8.0f, (i / 8 + 0.5f) / 8.0f);
        CHECK_FALSE(q.isWater(map.min() + map.size * uv));
    }
}

TEST_CASE("a water course reports a downstream direction and a falling surface", "[terrain][water]") {
    const world::WorldMap map = world::generateTerrain(paramsFor(world::TerrainStyle::Basin));
    const std::vector<world::WaterCourse> courses = world::waterCourses(map);
    const auto river = std::find_if(courses.begin(), courses.end(),
                                    [](const world::WaterCourse& c) {
                                        return c.kind == world::WaterKind::River;
                                    });
    REQUIRE(river != courses.end());
    const glm::vec2 head(river->centreline.front().x, river->centreline.front().z);
    const glm::vec2 mouth(river->centreline.back().x, river->centreline.back().z);
    CHECK_THAT(glm::length(river->flowAt(head)), WithinAbs(1.0f, 1e-4f));
    CHECK(river->surfaceAt(head) > river->surfaceAt(mouth));
    CHECK(river->alongAt(head) < river->alongAt(mouth));
    CHECK(river->flowSpeed() > 0.0f);
    CHECK(river->contains(head));
    CHECK(river->flowAt(head) != glm::vec2(0.0f));

    const auto still = std::find_if(courses.begin(), courses.end(),
                                    [](const world::WaterCourse& c) {
                                        return c.kind != world::WaterKind::River;
                                    });
    if (still != courses.end()) {
        // §12: ponds and lakes have no strong direction. Reporting one would have the renderer scroll
        // a pond in a straight line for ever.
        const glm::vec3& n = still->centreline.front();
        CHECK(still->flowAt(glm::vec2(n.x, n.z)) == glm::vec2(0.0f));
        CHECK(still->flowSpeed() == 0.0f);
    }
}

TEST_CASE("an authored world produces water courses too", "[terrain][water]") {
    // The seam is derived from WorldMap::features, not from the generator's own bookkeeping, so
    // Glowmere's hand-written river describes itself without being regenerated.
    const world::WorldMap map = world::defaultWorld();
    const std::vector<world::WaterCourse> courses = world::waterCourses(map);
    REQUIRE_FALSE(courses.empty());
    const auto river = std::find_if(courses.begin(), courses.end(),
                                    [](const world::WaterCourse& c) {
                                        return c.kind == world::WaterKind::River;
                                    });
    REQUIRE(river != courses.end());
    CHECK(river->descent > 0.0f);
    CHECK(river->length > 100.0f);
    CHECK(river->halfWidth > 0.0f);
}

TEST_CASE("terrain parameters round-trip through JSON", "[terrain][gen]") {
    for (const world::TerrainStyle style : world::terrainStyles()) {
        const world::TerrainParams p = paramsFor(style);
        const auto back = world::TerrainParams::fromJson(p.toJson());
        REQUIRE(back);
        INFO("style " << world::terrainStyleName(style));
        CHECK(back->structuralHash() == world::TerrainParams{p}.structuralHash());
        CHECK(back->style == style);
    }
    // A block naming only a style is legal and means "that landform, and the rest as usual".
    const auto sparse = world::TerrainParams::fromJson(nlohmann::json{{"style", "basin"}});
    REQUIRE(sparse);
    CHECK(sparse->style == world::TerrainStyle::Basin);
    CHECK(sparse->valleyStrength == world::terrainPreset(world::TerrainStyle::Basin).valleyStrength);
    // An unknown style is an error rather than a silent fallback: a typo that becomes rolling hills
    // is a typo nobody finds.
    CHECK_FALSE(world::TerrainParams::fromJson(nlohmann::json{{"style", "fjord"}}));
    CHECK_FALSE(world::TerrainParams::fromJson(nlohmann::json{{"roughness", 4.0f}}));
}

TEST_CASE("style names round-trip", "[terrain][gen]") {
    for (const world::TerrainStyle style : world::terrainStyles()) {
        const auto back = world::terrainStyleFromName(world::terrainStyleName(style));
        REQUIRE(back);
        CHECK(*back == style);
    }
    CHECK(world::terrainStyleFromName("rolling-hills") == world::TerrainStyle::RollingHills);
    CHECK_FALSE(world::terrainStyleFromName("nonsense"));
}

TEST_CASE("a recipe carries its terrain and the world's seed wins", "[terrain][gen][recipe]") {
    nlohmann::json j;
    j["world"] = "test";
    j["seed"] = 77u;
    j["extent"] = 500.0f;
    j["terrain"] = nlohmann::json{{"style", "plateau"}, {"seed", 5u}, {"extent", 90.0f}};
    const auto recipe = world::WorldRecipe::fromJson(j);
    REQUIRE(recipe);
    CHECK(recipe->terrain.style == world::TerrainStyle::Plateau);
    // A world with two seeds or two sizes is a world whose terrain and whose ecology were composed
    // for different places.
    CHECK(recipe->terrain.seed == 77u);
    CHECK(recipe->terrain.extent == 500.0f);
    CHECK(recipe->terrain.name == "test");

    // A recipe with no terrain block is legal and gets the default landform at its own size.
    nlohmann::json bare;
    bare["world"] = "bare";
    bare["extent"] = 800.0f;
    const auto plain = world::WorldRecipe::fromJson(bare);
    REQUIRE(plain);
    CHECK(plain->terrain.style == world::TerrainStyle::RollingHills);
    CHECK(plain->terrain.extent == 800.0f);
    CHECK(plain->terrain.elevationMax > world::terrainPreset(world::TerrainStyle::RollingHills).elevationMax);
}

TEST_CASE("the feature path accelerator changes no height", "[terrain][gen][determinism]") {
    // `Feature::blocks` skips runs of eight segments whose bounding box is further from the sample
    // than the nearest segment found so far. That cannot change the answer -- every point of a block
    // is inside its box, and the nearest-segment update is a strict less-than -- and this is the
    // check that keeps it true, because an accelerator that is wrong by an ulp is an accelerator that
    // breaks every byte-identical capture in the repository.
    for (const world::TerrainStyle style : world::terrainStyles()) {
        const world::WorldMap fast = world::generateTerrain(paramsFor(style));
        world::WorldMap slow = fast;
        for (world::Feature& f : slow.features) {
            f.blocks.clear();
        }
        INFO("style " << world::terrainStyleName(style) << ", " << fast.features.size() << " features");
        CHECK(lattice(fast, 97) == lattice(slow, 97));
        for (int i = 0; i < 97 * 97; ++i) {
            const glm::vec2 uv((i % 97 + 0.5f) / 97.0f, (i / 97 + 0.5f) / 97.0f);
            const glm::vec2 q = fast.min() + fast.size * uv;
            REQUIRE(fast.waterSurface(q) == slow.waterSurface(q));
            REQUIRE(fast.moisture(q, fast.altitude01(fast.height(q))) ==
                    slow.moisture(q, slow.altitude01(slow.height(q))));
        }
    }
}

// ---- the block accelerator is an accelerator, and this is what says so (ADR-482) ----------------
//
// `closestOnPath` is **62% of an entire timeline scrub** on Glowmere: `Navigator::pathClear` asks
// `WorldMap::sample` per walker per step of a 5,400-step replay, and that reaches this function
// once per terrain feature. The interactive-performance pass made the per-block skip bite from the
// first block instead of only after `best` had shrunk, which is a change to how many segments are
// visited and must be no change at all to the answer.
//
// `Feature::blocks` is documented as "purely an accelerator... passing an empty span walks every
// segment and gives the identical result". That is the contract, and until now nothing checked it
// -- the determinism captures would have caught a *drift* between two runs of the same build, and
// this is the other thing: a difference between the accelerated and the unaccelerated answer, in
// one build, which no comparison of two identical runs can see.
//
// Bit for bit over 4,225 heights per style, because the tolerance for this is zero. A height that
// differs in the last mantissa bit moves a walker by a hair, and a walker that is a hair away takes
// a different line past a trunk one second later -- the same argument ADR-295 makes about the
// navigation grid, and the reason that grid is allowed to refuse.
TEST_CASE("the path block accelerator returns the identical height, not merely a close one",
          "[terrain][gen][accel]") {
    for (const world::TerrainStyle style : world::terrainStyles()) {
        world::WorldMap accelerated = world::generateTerrain(paramsFor(style));
        std::size_t featuresWithBlocks = 0;
        for (const world::Feature& f : accelerated.features) {
            featuresWithBlocks += f.blocks.empty() ? 0u : 1u;
        }

        // The unaccelerated reference: the same prepared world with the block boxes thrown away,
        // so every segment of every feature is walked.
        world::WorldMap reference = accelerated;
        for (world::Feature& f : reference.features) {
            f.blocks.clear();
        }

        INFO("style " << world::terrainStyleName(style) << ", " << featuresWithBlocks
                      << " feature(s) carrying block boxes");
        const std::vector<float> fast = lattice(accelerated);
        const std::vector<float> slow = lattice(reference);
        REQUIRE(fast.size() == slow.size());
        std::size_t differing = 0;
        double worst = 0.0;
        std::size_t firstAt = 0;
        for (std::size_t i = 0; i < fast.size(); ++i) {
            if (fast[i] != slow[i]) {
                if (differing == 0) { firstAt = i; }
                ++differing;
                worst = std::max(worst, std::fabs(static_cast<double>(fast[i]) - slow[i]));
            }
        }
        INFO("differing " << differing << " of " << fast.size() << ", worst " << worst
                          << " m, first at " << firstAt << " (fast " << fast[firstAt] << " slow "
                          << slow[firstAt] << ")");
        CHECK(differing == 0);
    }
}

// The control (ADR-182). The test above compares two things that are supposed to agree, and a
// comparison of two things that agree proves nothing unless something could have made them
// disagree. A block box that is too small is exactly the defect the accelerator can have -- it
// rejects a block that did hold the nearest segment -- and it must be caught.
TEST_CASE("a block box that lies is caught by the same comparison", "[terrain][gen][accel]") {
    world::WorldMap sabotaged = world::generateTerrain(paramsFor(world::TerrainStyle::Valley));
    world::WorldMap reference = sabotaged;
    for (world::Feature& f : reference.features) {
        f.blocks.clear();
    }
    // Collapse every box to a point far outside the world. Every block is then "further away" than
    // anything real, so the skip fires on all of them and the answer comes from whatever survives.
    std::size_t boxes = 0;
    for (world::Feature& f : sabotaged.features) {
        for (glm::vec4& box : f.blocks) {
            box = glm::vec4(1.0e6f, 1.0e6f, 1.0e6f, 1.0e6f);
            ++boxes;
        }
    }
    REQUIRE(boxes > 0); // a world with no blocks would make this test vacuous

    const std::vector<float> lying = lattice(sabotaged);
    const std::vector<float> honest = lattice(reference);
    std::size_t differing = 0;
    for (std::size_t i = 0; i < lying.size() && i < honest.size(); ++i) {
        differing += lying[i] == honest[i] ? 0u : 1u;
    }
    INFO(boxes << " box(es) collapsed; " << differing << " of " << honest.size() << " heights moved");
    CHECK(differing > 0);
}
