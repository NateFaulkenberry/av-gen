// ADR-2040: a node's `opacity` key -- the base of `nodes/<name>/opacity`, read from the scene file.
//
// Scene builders wrote it and nothing read it, so a fade authored in the file drew at full strength (Strange
// Attractor's old rover tracks). These cases hold it both ways and through to the flattened scene's entities,
// because a key that parses but never reaches the frame is the failure docs/testing.md keeps naming.

#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "support/gltf_fixture.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <fstream>
#include <functional>
#include <string>

using namespace avgen;

namespace {

std::filesystem::path writeScene(const std::string& name, std::string text, const std::filesystem::path& glb) {
    for (std::size_t at = text.find("@GLB@"); at != std::string::npos; at = text.find("@GLB@")) {
        text.replace(at, 5, glb.filename().string());
    }
    const auto path = testsupport::processTempDir() / ("avgen_node_opacity_" + name + ".json");
    std::ofstream(path) << text;
    return path;
}

} // namespace

TEST_CASE("a node's opacity key is the parameter's base, fades its entities, and round-trips",
          "[composition][adr2040]") {
    const auto glb = testsupport::writeTriangleGlb("node_opacity_tri");
    assets::AssetRegistry registry{testsupport::processTempDir()};
    const auto path = writeScene("both", R"({
        "format": "avgen-scene", "version": 1, "name": "t",
        "nodes": [
            {"name": "solid", "kind": "gltf", "asset": "@GLB@"},
            {"name": "faded", "kind": "gltf", "asset": "@GLB@", "opacity": 0.4}
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

    auto* faded = params.findAs<float>("nodes/faded/opacity");
    auto* solid = params.findAs<float>("nodes/solid/opacity");
    REQUIRE(faded != nullptr);
    REQUIRE(solid != nullptr);
    CHECK(faded->base() == Catch::Approx(0.4f));
    CHECK(solid->base() == Catch::Approx(1.0f));

    const scene::Scene& s = (*comp)->scene();
    REQUIRE(s.entities.size() == 2);
    CHECK(s.entities[0].material.opacity == Catch::Approx(1.0f));
    CHECK(s.entities[1].material.opacity == Catch::Approx(0.4f));
    CHECK(s.entities[1].material.alphaMode == scene::AlphaMode::Blend); // an OPAQUE asset's alpha is otherwise discarded

    const nlohmann::json saved = (*comp)->toJson();
    for (const auto& n : saved.at("nodes")) {
        if (n.at("name") == "faded") {
            REQUIRE(n.contains("opacity"));
            CHECK(n.at("opacity").get<float>() == Catch::Approx(0.4f));
        } else {
            CHECK_FALSE(n.contains("opacity")); // an unfaded node writes no key: every existing save unchanged
        }
    }
}

TEST_CASE("a node opacity outside 0..1, or not a number, is refused", "[composition][adr2040]") {
    const auto glb = testsupport::writeTriangleGlb("node_opacity_bad");
    assets::AssetRegistry registry{testsupport::processTempDir()};
    for (const char* bad : {"1.5", "-0.1", "\"half\"", "true", "null"}) {
        const auto path = writeScene(std::string("bad") + std::to_string(std::hash<std::string>{}(bad) % 1000),
                                     std::string(R"({"format": "avgen-scene", "version": 1, "name": "t",
            "nodes": [{"name": "n", "kind": "gltf", "asset": "@GLB@", "opacity": )") +
                                         bad + "}]}",
                                     glb);
        INFO(bad);
        CHECK_FALSE(scene::Composition::loadFile(path.filename(), registry).has_value());
    }
}
