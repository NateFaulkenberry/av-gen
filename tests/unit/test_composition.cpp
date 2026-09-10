// Composition: node kinds, flattening with shared assets, parameter surface, nesting, files.
#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "scene/mesh_generators.hpp"
#include "support/gltf_fixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

using namespace avgen;
using Catch::Matchers::WithinAbs;

namespace {

std::filesystem::path tempDir() {
    return std::filesystem::temp_directory_path();
}

void checkVec(const glm::vec3& actual, const glm::vec3& expected, double tolerance = 1e-4) {
    CHECK_THAT(static_cast<double>(actual.x), WithinAbs(static_cast<double>(expected.x), tolerance));
    CHECK_THAT(static_cast<double>(actual.y), WithinAbs(static_cast<double>(expected.y), tolerance));
    CHECK_THAT(static_cast<double>(actual.z), WithinAbs(static_cast<double>(expected.z), tolerance));
}

void checkQuat(const glm::quat& actual, const glm::quat& expected, double tolerance = 1e-4) {
    // q and -q are the same rotation.
    const float sign = glm::dot(actual, expected) < 0.0f ? -1.0f : 1.0f;
    CHECK_THAT(static_cast<double>(actual.w * sign), WithinAbs(static_cast<double>(expected.w), tolerance));
    CHECK_THAT(static_cast<double>(actual.x * sign), WithinAbs(static_cast<double>(expected.x), tolerance));
    CHECK_THAT(static_cast<double>(actual.y * sign), WithinAbs(static_cast<double>(expected.y), tolerance));
    CHECK_THAT(static_cast<double>(actual.z * sign), WithinAbs(static_cast<double>(expected.z), tolerance));
}

scene::CompositionNode makeNode(scene::NodeKind kind, std::string name, std::filesystem::path asset = {}) {
    scene::CompositionNode node;
    node.kind = kind;
    node.name = std::move(name);
    node.asset = std::move(asset);
    return node;
}

scene::ParticleSystem sparkSettings() {
    scene::ParticleSystem p;
    p.capacity = 4096;
    p.seed = 7;
    p.shape = scene::EmitterShape::Box;
    p.position = glm::vec3(0.0f, 1.0f, 0.0f);
    p.extent = glm::vec3(0.5f, 0.25f, 0.5f);
    p.spawnRate = 321.0f;
    p.lifetimeMin = 0.5f;
    p.lifetimeMax = 2.5f;
    p.blend = scene::ParticleBlend::Alpha;
    p.colorStart = glm::vec4(0.1f, 0.2f, 0.3f, 0.4f);
    p.emissive = 2.5f;
    p.softness = 0.7f;
    return p;
}

std::filesystem::path writeJson(const std::string& name, const std::string& text) {
    const auto path = tempDir() / ("avgen_comp_" + name + ".json");
    std::ofstream out(path);
    out << text;
    return path;
}

const scene::Entity* findEntity(const scene::Scene& sc, const std::string& name) {
    for (const auto& e : sc.entities) {
        if (e.name == name) {
            return &e;
        }
    }
    return nullptr;
}

// Registry rooted in the temp directory; fixtures are referred to by file name.
struct Fixture {
    std::filesystem::path glb = testsupport::writeTriangleGlb("comp_tri");
    assets::AssetRegistry registry{tempDir()};
    std::vector<std::filesystem::path> files;
    ~Fixture() {
        std::filesystem::remove(glb);
        for (const auto& f : files) {
            std::filesystem::remove(f);
        }
    }
    std::filesystem::path json(const std::string& name, const std::string& text) {
        files.push_back(writeJson(name, text));
        return files.back();
    }
};

} // namespace

TEST_CASE("Composition flattens every node kind, sharing glTF assets between instances",
          "[scene][composition]") {
    Fixture fx;
    scene::Composition comp(fx.registry, "test");
    CHECK(comp.name() == "test");

    auto a = comp.addNode(makeNode(scene::NodeKind::Gltf, "a", fx.glb.filename()));
    REQUIRE(a.has_value());
    CHECK((*a)->name == "a");
    auto bNode = makeNode(scene::NodeKind::Gltf, "b", fx.glb.filename());
    bNode.transform.position = glm::vec3(0.0f, 0.0f, 5.0f);
    bNode.transform.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    REQUIRE(comp.addNode(std::move(bNode)).has_value());
    auto orbNode = makeNode(scene::NodeKind::Orb, "orb");
    orbNode.transform.position = glm::vec3(0.0f, 1.5f, 0.0f);
    REQUIRE(comp.addNode(std::move(orbNode)).has_value());
    REQUIRE(comp.addNode(makeNode(scene::NodeKind::Grid, "grid")).has_value());
    auto sparks = makeNode(scene::NodeKind::Particles, "sparks");
    sparks.particles = sparkSettings();
    REQUIRE(comp.addNode(std::move(sparks)).has_value());
    CHECK(comp.nodeCount() == 5);
    CHECK(fx.registry.sceneCount() == 1);

    // Unique names.
    auto orb2 = comp.addNode(makeNode(scene::NodeKind::Orb, "orb"));
    REQUIRE(orb2.has_value());
    CHECK((*orb2)->name == "orb2");
    auto orb3 = comp.addNode(makeNode(scene::NodeKind::Orb, "orb"));
    REQUIRE(orb3.has_value());
    CHECK((*orb3)->name == "orb3");
    CHECK(comp.removeNode("orb2"));
    CHECK(comp.removeNode("orb3"));
    CHECK_FALSE(comp.removeNode("orb3"));
    CHECK(comp.findNode("orb") != nullptr);
    CHECK(comp.findNode("nope") == nullptr);

    comp.update(FrameTime{});
    const scene::Scene& sc = comp.scene();
    const auto asset = fx.registry.loadScene(fx.glb);
    REQUIRE(asset.has_value());
    // One shared triangle mesh + icosphere + plane; entities: two triangles, orb, grid.
    CHECK(sc.meshes.size() == (*asset)->scene.meshes.size() + 2);
    REQUIRE(sc.entities.size() == 4);
    CHECK(sc.entities[0].name == "a/tri");
    CHECK(sc.entities[1].name == "b/tri");
    CHECK(sc.entities[0].mesh == sc.entities[1].mesh);
    checkVec(sc.entities[0].transform.position, glm::vec3(2.0f, 0.0f, 0.0f));
    // Rotated 90 degrees about +Y: the node at (2, 0, 0) lands at (0, 0, -2) plus the offset.
    checkVec(sc.entities[1].transform.position, glm::vec3(0.0f, 0.0f, 3.0f));
    CHECK(sc.entities[0].material.emissiveIntensity == 1.0f);
    CHECK(sc.entities[0].material.roughness == 0.6f);

    const scene::Entity& orb = sc.entities[2];
    CHECK(orb.name == "orb");
    CHECK(orb.style == scene::MeshStyle::Lit);
    checkVec(orb.material.baseColor, glm::vec3(0.75f, 0.2f, 0.9f));
    checkVec(orb.material.emissiveColor, glm::vec3(0.9f, 0.45f, 1.0f));
    CHECK(orb.material.emissiveIntensity == 0.15f);
    CHECK(orb.material.roughness == 0.35f);
    checkVec(orb.transform.position, glm::vec3(0.0f, 1.5f, 0.0f));
    CHECK(sc.meshes[orb.mesh].vertices.size() == scene::makeIcosphere(1.0f, 3).vertices.size());

    const scene::Entity& grid = sc.entities[3];
    CHECK(grid.style == scene::MeshStyle::Grid);
    CHECK(sc.meshes[grid.mesh].vertices.size() == scene::makePlane(12.0f, 48).vertices.size());

    REQUIRE(sc.particles.size() == 1);
    CHECK(sc.particles[0].name == "sparks");
    CHECK(sc.particles[0].spawnRate == 321.0f);
    CHECK(sc.particles[0].shape == scene::EmitterShape::Box);
    checkVec(sc.particles[0].position, glm::vec3(0.0f, 1.0f, 0.0f));

    // Both lamp instances are carried over, transformed by their nodes.
    REQUIRE(sc.lights.size() == 2);
    checkVec(sc.lights[0].position, glm::vec3(0.0f, 3.0f, 0.0f));
    checkVec(sc.lights[1].position, glm::vec3(0.0f, 3.0f, 5.0f));
    CHECK(comp.boundsRadius() > 1.0f);
    CHECK(sc.camera.target == comp.boundsCenter());
}

TEST_CASE("Composition without lights gets a key light and frames particles", "[scene][composition]") {
    Fixture fx;
    scene::Composition comp(fx.registry);
    auto sparks = makeNode(scene::NodeKind::Particles, "sparks");
    sparks.particles = sparkSettings();
    REQUIRE(comp.addNode(std::move(sparks)).has_value());
    comp.update(FrameTime{});
    REQUIRE(comp.scene().lights.size() == 1);
    CHECK(comp.scene().lights[0].name == "key");
    CHECK(comp.scene().entities.empty());
    checkVec(comp.boundsCenter(), glm::vec3(0.0f, 1.0f, 0.0f));
    CHECK(comp.boundsRadius() > 0.5f);
}

TEST_CASE("Composition registers parameters that drive instances, materials and particles",
          "[scene][composition]") {
    Fixture fx;
    scene::Composition comp(fx.registry, "params");
    REQUIRE(comp.addNode(makeNode(scene::NodeKind::Gltf, "a", fx.glb.filename())).has_value());
    auto b = makeNode(scene::NodeKind::Gltf, "b", fx.glb.filename());
    b.transform.position = glm::vec3(0.0f, 0.0f, 5.0f);
    REQUIRE(comp.addNode(std::move(b)).has_value());
    auto sparks = makeNode(scene::NodeKind::Particles, "sparks");
    sparks.particles = sparkSettings();
    REQUIRE(comp.addNode(std::move(sparks)).has_value());

    params::ParameterSet params;
    params::Modulator modulator;
    comp.attach(params, modulator);
    CHECK(comp.attached());
    for (const char* p : {"camera/distance",
                          "camera/height",
                          "camera/orbitSpeed",
                          "camera/fov",
                          "env/intensity",
                          "env/rotation",
                          "scene/brightness",
                          "scene/gridIntensity",
                          "root/scale",
                          "root/rotationSpeed",
                          "root/impulse",
                          "nodes/a/position",
                          "nodes/a/rotation",
                          "nodes/a/scale",
                          "nodes/a/visible",
                          "nodes/a/emissiveBoost",
                          "nodes/a/roughnessScale",
                          "nodes/b/position",
                          "nodes/sparks/position",
                          "particles/sparks/spawnRate",
                          "particles/sparks/enabled"}) {
        INFO(p);
        CHECK(params.find(p) != nullptr);
    }
    CHECK(params.find("nodes/a/visible")->kind() == params::ParamKind::Bool);
    CHECK(modulator.routes().size() == 4);
    checkVec(params.findAs<glm::vec3>("nodes/b/position")->value(), glm::vec3(0.0f, 0.0f, 5.0f));
    // Nodes added while attached register immediately.
    REQUIRE(comp.addNode(makeNode(scene::NodeKind::Orb, "orb")).has_value());
    CHECK(params.find("nodes/orb/position") != nullptr);

    // Static frame (dt = 0): no root rotation accumulates.
    params.findAs<float>("root/rotationSpeed")->setBase(0.0f);
    params.resetFinals();
    comp.update(FrameTime{});
    const scene::Scene& sc = comp.scene();
    REQUIRE(sc.entities.size() == 3);
    checkVec(sc.entities[1].transform.position, glm::vec3(2.0f, 0.0f, 5.0f));

    SECTION("node position, rotation and scale move the instance") {
        params.findAs<glm::vec3>("nodes/b/position")->setBase(glm::vec3(1.0f, 2.0f, 3.0f));
        params.findAs<glm::vec3>("nodes/b/rotation")->setBase(glm::vec3(0.0f, 90.0f, 0.0f));
        params.findAs<glm::vec3>("nodes/b/scale")->setBase(glm::vec3(2.0f));
        params.resetFinals();
        comp.update(FrameTime{});
        // (2, 0, 0) * 2 = (4, 0, 0), rotated 90 degrees about +Y -> (0, 0, -4), plus (1, 2, 3).
        checkVec(sc.entities[1].transform.position, glm::vec3(1.0f, 2.0f, -1.0f));
        checkVec(sc.entities[1].transform.scale, glm::vec3(2.0f));
        checkQuat(sc.entities[1].transform.rotation,
                  glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
        // The other instance is untouched; the authored node follows the parameter base.
        checkVec(sc.entities[0].transform.position, glm::vec3(2.0f, 0.0f, 0.0f));
        checkVec(comp.findNode("b")->transform.position, glm::vec3(1.0f, 2.0f, 3.0f));
    }
    SECTION("non-uniform node scale goes through matrix decomposition") {
        params.findAs<glm::vec3>("nodes/a/scale")->setBase(glm::vec3(1.0f, 2.0f, 3.0f));
        params.resetFinals();
        comp.update(FrameTime{});
        checkVec(sc.entities[0].transform.position, glm::vec3(2.0f, 0.0f, 0.0f));
        checkVec(sc.entities[0].transform.scale, glm::vec3(1.0f, 2.0f, 3.0f));
    }
    SECTION("root scale and rotation apply about the bounds centre") {
        const glm::vec3 center = comp.boundsCenter();
        params.findAs<float>("root/scale")->setBase(2.0f);
        params.resetFinals();
        comp.update(FrameTime{});
        checkVec(sc.entities[0].transform.position, center + (glm::vec3(2.0f, 0.0f, 0.0f) - center) * 2.0f);
        checkVec(sc.entities[0].transform.scale, glm::vec3(2.0f));
        // Impulse adds to the scale.
        params.findAs<float>("root/impulse")->setBase(0.5f);
        params.resetFinals();
        comp.update(FrameTime{});
        checkVec(sc.entities[0].transform.scale, glm::vec3(2.5f));

        params.findAs<float>("root/scale")->setBase(1.0f);
        params.findAs<float>("root/impulse")->setBase(0.0f);
        params.findAs<float>("root/rotationSpeed")->setBase(6.0f);
        params.resetFinals();
        FixedStepClock clock(60.0);
        clock.tick();
        comp.update(clock.tick()); // dt = 1/60 -> angle 0.1 rad
        const glm::quat rot = glm::angleAxis(0.1f, glm::vec3(0.0f, 1.0f, 0.0f));
        checkVec(sc.entities[0].transform.position, center + rot * (glm::vec3(2.0f, 0.0f, 0.0f) - center));
        checkQuat(sc.entities[0].transform.rotation, rot);
    }
    SECTION("visibility, emissive boost and roughness scale") {
        params.findAs<bool>("nodes/b/visible")->setBase(false);
        params.findAs<float>("nodes/a/emissiveBoost")->setBase(3.0f);
        params.findAs<float>("nodes/a/roughnessScale")->setBase(0.5f);
        params.resetFinals();
        comp.update(FrameTime{});
        CHECK_FALSE(sc.entities[1].visible);
        CHECK(sc.entities[0].visible);
        CHECK_THAT(static_cast<double>(sc.entities[0].material.emissiveIntensity), WithinAbs(3.0, 1e-6));
        CHECK_THAT(static_cast<double>(sc.entities[0].material.roughness), WithinAbs(0.3, 1e-6));
        CHECK(sc.entities[1].material.emissiveIntensity == 1.0f);
        CHECK_FALSE(comp.findNode("b")->visible);
    }
    SECTION("particle node parameters apply and follow the node transform") {
        params.findAs<float>("particles/sparks/spawnRate")->setBase(123.0f);
        params.findAs<glm::vec3>("nodes/sparks/position")->setBase(glm::vec3(0.0f, 0.0f, 4.0f));
        params.resetFinals();
        comp.update(FrameTime{});
        REQUIRE(sc.particles.size() == 1);
        CHECK(sc.particles[0].spawnRate == 123.0f);
        checkVec(sc.particles[0].position, glm::vec3(0.0f, 1.0f, 4.0f));
        CHECK(sc.particles[0].enabled);
        params.findAs<bool>("nodes/sparks/visible")->setBase(false);
        params.resetFinals();
        comp.update(FrameTime{});
        CHECK_FALSE(sc.particles[0].enabled);
    }
    SECTION("camera and environment parameters") {
        params.findAs<float>("camera/distance")->setBase(10.0f);
        params.findAs<float>("camera/height")->setBase(4.0f);
        params.findAs<float>("camera/fov")->setBase(60.0f);
        params.findAs<float>("scene/brightness")->setBase(2.0f);
        params.findAs<float>("scene/gridIntensity")->setBase(0.25f);
        params.findAs<float>("env/intensity")->setBase(1.5f);
        params.findAs<float>("env/rotation")->setBase(1.0f);
        params.resetFinals();
        comp.update(FrameTime{});
        const glm::vec3 center = comp.boundsCenter();
        checkVec(sc.camera.position, glm::vec3(center.x, 4.0f, center.z + 10.0f));
        checkVec(sc.camera.target, center);
        CHECK_THAT(static_cast<double>(sc.camera.fovYRadians), WithinAbs(glm::radians(60.0), 1e-6));
        CHECK(sc.environment.brightness == 2.0f);
        CHECK(sc.environment.gridIntensity == 0.25f);
        CHECK(sc.environment.environmentIntensity == 1.5f);
        CHECK(sc.environment.environmentRotation == 1.0f);
    }
    SECTION("removeNode unregisters its parameters and rebuilds") {
        const std::size_t before = params.size();
        CHECK(comp.removeNode("sparks"));
        CHECK(params.find("nodes/sparks/position") == nullptr);
        CHECK(params.find("particles/sparks/spawnRate") == nullptr);
        CHECK(params.size() == before - 26); // 6 node parameters + 20 particle parameters
        CHECK(comp.removeNode("b"));
        CHECK(params.find("nodes/b/position") == nullptr);
        CHECK(params.find("nodes/a/position") != nullptr);
        comp.update(FrameTime{});
        CHECK(sc.entities.size() == 2);
        CHECK(sc.particles.empty());
    }
    SECTION("detach leaves no dangling use; attaching to a fresh set works") {
        comp.detach();
        CHECK_FALSE(comp.attached());
        params.clear();
        comp.update(FrameTime{}); // runs on the authored values
        checkVec(sc.entities[1].transform.position, glm::vec3(2.0f, 0.0f, 5.0f));
        params::ParameterSet fresh;
        params::Modulator freshModulator;
        comp.attach(fresh, freshModulator);
        CHECK(fresh.find("nodes/a/position") != nullptr);
        CHECK(fresh.find("particles/sparks/spawnRate") != nullptr);
        fresh.findAs<glm::vec3>("nodes/a/position")->setBase(glm::vec3(0.0f, 1.0f, 0.0f));
        fresh.resetFinals();
        comp.update(FrameTime{});
        checkVec(sc.entities[0].transform.position, glm::vec3(2.0f, 1.0f, 0.0f));
        CHECK(freshModulator.routes().size() == 4);
    }
}

TEST_CASE("Composition serialises to JSON and back", "[scene][composition][json]") {
    Fixture fx;
    const std::string text = R"({
  "format": "avgen-scene", "version": 1, "name": "roundtrip",
  "camera": {"distance": 9.0, "height": 2.0, "orbitSpeed": 0.3, "fov": 45.0},
  "environment": {"map": "sky.hdr", "intensity": 1.5},
  "nodes": [
    {"name": "a", "kind": "gltf", "asset": ")" +
                             fx.glb.filename().string() + R"(", "position": [1, 2, 3], "rotation": [0, 90, 0],
     "scale": [2, 2, 2], "visible": false, "emissiveBoost": 2.0, "roughnessScale": 0.5},
    {"name": "orb", "kind": "orb", "position": [0, 1.5, 0]},
    {"name": "grid", "kind": "grid"},
    {"name": "sparks", "kind": "particles", "position": [0, 0, 1], "particles": {
      "capacity": 1024, "seed": 3, "shape": "box", "blend": "alpha", "spawnRate": 42.0,
      "position": [0, 2, 0], "extent": [1, 0.5, 1], "colorStart": [0.1, 0.2, 0.3, 0.4], "softness": 0.9}}
  ]})";
    auto loaded = scene::Composition::fromJson(nlohmann::json::parse(text), fx.registry);
    REQUIRE(loaded.has_value());
    scene::Composition& comp = **loaded;
    CHECK(comp.name() == "roundtrip");
    REQUIRE(comp.nodeCount() == 4);
    CHECK(comp.environmentMap() == std::filesystem::path("sky.hdr"));
    const scene::CompositionNode* a = comp.findNode("a");
    REQUIRE(a != nullptr);
    CHECK(a->kind == scene::NodeKind::Gltf);
    checkVec(a->transform.position, glm::vec3(1.0f, 2.0f, 3.0f));
    checkQuat(a->transform.rotation, glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
    checkVec(a->transform.scale, glm::vec3(2.0f));
    CHECK_FALSE(a->visible);
    CHECK(a->emissiveBoost == 2.0f);
    CHECK(a->roughnessScale == 0.5f);
    const scene::CompositionNode* sparks = comp.findNode("sparks");
    REQUIRE(sparks != nullptr);
    CHECK(sparks->particles.capacity == 1024);
    CHECK(sparks->particles.seed == 3);
    CHECK(sparks->particles.shape == scene::EmitterShape::Box);
    CHECK(sparks->particles.blend == scene::ParticleBlend::Alpha);
    CHECK(sparks->particles.spawnRate == 42.0f);
    CHECK(sparks->particles.softness == 0.9f);
    CHECK(sparks->particles.lifetimeMax == scene::ParticleSystem{}.lifetimeMax); // default kept
    checkVec(sparks->particles.extent, glm::vec3(1.0f, 0.5f, 1.0f));

    const nlohmann::json j = comp.toJson();
    CHECK(j["format"] == "avgen-scene");
    CHECK(j["version"] == 1);
    CHECK(j["name"] == "roundtrip");
    CHECK(j["camera"]["distance"] == 9.0);
    CHECK(j["camera"]["height"] == 2.0);
    CHECK_THAT(j["camera"]["orbitSpeed"].get<double>(), WithinAbs(0.3, 1e-6));
    CHECK(j["camera"]["fov"] == 45.0);
    CHECK(j["environment"]["map"] == "sky.hdr");
    CHECK(j["environment"]["intensity"] == 1.5);
    REQUIRE(j["nodes"].size() == 4);
    CHECK(j["nodes"][0]["kind"] == "gltf");
    CHECK(j["nodes"][0]["asset"] == fx.glb.filename().string());
    CHECK(j["nodes"][0]["position"] == nlohmann::json::array({1.0, 2.0, 3.0}));
    CHECK_THAT(j["nodes"][0]["rotation"][1].get<double>(), WithinAbs(90.0, 1e-3));
    CHECK(j["nodes"][0]["visible"] == false);
    CHECK(j["nodes"][3]["particles"]["shape"] == "box");
    CHECK(j["nodes"][3]["particles"]["blend"] == "alpha");
    CHECK(j["nodes"][3]["particles"]["capacity"] == 1024);
    CHECK(j["nodes"][3]["particles"]["spawnRate"] == 42.0);
    CHECK_FALSE(j["nodes"][1].contains("particles"));

    // Second pass through the format reproduces the same nodes.
    auto again = scene::Composition::fromJson(j, fx.registry);
    REQUIRE(again.has_value());
    REQUIRE((*again)->nodeCount() == 4);
    for (std::size_t i = 0; i < 4; ++i) {
        const auto& x = *comp.nodes()[i];
        const auto& y = *(*again)->nodes()[i];
        INFO(x.name);
        CHECK(x.name == y.name);
        CHECK(x.kind == y.kind);
        CHECK(x.asset == y.asset);
        checkVec(x.transform.position, y.transform.position);
        checkQuat(x.transform.rotation, y.transform.rotation);
        checkVec(x.transform.scale, y.transform.scale);
        CHECK(x.visible == y.visible);
        CHECK(x.emissiveBoost == y.emissiveBoost);
        CHECK(x.roughnessScale == y.roughnessScale);
        CHECK(x.particles.capacity == y.particles.capacity);
        CHECK(x.particles.spawnRate == y.particles.spawnRate);
        CHECK(x.particles.blend == y.particles.blend);
    }
    CHECK((*again)->toJson() == j);

    // When attached, the camera and node values come from the parameter bases.
    params::ParameterSet params;
    params::Modulator modulator;
    comp.attach(params, modulator);
    CHECK(params.findAs<float>("camera/distance")->value() == 9.0f);
    CHECK(params.findAs<float>("env/intensity")->value() == 1.5f);
    CHECK_FALSE(params.findAs<bool>("nodes/a/visible")->value());
    params.findAs<float>("camera/distance")->setBase(12.0f);
    params.findAs<glm::vec3>("nodes/orb/position")->setBase(glm::vec3(5.0f, 0.0f, 0.0f));
    params.resetFinals();
    comp.update(FrameTime{});
    const nlohmann::json edited = comp.toJson();
    CHECK(edited["camera"]["distance"] == 12.0);
    CHECK(edited["nodes"][1]["position"] == nlohmann::json::array({5.0, 0.0, 0.0}));
}

TEST_CASE("Composition saves and loads scene files", "[scene][composition][json]") {
    Fixture fx;
    scene::Composition comp(fx.registry, "saved");
    REQUIRE(comp.addNode(makeNode(scene::NodeKind::Gltf, "a", fx.glb.filename())).has_value());
    REQUIRE(comp.addNode(makeNode(scene::NodeKind::Orb, "orb")).has_value());
    const auto path = tempDir() / "avgen_comp_saved.json";
    fx.files.push_back(path);
    REQUIRE(comp.saveFile(path).has_value());
    CHECK(comp.sourcePath().empty());

    auto loaded = scene::Composition::loadFile(path.filename(), fx.registry);
    REQUIRE(loaded.has_value());
    CHECK((*loaded)->name() == "saved");
    CHECK((*loaded)->nodeCount() == 2);
    CHECK((*loaded)->sourcePath() == fx.registry.resolve(path));
    (*loaded)->update(FrameTime{});
    CHECK((*loaded)->scene().entities.size() == 2);

    CHECK_FALSE(comp.saveFile("/nonexistent-dir/x.json").has_value());
    CHECK_FALSE(scene::Composition::loadFile("avgen_comp_missing.json", fx.registry).has_value());
}

TEST_CASE("Composition rejects malformed files and skips missing assets", "[scene][composition][json]") {
    Fixture fx;
    const auto bad = fx.json("bad", "{ not json");
    auto r = scene::Composition::loadFile(bad, fx.registry);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().message.find("invalid JSON") != std::string::npos);

    const auto wrongFormat = fx.json("format", R"({"format": "other", "version": 1})");
    CHECK_FALSE(scene::Composition::loadFile(wrongFormat, fx.registry).has_value());
    const auto wrongVersion = fx.json("version", R"({"format": "avgen-scene", "version": 99})");
    CHECK_FALSE(scene::Composition::loadFile(wrongVersion, fx.registry).has_value());
    const auto badKind =
        fx.json("kind", R"({"format": "avgen-scene", "version": 1, "nodes": [{"kind": "cube"}]})");
    CHECK_FALSE(scene::Composition::loadFile(badKind, fx.registry).has_value());
    const auto badVec = fx.json(
        "vec", R"({"format": "avgen-scene", "version": 1, "nodes": [{"kind": "orb", "position": [1, 2]}]})");
    CHECK_FALSE(scene::Composition::loadFile(badVec, fx.registry).has_value());
    CHECK_FALSE(scene::Composition::fromJson(nlohmann::json::array(), fx.registry).has_value());

    // A missing glTF or nested scene costs its node, not the scene.
    const auto missing = fx.json("missing", R"({"format": "avgen-scene", "version": 1, "nodes": [
        {"name": "gone", "kind": "gltf", "asset": "avgen_comp_nope.glb"},
        {"name": "inner", "kind": "scene", "asset": "avgen_comp_nope.json"},
        {"name": "orb", "kind": "orb"}]})");
    auto loaded = scene::Composition::loadFile(missing, fx.registry);
    REQUIRE(loaded.has_value());
    CHECK((*loaded)->nodeCount() == 1);
    CHECK((*loaded)->findNode("orb") != nullptr);
    CHECK((*loaded)->findNode("gone") == nullptr);
}

TEST_CASE("Composition takes material programs inline or from a file", "[scene][composition][material]") {
    Fixture fx;
    // A library material as its own file, the way examples/materials/*.material.json ship.
    const auto lib = fx.json("library_material", R"({"name": "libIron",
        "ops": [{"kind": "constant", "dst": 1, "constant": [0.2, 0.1, 0.05, 1]}],
        "baseColor": 1, "roughness": -1})");
    // A file that forgot its name falls back to the file's, minus the ".material" the library uses.
    const auto unnamed = fx.json("anon.material", R"({"ops": [{"kind": "constant", "dst": 2,
        "constant": [1, 1, 1, 1]}], "metallic": 2})");
    const auto scene = fx.json("materials_scene", R"({"format": "avgen-scene", "version": 1,
        "name": "mats", "materialPrograms": [
            ")" + lib.filename().string() + R"(",
            {"name": "inlineGlass", "ops": [{"kind": "fresnel", "dst": 3, "value": 2.0}], "emission": 3},
            ")" + unnamed.filename().string() + R"("],
        "nodes": [{"name": "orb", "kind": "orb"}]})");

    auto loaded = scene::Composition::loadFile(scene, fx.registry);
    REQUIRE(loaded.has_value());
    const auto& programs = (*loaded)->materialPrograms();
    REQUIRE(programs.size() == 3);
    CHECK(programs[0].name == "libIron"); // from the file, in the order the array gives
    REQUIRE(programs[0].ops.size() == 1);
    CHECK(programs[0].ops[0].constant == glm::vec4(0.2f, 0.1f, 0.05f, 1.0f));
    CHECK(programs[1].name == "inlineGlass");
    CHECK(programs[2].name == "avgen_comp_anon"); // named by its file, ".material" stripped

    // A path that does not resolve, and a malformed program, are both errors rather than a
    // silently missing material.
    const auto missing = fx.json("materials_missing", R"({"format": "avgen-scene", "version": 1,
        "materialPrograms": ["avgen_no_such_material.json"]})");
    CHECK_FALSE(scene::Composition::loadFile(missing, fx.registry).has_value());
    const auto wrongType = fx.json("materials_wrong", R"({"format": "avgen-scene", "version": 1,
        "materialPrograms": [42]})");
    CHECK_FALSE(scene::Composition::loadFile(wrongType, fx.registry).has_value());
}

TEST_CASE("Composition nests scene files and flattens them with prefixed parameters",
          "[scene][composition]") {
    Fixture fx;
    const auto child = fx.json("child", R"({"format": "avgen-scene", "version": 1, "name": "child", "nodes": [
        {"name": "orb", "kind": "orb", "position": [0, 1.5, 0]},
        {"name": "tri", "kind": "gltf", "asset": ")" +
                                            fx.glb.filename().string() + R"("},
        {"name": "p", "kind": "particles", "particles": {"spawnRate": 55.0, "position": [0, 1, 0]}}]})");
    const auto parent =
        fx.json("parent", R"({"format": "avgen-scene", "version": 1, "name": "parent", "nodes": [
        {"name": "inner", "kind": "scene", "asset": ")" +
                              child.filename().string() + R"(", "position": [0, 0, 10]},
        {"name": "grid", "kind": "grid"}]})");

    auto loaded = scene::Composition::loadFile(parent, fx.registry);
    REQUIRE(loaded.has_value());
    scene::Composition& comp = **loaded;
    REQUIRE(comp.nodeCount() == 2);
    const scene::CompositionNode* inner = comp.findNode("inner");
    REQUIRE(inner != nullptr);
    REQUIRE(inner->child != nullptr);
    CHECK(inner->child->nodeCount() == 3);
    CHECK(inner->child->sourcePath() == fx.registry.resolve(child));

    params::ParameterSet params;
    params::Modulator modulator;
    comp.attach(params, modulator);
    for (const char* p :
         {"nodes/inner/position", "nodes/inner/nodes/orb/position", "nodes/inner/nodes/tri/visible",
          "nodes/inner/root/scale", "nodes/inner/nodes/p/position", "particles/nodes_inner_p/spawnRate"}) {
        INFO(p);
        CHECK(params.find(p) != nullptr);
    }
    CHECK(inner->child->attached());
    CHECK(modulator.routes().size() == 4); // default routes come from the root only
    params.findAs<float>("root/rotationSpeed")->setBase(0.0f);
    params.resetFinals();
    comp.update(FrameTime{});

    const scene::Scene& sc = comp.scene();
    CHECK(sc.entities.size() == 3); // orb + tri from the child, plus the grid
    CHECK(sc.meshes.size() == 3);
    const scene::Entity* orb = findEntity(sc, "inner/orb");
    REQUIRE(orb != nullptr);
    checkVec(orb->transform.position, glm::vec3(0.0f, 1.5f, 10.0f));
    const scene::Entity* tri = findEntity(sc, "inner/tri/tri");
    REQUIRE(tri != nullptr);
    checkVec(tri->transform.position, glm::vec3(2.0f, 0.0f, 10.0f));
    CHECK(tri->mesh < sc.meshes.size());
    REQUIRE(sc.particles.size() == 1);
    CHECK(sc.particles[0].spawnRate == 55.0f);
    checkVec(sc.particles[0].position, glm::vec3(0.0f, 1.0f, 10.0f));
    REQUIRE_FALSE(sc.lights.empty());
    checkVec(sc.lights[0].position, glm::vec3(0.0f, 3.0f, 10.0f)); // the lamp inside the child

    // Nested parameters reach the flattened entities through the child's update.
    params.findAs<glm::vec3>("nodes/inner/nodes/orb/position")->setBase(glm::vec3(1.0f, 0.0f, 0.0f));
    params.findAs<float>("particles/nodes_inner_p/spawnRate")->setBase(5.0f);
    params.findAs<bool>("nodes/inner/nodes/tri/visible")->setBase(false);
    params.resetFinals();
    comp.update(FrameTime{});
    checkVec(findEntity(sc, "inner/orb")->transform.position, glm::vec3(1.0f, 0.0f, 10.0f));
    CHECK(sc.particles[0].spawnRate == 5.0f);
    CHECK_FALSE(findEntity(sc, "inner/tri/tri")->visible);
    // Structural changes inside the child propagate on the next update.
    REQUIRE(inner->child->addNode(makeNode(scene::NodeKind::Orb, "extra")).has_value());
    CHECK(params.find("nodes/inner/nodes/extra/position") != nullptr);
    comp.update(FrameTime{});
    CHECK(sc.entities.size() == 4);
    CHECK(findEntity(sc, "inner/extra") != nullptr);

    // Saving keeps the reference to the child file rather than inlining it.
    const nlohmann::json j = comp.toJson();
    CHECK(j["nodes"][0]["kind"] == "scene");
    CHECK(j["nodes"][0]["asset"] == child.filename().string());

    // Removing the nested node drops every nested parameter.
    CHECK(comp.removeNode("inner"));
    CHECK(params.find("nodes/inner/nodes/orb/position") == nullptr);
    CHECK(params.find("nodes/inner/camera/distance") == nullptr);
    CHECK(params.find("particles/nodes_inner_p/spawnRate") == nullptr);
    CHECK(params.find("nodes/grid/position") != nullptr);
    comp.update(FrameTime{});
    CHECK(sc.entities.size() == 1);
}

TEST_CASE("Composition refuses cycles and excessive nesting", "[scene][composition]") {
    Fixture fx;
    const std::string head =
        R"({"format": "avgen-scene", "version": 1, "nodes": [{"kind": "scene", "asset": ")";
    const std::string tail = R"("}]})";
    const auto self = tempDir() / "avgen_comp_self.json";
    fx.json("self", head + self.filename().string() + tail);
    auto r = scene::Composition::loadFile(self, fx.registry);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().message.find("includes itself") != std::string::npos);

    // Indirect cycle: a -> b -> a.
    const auto a = tempDir() / "avgen_comp_cycle_a.json";
    const auto b = tempDir() / "avgen_comp_cycle_b.json";
    fx.json("cycle_a", head + b.filename().string() + tail);
    fx.json("cycle_b", head + a.filename().string() + tail);
    auto cycle = scene::Composition::loadFile(a, fx.registry);
    REQUIRE_FALSE(cycle.has_value());
    CHECK(cycle.error().message.find("includes itself") != std::string::npos);

    // addNode on a loaded composition performs the same check.
    const auto leaf =
        fx.json("leaf", R"({"format": "avgen-scene", "version": 1, "nodes": [{"kind": "orb"}]})");
    auto loadedLeaf = scene::Composition::loadFile(leaf, fx.registry);
    REQUIRE(loadedLeaf.has_value());
    CHECK_FALSE((*loadedLeaf)->addNode(makeNode(scene::NodeKind::Scene, "me", leaf.filename())).has_value());

    // Depth: d0 -> d1 -> ... -> d5 is six levels; the limit allows five.
    for (int i = 0; i < 6; ++i) {
        const std::string name = "depth" + std::to_string(i);
        if (i == 5) {
            fx.json(name, R"({"format": "avgen-scene", "version": 1, "nodes": [{"kind": "orb"}]})");
        } else {
            fx.json(name, head + "avgen_comp_depth" + std::to_string(i + 1) + ".json" + tail);
        }
    }
    auto okChain = scene::Composition::loadFile("avgen_comp_depth1.json", fx.registry);
    REQUIRE(okChain.has_value());
    (*okChain)->update(FrameTime{});
    CHECK((*okChain)->scene().entities.size() == 1);
    CHECK(findEntity((*okChain)->scene(), "scene/scene/scene/scene/orb") != nullptr);
    auto tooDeep = scene::Composition::loadFile("avgen_comp_depth0.json", fx.registry);
    REQUIRE_FALSE(tooDeep.has_value());
    CHECK(tooDeep.error().message.find("deeper") != std::string::npos);
    CHECK_FALSE(scene::Composition::loadFile("avgen_comp_depth5.json", fx.registry,
                                             scene::Composition::kMaxNestingDepth + 1)
                    .has_value());
}

TEST_CASE("Composition is deterministic for the same clock", "[scene][composition]") {
    Fixture fx;
    const auto file = fx.json("determinism", R"({"format": "avgen-scene", "version": 1, "nodes": [
        {"name": "a", "kind": "gltf", "asset": ")" +
                                                 fx.glb.filename().string() + R"("},
        {"name": "b", "kind": "gltf", "asset": ")" +
                                                 fx.glb.filename().string() + R"(", "position": [0, 0, 3]},
        {"name": "orb", "kind": "orb", "position": [0, 1.5, 0], "rotation": [10, 20, 30]},
        {"name": "sparks", "kind": "particles"}]})");
    auto one = scene::Composition::loadFile(file, fx.registry);
    auto two = scene::Composition::loadFile(file, fx.registry);
    REQUIRE(one.has_value());
    REQUIRE(two.has_value());
    params::ParameterSet p1;
    params::ParameterSet p2;
    params::Modulator m1;
    params::Modulator m2;
    (*one)->attach(p1, m1);
    (*two)->attach(p2, m2);
    p1.findAs<float>("root/rotationSpeed")->setBase(2.0f);
    p2.findAs<float>("root/rotationSpeed")->setBase(2.0f);
    p1.resetFinals();
    p2.resetFinals();
    FixedStepClock c1(60.0);
    FixedStepClock c2(60.0);
    for (int i = 0; i < 90; ++i) {
        (*one)->update(c1.tick());
        (*two)->update(c2.tick());
    }
    const auto& s1 = (*one)->scene();
    const auto& s2 = (*two)->scene();
    REQUIRE(s1.entities.size() == s2.entities.size());
    REQUIRE(s1.entities.size() == 3);
    for (std::size_t i = 0; i < s1.entities.size(); ++i) {
        CHECK(s1.entities[i].transform.matrix() == s2.entities[i].transform.matrix());
    }
    CHECK(s1.camera.position == s2.camera.position);
    CHECK(s1.particles[0].position == s2.particles[0].position);
    // The root actually rotated.
    CHECK(s1.entities[0].transform.position.z != 0.0f);
}

TEST_CASE("nodeKindName and nodeKindFromName round trip", "[scene][composition]") {
    for (const scene::NodeKind kind : {scene::NodeKind::Gltf, scene::NodeKind::Orb, scene::NodeKind::Grid,
                                       scene::NodeKind::Particles, scene::NodeKind::Scene}) {
        auto back = scene::nodeKindFromName(scene::nodeKindName(kind));
        REQUIRE(back.has_value());
        CHECK(*back == kind);
    }
    CHECK_FALSE(scene::nodeKindFromName("cube").has_value());
}

TEST_CASE("Composition node parenting: children follow the parent, cycles are refused, removal reparents",
          "[scene][composition][parenting]") {
    Fixture fx;
    scene::Composition comp(fx.registry, "tree");
    auto parent = makeNode(scene::NodeKind::Orb, "parent");
    parent.transform.position = glm::vec3(1.0f, 0.0f, 0.0f);
    parent.transform.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    parent.transform.scale = glm::vec3(2.0f);
    REQUIRE(comp.addNode(std::move(parent)).has_value());
    auto child = makeNode(scene::NodeKind::Orb, "child");
    child.parent = "parent";
    child.transform.position = glm::vec3(1.0f, 0.0f, 0.0f);
    REQUIRE(comp.addNode(std::move(child)).has_value());
    auto grandchild = makeNode(scene::NodeKind::Orb, "grandchild");
    grandchild.parent = "child";
    grandchild.transform.position = glm::vec3(0.0f, 1.0f, 0.0f);
    REQUIRE(comp.addNode(std::move(grandchild)).has_value());
    comp.update(FrameTime{});
    const scene::Scene& sc = comp.scene();
    const glm::quat ninety = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    // child: local (1,0,0) scaled by 2 -> (2,0,0), rotated 90 deg about +Y -> (0,0,-2), plus (1,0,0).
    REQUIRE(findEntity(sc, "child") != nullptr);
    checkVec(findEntity(sc, "child")->transform.position, glm::vec3(1.0f, 0.0f, -2.0f));
    checkVec(findEntity(sc, "child")->transform.scale, glm::vec3(2.0f));
    checkQuat(findEntity(sc, "child")->transform.rotation, ninety);
    // grandchild: local (0,1,0) under the child's world (scale 2, rotated) -> (0,2,0) + (1,0,-2).
    REQUIRE(findEntity(sc, "grandchild") != nullptr);
    checkVec(findEntity(sc, "grandchild")->transform.position, glm::vec3(1.0f, 2.0f, -2.0f));
    checkVec(findEntity(sc, "grandchild")->transform.scale, glm::vec3(2.0f));
    // The parent itself is unaffected; nodeWorldTransform agrees with the flattened scene.
    checkVec(findEntity(sc, "parent")->transform.position, glm::vec3(1.0f, 0.0f, 0.0f));
    checkVec(comp.nodeWorldTransform(*comp.findNode("grandchild")).position, glm::vec3(1.0f, 2.0f, -2.0f));

    SECTION("a parameter change on the parent moves the child; parameter paths are unchanged") {
        params::ParameterSet params;
        params::Modulator modulator;
        comp.attach(params, modulator);
        CHECK(params.find("nodes/child/position") != nullptr);
        CHECK(params.find("nodes/parent/nodes/child/position") == nullptr);
        params.findAs<float>("root/rotationSpeed")->setBase(0.0f);
        params.findAs<glm::vec3>("nodes/parent/position")->setBase(glm::vec3(5.0f, 0.0f, 0.0f));
        params.resetFinals();
        comp.update(FrameTime{});
        checkVec(findEntity(sc, "child")->transform.position, glm::vec3(5.0f, 0.0f, -2.0f));
        checkVec(findEntity(sc, "grandchild")->transform.position, glm::vec3(5.0f, 2.0f, -2.0f));
        params.findAs<glm::vec3>("nodes/parent/rotation")->setBase(glm::vec3(0.0f));
        params.findAs<glm::vec3>("nodes/parent/scale")->setBase(glm::vec3(1.0f));
        params.resetFinals();
        comp.update(FrameTime{});
        checkVec(findEntity(sc, "child")->transform.position, glm::vec3(6.0f, 0.0f, 0.0f));
        checkVec(findEntity(sc, "child")->transform.scale, glm::vec3(1.0f));
    }
    SECTION("cycles are rejected") {
        CHECK_FALSE(comp.setParent("parent", "grandchild").has_value());
        CHECK_FALSE(comp.setParent("parent", "parent").has_value());
        CHECK(comp.findNode("parent")->parent.empty());
        auto self = makeNode(scene::NodeKind::Orb, "loop");
        self.parent = "loop";
        CHECK_FALSE(comp.addNode(std::move(self)).has_value());
        CHECK(comp.findNode("loop") == nullptr);
        // A dangling parent that a later node would close into a cycle.
        auto dangling = makeNode(scene::NodeKind::Orb, "d1");
        dangling.parent = "d2";
        REQUIRE(comp.addNode(std::move(dangling)).has_value());
        auto closing = makeNode(scene::NodeKind::Orb, "d2");
        closing.parent = "d1";
        CHECK_FALSE(comp.addNode(std::move(closing)).has_value());
        CHECK_FALSE(comp.setParent("nope", "parent").has_value());
        CHECK(comp.setParent("grandchild", "").has_value());
        comp.update(FrameTime{});
        checkVec(findEntity(sc, "grandchild")->transform.position, glm::vec3(0.0f, 1.0f, 0.0f));
    }
    SECTION("an unknown parent is a root") {
        auto orphan = makeNode(scene::NodeKind::Orb, "orphan");
        orphan.parent = "ghost";
        orphan.transform.position = glm::vec3(0.0f, 0.0f, 3.0f);
        REQUIRE(comp.addNode(std::move(orphan)).has_value());
        CHECK(comp.findNode("orphan")->parent == "ghost"); // kept for the file
        comp.update(FrameTime{});
        checkVec(findEntity(sc, "orphan")->transform.position, glm::vec3(0.0f, 0.0f, 3.0f));
    }
    SECTION("removing a node hands its children to the grandparent") {
        REQUIRE(comp.removeNode("child"));
        CHECK(comp.findNode("grandchild")->parent == "parent");
        comp.update(FrameTime{});
        // local (0,1,0) under the parent: scaled (0,2,0), rotation about Y leaves it, plus (1,0,0).
        checkVec(findEntity(sc, "grandchild")->transform.position, glm::vec3(1.0f, 2.0f, 0.0f));
        REQUIRE(comp.removeNode("parent"));
        CHECK(comp.findNode("grandchild")->parent.empty());
    }
}

TEST_CASE("Composition parents round-trip JSON, allow forward references and refuse cycles",
          "[scene][composition][parenting][json]") {
    Fixture fx;
    const std::string text = R"({
  "format": "avgen-scene", "version": 1, "name": "parented",
  "nodes": [
    {"name": "leaf", "kind": "orb", "parent": "root", "position": [0, 1, 0]},
    {"name": "root", "kind": "orb", "position": [3, 0, 0], "scale": [2, 2, 2]},
    {"name": "lost", "kind": "grid", "parent": "missing"}
  ]})";
    auto loaded = scene::Composition::fromJson(nlohmann::json::parse(text), fx.registry);
    REQUIRE(loaded.has_value());
    scene::Composition& comp = **loaded;
    REQUIRE(comp.nodeCount() == 3);
    CHECK(comp.findNode("leaf")->parent == "root");
    CHECK(comp.findNode("lost")->parent == "missing");
    comp.update(FrameTime{});
    checkVec(findEntity(comp.scene(), "leaf")->transform.position, glm::vec3(3.0f, 2.0f, 0.0f));
    const nlohmann::json j = comp.toJson();
    CHECK(j["nodes"][0]["parent"] == "root");
    CHECK_FALSE(j["nodes"][1].contains("parent"));
    auto again = scene::Composition::fromJson(j, fx.registry);
    REQUIRE(again.has_value());
    CHECK((*again)->findNode("leaf")->parent == "root");
    (*again)->update(FrameTime{});
    checkVec(findEntity((*again)->scene(), "leaf")->transform.position, glm::vec3(3.0f, 2.0f, 0.0f));

    const std::string cycle = R"({"format": "avgen-scene", "version": 1, "nodes": [
        {"name": "a", "kind": "orb", "parent": "b"}, {"name": "b", "kind": "orb", "parent": "a"}]})";
    auto bad = scene::Composition::fromJson(nlohmann::json::parse(cycle), fx.registry);
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().message.find("cycle") != std::string::npos);
}

TEST_CASE("Composition round-trips simulated grids and the volumetric environment (ADR-032)",
          "[scene][composition][grid]") {
    Fixture fx;
    const std::string text = R"({
      "format": "avgen-scene", "version": 1, "name": "volumes",
      "environment": {
        "volumeDensity": 0.05, "fogHeight": 2.0, "fogHeightFalloff": 0.25,
        "volumeScattering": 1.2, "volumeAbsorption": 0.8, "volumeAnisotropy": 0.4,
        "volumeNoise": 0.6, "volumeNoiseScale": 0.09, "volumeNoiseSpeed": 0.2,
        "volumeEmission": 0.3, "volumeSteps": 48, "volumeMaxDistance": 120.0,
        "volumeDensityField": "smokeField", "volumeColorField": "heat"
      },
      "grids": [
        { "name": "smoke", "mode": "scalar", "resolution": 16,
          "boundsMin": [-4, 0, -4], "boundsMax": [4, 8, 4],
          "injectField": "heat", "diffusion": 0.5, "diffuseIterations": 3, "dissipation": 0.2 }
      ],
      "nodes": [
        { "name": "heat", "kind": "field", "field": { "kind": "radial", "radius": 4.0 } },
        { "name": "smokeField", "kind": "field",
          "field": { "kind": "grid", "reference": "smoke" } }
      ]
    })";
    auto comp = scene::Composition::fromJson(nlohmann::json::parse(text), fx.registry);
    REQUIRE(comp.has_value());
    REQUIRE((*comp)->grids().size() == 1);
    CHECK((*comp)->grids()[0].resolution == glm::ivec3(16));
    CHECK((*comp)->grids()[0].diffuseIterations == 3);

    params::ParameterSet params;
    params::Modulator modulator;
    (*comp)->attach(params, modulator);
    (*comp)->update(FrameTime{});
    const scene::Scene& s = (*comp)->scene();
    REQUIRE(s.fields.grids.size() == 1);
    CHECK(s.fields.grids[0].name == "smoke");
    CHECK(s.fields.grids[0].injectField == "heat");
    REQUIRE(s.fields.findGrid("smoke") != nullptr);
    // The Grid field resolves to it, so anything that samples fields sees the grid.
    const spatial::FieldSpec* gridField = s.fields.find("smokeField");
    REQUIRE(gridField != nullptr);
    CHECK(gridField->kind == spatial::FieldKind::Grid);
    CHECK(gridField->reference == "smoke");
    // The environment block came through and the parameters carry it.
    CHECK(s.environment.volumeDensity == 0.05f);
    CHECK(s.environment.volumeSteps == 48);
    CHECK(s.environment.volumeDensityField == "smokeField");
    CHECK(s.environment.volumeColorField == "heat");
    REQUIRE(params.find("scene/volumeDensity") != nullptr);
    REQUIRE(params.find("scene/volumeSteps") != nullptr);

    // A JSON round trip preserves the settings and never writes the cell values.
    const nlohmann::json j = (*comp)->toJson();
    REQUIRE(j.contains("grids"));
    CHECK_FALSE(j["grids"][0].contains("data"));
    CHECK(j["environment"]["volumeSteps"] == 48);
    auto again = scene::Composition::fromJson(j, fx.registry);
    REQUIRE(again.has_value());
    REQUIRE((*again)->grids().size() == 1);
    CHECK((*again)->grids()[0].structuralHash() == (*comp)->grids()[0].structuralHash());
    CHECK((*again)->toJson() == j);
}

TEST_CASE("A scene file's environment can name a light rig", "[scene][composition][lightrig]") {
    Fixture fx;
    // The rig is written next to the scene file, so the registry resolves the relative path.
    const std::filesystem::path rig = fx.json("comp_rig.rig.json", R"({
        "format": "avgen-lightrig", "version": 1, "name": "CompRig",
        "keyIntensity": 2.0, "ambientIntensity": 0.25,
        "lights": [
          {"name": "key", "type": "directional", "role": "key",
           "azimuth": 30, "elevation": 40, "distance": 3.0, "intensity": 1.0,
           "temperature": 3200, "castsShadow": true},
          {"name": "sky", "type": "directional", "role": "ambient",
           "azimuth": 0, "elevation": 85, "distance": 4.0, "intensity": 1.0, "temperature": 9000}
        ]})");
    const std::string sceneText = R"({"format": "avgen-scene", "version": 1, "name": "rigged",
        "environment": {"lightRig": ")" + rig.filename().generic_string() + R"("},
        "nodes": [{"name": "orb", "kind": "orb"}]})";
    const std::filesystem::path scenePath = fx.json("comp_rigged.scene.json", sceneText);

    auto comp = scene::Composition::loadFile(scenePath, fx.registry);
    REQUIRE(comp.has_value());
    REQUIRE((*comp)->lightRig() != nullptr);
    CHECK((*comp)->lightRig()->name == "CompRig");
    CHECK((*comp)->lightRigPath() == rig.filename());

    // The rig replaces the default key light with its own, expanded around the composition's bounds.
    (*comp)->update(FrameTime{});
    const scene::Scene& s = (*comp)->scene();
    REQUIRE(s.lights.size() == 2);
    CHECK(s.lights[0].name == "key");
    CHECK(s.lights[0].castsShadow);
    CHECK(s.lights[0].temperature == 3200.0f);
    CHECK(s.lights[0].intensity > 0.0f);
    CHECK(s.lights[1].name == "sky");
    CHECK(s.lights[1].position.y > s.lights[0].position.y); // 85 degrees of elevation is higher

    // Re-expanded every frame rather than accumulated.
    (*comp)->update(FrameTime{});
    CHECK((*comp)->scene().lights.size() == 2);

    // The path survives a save/load round trip.
    const nlohmann::json doc = (*comp)->toJson();
    REQUIRE(doc.contains("environment"));
    CHECK(doc["environment"]["lightRig"] == rig.filename().generic_string());

    // Clearing it restores the default key.
    REQUIRE((*comp)->setLightRig({}).has_value());
    (*comp)->update(FrameTime{});
    CHECK((*comp)->lightRig() == nullptr);
    CHECK((*comp)->scene().lights.size() == 1);

    // A missing rig is a warning, not a failed load.
    const std::filesystem::path missing = fx.json("comp_norig.scene.json",
        R"({"format": "avgen-scene", "version": 1, "name": "norig",
            "environment": {"lightRig": "does-not-exist.rig.json"},
            "nodes": [{"name": "orb", "kind": "orb"}]})");
    auto fallbackComp = scene::Composition::loadFile(missing, fx.registry);
    REQUIRE(fallbackComp.has_value());
    CHECK((*fallbackComp)->lightRig() == nullptr);
}

// Composition framing (ADR-038): `targetScreenPosition` and `framingStrength` were parsed,
// hashed and serialised, and read by nothing, so a scene could ask for an off-centre subject and
// silently get a centred one.
TEST_CASE("Composition framing puts the focal point where the scene asks", "[composition][framing]") {
    auto sceneText = [](float sx, float sy, float strength) {
        return fmt::format(R"({{
          "format": "avgen-scene", "version": 1, "name": "framed",
          "camera": {{ "mode": 1, "position": [0, 0, 20], "target": [0, 0, 0], "fov": 50.0 }},
          "composition": {{
            "focalPoints": [{{ "name": "hero", "position": [0, 0, 0], "radius": 2.0 }}],
            "cameraTarget": "hero",
            "targetScreenPosition": [{}, {}],
            "framingStrength": {}
          }},
          "nodes": []
        }})", sx, sy, strength);
    };

    Fixture fx;
    // Where the focal point lands on screen, as a fraction from the top-left.
    auto screenOf = [&](float sx, float sy, float strength) {
        const auto path = writeJson("framing", sceneText(sx, sy, strength));
        fx.files.push_back(path);
        auto comp = scene::Composition::loadFile(path.filename(), fx.registry);
        REQUIRE(comp.has_value());
        (*comp)->update(FrameTime{});
        const scene::Scene& s = (*comp)->scene();
        const float aspect = s.camera.lens.sensorHeight > 1e-4f
                                 ? s.camera.lens.sensorWidth / s.camera.lens.sensorHeight
                                 : 16.0f / 9.0f;
        const glm::vec4 clip = s.camera.projection(aspect) * s.camera.view() * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        REQUIRE(clip.w > 0.0f);
        const glm::vec2 ndc(clip.x / clip.w, clip.y / clip.w);
        return glm::vec2((ndc.x + 1.0f) * 0.5f, (1.0f - ndc.y) * 0.5f);
    };

    // Strength 0 leaves the aim alone: the subject stays where the camera was pointed.
    const glm::vec2 untouched = screenOf(0.25f, 0.7f, 0.0f);
    CHECK_THAT(static_cast<double>(untouched.x), WithinAbs(0.5, 0.01));
    CHECK_THAT(static_cast<double>(untouched.y), WithinAbs(0.5, 0.01));

    // Full strength puts it exactly where the scene asked.
    const glm::vec2 framed = screenOf(0.25f, 0.7f, 1.0f);
    CHECK_THAT(static_cast<double>(framed.x), WithinAbs(0.25, 0.01));
    CHECK_THAT(static_cast<double>(framed.y), WithinAbs(0.7, 0.01));

    // Half strength lands between the two, so a shot can be nudged rather than snapped.
    const glm::vec2 half = screenOf(0.25f, 0.7f, 0.5f);
    CHECK(half.x > 0.25f);
    CHECK(half.x < 0.5f);
    CHECK(half.y > 0.5f);
    CHECK(half.y < 0.7f);
}

TEST_CASE("A terrain node flattens into chunk entities that pick their own level", "[composition][terrain]") {
    // The terrain node's whole claim: geometry is built once and the per-frame cost is choosing
    // which of a chunk's meshes is drawn and whether it is drawn at all. So the test is not that a
    // mesh exists -- it is that moving the camera changes the mesh a chunk points at, and that
    // chunks behind the camera stop being drawn, without the mesh list changing at all.
    const std::string text = R"({
      "format": "avgen-scene", "version": 1, "name": "terra",
      "camera": { "mode": 1, "position": [0, 30, 60], "target": [0, 0, -60], "fov": 50.0 },
      "nodes": [
        { "name": "ground", "kind": "terrain",
          "world": { "name": "small", "size": [160, 160] },
          "terrain": { "chunkSize": 40.0, "resolution": 8, "lodLevels": 4,
                       "lodDistance": 50.0, "viewDistance": 400.0 },
          "material": { "baseColor": [0.2, 0.4, 0.3], "roughness": 0.9 } }
      ]
    })";
    Fixture fx;
    const auto path = writeJson("terrain", text);
    fx.files.push_back(path);
    auto comp = scene::Composition::loadFile(path.filename(), fx.registry);
    REQUIRE(comp.has_value());
    params::ParameterSet params;
    params::Modulator modulator;
    (*comp)->attach(params, modulator);
    (*comp)->update(FrameTime{});
    const scene::Scene& s = (*comp)->scene();

    const scene::CompositionNode* node = (*comp)->findNode("ground");
    REQUIRE(node != nullptr);
    CHECK(node->chunks.size() == 16u);            // 160 m of world in 40 m chunks
    // One entity per chunk, plus one per chunk that has water; one mesh per level per chunk, plus
    // one water surface per wet chunk. A dry chunk contributes neither.
    const auto wet = static_cast<std::size_t>(std::count_if(
        node->chunks.begin(), node->chunks.end(),
        [](const world::TerrainChunk& c) { return c.water != scene::kInvalidMesh; }));
    CHECK(wet > 0); // the shipped world has a river through it, or this test proves nothing
    CHECK(s.entities.size() == node->chunks.size() + wet);
    CHECK(s.meshes.size() == node->chunks.size() * 4u + wet);
    const std::uint64_t meshVersion = s.meshVersion;

    // The chunk the camera is looking at from close range takes the finest level; a chunk the same
    // camera sees only in the far distance takes a coarser one.
    auto meshOfChunkNearest = [&](const glm::vec2& p) {
        std::size_t best = 0;
        float bestDistance = 1e30f;
        for (std::size_t i = 0; i < node->chunks.size(); ++i) {
            const float d = glm::distance(node->chunks[i].center, p);
            if (d < bestDistance) {
                bestDistance = d;
                best = i;
            }
        }
        return best;
    };
    const std::size_t nearChunk = meshOfChunkNearest({0.0f, 60.0f});
    const std::size_t farChunk = meshOfChunkNearest({0.0f, -60.0f});
    const scene::MeshId nearMesh = s.entities[nearChunk].mesh;
    const scene::MeshId farMesh = s.entities[farChunk].mesh;
    CHECK(nearMesh == node->chunks[nearChunk].meshes[0]);
    CHECK(farMesh != node->chunks[farChunk].meshes[0]);
    CHECK(s.entities[farChunk].visible);

    // Turn the camera around: the far chunk leaves the frustum and stops being drawn, and nothing
    // about the geometry changed to make that happen.
    params::Parameter<glm::vec3>* target = params.findAs<glm::vec3>("camera/target");
    REQUIRE(target != nullptr);
    target->setBase(glm::vec3(0.0f, 30.0f, 400.0f));
    params.resetFinals(); // what the modulation pass does at the start of every real frame
    (*comp)->update(FrameTime{});
    CHECK_FALSE(s.entities[farChunk].visible);
    CHECK(s.meshVersion == meshVersion);
    CHECK(s.meshes.size() == node->chunks.size() * 4u + wet);

    // Debug switches: with LOD off every visible chunk draws its finest mesh, whatever the distance.
    params::Parameter<bool>* lod = params.findAs<bool>("nodes/ground/terrainLod");
    REQUIRE(lod != nullptr);
    lod->setBase(false);
    target->setBase(glm::vec3(0.0f, 0.0f, -60.0f));
    params.resetFinals();
    (*comp)->update(FrameTime{});
    CHECK(s.entities[farChunk].mesh == node->chunks[farChunk].meshes[0]);
}

TEST_CASE("A scatter layer's material program and the ground's glow reach the flattened scene",
          "[composition][terrain][ecology]") {
    // Both halves of this failed silently during development, which is the reason it is a test.
    // A layer naming a material program produced no warning and no program: the emission output
    // key is `emission`, not `emissionRegister`, and registered programs carry the composition's
    // prefix, so a raw name from the scene file matched nothing. Either mistake leaves the layer
    // rendering with its plain material -- visually "the whole plant glows" rather than "points on
    // the plant glow" -- and nothing in the logs says so.
    const std::filesystem::path asset =
        std::filesystem::path(AVGEN_SOURCE_DIR) / "assets/quaternius/glTF/Mushroom_Common.gltf";
    if (!std::filesystem::exists(asset)) {
        SKIP("the Quaternius library is not present in this checkout");
    }
    const std::string text = R"({
      "format": "avgen-scene", "version": 1, "name": "eco",
      "camera": { "mode": 1, "position": [0, 20, 40], "target": [0, 0, 0], "fov": 50.0 },
      "materialPrograms": [
        { "name": "spots",
          "ops": [ { "kind": "constant", "dst": 4, "constant": [0.2, 0.9, 1.0, 1.0] } ],
          "emission": 4, "emissionIntensity": 1.0 }
      ],
      "nodes": [
        { "name": "ground", "kind": "terrain",
          "world": { "name": "small", "size": [160, 160] },
          "terrain": { "chunkSize": 40.0, "resolution": 8, "lodLevels": 2,
                       "viewDistance": 400.0,
                       "groundGlow": 0.8, "groundGlowScale": 0.05,
                       "groundGlowCoverage": 0.3, "groundGlowColor": [0.1, 1.0, 0.7] },
          "scatter": [
            { "name": "lamps", "asset": "@ASSET@", "densities": { "meadow": 0.02, "forest": 0.02 },
              "height": 0.5, "emissiveIntensity": 6.0, "emissiveColor": [0.1, 0.9, 1.0],
              "materialProgram": "spots" }
          ] }
      ]
    })";
    Fixture fx;
    std::string filled = text;
    filled.replace(filled.find("@ASSET@"), 7, asset.string());
    const auto path = writeJson("ecology_program", filled);
    fx.files.push_back(path);
    auto comp = scene::Composition::loadFile(path.filename(), fx.registry);
    if (!comp) {
        FAIL(comp.error().message);
    }
    params::ParameterSet params;
    params::Modulator modulator;
    // Attached under a prefix on purpose. Registered programs are renamed with the composition's
    // prefix, so a layer that referenced its program by the raw name matched nothing -- and with
    // an empty prefix that bug is invisible, which is how it shipped twice.
    (*comp)->attach(params, modulator, "sub_");
    (*comp)->update(FrameTime{});
    const scene::Scene& s = (*comp)->scene();

    SECTION("the ground program carries an emission output when the terrain asks for glow") {
        const auto ground = std::ranges::find_if(s.materialPrograms, [](const scene::MaterialProgram& p) {
            return p.name.ends_with("_ground");
        });
        REQUIRE(ground != s.materialPrograms.end());
        CHECK(ground->emissionRegister >= 0);
    }

    SECTION("a layer's program name resolves to a program that exists in the scene") {
        const auto lamps = std::ranges::find_if(s.procedurals, [](const scene::ProceduralGeometry& p) {
            return p.name.find("lamps") != std::string::npos;
        });
        REQUIRE(lamps != s.procedurals.end());
        REQUIRE(!lamps->material.program.empty());
        // The resolved name must match a registered program exactly, or the renderer silently
        // shades with no program at all.
        CHECK(std::ranges::any_of(s.materialPrograms, [&](const scene::MaterialProgram& p) {
            return p.name == lamps->material.program;
        }));
    }
}
