// ADR-2020: a gltf node's `waterMaterials` makes the entities of a named glTF material a water surface -- the terrain
// water's path, so an imported lake gets the cinematic march and the sky mirror. The CPU half:
//   * the value is read by terrain.water's own reader (its keys, ranges and messages);
//   * an exact, case-sensitive match turns those entities into MeshStyle::Water with their own WaterSurface, and only
//     those; the node's `program` does not reach them, at the flatten or per frame;
//   * a name that matches nothing is a warning naming what is there, in the log and in Engine::projectWarnings;
//   * more surfaces than the renderer has slots is refused at load; a skinned entity is skipped;
//   * absent, nothing changes; the key round-trips as authored.

#include "app/engine.hpp"
#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "support/gltf_fixture.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <fstream>
#include <string>

using namespace avgen;

namespace {

// A lake, a rock and a block: three materials, so a match can be seen to take one and leave the others.
std::filesystem::path lakeGlb() {
    std::array<testsupport::GlbMesh, 3> meshes;
    meshes[0].node = "water";
    meshes[0].material = "Lake";
    meshes[0].color = {0.1f, 0.4f, 0.4f};
    meshes[0].roughness = 0.04f;
    testsupport::appendPlane(meshes[0], {-50.0f, -80.0f}, {50.0f, 10.0f}, 0.0f);
    meshes[1].node = "ridge";
    meshes[1].material = "Rock";
    meshes[1].roughness = 0.9f;
    testsupport::appendBox(meshes[1], {-40.0f, 0.0f, -100.0f}, {40.0f, 30.0f, -90.0f});
    meshes[2].node = "block";
    meshes[2].material = "Red";
    meshes[2].color = {0.9f, 0.05f, 0.03f};
    testsupport::appendBox(meshes[2], {5.0f, 0.0f, -20.0f}, {9.0f, 3.0f, -16.0f});
    return testsupport::writeMeshesGlb("gltf_water_lake", meshes);
}

std::filesystem::path writeScene(const std::string& name, const std::string& nodeExtra,
                                 const std::filesystem::path& glb, const std::string& sceneExtra = "") {
    const std::string text = R"({"format": "avgen-scene", "version": 1, "name": "t",)" + sceneExtra +
                             R"( "nodes": [{"name": "environment", "kind": "gltf", "asset": ")" +
                             glb.filename().string() + "\"" + nodeExtra + "}]}";
    const auto path = testsupport::processTempDir() / ("avgen_gltf_water_" + name + ".json");
    std::ofstream(path) << text;
    return path;
}

const scene::Entity* entityNamed(const scene::Scene& s, const std::string& name) {
    const auto it = std::find_if(s.entities.begin(), s.entities.end(),
                                 [&](const scene::Entity& e) { return e.name == name; });
    return it == s.entities.end() ? nullptr : &*it;
}

void run(scene::Composition& comp, params::ParameterSet& params, int frames) {
    for (int frame = 0; frame < frames; ++frame) {
        params.resetFinals();
        FrameTime ft{};
        ft.renderTime = frame / 30.0;
        comp.update(ft);
    }
}

constexpr const char* kLake = R"(, "waterMaterials": {"Lake": {"shallowColor": [0.113, 0.25, 0.251],
    "clarity": 6.0, "roughness": 0.04, "ripple": 0.03, "rippleSpeed": 0.0, "ssr": 1.0, "ssrDistance": 2000,
    "ssrSteps": 128, "skyMirror": 1.0}})";

} // namespace

TEST_CASE("a gltf node's water material becomes a cinematic water surface; the others are untouched",
          "[composition][gltf][water][adr2020]") {
    const auto glb = lakeGlb();
    assets::AssetRegistry registry{testsupport::processTempDir()};
    auto comp = scene::Composition::loadFile(writeScene("match", kLake, glb).filename(), registry);
    if (!comp) {
        FAIL(comp.error().message);
    }
    params::ParameterSet params;
    params::Modulator modulator;
    (*comp)->attach(params, modulator);
    CHECK((*comp)->waterMaterialProblems().empty());
    run(**comp, params, 3);

    const scene::Scene& s = (*comp)->scene();
    REQUIRE(s.entities.size() == 3);
    const scene::Entity* lake = entityNamed(s, "environment/water");
    const scene::Entity* ridge = entityNamed(s, "environment/ridge");
    REQUIRE(lake != nullptr);
    REQUIRE(ridge != nullptr);
    CHECK(lake->style == scene::MeshStyle::Water);
    CHECK(lake->material.program == "environment_water_Lake");
    CHECK(lake->material.surfaceClass == scene::SurfaceClass::Water);
    CHECK(lake->material.doubleSided);
    CHECK_FALSE(lake->castsShadow);
    CHECK(lake->materialName == "Lake"); // the address survives
    CHECK(ridge->style == scene::MeshStyle::Lit);
    CHECK(ridge->material.program.empty());
    CHECK(ridge->material.roughness == 0.9f);

    REQUIRE(s.waters.size() == 1);
    const scene::WaterSurface& surface = s.waters[0];
    CHECK(surface.program == lake->material.program); // how the draw finds its settings
    CHECK(surface.settings.ssr == 1.0f);
    CHECK(surface.settings.ssrDistance == 2000.0f);
    CHECK(surface.settings.ssrSteps == 128.0f);
    CHECK(surface.settings.skyMirror == 1.0f);
    CHECK(surface.settings.clarity == 6.0f);
    CHECK(surface.settings.shallowColor == glm::vec3(0.113f, 0.25f, 0.251f));
    CHECK(surface.settings.fresnel == scene::WaterSettings{}.fresnel); // unauthored keys keep terrain.water's defaults
    CHECK(surface.settings.cinematic());

    // round-trips as authored, and only where it was authored
    const nlohmann::json saved = (*comp)->toJson();
    REQUIRE(saved.at("nodes").size() == 1);
    const nlohmann::json& node = saved.at("nodes")[0];
    REQUIRE(node.contains("waterMaterials"));
    CHECK(node.at("waterMaterials").at("Lake").at("ssrSteps") == 128);
    CHECK(node.at("waterMaterials").size() == 1);
}

TEST_CASE("absent, waterMaterials changes nothing and is not written", "[composition][gltf][water][adr2020]") {
    const auto glb = lakeGlb();
    assets::AssetRegistry registry{testsupport::processTempDir()};
    auto comp = scene::Composition::loadFile(writeScene("absent", "", glb).filename(), registry);
    REQUIRE(comp.has_value());
    params::ParameterSet params;
    params::Modulator modulator;
    (*comp)->attach(params, modulator);
    run(**comp, params, 2);
    const scene::Scene& s = (*comp)->scene();
    CHECK(s.waters.empty());
    for (const scene::Entity& e : s.entities) {
        CHECK(e.style == scene::MeshStyle::Lit);
        CHECK(e.material.program.empty());
    }
    CHECK(entityNamed(s, "environment/water")->material.roughness == 0.04f); // the asset's own number
    CHECK_FALSE((*comp)->toJson().at("nodes")[0].contains("waterMaterials"));
}

TEST_CASE("a node program does not reach a water material's entities", "[composition][gltf][water][adr2020]") {
    const auto glb = lakeGlb();
    assets::AssetRegistry registry{testsupport::processTempDir()};
    const std::string program = R"( "materialPrograms": [{"name": "glow", "baseColor": -1, "metallic": -1,
        "roughness": -1, "emission": 0, "emissionIntensity": 1.0, "opacity": -1,
        "ops": [{"kind": "constant", "dst": 0, "constant": [1, 0.5, 0.2, 1]}]}],)";
    auto comp = scene::Composition::loadFile(
        writeScene("program", std::string(kLake) + R"(, "program": "glow")", glb, program).filename(), registry);
    if (!comp) {
        FAIL(comp.error().message);
    }
    params::ParameterSet params;
    params::Modulator modulator;
    (*comp)->attach(params, modulator);
    CHECK(entityNamed((*comp)->scene(), "environment/water")->material.program == "environment_water_Lake");
    run(**comp, params, 3); // and not per frame either
    const scene::Scene& s = (*comp)->scene();
    CHECK(entityNamed(s, "environment/water")->material.program == "environment_water_Lake");
    CHECK(entityNamed(s, "environment/water")->style == scene::MeshStyle::Water);
    CHECK(entityNamed(s, "environment/ridge")->material.program == "glow"); // the control
}

TEST_CASE("a water material name that matches nothing is a warning naming what is there",
          "[composition][gltf][water][adr2020]") {
    const auto glb = lakeGlb();
    assets::AssetRegistry registry{testsupport::processTempDir()};
    // case-sensitive: "lake" is not "Lake"
    const auto path = writeScene("unmatched", R"(, "waterMaterials": {"Lakee": {}, "lake": {}})", glb);
    auto comp = scene::Composition::loadFile(path.filename(), registry);
    REQUIRE(comp.has_value());
    const std::vector<std::string> problems = (*comp)->waterMaterialProblems();
    REQUIRE(problems.size() == 2);
    CHECK(problems[0] == "gltf node 'environment': waterMaterials 'Lakee' matches no material; available: Lake, "
                         "Rock, Red");
    CHECK(problems[1].find("'lake' matches no material") != std::string::npos);
    params::ParameterSet params;
    params::Modulator modulator;
    (*comp)->attach(params, modulator);
    CHECK((*comp)->scene().waters.empty());
    CHECK(entityNamed((*comp)->scene(), "environment/water")->style == scene::MeshStyle::Lit);

    SECTION("and reaches the project's warnings through the Engine") {
        app::Engine engine(app::EngineMode::Offline);
        REQUIRE(engine.loadComposition(path).has_value());
        const auto& warnings = engine.projectWarnings();
        INFO(warnings.size() << " warning(s)");
        CHECK(std::find(warnings.begin(), warnings.end(), problems[0]) != warnings.end());
    }
}

TEST_CASE("waterMaterials values are read by terrain.water's reader", "[composition][gltf][water][adr2020]") {
    const auto glb = lakeGlb();
    assets::AssetRegistry registry{testsupport::processTempDir()};
    const auto refused = [&](const std::string& name, const std::string& extra, const std::string& words) {
        auto comp = scene::Composition::loadFile(writeScene(name, extra, glb).filename(), registry);
        REQUIRE_FALSE(comp.has_value());
        INFO(comp.error().message);
        CHECK(comp.error().message.find(words) != std::string::npos);
    };
    refused("range", R"(, "waterMaterials": {"Lake": {"ssrDistance": 8000}})",
            "node 'environment': waterMaterials 'Lake': water: ssrDistance must be in [1, 2000] metres");
    refused("type", R"(, "waterMaterials": {"Lake": {"ssr": "on"}})", "waterMaterials 'Lake': water:");
    refused("enabled", R"(, "waterMaterials": {"Lake": {"enabled": 1}})",
            "waterMaterials 'Lake': water 'enabled' must be a boolean");
    refused("value", R"(, "waterMaterials": {"Lake": 1})", "waterMaterials 'Lake': must be an object");
    refused("object", R"(, "waterMaterials": ["Lake"])", "'waterMaterials' must be an object");

    // `enabled: false` is the key absent
    auto off = scene::Composition::loadFile(
        writeScene("disabled", R"(, "waterMaterials": {"Lake": {"enabled": false}})", glb).filename(), registry);
    REQUIRE(off.has_value());
    params::ParameterSet params;
    params::Modulator modulator;
    (*off)->attach(params, modulator);
    CHECK((*off)->scene().waters.empty());
    CHECK(entityNamed((*off)->scene(), "environment/water")->style == scene::MeshStyle::Lit);
}

TEST_CASE("waterMaterials on a node that is not gltf is refused", "[composition][gltf][water][adr2020]") {
    assets::AssetRegistry registry{testsupport::processTempDir()};
    const auto path = testsupport::processTempDir() / "avgen_gltf_water_orb.json";
    std::ofstream(path) << R"({"format": "avgen-scene", "version": 1, "name": "t",
        "nodes": [{"name": "o", "kind": "orb", "waterMaterials": {"Lake": {}}}]})";
    auto comp = scene::Composition::loadFile(path.filename(), registry);
    REQUIRE_FALSE(comp.has_value());
    CHECK(comp.error().message.find("'waterMaterials' applies to gltf nodes only") != std::string::npos);
}

TEST_CASE("more water surfaces than the renderer has slots is refused at load", "[composition][gltf][water][adr2020]") {
    const auto glb = lakeGlb();
    assets::AssetRegistry registry{testsupport::processTempDir()};
    std::string many = R"(, "waterMaterials": {)";
    for (std::uint32_t i = 0; i <= scene::kMaxWaterSurfaces; ++i) {
        many += (i == 0 ? "" : ", ") + ("\"m" + std::to_string(i) + "\": {}");
    }
    many += "}";
    auto comp = scene::Composition::loadFile(writeScene("cap", many, glb).filename(), registry);
    REQUIRE_FALSE(comp.has_value());
    INFO(comp.error().message);
    CHECK(comp.error().message.find("a scene draws at most 8 water surfaces (kMaxWaterMaterials)") !=
          std::string::npos);

    // across nodes: two nodes of five are ten surfaces
    std::string five = R"(, "waterMaterials": {"a": {}, "b": {}, "c": {}, "d": {}, "e": {}})";
    const std::string text = R"({"format": "avgen-scene", "version": 1, "name": "t", "nodes": [
        {"name": "one", "kind": "gltf", "asset": ")" + glb.filename().string() + "\"" + five + R"(},
        {"name": "two", "kind": "gltf", "asset": ")" + glb.filename().string() + "\"" + five + "}]}";
    const auto path = testsupport::processTempDir() / "avgen_gltf_water_cap2.json";
    std::ofstream(path) << text;
    auto two = scene::Composition::loadFile(path.filename(), registry);
    REQUIRE_FALSE(two.has_value());
    INFO(two.error().message);
    CHECK(two.error().message.find("10 water surfaces (0 from terrains, 10 from gltf waterMaterials)") !=
          std::string::npos);
}

TEST_CASE("a skinned entity of a water material is skipped", "[composition][gltf][water][adr2020]") {
    std::array<testsupport::GlbMesh, 2> meshes;
    meshes[0].node = "pond";
    meshes[0].material = "Lake";
    testsupport::appendPlane(meshes[0], {-5.0f, -5.0f}, {5.0f, 5.0f}, 0.0f);
    meshes[1].node = "jelly";
    meshes[1].material = "Lake";
    meshes[1].skinned = true;
    testsupport::appendBox(meshes[1], {-1.0f, 0.0f, -1.0f}, {1.0f, 1.0f, 1.0f});
    const auto glb = testsupport::writeMeshesGlb("gltf_water_skinned", meshes);
    assets::AssetRegistry registry{testsupport::processTempDir()};
    auto comp = scene::Composition::loadFile(
        writeScene("skinned", R"(, "waterMaterials": {"Lake": {"skyMirror": 1}})", glb).filename(), registry);
    if (!comp) {
        FAIL(comp.error().message);
    }
    params::ParameterSet params;
    params::Modulator modulator;
    (*comp)->attach(params, modulator);
    const scene::Scene& s = (*comp)->scene();
    const scene::Entity* pond = entityNamed(s, "environment/pond");
    const scene::Entity* jelly = entityNamed(s, "environment/jelly");
    REQUIRE(pond != nullptr);
    REQUIRE(jelly != nullptr);
    REQUIRE(jelly->rig != scene::kInvalidRig); // the fixture did make a skinned entity
    CHECK(jelly->style == scene::MeshStyle::Lit);
    CHECK(jelly->material.program.empty());
    CHECK(pond->style == scene::MeshStyle::Water);
    CHECK(s.waters.size() == 1);
}
