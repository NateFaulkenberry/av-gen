// ADR-1934: the slit's identity gate -- the node key that names a slot, and the slot table's settings.
//
// The GPU half (the gate rejects ungated history; trails and fracture; FIR) is `rendering/test_temporal_gate_gpu.cpp`.
// These cases hold the authored half both ways, and -- because a key that parses but never reaches the frame is the
// failure docs/testing.md keeps naming -- the flattened scene's entities, through attach and per-frame application.

#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "scene/temporal_settings.hpp"
#include "support/gltf_fixture.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <functional>
#include <string>

using namespace avgen;

namespace {

std::filesystem::path writeScene(const std::string& name, std::string text, const std::filesystem::path& glb) {
    for (std::size_t at = text.find("@GLB@"); at != std::string::npos; at = text.find("@GLB@")) {
        text.replace(at, 5, glb.filename().string());
    }
    const auto path = testsupport::processTempDir() / ("avgen_temporal_gate_" + name + ".json");
    std::ofstream(path) << text;
    return path;
}

} // namespace

TEST_CASE("a node's temporalSlot reaches its entities, round-trips, and is absent when unset",
          "[composition][temporal][adr1934]") {
    const auto glb = testsupport::writeTriangleGlb("temporal_gate_tri");
    assets::AssetRegistry registry{testsupport::processTempDir()};
    const auto path = writeScene("both", R"({
        "format": "avgen-scene", "version": 1, "name": "t",
        "nodes": [
            {"name": "face", "kind": "gltf", "asset": "@GLB@"},
            {"name": "soul", "kind": "gltf", "asset": "@GLB@", "temporalSlot": 3}
        ]})", glb);
    auto comp = scene::Composition::loadFile(path.filename(), registry);
    if (!comp) {
        FAIL(comp.error().message);
    }
    params::ParameterSet params;
    params::Modulator modulator;
    (*comp)->attach(params, modulator);
    for (int frame = 0; frame < 3; ++frame) {
        params.resetFinals();
        FrameTime ft{};
        ft.renderTime = frame / 30.0;
        (*comp)->update(ft);
    }
    const scene::Scene& s = (*comp)->scene();
    REQUIRE(s.entities.size() == 2);
    CHECK(s.entities[0].temporalSlot == 0);
    CHECK(s.entities[1].temporalSlot == 3);

    const nlohmann::json saved = (*comp)->toJson();
    for (const auto& n : saved.at("nodes")) {
        if (n.at("name") == "soul") {
            REQUIRE(n.contains("temporalSlot"));
            CHECK(n.at("temporalSlot") == 3);
        } else {
            CHECK_FALSE(n.contains("temporalSlot")); // ungated writes no key: every existing save unchanged
        }
    }
}

TEST_CASE("a temporalSlot outside 0..16 is refused", "[composition][temporal][adr1934]") {
    const auto glb = testsupport::writeTriangleGlb("temporal_gate_bad");
    assets::AssetRegistry registry{testsupport::processTempDir()};
    for (const char* bad : {"17", "-1", "2.5", "\"one\""}) {
        const auto path = writeScene(std::string("bad") + std::to_string(std::hash<std::string>{}(bad) % 1000),
                                     std::string(R"({"format": "avgen-scene", "version": 1, "name": "t",
            "nodes": [{"name": "soul", "kind": "gltf", "asset": "@GLB@", "temporalSlot": )") +
                                         bad + "}]}",
                                     glb);
        INFO(bad);
        CHECK_FALSE(scene::Composition::loadFile(path.filename(), registry).has_value());
    }
}

TEST_CASE("the gate and its slot table round-trip, and a scene without them writes neither", "[temporal][adr1934]") {
    const scene::TemporalSettings plain;
    const nlohmann::json plainJson = scene::temporalToJson(plain);
    CHECK_FALSE(plainJson.at("slit").contains("gate"));
    CHECK_FALSE(plainJson.at("slit").contains("slots"));

    scene::TemporalSettings s;
    s.slit.enabled = true;
    s.slit.gate = 1.0f;
    s.slit.slots[0].trail = 40.0f;
    s.slit.slots[0].echo = 6.0f;
    s.slit.slots[0].fade = 0.8f;
    s.slit.slots[15].fracture = 0.6f;
    s.slit.slots[15].strips = 30.0f;
    s.slit.slots[15].angle = -20.0f;
    const nlohmann::json j = scene::temporalToJson(s);
    REQUIRE(j.at("slit").at("slots").size() == 16);
    auto back = scene::temporalFromJson(j);
    REQUIRE(back.has_value());
    CHECK(back->slit.gate == 1.0f);
    CHECK(back->slit.slots[0].trail == 40.0f);
    CHECK(back->slit.slots[0].echo == 6.0f);
    CHECK(back->slit.slots[0].fade == 0.8f);
    CHECK(back->slit.slots[15].fracture == 0.6f);
    CHECK(back->slit.slots[15].strips == 30.0f);
    CHECK(back->slit.slots[15].angle == -20.0f);
    CHECK(scene::temporalToJson(*back) == j);
    // the bound is still the span: a slot reads no further back than it
    CHECK(s.historyFrames() == static_cast<std::uint32_t>(std::ceil(s.slit.span)));
}

TEST_CASE("every slot's parameters are registered and applied", "[temporal][adr1934]") {
    params::ParameterSet params;
    const scene::TemporalSettings defaults;
    const scene::TemporalParameters p = scene::registerTemporalParameters(params, defaults);
    REQUIRE(p.slitGate != nullptr);
    for (int k = 1; k <= scene::SlitSettings::kSlots; ++k) {
        for (const char* leaf : {"trail", "echo", "fade", "fracture", "strips", "angle"}) {
            const std::string path = "temporal/slit/slot" + std::to_string(k) + "/" + leaf;
            INFO(path);
            CHECK(params.find(path) != nullptr);
        }
    }
    p.slitGate->setBase(1.0f);
    p.slitSlots[4].trail->setBase(25.0f);
    p.slitSlots[4].fracture->setBase(0.5f);
    params.resetFinals();
    scene::TemporalSettings out;
    scene::applyTemporalParameters(p, out);
    CHECK(out.slit.gate == 1.0f);
    CHECK(out.slit.slots[4].trail == 25.0f);
    CHECK(out.slit.slots[4].fracture == 0.5f);
}
