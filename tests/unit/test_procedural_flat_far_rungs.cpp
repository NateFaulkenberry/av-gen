// ADR-1576: a procedural node can keep its far LOD rungs out of the live tiers' flat demotion (ADR-155),
// `"flatFarRungs": false`. Rainbow World's ground is forty-eight distance-LOD bands; live, every band past rung 0
// shaded flat -- another colour and no shadow -- so the ground and its shadows popped as bands crossed a rung boundary.
// These cases hold the JSON both ways, the key reaching the flattened scene, and the renderer's predicate.

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

TEST_CASE("flatFarRungs reads, round-trips, and is absent when on", "[procedural][json][adr1576]") {
    auto off = ProceduralGeometry::fromJson(nlohmann::json::parse(
        R"({"name": "ground", "flatFarRungs": false, "source": {"kind": "box"}, "distribution": {"kind": "single"}})"));
    REQUIRE(off.has_value());
    CHECK_FALSE(off->flatFarRungs);
    const nlohmann::json j = off->toJson();
    REQUIRE(j.contains("flatFarRungs"));
    CHECK(j.at("flatFarRungs") == false);
    auto back = ProceduralGeometry::fromJson(j);
    REQUIRE(back.has_value());
    CHECK_FALSE(back->flatFarRungs);

    // The control: no key means demotion as before, and the default writes no key (every existing save unchanged).
    auto on = ProceduralGeometry::fromJson(nlohmann::json::parse(R"({"name": "rock", "source": {"kind": "box"}})"));
    REQUIRE(on.has_value());
    CHECK(on->flatFarRungs);
    CHECK_FALSE(on->toJson().contains("flatFarRungs"));

    CHECK_FALSE(ProceduralGeometry::fromJson(nlohmann::json::parse(R"({"flatFarRungs": 0})")).has_value());
}

TEST_CASE("the flat demotion skips an object that keeps its far rungs", "[procedural][render][adr1576][adr155]") {
    using scene::proceduralRungDemoted;
    // ADR-155 as before: from the tier's rung on, and nothing when the tier demotes nothing
    CHECK_FALSE(proceduralRungDemoted(1, 0, true));
    CHECK(proceduralRungDemoted(1, 1, true));
    CHECK(proceduralRungDemoted(1, 3, true));
    CHECK_FALSE(proceduralRungDemoted(-1, 3, true));
    CHECK_FALSE(proceduralRungDemoted(2, 1, true));
    // ADR-1576: never, for an object that opts out
    for (std::uint32_t level = 0; level < 4; ++level) {
        CHECK_FALSE(proceduralRungDemoted(1, level, false));
        CHECK_FALSE(proceduralRungDemoted(0, level, false));
    }
}

TEST_CASE("a scene file's flatFarRungs reaches the flattened scene", "[procedural][composition][adr1576]") {
    const auto scene = nlohmann::json::parse(R"({
        "format": "avgen-scene", "version": 1, "name": "t",
        "nodes": [
            {"name": "ground", "kind": "procedural",
             "procedural": {"flatFarRungs": false, "source": {"kind": "box"}, "distribution": {"kind": "single"}}},
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
    int seenGround = 0;
    int seenRock = 0;
    for (const auto& p : (*c)->scene().procedurals) {
        if (p.name.find("ground") != std::string::npos) {
            ++seenGround;
            CHECK_FALSE(p.flatFarRungs);
        } else if (p.name.find("rock") != std::string::npos) {
            ++seenRock;
            CHECK(p.flatFarRungs);
        }
    }
    CHECK(seenGround == 1);
    CHECK(seenRock == 1);
}
