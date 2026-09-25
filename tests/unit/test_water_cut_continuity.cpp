// ADR-830: two waters that overlap cut the terrain continuously.
//
// Ember's feet jumped 0.40 m in one frame at 17 s in the multicam film, climbing a bank. The bank
// was an 0.88 m cliff in the terrain: the height function kept ONE cut target (the lowest of the
// waters') and ONE weight (the largest), and wherever a pool's influence began inside a river's
// shoulder, the pool's low bed arrived at the river's weight in one step. The foot IK and body
// compensation were doing their job on a surface that had a step in it.

#include "app/engine.hpp"
#include "scene/animation.hpp"
#include "scene/composition.hpp"
#include "scene/detail_limits.hpp"
#include "scene/pose_layers.hpp"
#include "world/terrain_query.hpp"
#include "world/world_map.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <map>
#include <string>

using namespace avgen;

TEST_CASE("a pool reaching into a river's shoulder does not cut a cliff", "[unit][world][adr830]") {
    world::WorldMap map;
    map.size = {200.0f, 200.0f};
    map.baseHeight = 5.0f; // flat ground: every change in height below is a cut
    world::Feature river;
    river.name = "river";
    river.kind = world::FeatureKind::River;
    river.path = {{0.0f, 0.0f, -100.0f}, {0.0f, 0.0f, 100.0f}};
    river.width = 20.0f;
    river.amplitude = 1.0f; // bed at -1
    river.water = true;
    map.features.push_back(river);
    world::Feature pool;
    pool.name = "pool";
    pool.kind = world::FeatureKind::Flat;
    pool.path = {{15.0f, -3.0f, -1.0f}, {15.0f, -3.0f, 1.0f}}; // bed at -3, reach 10 m: from x = 5
    pool.width = 10.0f;
    pool.water = true;
    map.features.push_back(pool);
    map.prepare();
    REQUIRE(map.validate().has_value());

    // Across the river's shoulder and the pool's edge, a millimetre at a time.
    float worst = 0.0f;
    float at = 0.0f;
    float previous = map.height({0.0f, 0.0f});
    for (int i = 1; i <= 25000; ++i) {
        const float x = static_cast<float>(i) * 0.001f;
        const float h = map.height({x, 0.0f});
        if (std::abs(h - previous) > worst) {
            worst = std::abs(h - previous);
            at = x;
        }
        previous = h;
    }
    INFO("largest 1 mm step " << worst << " m at x = " << at);
    CHECK(worst < 0.01f); // the old cut: 1.67 m at x = 5, where the pool begins

    // Each water alone is unchanged: a single cut is exactly what it was.
    world::WorldMap alone = map;
    alone.features.pop_back();
    alone.prepare();
    const float u = 1.0f - (3.0f / 20.0f);
    const float weight = u * u * (3.0f - (2.0f * u)); // featureWeight's smoothstep
    CHECK(std::abs(alone.height({3.0f, 0.0f}) - (5.0f + ((-1.0f - 5.0f) * weight))) < 1e-4f);
}

namespace {
bool glowmerePresent() {
    return std::filesystem::exists(std::filesystem::path(AVGEN_SOURCE_DIR) / "assets" / "aliens" / "alien-scout.glb");
}
std::filesystem::path multicam() {
    return std::filesystem::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-valley-2-multicam.json";
}
} // namespace

TEST_CASE("the Glowmere bank Ember climbed is a slope, not a step", "[world][glowmere][adr830]") {
    if (!glowmerePresent()) {
        SKIP("Glowmere assets are not present");
    }
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(multicam()).has_value());
    const world::TerrainQuery terrain = engine.composition()->terrainQuery();
    REQUIRE(terrain.map != nullptr);
    // Ember's line up the bank, where the elder pool's reach meets glowmere-run-2's shoulder.
    const glm::vec2 from(-61.0f, 39.6f);
    const glm::vec2 to(-61.45f, 37.8f);
    float worst = 0.0f;
    float previous = terrain.map->height(from);
    for (int k = 1; k <= 2000; ++k) {
        const float h = terrain.map->height(glm::mix(from, to, static_cast<float>(k) / 2000.0f));
        worst = std::max(worst, std::abs(h - previous));
        previous = h;
    }
    INFO("largest step along the 1.86 m line, at 0.9 mm spacing: " << worst << " m");
    CHECK(worst < 0.01f); // was 0.875
}

// The film, per posed frame: no alien's foot jumps. Ember's worst was 0.396 m at 17.00 s.
TEST_CASE("no alien's foot jumps in the first 40 s of the multicam film", "[world][glowmere][adr830][benchmark]") {
    if (!glowmerePresent()) {
        SKIP("Glowmere assets are not present");
    }
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(multicam()).has_value());
    engine.setDetailLimits(scene::DetailLimits::unlimited());
    struct Track {
        double posed = -1.0;
        bool has = false;
        glm::vec3 l{0.0f};
        glm::vec3 r{0.0f};
        float worst = 0.0f;
        double at = 0.0;
    };
    std::map<std::string, Track> tracks;
    for (const char* name : {"rook", "tide", "sage", "ember", "vane"}) {
        tracks[name] = Track{};
    }
    for (int i = 0; i <= 60 * 40; ++i) {
        engine.update(FrameTime{i / 60.0, i == 0 ? 0.0 : 1.0 / 60.0, static_cast<std::uint64_t>(i)});
        for (auto& [name, t] : tracks) {
            const scene::CompositionNode* node = engine.composition()->findNode(name);
            REQUIRE(node != nullptr);
            const scene::SkinnedRig& rig = engine.composition()->scene().rigs.at(node->rigs.front());
            if (rig.paletteTime == t.posed) {
                continue; // a frame the 30 Hz rig was not re-posed on
            }
            t.posed = rig.paletteTime;
            std::vector<glm::mat4> model;
            scene::poseToModel(rig.skeleton, rig.pose, model);
            const glm::vec3 l(model[static_cast<std::size_t>(rig.skeleton.find("foot.l"))][3]);
            const glm::vec3 r(model[static_cast<std::size_t>(rig.skeleton.find("foot.r"))][3]);
            if (t.has) {
                const float h = std::max(std::abs(l.y - t.l.y), std::abs(r.y - t.r.y));
                if (h > t.worst) {
                    t.worst = h;
                    t.at = i / 60.0;
                }
            }
            t.has = true;
            t.l = l;
            t.r = r;
        }
    }
    for (const auto& [name, t] : tracks) {
        INFO(name << ": worst foot-height step " << t.worst << " m at " << t.at << " s");
        // A fast walk down a steep bank bobs a foot ~0.1 per posed frame (Vane, 16.5 s); the step
        // in the terrain threw Ember's 0.40.
        CHECK(t.worst < 0.15f);
    }
}
