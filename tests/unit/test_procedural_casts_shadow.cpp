// ADR-1545: a procedural node in a scene file can opt out of the shadow maps (`"castsShadow": false`).
//
// The field and the renderer's branch already existed (ADR-287, for ecology layers); a scene file simply had no way
// to set it. These cases hold the JSON both ways and, because a key that parses but never reaches the scene is the
// failure this repository keeps meeting (docs/testing.md), the composition's flattened scene too.

#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "scene/procedural.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

using namespace avgen;
using scene::ProceduralGeometry;

TEST_CASE("castsShadow reads, round-trips, and is absent when on", "[procedural][json][adr1545]") {
    auto off = ProceduralGeometry::fromJson(nlohmann::json::parse(
        R"({"name": "grass", "castsShadow": false, "source": {"kind": "box"}, "distribution": {"kind": "single"}})"));
    REQUIRE(off.has_value());
    CHECK_FALSE(off->castsShadow);
    const nlohmann::json j = off->toJson();
    REQUIRE(j.contains("castsShadow"));
    CHECK(j.at("castsShadow") == false);
    auto back = ProceduralGeometry::fromJson(j);
    REQUIRE(back.has_value());
    CHECK_FALSE(back->castsShadow);

    // The control: no key means it casts, as before, and a caster writes no key (every existing save unchanged).
    auto on = ProceduralGeometry::fromJson(nlohmann::json::parse(R"({"name": "rock", "source": {"kind": "box"}})"));
    REQUIRE(on.has_value());
    CHECK(on->castsShadow);
    CHECK_FALSE(on->toJson().contains("castsShadow"));

    CHECK_FALSE(ProceduralGeometry::fromJson(nlohmann::json::parse(R"({"castsShadow": "no"})")).has_value());
}

TEST_CASE("a scene file's castsShadow reaches the flattened scene", "[procedural][composition][adr1545]") {
    const auto scene = nlohmann::json::parse(R"({
        "format": "avgen-scene", "version": 1, "name": "t",
        "nodes": [
            {"name": "grass", "kind": "procedural",
             "procedural": {"castsShadow": false, "source": {"kind": "box"}, "distribution": {"kind": "single"}}},
            {"name": "rock", "kind": "procedural",
             "procedural": {"source": {"kind": "box"}, "distribution": {"kind": "single"}}}
        ]})");
    assets::AssetRegistry registry{testsupport::processTempDir()};
    auto c = scene::Composition::fromJson(scene, registry);
    REQUIRE(c.has_value());
    params::ParameterSet params;
    params::Modulator modulator;
    (*c)->attach(params, modulator);
    for (int frame = 0; frame < 2; ++frame) { // and it survives the per-frame parameter application
        params.resetFinals();
        FrameTime ft{};
        ft.renderTime = frame / 30.0;
        (*c)->update(ft);
    }
    const auto& procs = (*c)->scene().procedurals;
    int seenGrass = 0;
    int seenRock = 0;
    for (const auto& p : procs) {
        if (p.name.find("grass") != std::string::npos) {
            ++seenGrass;
            CHECK_FALSE(p.castsShadow);
        } else if (p.name.find("rock") != std::string::npos) {
            ++seenRock;
            CHECK(p.castsShadow);
        }
    }
    CHECK(seenGrass == 1);
    CHECK(seenRock == 1);
}
