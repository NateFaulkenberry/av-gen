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
