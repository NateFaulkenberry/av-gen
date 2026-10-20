// ADR-1572: a gltf node in a scene file can draw its meshes through one of the scene's material programs
// (`"program": "<name>"`). Rainbow World's arches keep their glTF tube and let a program move light along it. The key
// must reach the flattened scene's entities through attach and the per-frame application, and round-trip.

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
    const auto path = testsupport::processTempDir() / ("avgen_node_program_" + name + ".json");
    std::ofstream(path) << text;
    return path;
}

constexpr const char* kProgram = R"({"name": "glow", "baseColor": -1, "metallic": -1, "roughness": -1, "emission": 0,
    "emissionIntensity": 1.0, "opacity": -1, "ops": [{"kind": "constant", "dst": 0, "constant": [1, 0.5, 0.2, 1]}]})";

} // namespace

TEST_CASE("a gltf node's program reaches its entities and round-trips; a node without one is untouched",
          "[composition][gltf][adr1572]") {
    const auto glb = testsupport::writeTriangleGlb("node_program_tri");
    assets::AssetRegistry registry{testsupport::processTempDir()};
    const auto path = writeScene("both", std::string(R"({
        "format": "avgen-scene", "version": 1, "name": "t", "materialPrograms": [)") + kProgram + R"(],
        "nodes": [
            {"name": "bow", "kind": "gltf", "asset": "@GLB@", "program": "glow"},
            {"name": "rock", "kind": "gltf", "asset": "@GLB@"}
        ]})", glb);
    auto comp = scene::Composition::loadFile(path.filename(), registry);
    if (!comp) {
        FAIL(comp.error().message);
    }
    params::ParameterSet params;
    params::Modulator modulator;
    (*comp)->attach(params, modulator);
    // From the flatten on, before any frame: route liveness asks which programs a surface names at load.
    REQUIRE((*comp)->scene().entities.size() == 2);
    CHECK((*comp)->scene().entities[0].material.program == "glow");
    for (int frame = 0; frame < 3; ++frame) {
        params.resetFinals();
        FrameTime ft{};
        ft.renderTime = frame / 30.0;
        (*comp)->update(ft);
    }
    const scene::Scene& s = (*comp)->scene();
    REQUIRE(s.entities.size() == 2);
    CHECK(s.entities[0].material.program == "glow");
    CHECK(s.entities[1].material.program.empty()); // the control: the asset's own material
    bool found = false;
    for (const auto& mp : s.materialPrograms) {
        found = found || mp.name == s.entities[0].material.program;
    }
    CHECK(found); // the name resolves to a program the scene carries

    const nlohmann::json saved = (*comp)->toJson();
    for (const auto& n : saved.at("nodes")) {
        if (n.at("name") == "bow") {
            CHECK(n.at("program") == "glow");
        } else {
            CHECK_FALSE(n.contains("program")); // written only when set
        }
    }
}

TEST_CASE("a non-string program on a gltf node is refused", "[composition][gltf][adr1572]") {
    const auto glb = testsupport::writeTriangleGlb("node_program_bad");
    assets::AssetRegistry registry{testsupport::processTempDir()};
    const auto path = writeScene("bad", R"({"format": "avgen-scene", "version": 1, "name": "t",
        "nodes": [{"name": "bow", "kind": "gltf", "asset": "@GLB@", "program": 3}]})", glb);
    CHECK_FALSE(scene::Composition::loadFile(path.filename(), registry).has_value());
}
