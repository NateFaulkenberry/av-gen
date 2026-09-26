// Deliberate water tears (ADR-916): the settings' CPU half. The GPU half -- that each setting
// reaches the picture, that the seams are what they claim to be -- is test_water_tears_gpu.cpp.
//
// What is checked here is that an authored tear survives the round trip a scene file takes, that
// nonsense is refused with a message naming the key, and that exactly the three routable settings are
// parameters and reach the surface the renderer reads.

#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "scene/water_surface.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <limits>
#include <string>

using namespace avgen;
using json = nlohmann::json;

namespace {

json terrainWith(const json& water) {
    json doc = json::parse(R"({
      "format": "avgen-scene", "version": 1, "name": "tears",
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

TEST_CASE("tear settings round-trip through a scene file", "[unit][water][tears]") {
    // Off by default, and valid as shipped.
    const scene::WaterSettings defaults;
    CHECK(defaults.tears == 0.0f);
    CHECK(defaults.tearFollowsWind);
    CHECK(defaults.validate());

    assets::AssetRegistry registry;
    const json authored = {{"enabled", true},      {"tears", 0.45},        {"tearShear", 2.5},
                           {"tearCoverage", 0.6},  {"tearCell", 0.9},      {"tearSpacing", 22.0},
                           {"tearStretch", 2.2},   {"tearDirection", 1.1}, {"tearDrift", -0.2},
                           {"tearWind", 0.3}};
    auto loaded = scene::Composition::fromJson(terrainWith(authored), registry);
    REQUIRE(loaded.has_value());
    const scene::WaterSettings& w = waterOf(**loaded);
    CHECK(w.tears == 0.45f);
    CHECK(w.tearShear == 2.5f);
    CHECK(w.tearCoverage == 0.6f);
    CHECK(w.tearCell == 0.9f);
    CHECK(w.tearSpacing == 22.0f);
    CHECK(w.tearStretch == 2.2f);
    CHECK_FALSE(w.tearFollowsWind); // a number is an angle
    CHECK(w.tearAngle == 1.1f);
    CHECK(w.tearDrift == -0.2f);
    CHECK(w.tearWind == 0.3f);

    // Written back out, and read again: the same settings. A key the writer drops or the reader
    // ignores is the "a setting the application does not keep is not a setting" defect (ADR-225).
    const json written = (*loaded)->toJson();
    const json& water = written.at("nodes").at(0).at("terrain").at("water");
    CHECK(water.at("tearDirection").is_number());
    auto again = scene::Composition::fromJson(written, registry);
    REQUIRE(again.has_value());
    const scene::WaterSettings& back = waterOf(**again);
    CHECK(back.tears == w.tears);
    CHECK(back.tearShear == w.tearShear);
    CHECK(back.tearCoverage == w.tearCoverage);
    CHECK(back.tearCell == w.tearCell);
    CHECK(back.tearSpacing == w.tearSpacing);
    CHECK(back.tearStretch == w.tearStretch);
    CHECK(back.tearFollowsWind == w.tearFollowsWind);
    CHECK(back.tearAngle == w.tearAngle);
    CHECK(back.tearDrift == w.tearDrift);
    CHECK(back.tearWind == w.tearWind);

    // "wind" is the other spelling, and it survives the round trip as itself.
    auto windy = scene::Composition::fromJson(terrainWith({{"tears", 0.3}, {"tearDirection", "wind"}}), registry);
    REQUIRE(windy.has_value());
    CHECK(waterOf(**windy).tearFollowsWind);
    const json windyOut = (*windy)->toJson();
    CHECK(windyOut.at("nodes").at(0).at("terrain").at("water").at("tearDirection") == "wind");
}

TEST_CASE("tear settings reject nonsense and say which key", "[unit][water][tears]") {
    assets::AssetRegistry registry;
    struct Bad {
        json water;
        const char* mentions;
    };
    const Bad bad[] = {
        {{{"tearDirection", "north"}}, "tearDirection"},
        {{{"tearDirection", true}}, "tearDirection"},
        {{{"tearShear", 9.0}}, "tearShear"},
        {{{"tearCoverage", 1.5}}, "tearCoverage"},
        {{{"tearCell", 0.0}}, "tearCell"},
        {{{"tearCell", 2.0}, {"tearSpacing", 3.0}}, "tearSpacing"}, // closer than two cells
        {{{"tearStretch", 0.1}}, "tearStretch"},
        {{{"tearDrift", 25.0}}, "tearDrift"},
        {{{"tearWind", -0.1}}, "tearWind"},
        {{{"tears", -1.0}}, "tears"},
        {{{"tearShear", "lots"}}, "tearShear"},
    };
    for (const Bad& b : bad) {
        INFO(b.water.dump());
        auto loaded = scene::Composition::fromJson(terrainWith(b.water), registry);
        REQUIRE_FALSE(loaded.has_value());
        INFO(loaded.error().message);
        CHECK(loaded.error().message.find(b.mentions) != std::string::npos);
    }
    // The control: the same document with sane tear settings loads, so the refusals above are about
    // the values and not about the harness.
    CHECK(scene::Composition::fromJson(terrainWith({{"tears", 0.5}, {"tearShear", 8.0}}), registry).has_value());

    // And a NaN, which no comparison-shaped check refuses unless it is written to.
    scene::WaterSettings w;
    w.tearShear = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(w.validate());
}

TEST_CASE("exactly the tears' amount, shear and coverage are parameters, and they reach the surface",
          "[unit][water][tears][parameters]") {
    assets::AssetRegistry registry;
    auto loaded = scene::Composition::fromJson(
        terrainWith({{"tears", 0.25}, {"tearShear", 2.0}, {"tearCoverage", 0.5}}), registry);
    REQUIRE(loaded.has_value());
    scene::Composition& comp = **loaded;
    params::ParameterSet params;
    params::Modulator modulator;
    comp.attach(params, modulator);
    comp.update(FrameTime{});
    REQUIRE(comp.scene().waters.size() == 1);

    auto* tears = params.findAs<float>("nodes/sea/water/tears");
    auto* shear = params.findAs<float>("nodes/sea/water/tearShear");
    auto* coverage = params.findAs<float>("nodes/sea/water/tearCoverage");
    REQUIRE(tears != nullptr);
    REQUIRE(shear != nullptr);
    REQUIRE(coverage != nullptr);
    // Registered with the authored values, so a project that never touches them keeps the scene's.
    CHECK(tears->base() == 0.25f);
    CHECK(shear->base() == 2.0f);
    CHECK(coverage->base() == 0.5f);

    // Moved as a route or an edit would move them, they reach the settings the renderer reads.
    tears->setBase(0.8f);
    shear->setBase(5.0f);
    coverage->setBase(0.9f);
    params.resetFinals();
    comp.update(FrameTime{});
    const scene::WaterSettings& drawn = comp.scene().waters[0].settings;
    CHECK(drawn.tears == 0.8f);
    CHECK(drawn.tearShear == 5.0f);
    CHECK(drawn.tearCoverage == 0.9f);

    // The static ones are not parameters, on purpose: routing one would rescale or turn the whole seam
    // lattice about the world origin, or multiply the timeline second. The control on the check is the
    // three found above.
    for (const char* leaf : {"tearCell", "tearSpacing", "tearStretch", "tearDirection", "tearAngle", "tearDrift",
                             "tearWind"}) {
        const std::string path = std::string("nodes/sea/water/") + leaf;
        INFO(path);
        CHECK(params.find(path) == nullptr);
    }
}
