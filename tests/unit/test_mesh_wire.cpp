// ADR-1862: the mesh wire's file block, parameters and gate. The drawing is tests/rendering/test_mesh_wire_gpu.cpp.

#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "scene/mesh_wire.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

using namespace avgen;
using Catch::Approx;

TEST_CASE("the mesh wire block parses, round-trips and refuses what it does not know", "[mesh-wire][adr1862]") {
    auto w = scene::meshWireFromJson(nlohmann::json::parse(
        R"({"targets": ["attractor.terrain"], "color": [1, 0.5, 0.2], "intensity": 0.4, "reveal": 1, "wave": 0.5,
            "clipBelow": -0.5})"));
    REQUIRE(w.has_value());
    CHECK(w->targets == std::vector<std::string>{"attractor.terrain"});
    CHECK(w->intensity == Approx(0.4f));
    CHECK(w->clipBelow == Approx(-0.5f));
    auto back = scene::meshWireFromJson(scene::meshWireToJson(*w));
    REQUIRE(back.has_value());
    CHECK(back->color == w->color);
    CHECK(back->reveal == Approx(1.0f));
    CHECK(back->targets == w->targets);

    CHECK_FALSE(scene::meshWireFromJson(nlohmann::json::parse(R"({"intensty": 1})")).has_value());
    CHECK_FALSE(scene::meshWireFromJson(nlohmann::json::parse(R"({"reveal": 2})")).has_value());
    CHECK_FALSE(scene::meshWireFromJson(nlohmann::json::parse(R"({"targets": "a"})")).has_value());
    CHECK_FALSE(scene::meshWireFromJson(nlohmann::json::parse(R"({"color": [1, 2]})")).has_value());
}

TEST_CASE("the mesh wire targets entities by name prefix, and is inactive until it can draw", "[mesh-wire][adr1862]") {
    scene::MeshWire w;
    CHECK_FALSE(w.active()); // no targets
    w.targets = {"attractor.terrain"};
    CHECK(scene::meshWireTargets(w, "attractor.terrain17"));
    CHECK_FALSE(scene::meshWireTargets(w, "attractor.river"));
    CHECK_FALSE(w.active()); // intensity 0 and no wave: nothing to draw, so nothing is bound (the gate)
    w.wave = 0.5f;
    CHECK(w.active());       // a wave alone draws
    w.wave = 0.0f;
    w.intensity = 0.2f;
    CHECK(w.active());
    w.opacity = 0.0f;
    CHECK_FALSE(w.active());
}

TEST_CASE("the composition registers the mesh wire's parameters and carries their finals to the scene",
          "[mesh-wire][adr1862]") {
    const nlohmann::json scene = {
        {"format", "avgen-scene"}, {"version", 1}, {"name", "wire"},
        {"meshWire", {{"targets", {"rock"}}, {"intensity", 0.1}, {"waveRadius", 500.0}}}};
    assets::AssetRegistry registry{testsupport::processTempDir()};
    auto c = scene::Composition::fromJson(scene, registry);
    REQUIRE(c.has_value());
    params::ParameterSet params;
    params::Modulator modulator;
    (*c)->attach(params, modulator);
    for (const char* leaf : {"intensity", "opacity", "width", "creaseLow", "creaseHigh", "reveal", "fadeNear", "fadeFar",
                             "minPixels", "wave", "waveRadius", "waveWidth", "waveGain", "waveAhead", "clipBelow",
                             "color"}) {
        INFO(leaf);
        CHECK(params.find(std::string("meshWire/") + leaf) != nullptr);
    }
    params.findAs<float>("meshWire/wave")->setBase(0.75f);
    params.resetFinals();
    FrameTime ft{};
    (*c)->update(ft);
    const scene::MeshWire& live = (*c)->scene().meshWire;
    CHECK(live.wave == Approx(0.75f));
    CHECK(live.intensity == Approx(0.1f));
    CHECK(live.waveRadius == Approx(500.0f));
    CHECK(live.targets == std::vector<std::string>{"rock"});
    // and the authored block is what a save writes, not the final
    const nlohmann::json saved = (*c)->toJson();
    REQUIRE(saved.contains("meshWire"));
    CHECK(saved["meshWire"]["wave"].get<float>() == Approx(0.0f));
}

TEST_CASE("mesh wire v2: the focus-driven scan keys parse and round-trip, and pulses or shards alone make it active",
          "[mesh-wire][adr1872]") {
    auto w = scene::meshWireFromJson(nlohmann::json::parse(
        R"({"targets": ["rock"], "fromFocus": 1, "pulse": 1.2, "pulseSpacing": 150, "pulseSpeed": 45, "pulseWidth": 8,
            "focusGain": 0.5, "rimGain": 6, "shards": 0.35, "shardRate": 0.4, "frontColor": [1, 0.78, 0.4]})"));
    REQUIRE(w.has_value());
    CHECK(w->pulse == Approx(1.2f));
    CHECK(w->shards == Approx(0.35f));
    CHECK(w->frontColor == glm::vec3(1.0f, 0.78f, 0.4f));
    auto back = scene::meshWireFromJson(scene::meshWireToJson(*w));
    REQUIRE(back.has_value());
    CHECK(back->frontColor == w->frontColor);
    CHECK(back->pulseSpacing == Approx(150.0f));
    CHECK(back->fromFocus == Approx(1.0f));
    CHECK_FALSE(scene::meshWireFromJson(nlohmann::json::parse(R"({"shards": 2})")).has_value());
    CHECK_FALSE(scene::meshWireFromJson(nlohmann::json::parse(R"({"frontColor": [1]})")).has_value());

    scene::MeshWire a;
    a.targets = {"rock"};
    CHECK_FALSE(a.active());
    a.pulse = 1.0f;
    CHECK(a.active());
    a.pulse = 0.0f;
    a.shards = 0.2f;
    CHECK(a.active());
}
