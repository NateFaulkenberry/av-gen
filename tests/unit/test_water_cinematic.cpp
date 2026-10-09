// The cinematic water path (ADR-1286): the settings' CPU half (the packing and the shader source are checked
// with the GPU tests, where the renderer's headers are available). The GPU half -- that a surface with every
// cinematic setting at 0 is byte-identical to the shader with the cinematic code removed, and that each
// setting reaches the picture -- is test_water_cinematic_gpu.cpp.

#include "assets/asset_registry.hpp"
#include "scene/composition.hpp"
#include "scene/water_surface.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <string>

using namespace avgen;
using json = nlohmann::json;

namespace {

json terrainWith(const json& water) {
    json doc = json::parse(R"({
      "format": "avgen-scene", "version": 1, "name": "cinematic",
      "camera": { "mode": 1, "position": [0, 30, 60], "target": [0, 0, -60], "fov": 50.0 },
      "nodes": [
        { "name": "sea", "kind": "terrain",
          "world": { "name": "small", "size": [160, 160] },
          "terrain": { "chunkSize": 40.0, "resolution": 8, "lodLevels": 2,
                       "lodDistance": 50.0, "viewDistance": 400.0 } }
      ]
    })");
    doc["nodes"][0]["terrain"]["water"] = water;
    return doc;
}

const scene::WaterSettings& waterOf(const scene::Composition& comp) {
    const scene::CompositionNode* node = comp.findNode("sea");
    REQUIRE(node != nullptr);
    return node->terrain.water;
}

} // namespace

TEST_CASE("cinematic water settings are off by default and round-trip through a scene file",
          "[unit][water][adr1286]") {
    const scene::WaterSettings defaults;
    CHECK_FALSE(defaults.cinematic());
    CHECK(defaults.validate());

    assets::AssetRegistry registry;
    const json authored = {{"ssr", 0.8},           {"ssrDistance", 90.0},      {"ssrSteps", 96.0},
                           {"ssrGain", 1.5},       {"transmission", 1.0},      {"absorption", {3.0, 2.0, 1.1}},
                           {"undulation", 0.6},    {"undulationScale", 0.06},  {"roughnessVariation", 0.7},
                           {"streak", 0.08},       {"reflectionRays", 4.0},    {"tintPatches", 0.7},
                           {"reflectionTint2", {0.2, 0.1, 0.5}}, {"tintPatchScale", 0.05}, {"glints", 3.0}};
    auto loaded = scene::Composition::fromJson(terrainWith(authored), registry);
    REQUIRE(loaded.has_value());
    const scene::WaterSettings& w = waterOf(**loaded);
    CHECK(w.cinematic());
    CHECK(w.ssr == 0.8f);
    CHECK(w.ssrDistance == 90.0f);
    CHECK(w.ssrSteps == 96.0f);
    CHECK(w.ssrGain == 1.5f);
    CHECK(w.transmission == 1.0f);
    CHECK(w.absorption == glm::vec3(3.0f, 2.0f, 1.1f));
    CHECK(w.undulation == 0.6f);
    CHECK(w.undulationScale == 0.06f);
    CHECK(w.roughnessVariation == 0.7f);
    CHECK(w.streak == 0.08f);
    CHECK(w.reflectionRays == 4.0f);
    CHECK(w.tintPatches == 0.7f);
    CHECK(w.reflectionTint2 == glm::vec3(0.2f, 0.1f, 0.5f));
    CHECK(w.tintPatchScale == 0.05f);
    CHECK(w.glints == 3.0f);

    // written back and read again: the same settings (a key the writer drops is not a setting, ADR-225)
    auto again = scene::Composition::fromJson((*loaded)->toJson(), registry);
    REQUIRE(again.has_value());
    const scene::WaterSettings& b = waterOf(**again);
    CHECK(b.ssr == w.ssr);
    CHECK(b.ssrDistance == w.ssrDistance);
    CHECK(b.ssrSteps == w.ssrSteps);
    CHECK(b.ssrGain == w.ssrGain);
    CHECK(b.transmission == w.transmission);
    CHECK(b.absorption == w.absorption);
    CHECK(b.undulation == w.undulation);
    CHECK(b.undulationScale == w.undulationScale);
    CHECK(b.roughnessVariation == w.roughnessVariation);
    CHECK(b.streak == w.streak);
    CHECK(b.reflectionRays == w.reflectionRays);
    CHECK(b.tintPatches == w.tintPatches);
    CHECK(b.reflectionTint2 == w.reflectionTint2);
    CHECK(b.tintPatchScale == w.tintPatchScale);
    CHECK(b.glints == w.glints);
}

TEST_CASE("cinematic water settings refuse nonsense and say which key", "[unit][water][adr1286]") {
    assets::AssetRegistry registry;
    struct Bad {
        json water;
        const char* mentions;
    };
    const Bad bad[] = {
        {{{"ssr", 1.5}}, "ssr"},
        {{{"ssrDistance", 0.5}}, "ssrDistance"},
        {{{"ssrSteps", 4.0}}, "ssrSteps"},
        {{{"ssrGain", -1.0}}, "ssrGain"},
        {{{"transmission", 2.0}}, "transmission"},
        {{{"absorption", {1.0, -1.0, 1.0}}}, "absorption"},
        {{{"undulation", 30.0}}, "undulation"},
        {{{"undulationScale", 0.0}}, "undulationScale"},
        {{{"roughnessVariation", 1.2}}, "roughnessVariation"},
        {{{"streak", 3.0}}, "streak"},
        {{{"reflectionRays", 0.0}}, "reflectionRays"},
        {{{"tintPatches", -0.1}}, "tintPatches"},
        {{{"tintPatchScale", 0.0}}, "tintPatchScale"},
        {{{"glints", 60.0}}, "glints"},
    };
    for (const Bad& b : bad) {
        INFO(b.water.dump());
        auto loaded = scene::Composition::fromJson(terrainWith(b.water), registry);
        REQUIRE_FALSE(loaded.has_value());
        INFO(loaded.error().message);
        CHECK(loaded.error().message.find(b.mentions) != std::string::npos);
    }
}

