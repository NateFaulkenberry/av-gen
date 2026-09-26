// A feature's level is continuous across its path's bends, and two waters meet without a wall.
//
// Found by `avgen_world_preview --seams` on Glowmere Valley 2 while scouting Glowmere Valley 3:
//
//   * four straight cliffs of up to 2.35 m across the valley. A path feature's level is the path's
//     height at the NEAREST point, and on the inside of a bend the nearest point jumps from one arm
//     to the other across the medial axis -- so a 300 m valley corridor that descends with its river
//     flattened the ground toward two different heights on either side of a line;
//   * a 2.63 m wall of water across the river where the elder-pool's reach ended: the water line was
//     the HIGHEST surface of any water reaching a point, and the pool's sits 1.2 m above the river's.
//
// Each case has a control that shows the old rule would have failed on the same samples (ADR-182),
// so a pass is not a transect that simply missed the fault.

#include "world/world_map.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <vector>

using namespace avgen;

namespace {

// The level the old rule used: the path's height at the single nearest point.
float nearestLevel(const std::vector<glm::vec3>& path, glm::vec2 p) {
    float best = std::numeric_limits<float>::max();
    float level = 0.0f;
    for (std::size_t i = 0; i + 1 < path.size(); ++i) {
        const glm::vec2 a(path[i].x, path[i].z);
        const glm::vec2 ab = glm::vec2(path[i + 1].x, path[i + 1].z) - a;
        const float t = glm::clamp(glm::dot(p - a, ab) / glm::dot(ab, ab), 0.0f, 1.0f);
        const float d = glm::distance(p, a + ab * t);
        if (d < best) {
            best = d;
            level = glm::mix(path[i].y, path[i + 1].y, t);
        }
    }
    return level;
}

// The largest change between neighbouring samples along a line, `steps` of them.
float largestStep(const auto& sample, glm::vec2 from, glm::vec2 to, int steps, glm::vec2* where = nullptr) {
    float worst = 0.0f;
    float previous = sample(from);
    for (int k = 1; k <= steps; ++k) {
        const glm::vec2 q = glm::mix(from, to, static_cast<float>(k) / static_cast<float>(steps));
        const float v = sample(q);
        if (std::abs(v - previous) > worst) {
            worst = std::abs(v - previous);
            if (where != nullptr) {
                *where = q;
            }
        }
        previous = v;
    }
    return worst;
}

// A right-angled bend whose level falls from 10 to 0 along its course: the upper arm runs from
// (-100, -100) to the corner at the origin, the lower arm back out to (-100, 100). Inside the bend
// (x < 0) the medial axis is the line z = 0, where the two arms are equally near.
world::WorldMap bentCorridor() {
    world::WorldMap map;
    map.size = {400.0f, 400.0f};
    map.baseHeight = 5.0f;
    world::Feature corridor;
    corridor.name = "corridor";
    corridor.kind = world::FeatureKind::Flat;
    corridor.path = {{-100.0f, 10.0f, -100.0f}, {0.0f, 5.0f, 0.0f}, {-100.0f, 0.0f, 100.0f}};
    corridor.width = 200.0f;
    corridor.flatten = 1.0f;
    corridor.smoothing = 0; // the polyline exactly, so the axis is exactly z = 0
    map.features.push_back(corridor);
    map.prepare();
    return map;
}

} // namespace

TEST_CASE("a descending corridor's level is continuous across its bend's medial axis", "[unit][world][seams]") {
    world::WorldMap map = bentCorridor();
    REQUIRE(map.validate().has_value());
    const auto& path = map.features.front().samplePath();

    // Control: the old nearest-point level jumps by metres across this very line.
    const glm::vec2 north(-60.0f, -0.01f);
    const glm::vec2 south(-60.0f, 0.01f);
    REQUIRE(std::abs(nearestLevel(path, north) - nearestLevel(path, south)) > 1.0f);

    // The height across the axis, a millimetre at a time.
    glm::vec2 at{0.0f};
    const float worst = largestStep([&](glm::vec2 q) { return map.height(q); }, {-60.0f, -12.0f},
                                    {-60.0f, 12.0f}, 24000, &at);
    INFO("largest 1 mm step " << worst << " m at (" << at.x << ", " << at.y << ")");
    CHECK(worst < 0.01f);

    // On the axis the level is the two arms' mean, which for this symmetric bend is 5 -- the corner's
    // level and the base height -- where either arm alone would put the ground at 6.3 or 3.7.
    CHECK(std::abs(0.5f * (nearestLevel(path, north) + nearestLevel(path, south)) - 5.0f) < 1e-3f);
    CHECK(std::abs(map.height({-60.0f, 0.0f}) - 5.0f) < 0.05f);
    // Not vacuous: 12 m either side, the corridor has pulled the ground well off the base height.
    CHECK(std::abs(map.height({-60.0f, -12.0f}) - map.baseHeight) > 0.5f);
}

TEST_CASE("beside a straight run a corridor's level is the nearest point's", "[unit][world][seams]") {
    world::WorldMap map = bentCorridor();
    const auto& path = map.features.front().samplePath();
    // Halfway along the upper arm, 20 m off it on the outside of the bend: one arm in reach, a
    // symmetric stretch of it, so the average and the nearest point agree and the nearest is kept.
    const glm::vec2 along(-50.0f, -50.0f);
    const glm::vec2 outward = glm::normalize(glm::vec2(1.0f, -1.0f));
    for (const float off : {5.0f, 20.0f, 40.0f}) {
        const glm::vec2 q = along + outward * off;
        const float d = off;
        const float u = 1.0f - d / 200.0f;
        const float w = u * u * (3.0f - 2.0f * u); // featureWeight's smoothstep, falloff 1
        const float expected = glm::mix(map.baseHeight, nearestLevel(path, q), w);
        INFO("offset " << off << " m");
        CHECK(std::abs(map.height(q) - expected) < 1e-4f);
    }
}

TEST_CASE("a pool perched above a river meets the river's water line without a wall", "[unit][world][seams]") {
    world::WorldMap map;
    map.size = {200.0f, 200.0f};
    map.baseHeight = 5.0f;
    world::Feature river;
    river.name = "river";
    river.kind = world::FeatureKind::River;
    river.path = {{0.0f, 0.0f, -100.0f}, {0.0f, 0.0f, 100.0f}};
    river.width = 26.0f;
    river.amplitude = 3.6f;
    river.water = true;
    river.smoothing = 0;
    map.features.push_back(river);
    world::Feature pool; // Glowmere's elder-pool: bed 1.1 m, 1.3 m deep, reaching 36 m
    pool.name = "pool";
    pool.kind = world::FeatureKind::Flat;
    pool.path = {{0.0f, 1.1f, 0.0f}};
    pool.width = 36.0f;
    pool.flatten = 0.8f;
    pool.falloff = 0.9f;
    pool.water = true;
    pool.waterDepth = 1.3f;
    pool.smoothing = 0;
    map.features.push_back(pool);
    map.prepare();
    REQUIRE(map.validate().has_value());

    // Control: the pool's surface (2.4 m) stands above the river's (0), and it really does raise the
    // water where both reach -- so the highest-surface rule would drop about 2.4 m in one step where
    // the pool's reach ends at z = 36, and a smooth transect below is not a pool that never counted.
    REQUIRE(map.waterSurface({0.0f, 0.0f}) > 1.0f);
    REQUIRE(map.waterSurface({0.0f, 35.0f}) > 0.0f);

    // Down the river's centre line, through the edge of the pool's reach.
    glm::vec2 at{0.0f};
    const float worst = largestStep([&](glm::vec2 q) { return map.waterSurface(q); }, {0.0f, 20.0f},
                                    {0.0f, 50.0f}, 30000, &at);
    INFO("largest 1 mm step of the water line " << worst << " m at z = " << at.y);
    CHECK(worst < 0.01f);

    // Past the pool's reach the river's surface is its own, exactly.
    CHECK(map.waterSurface({0.0f, 60.0f}) == 0.0f);
}

TEST_CASE("one water alone keeps its own surface", "[unit][world][seams]") {
    world::WorldMap map;
    map.size = {200.0f, 200.0f};
    world::Feature river;
    river.name = "river";
    river.kind = world::FeatureKind::River;
    river.path = {{0.0f, 2.0f, -100.0f}, {0.0f, -2.0f, 100.0f}};
    river.width = 20.0f;
    river.water = true;
    river.waterDepth = 0.5f;
    river.smoothing = 0;
    map.features.push_back(river);
    map.prepare();
    // Beside a straight river, 3 m off its line at z = 50: level 2 - 4 * 0.75 = -1, plus the depth.
    CHECK(std::abs(map.waterSurface({3.0f, 50.0f}) - (-1.0f + 0.5f)) < 1e-5f);
}

TEST_CASE("Glowmere Valley 2's terrain has no seam across its bends", "[world][glowmere][seams]") {
    // The world is inline in the scene file, so this needs no licensed asset and runs anywhere.
    const std::filesystem::path scene =
        std::filesystem::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-valley-2-multicam.scene.json";
    std::ifstream in(scene);
    REQUIRE(in.good());
    nlohmann::json doc;
    in >> doc;
    const nlohmann::json* world = nullptr;
    for (const auto& node : doc.at("nodes")) {
        if (node.contains("world")) {
            world = &node.at("world");
            break;
        }
    }
    REQUIRE(world != nullptr);
    auto parsed = world::worldMapFromJson(*world);
    REQUIRE(parsed.has_value());
    world::WorldMap map = std::move(*parsed);
    map.prepare();

    // Across each of the four cliffs the scan found (2.03, 2.28, 1.24 and 2.32 m), and the water
    // wall (2.63 m), a centimetre at a time.
    struct Crossing {
        glm::vec2 from;
        glm::vec2 to;
        bool water;
    };
    const Crossing crossings[] = {
        {{-200.0f, -110.0f}, {-200.0f, -80.0f}, false}, // west of the bend at (27, -92)
        {{150.0f, 45.0f}, {150.0f, 70.0f}, false},      // east of the bend at (-41, 76)
        {{120.0f, -320.0f}, {120.0f, -290.0f}, false},  // under the northern rim
        {{-100.0f, 230.0f}, {-100.0f, 255.0f}, false},  // west of the bend at (45, 244)
        {{-35.0f, 70.0f}, {-35.0f, 95.0f}, true},       // down the river, out of the elder-pool
    };
    const auto feature = [&](const char* name) -> const world::Feature& {
        const auto it = std::find_if(map.features.begin(), map.features.end(),
                                     [&](const world::Feature& f) { return f.name == name; });
        REQUIRE(it != map.features.end());
        return *it;
    };
    const auto& corridor = feature("valley-corridor").samplePath();
    const world::Feature& pool = feature("elder-pool");
    const world::Feature& river = feature("glowmere-run-2");
    for (const Crossing& c : crossings) {
        // Control: each ground crossing really crosses the corridor's medial axis -- the old nearest
        // level jumps by more than a metre along it -- and the water crossing leaves a pool whose
        // surface stands a metre above the river's.
        if (!c.water) {
            REQUIRE(largestStep([&](glm::vec2 q) { return nearestLevel(corridor, q); }, c.from, c.to, 2500) > 1.0f);
        } else {
            REQUIRE(pool.path.front().y + pool.waterDepth - nearestLevel(river.samplePath(), c.to) > 1.0f);
        }
        glm::vec2 at{0.0f};
        const float worst =
            c.water ? largestStep([&](glm::vec2 q) { return map.waterSurface(q); }, c.from, c.to, 2500, &at)
                    : largestStep([&](glm::vec2 q) { return map.height(q); }, c.from, c.to, 2500, &at);
        INFO((c.water ? "water" : "ground") << " crossing from (" << c.from.x << ", " << c.from.y << "): largest 1 cm step "
                                            << worst << " m at (" << at.x << ", " << at.y << ")");
        CHECK(worst < 0.05f);
    }
}
