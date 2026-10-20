// ADR-1547: a gltf node in a scene file can keep its meshes out of the shadow maps (`"castsShadow": false`).
//
// Rainbow World's arches are glTF tubes made of light; each one threw a solid curved shadow across the meadow under
// it. ADR-1545 gave procedural nodes this key; this is the same key on a gltf node. The cases hold the JSON both ways
// and -- because a key that parses but never reaches the frame is the failure docs/testing.md keeps naming -- the
// flattened scene's entities, through attach and several frames of per-frame application.

#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "support/gltf_fixture.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <fstream>
#include <string>

using namespace avgen;

namespace {

std::filesystem::path writeScene(const std::string& name, std::string text, const std::filesystem::path& glb) {
    for (std::size_t at = text.find("@GLB@"); at != std::string::npos; at = text.find("@GLB@")) {
        text.replace(at, 5, glb.filename().string());
    }
    const auto path = testsupport::processTempDir() / ("avgen_node_shadow_" + name + ".json");
    std::ofstream(path) << text;
    return path;
}

} // namespace

TEST_CASE("a gltf node's castsShadow reaches its entities, round-trips, and is absent when on",
          "[composition][gltf][adr1547]") {
    const auto glb = testsupport::writeTriangleGlb("node_shadow_tri");
    assets::AssetRegistry registry{testsupport::processTempDir()};
    const auto path = writeScene("both", R"({
        "format": "avgen-scene", "version": 1, "name": "t",
        "nodes": [
            {"name": "bow", "kind": "gltf", "asset": "@GLB@", "castsShadow": false},
            {"name": "rock", "kind": "gltf", "asset": "@GLB@"}
        ]})", glb);
    auto comp = scene::Composition::loadFile(path.filename(), registry);
    if (!comp) {
        FAIL(comp.error().message);
    }
    params::ParameterSet params;
    params::Modulator modulator;
    (*comp)->attach(params, modulator);
    for (int frame = 0; frame < 3; ++frame) { // and it survives the per-frame application
        params.resetFinals();
        FrameTime ft{};
        ft.renderTime = frame / 30.0;
        (*comp)->update(ft);
    }
    const scene::Scene& s = (*comp)->scene();
    REQUIRE(s.entities.size() == 2);
    CHECK_FALSE(s.entities[0].castsShadow); // the bow
    CHECK(s.entities[1].castsShadow);       // the control: no key casts, as before

    const nlohmann::json saved = (*comp)->toJson();
    bool sawBow = false;
    for (const auto& n : saved.at("nodes")) {
        if (n.at("name") == "bow") {
            sawBow = true;
            REQUIRE(n.contains("castsShadow"));
            CHECK(n.at("castsShadow") == false);
        } else {
            CHECK_FALSE(n.contains("castsShadow")); // a caster writes no key: every existing save unchanged
        }
    }
    CHECK(sawBow);
}

TEST_CASE("a non-boolean castsShadow on a gltf node is refused", "[composition][gltf][adr1547]") {
    const auto glb = testsupport::writeTriangleGlb("node_shadow_bad");
    assets::AssetRegistry registry{testsupport::processTempDir()};
    const auto path = writeScene("bad", R"({
        "format": "avgen-scene", "version": 1, "name": "t",
        "nodes": [{"name": "bow", "kind": "gltf", "asset": "@GLB@", "castsShadow": "no"}]})", glb);
    CHECK_FALSE(scene::Composition::loadFile(path.filename(), registry).has_value());
}
