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
#include "support/temp_dir.hpp"

#include <filesystem>
#include <fstream>
#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <vector>

using namespace avgen;
using Catch::Matchers::WithinAbs;

namespace {

// The same directory the glTF fixtures write into, so a registry rooted here can find them. Bare
// temp_directory_path() was shared between concurrent test processes writing identical filenames.
std::filesystem::path tempDir() {
    return testsupport::processTempDir();
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

TEST_CASE("Stylized scene mode validates, round-trips and detaches", "[composition][stylized]") {
    Fixture fixture;
    auto document = nlohmann::json::parse(R"({"format":"avgen-scene","version":1,
        "name":"style", "environment":{"stylized":true}, "nodes":[]})");
    params::ParameterSet parameters;
    params::Modulator modulator;
    auto loaded = scene::Composition::fromJson(document, fixture.registry);
    REQUIRE(loaded);
    auto composition = std::move(*loaded);
    composition->attach(parameters, modulator);
    composition->update({});
    CHECK(composition->scene().environment.stylized);
    CHECK(composition->toJson()["environment"]["stylized"] == true);
    auto* control = parameters.find("scene/stylized");
    REQUIRE(control != nullptr);
    control->setBaseComponent(0, 0.0f);
    parameters.resetFinals();
    composition->update({});
    CHECK_FALSE(composition->scene().environment.stylized);
    CHECK(composition->toJson()["environment"]["stylized"] == false);
    composition->detach();
    composition->update({});
    CHECK(composition->scene().environment.stylized);
    document["environment"]["stylized"] = "true";
    CHECK_FALSE(scene::Composition::fromJson(document, fixture.registry));
    document["environment"].erase("stylized");
    auto legacy = scene::Composition::fromJson(document, fixture.registry);
    REQUIRE(legacy);
    (*legacy)->update({});
    CHECK_FALSE((*legacy)->scene().environment.stylized);
}

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
    SECTION("an asset's lights are scaled, tinted and moved by the node that brought them") {
        // Lights were the one thing in a scene that could not be edited at all: `rebuild`
        // repopulates `scene_.lights` wholesale, so a value written onto a light was discarded at
        // the next rebuild, and the AI tool for them said so in its own description. The fix is a
        // scale and a tint on the node, applied every frame like every other final.
        const auto lightIndex = [&]() -> std::size_t {
            for (std::size_t i = 0; i < sc.lights.size(); ++i) {
                if (sc.lights[i].name.find("lamp") != std::string::npos) {
                    return i;
                }
            }
            FAIL("the fixture's light is not in the scene");
            return 0;
        };
        const std::size_t idx = lightIndex();
        const float rest = sc.lights[idx].intensity;
        const glm::vec3 restColour = sc.lights[idx].color;
        REQUIRE(rest > 0.0f);

        params.findAs<float>("nodes/a/lightIntensity")->setBase(0.5f);
        params.resetFinals();
        comp.update(FrameTime{});
        CHECK_THAT(static_cast<double>(sc.lights[idx].intensity), WithinAbs(rest * 0.5, 1e-4));

        // And again, because the bug was that it did not *survive*: a second frame must not
        // compound the scale, and a rebuild must not throw it away.
        comp.update(FrameTime{});
        CHECK_THAT(static_cast<double>(sc.lights[idx].intensity), WithinAbs(rest * 0.5, 1e-4));
        REQUIRE(comp.addNode(makeNode(scene::NodeKind::Orb, "forcerebuild")).has_value());
        params.resetFinals();
        comp.update(FrameTime{});
        const std::size_t after = lightIndex();
        CHECK_THAT(static_cast<double>(sc.lights[after].intensity), WithinAbs(rest * 0.5, 1e-4));

        // The tint multiplies the asset's own colour rather than replacing it.
        params.findAs<glm::vec3>("nodes/a/lightColor")->setBase(glm::vec3(1.0f, 0.0f, 0.0f));
        params.resetFinals();
        comp.update(FrameTime{});
        CHECK_THAT(static_cast<double>(sc.lights[after].color.r), WithinAbs(restColour.r, 1e-4));
        CHECK_THAT(static_cast<double>(sc.lights[after].color.g), WithinAbs(0.0, 1e-4));

        // Hiding the node puts its lamp out, which is what hiding a lamp means.
        params.findAs<bool>("nodes/a/visible")->setBase(false);
        params.resetFinals();
        comp.update(FrameTime{});
        CHECK_THAT(static_cast<double>(sc.lights[after].intensity), WithinAbs(0.0, 1e-6));
    }
    SECTION("hiding a group hides what is inside it") {
        // The layer-list model: an eye on a group is an eye on its contents. Before this, hiding a
        // group left every child standing, because visibility was read off one node at a time --
        // which looks like the feature simply not working, since a Group has no geometry of its own
        // and so nothing visibly changes at all.
        auto group = makeNode(scene::NodeKind::Group, "holder");
        REQUIRE(comp.addNode(std::move(group)).has_value());
        REQUIRE(comp.setParent("b", "holder").has_value());
        params.resetFinals();
        comp.update(FrameTime{});
        REQUIRE(sc.entities[1].visible); // the control: parented, still shown

        params.findAs<bool>("nodes/holder/visible")->setBase(false);
        params.resetFinals();
        comp.update(FrameTime{});
        CHECK_FALSE(sc.entities[1].visible);
        CHECK(sc.entities[0].visible); // and only what is under it

        // And back, so this is a switch rather than a one-way door.
        params.findAs<bool>("nodes/holder/visible")->setBase(true);
        params.resetFinals();
        comp.update(FrameTime{});
        CHECK(sc.entities[1].visible);
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
    // about the geometry changed to make that happen. Leaving the frustum is `cameraCulled`, not
    // `visible` -- see the shadow-caster test below.
    params::Parameter<glm::vec3>* target = params.findAs<glm::vec3>("camera/target");
    REQUIRE(target != nullptr);
    target->setBase(glm::vec3(0.0f, 30.0f, 400.0f));
    params.resetFinals(); // what the modulation pass does at the start of every real frame
    (*comp)->update(FrameTime{});
    CHECK(s.entities[farChunk].cameraCulled);
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

TEST_CASE("ordinary authored mesh nodes participate in camera culling", "[composition][culling]") {
    Fixture fx;
    scene::Composition composition(fx.registry, "culling");
    params::ParameterSet parameters;
    params::Modulator modulator;
    auto node = makeNode(scene::NodeKind::Orb, "orb");
    node.transform.position = {0.0f, 0.0f, -8.0f};
    REQUIRE(composition.addNode(std::move(node)).has_value());
    composition.attach(parameters, modulator);
    composition.setViewport(320, 180);
    composition.update(FrameTime{});
    REQUIRE(composition.scene().entities.size() == 1);
    CHECK_FALSE(composition.scene().entities[0].cameraCulled);

    auto* nodePosition = parameters.findAs<glm::vec3>("nodes/orb/position");
    REQUIRE(nodePosition != nullptr);
    nodePosition->setBase(glm::vec3(100.0f, 0.0f, -8.0f));
    parameters.resetFinals();
    composition.update(FrameTime{});
    CHECK(composition.scene().entities[0].cameraCulled);
}

TEST_CASE("A mushroom's cap and its stem keep their own colours", "[composition][mesh][material]") {
    // The symptom docs/world.md recorded. This asset's two materials differ in their *factors*
    // rather than their maps -- a white stem and a red cap -- which is the half of the split that
    // the per-frame parameter pass can undo: material parameters are registered from part 0, so
    // applying them writes part 0's colour into every part unless each part keeps its own.
    const std::filesystem::path asset =
        std::filesystem::path(AVGEN_SOURCE_DIR) / "assets/kenney/mushroom_red.glb";
    if (!std::filesystem::exists(asset)) {
        SKIP("the Kenney library is not present in this checkout");
    }
    const std::string text = R"({
      "format": "avgen-scene", "version": 1, "name": "fungi",
      "camera": { "mode": 1, "position": [0, 2, 5], "target": [0, 0, 0], "fov": 50.0 },
      "nodes": [
        { "name": "caps", "kind": "procedural",
          "procedural": {
            "source": { "kind": "mesh", "asset": "@ASSET@" },
            "distribution": { "kind": "radial", "count": 4, "radius": 2.0, "plane": "xz" } } }
      ]
    })";
    Fixture fx;
    std::string filled = text;
    filled.replace(filled.find("@ASSET@"), 7, asset.string());
    const auto path = writeJson("mushroom_colour", filled);
    fx.files.push_back(path);
    auto comp = scene::Composition::loadFile(path.filename(), fx.registry);
    if (!comp) {
        FAIL(comp.error().message);
    }
    params::ParameterSet params;
    params::Modulator modulator;
    (*comp)->attach(params, modulator);
    (*comp)->update(FrameTime{});
    const scene::Scene& s = (*comp)->scene();

    std::vector<const scene::ProceduralGeometry*> parts;
    for (const scene::ProceduralGeometry& g : s.procedurals) {
        if (g.name.rfind("caps", 0) == 0) {
            parts.push_back(&g);
        }
    }
    REQUIRE(parts.size() == 2);
    CHECK(parts[0]->material.baseColor != parts[1]->material.baseColor);

    // Every frame after the first runs the parameter pass over both parts. The cap must not turn
    // the colour of the stem on frame two.
    const glm::vec3 first = parts[0]->material.baseColor;
    const glm::vec3 second = parts[1]->material.baseColor;
    for (int frame = 0; frame < 3; ++frame) {
        params.resetFinals();
        (*comp)->update(FrameTime{});
    }
    CHECK(parts[0]->material.baseColor == first);
    CHECK(parts[1]->material.baseColor == second);

    // An author who moves the colour moves both, because they wrote one material for one asset.
    params::Parameter<glm::vec3>* colour = params.findAs<glm::vec3>("procedural/caps/material/baseColor");
    REQUIRE(colour != nullptr);
    colour->setBase(glm::vec3(0.0f, 1.0f, 0.0f));
    params.resetFinals();
    (*comp)->update(FrameTime{});
    CHECK(parts[0]->material.baseColor == glm::vec3(0.0f, 1.0f, 0.0f));
    CHECK(parts[1]->material.baseColor == glm::vec3(0.0f, 1.0f, 0.0f));
}

TEST_CASE("A chunk the camera cannot see still casts into the shadow maps", "[composition][terrain][shadows]") {
    // The camera's frustum is the wrong question for a shadow map, and the shadow pass already
    // applies the right one -- each cascade's own frustum. So the chunk must survive as a
    // candidate: visible, camera-culled, still casting, and still pointing at a real mesh, because
    // a caster with no geometry casts nothing. View-distance culling is separate runtime state and
    // must not permanently overwrite authored visibility when the camera comes back.
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
    const auto path = writeJson("terrain_shadow", text);
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
    const std::size_t chunkCount = node->chunks.size();

    // Turn the camera around. The chunks it was looking at are now behind it; none of them is
    // beyond the 400 m view distance, so none of them has left the world.
    params::Parameter<glm::vec3>* target = params.findAs<glm::vec3>("camera/target");
    REQUIRE(target != nullptr);
    target->setBase(glm::vec3(0.0f, 30.0f, 400.0f));
    params.resetFinals();
    (*comp)->update(FrameTime{});

    std::size_t culled = 0;
    for (std::size_t c = 0; c < chunkCount; ++c) {
        const scene::Entity& e = s.entities[c];
        if (!e.cameraCulled) {
            continue;
        }
        ++culled;
        CHECK(e.visible);       // off screen is not absent
        CHECK(e.castsShadow);   // and it is inside the shadow distance
        CHECK(e.mesh != scene::kInvalidMesh);
    }
    CHECK(culled > 0); // the camera must actually be looking away, or this test proves nothing

    // Distance is a different claim, and still removes a chunk from this frame: nothing a cascade
    // covers reaches out there. Pull the view distance in under the near chunks.
    params::Parameter<float>* viewDistance = params.findAs<float>("nodes/ground/terrainViewDistance");
    REQUIRE(viewDistance != nullptr);
    viewDistance->setBase(5.0f);
    target->setBase(glm::vec3(0.0f, 0.0f, -60.0f));
    params.resetFinals();
    (*comp)->update(FrameTime{});
    std::size_t dropped = 0;
    std::size_t waterIndex = node->chunks.size();
    for (std::size_t c = 0; c < chunkCount; ++c) {
        if (s.entities[c].cameraCulled) {
            ++dropped;
            CHECK(s.entities[c].visible);       // authored visibility survives runtime culling
            CHECK_FALSE(s.entities[c].castsShadow);
        }
        if (node->chunks[c].water != scene::kInvalidMesh) {
            CHECK(s.entities[waterIndex].visible);
            CHECK(s.entities[waterIndex].cameraCulled);
            ++waterIndex;
        }
    }
    CHECK(dropped > 0);

    // Return the view distance and camera to the authored state. Chunks must become drawable again
    // without rebuilding the composition or restoring visibility from a second source of truth.
    viewDistance->setBase(400.0f);
    target->setBase(glm::vec3(0.0f, 0.0f, -60.0f));
    params.resetFinals();
    (*comp)->update(FrameTime{});
    std::size_t drawable = 0;
    std::size_t drawableWater = 0;
    waterIndex = node->chunks.size();
    for (std::size_t c = 0; c < chunkCount; ++c) {
        CHECK(s.entities[c].visible);
        if (!s.entities[c].cameraCulled) {
            ++drawable;
        }
        if (node->chunks[c].water != scene::kInvalidMesh) {
            CHECK(s.entities[waterIndex].visible);
            if (!s.entities[waterIndex].cameraCulled) {
                ++drawableWater;
            }
            ++waterIndex;
        }
    }
    CHECK(drawable > 0);
    CHECK(drawableWater > 0);
}

TEST_CASE("Terrain LOD follows the viewport as well as the lens", "[composition][terrain][lod]") {
    // ADR-046: LOD is chosen from how large a chunk's quads are on screen, which is a function of
    // the lens *and* the viewport. The composition used to substitute a fixed 900-pixel reference
    // height for the real one, so the same ground came back at the same level whatever it was
    // being rendered into -- and a bigger window got exactly the same mesh it got in a small one.
    const std::string text = R"({
      "format": "avgen-scene", "version": 1, "name": "terra",
      "camera": { "mode": 1, "position": [0, 30, 60], "target": [0, 0, -60], "fov": 50.0 },
      "nodes": [
        { "name": "ground", "kind": "terrain",
          "world": { "name": "small", "size": [320, 320] },
          "terrain": { "chunkSize": 40.0, "resolution": 8, "lodLevels": 4,
                       "lodDistance": 50.0, "viewDistance": 900.0 },
          "material": { "baseColor": [0.2, 0.4, 0.3], "roughness": 0.9 } }
      ]
    })";
    Fixture fx;
    const auto path = writeJson("terrain_viewport", text);
    fx.files.push_back(path);
    auto comp = scene::Composition::loadFile(path.filename(), fx.registry);
    REQUIRE(comp.has_value());
    params::ParameterSet params;
    params::Modulator modulator;
    (*comp)->attach(params, modulator);
    const scene::Scene& s = (*comp)->scene();

    const auto levels = [&](std::uint32_t width, std::uint32_t height) {
        (*comp)->setViewport(width, height);
        params.resetFinals();
        (*comp)->update(FrameTime{});
        const scene::CompositionNode* node = (*comp)->findNode("ground");
        std::vector<int> out;
        for (std::size_t c = 0; c < node->chunks.size(); ++c) {
            int level = -1;
            for (int k = 0; k < world::kMaxTerrainLods; ++k) {
                if (node->chunks[c].meshes[static_cast<std::size_t>(k)] == s.entities[c].mesh) {
                    level = k;
                    break;
                }
            }
            out.push_back(level);
        }
        return out;
    };

    // All three are 16:9, which is below the conservative floor the chunk cull frustum uses, so the
    // frustum is byte-identical across them and the only thing that changes is the pixel scale. A
    // first version of this test varied the height alone, which also widened the cull frustum and
    // let a chunk that had simply been culled at one size report a different level at another.
    const std::vector<int> reference = levels(1600, 900);
    const std::vector<int> tall = levels(3200, 1800);
    const std::vector<int> squat = levels(800, 450);
    REQUIRE(reference.size() == tall.size());
    REQUIRE(reference.size() == squat.size());

    // A viewport twice as tall makes the same ground twice as large in pixels, so the switch to a
    // coarser level moves further out: no chunk may get coarser, and at least one must get finer.
    bool finer = false;
    bool coarser = false;
    for (std::size_t c = 0; c < reference.size(); ++c) {
        if (tall[c] < reference[c]) finer = true;
        if (tall[c] > reference[c]) coarser = true;
    }
    CHECK(finer);
    CHECK_FALSE(coarser);

    // And a viewport half as tall goes the other way.
    bool squatCoarser = false;
    bool squatFiner = false;
    for (std::size_t c = 0; c < reference.size(); ++c) {
        if (squat[c] > reference[c]) squatCoarser = true;
        if (squat[c] < reference[c]) squatFiner = true;
    }
    CHECK(squatCoarser);
    CHECK_FALSE(squatFiner);
}

TEST_CASE("A multi-material asset is drawn with all of its materials", "[composition][mesh][material]") {
    // ADR-044 shipped with a limitation: an asset's entities were merged into one mesh and one
    // material was picked for all of it, so a tree drew its leaves with the bark's texture (or,
    // after the heuristic changed, its bark with the leaves'). The asset is one *source* per
    // material now, and each part is an ordinary procedural object: same cloud, same seed, same
    // culling, its own material and its own draw.
    const std::filesystem::path asset =
        std::filesystem::path(AVGEN_SOURCE_DIR) / "assets/quaternius/glTF/CommonTree_1.gltf";
    if (!std::filesystem::exists(asset)) {
        SKIP("the Quaternius library is not present in this checkout");
    }
    const std::string text = R"({
      "format": "avgen-scene", "version": 1, "name": "tree",
      "camera": { "mode": 1, "position": [0, 6, 14], "target": [0, 3, 0], "fov": 50.0 },
      "nodes": [
        { "name": "grove", "kind": "procedural",
          "procedural": {
            "source": { "kind": "mesh", "asset": "@ASSET@" },
            "distribution": { "kind": "radial", "count": 5, "radius": 8.0, "plane": "xz" } } }
      ]
    })";
    Fixture fx;
    std::string filled = text;
    filled.replace(filled.find("@ASSET@"), 7, asset.string());
    const auto path = writeJson("multi_material", filled);
    fx.files.push_back(path);
    auto comp = scene::Composition::loadFile(path.filename(), fx.registry);
    if (!comp) {
        FAIL(comp.error().message);
    }
    params::ParameterSet params;
    params::Modulator modulator;
    (*comp)->attach(params, modulator);
    (*comp)->update(FrameTime{});
    const scene::Scene& s = (*comp)->scene();

    // The asset carries bark and leaves. Two materials in, two drawables out.
    std::vector<const scene::ProceduralGeometry*> parts;
    for (const scene::ProceduralGeometry& g : s.procedurals) {
        if (g.name.rfind("grove", 0) == 0) {
            parts.push_back(&g);
        }
    }
    REQUIRE(parts.size() == 2);

    // Different materials, and different geometry: neither part may be a copy of the other, and
    // between them they must account for the whole asset. The two materials of this asset differ
    // in their maps rather than in their factors -- bark and leaves are two photographs -- which is
    // exactly the case the old single-material merge got wrong: one of them was drawn with the
    // other's texture.
    REQUIRE(parts[0]->material.baseColorTexture.valid());
    REQUIRE(parts[1]->material.baseColorTexture.valid());
    CHECK(parts[0]->material.baseColorTexture.texture != parts[1]->material.baseColorTexture.texture);
    REQUIRE(parts[0]->source.assetMesh);
    REQUIRE(parts[1]->source.assetMesh);
    CHECK(parts[0]->source.assetMesh->indices.size() > 0);
    CHECK(parts[1]->source.assetMesh->indices.size() > 0);
    CHECK(parts[0]->source.assetMesh != parts[1]->source.assetMesh);

    // The renderer caches source meshes by the source hash, so two parts of one asset that hashed
    // the same would both be drawn with whichever mesh got there first.
    CHECK(parts[0]->source.structuralHash() != parts[1]->source.structuralHash());

    // Every part is placed identically: they are the same tree seen through two materials, so a
    // mismatch here is a canopy floating beside its trunk.
    REQUIRE(parts[0]->instances.size() == parts[1]->instances.size());
    CHECK(parts[0]->instances.size() == 5u);
    for (std::size_t i = 0; i < parts[0]->instances.size(); ++i) {
        CHECK(parts[0]->instances[i].position == parts[1]->instances[i].position);
        CHECK(parts[0]->instances[i].scale == parts[1]->instances[i].scale);
    }

    // Together they are the whole asset, and neither is the whole asset on its own.
    const std::size_t whole = parts[0]->source.assetMesh->indices.size() +
                              parts[1]->source.assetMesh->indices.size();
    CHECK(parts[0]->source.assetMesh->indices.size() < whole);
    CHECK(parts[1]->source.assetMesh->indices.size() < whole);

    // A live parameter change moves every part, or the tree comes apart the first time an author
    // touches a slider.
    params::Parameter<float>* radius = params.findAs<float>("procedural/grove/distribution/radius");
    REQUIRE(radius != nullptr);
    radius->setBase(20.0f);
    params.resetFinals();
    (*comp)->update(FrameTime{});
    REQUIRE(parts[0]->instances.size() == parts[1]->instances.size());
    for (std::size_t i = 0; i < parts[0]->instances.size(); ++i) {
        CHECK(parts[0]->instances[i].position == parts[1]->instances[i].position);
    }
    CHECK(glm::length(parts[0]->instances[0].position) > 15.0f); // the change actually took
    // ... and each part still has its own maps after the parameters have run: the per-frame
    // parameter pass writes part 0's material into every object it touches, so a part that did not
    // keep its own identity would be repainted with the bark's texture one frame in.
    CHECK(parts[0]->material.baseColorTexture.texture != parts[1]->material.baseColorTexture.texture);

    const auto firstColor = parts[0]->material.baseColor;
    const auto secondColor = parts[1]->material.baseColor;
    auto* tint = params.findAs<glm::vec3>("procedural/grove/parts/1/tint");
    auto* firstEmission = params.findAs<float>("procedural/grove/parts/0/emissiveGain");
    auto* secondEmission = params.findAs<float>("procedural/grove/parts/1/emissiveGain");
    auto* globalEmission = params.findAs<float>("procedural/grove/material/emissive");
    REQUIRE(tint != nullptr);
    REQUIRE(firstEmission != nullptr);
    REQUIRE(secondEmission != nullptr);
    REQUIRE(globalEmission != nullptr);
    tint->setBase(glm::vec3(0.25f, 0.7f, 0.9f));
    firstEmission->setBase(0.0f);
    secondEmission->setBase(3.0f);
    globalEmission->setBase(2.0f);
    for (int frame = 0; frame < 3; ++frame) {
        params.resetFinals();
        (*comp)->update(FrameTime{});
        CHECK(parts[0]->material.baseColor == firstColor);
        CHECK(parts[1]->material.baseColor == secondColor * glm::vec3(0.25f, 0.7f, 0.9f));
        CHECK(parts[0]->material.emissiveIntensity == 0.0f);
        CHECK(parts[1]->material.emissiveIntensity == 6.0f);
        CHECK(parts[0]->material.baseColorTexture.texture != parts[1]->material.baseColorTexture.texture);
        REQUIRE(parts[0]->instances.size() == parts[1]->instances.size());
    }
    (*comp)->removeNode("grove");
    CHECK(params.find("procedural/grove/parts/0/emissiveGain") == nullptr);
    CHECK(params.find("procedural/grove/parts/1/tint") == nullptr);
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
                            "materialProgram": "spots" },
                        { "name": "companions", "asset": "@ASSET@", "densities": { "meadow": 0.03, "forest": 0.03 },
                            "height": 0.2, "proximity": { "layer": "lamps", "minDistance": 1.0,
                                                                                     "maxDistance": 5.0, "fade": 1.0 } }
          ] }
      ]
    })";
    Fixture fx;
    std::string filled = text;
    filled.replace(filled.find("@ASSET@"), 7, asset.string());
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

    SECTION("dependent layers use actual preceding placements through composition flattening") {
        const auto lamps = std::ranges::find_if(s.procedurals, [](const scene::ProceduralGeometry& object) {
            return object.name.ends_with("_lamps");
        });
        const auto companions = std::ranges::find_if(s.procedurals, [](const scene::ProceduralGeometry& object) {
            return object.name.ends_with("_companions");
        });
        REQUIRE(lamps != s.procedurals.end());
        REQUIRE(companions != s.procedurals.end());
        const auto& anchors = *lamps->distribution.scatterCloud;
        const auto& followers = *companions->distribution.scatterCloud;
        REQUIRE(followers.count() > 10);
        for (const glm::vec3 position : followers.positions()) {
            float nearest = std::numeric_limits<float>::max();
            for (const glm::vec3 anchor : anchors.positions()) {
                nearest = std::min(nearest, glm::length(glm::vec2(position.x - anchor.x, position.z - anchor.z)));
            }
            CHECK(nearest >= 1.0f);
            CHECK(nearest < 5.0f);
        }
    }

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

// ---- heroes (ADR-074) ---------------------------------------------------------------------------

namespace {

// A scene whose only interesting content is its heroes block. Nodes are left out deliberately: a
// hero describes something already placed, and nothing here should need geometry to exist.
std::string heroScene(const std::string& heroes) {
    return R"({"format": "avgen-scene", "version": 1, "name": "heroic", "heroes": )" + heroes + "}";
}

} // namespace

TEST_CASE("An authored scene declares heroes and they survive a file round trip",
          "[scene][composition][json][hero]") {
    Fixture fx;
    const std::string text = heroScene(R"([
      {"name": "elder", "position": [-1, -9, -46], "yaw": 0.5, "scale": 1.0,
       "radius": 8.2, "height": 16.5, "importance": 0.95, "focalWeight": 0.9,
       "preferredCameraDistance": 50.0, "preferredCameraElevation": 6.0, "activationRadius": 160.0,
       "colorAccent": [1.0, 0.47, 0.15], "reactionProfile": "organism"},
      {"name": "cairn", "asset": "rock_big", "position": [12, 0, 4], "importance": 0.4}
    ])");
    auto loaded = scene::Composition::fromJson(nlohmann::json::parse(text), fx.registry);
    REQUIRE(loaded.has_value());
    scene::Composition& comp = **loaded;
    REQUIRE(comp.heroes().size() == 2);
    const world::HeroPoint& elder = comp.heroes()[0];
    CHECK(elder.name == "elder");
    // No asset id: what stands here is the scene's own node of that name, not a library entry
    // (ADR-107).
    CHECK(elder.assetId.empty());
    checkVec(elder.position, glm::vec3(-1.0f, -9.0f, -46.0f));
    CHECK_THAT(static_cast<double>(elder.yaw), WithinAbs(0.5, 1e-6));
    CHECK_THAT(static_cast<double>(elder.radius), WithinAbs(8.2, 1e-5));
    CHECK_THAT(static_cast<double>(elder.height), WithinAbs(16.5, 1e-5));
    CHECK_THAT(static_cast<double>(elder.importance), WithinAbs(0.95, 1e-6));
    CHECK_THAT(static_cast<double>(elder.focalWeight), WithinAbs(0.9, 1e-6));
    CHECK_THAT(static_cast<double>(elder.preferredCameraDistance), WithinAbs(50.0, 1e-5));
    CHECK_THAT(static_cast<double>(elder.preferredCameraElevationDegrees), WithinAbs(6.0, 1e-5));
    CHECK_THAT(static_cast<double>(elder.activationRadius), WithinAbs(160.0, 1e-5));
    checkVec(elder.colorAccent, glm::vec3(1.0f, 0.47f, 0.15f));
    CHECK(elder.reactionProfile == "organism");
    CHECK(comp.heroes()[1].assetId == "rock_big");

    const nlohmann::json j = comp.toJson();
    REQUIRE(j.contains("heroes"));
    REQUIRE(j["heroes"].size() == 2);
    CHECK(j["heroes"][0]["name"] == "elder");
    CHECK_FALSE(j["heroes"][0].contains("assembly"));   // the concept is gone, ADR-107
    CHECK(j["heroes"][0]["reactionProfile"] == "organism");
    // A second pass through the format reproduces the first exactly.
    auto again = scene::Composition::fromJson(j, fx.registry);
    REQUIRE(again.has_value());
    CHECK((*again)->toJson() == j);

    // The part that matters, and the reason this is not just a toJson check: an offline render
    // saves the project and reloads it from disk before drawing a single frame. ADR-067 lost a
    // corridor that way and ADR-070 lost a light rig; a hero that lived only in memory would be a
    // camera director that frames nothing when rendered and works fine in the editor.
    const auto path = tempDir() / "avgen_comp_heroes.json";
    fx.files.push_back(path);
    REQUIRE(comp.saveFile(path).has_value());
    auto reloaded = scene::Composition::loadFile(path.filename(), fx.registry);
    REQUIRE(reloaded.has_value());
    REQUIRE((*reloaded)->heroes().size() == 2);
    const world::HeroPoint& afterFile = (*reloaded)->heroes()[0];
    CHECK(afterFile.name == "elder");
    CHECK(afterFile.name == "elder");
    CHECK(afterFile.reactionProfile == "organism");
    checkVec(afterFile.position, glm::vec3(-1.0f, -9.0f, -46.0f));
    checkVec(afterFile.colorAccent, glm::vec3(1.0f, 0.47f, 0.15f));
    CHECK_THAT(static_cast<double>(afterFile.preferredCameraDistance), WithinAbs(50.0, 1e-5));
    CHECK_THAT(static_cast<double>(afterFile.activationRadius), WithinAbs(160.0, 1e-5));
    CHECK_THAT(static_cast<double>(afterFile.height), WithinAbs(16.5, 1e-5));
}

TEST_CASE("A scene that declares no heroes is unchanged by the heroes block",
          "[scene][composition][json][hero]") {
    Fixture fx;
    const std::string text = R"({"format": "avgen-scene", "version": 1, "name": "plain",
      "nodes": [{"name": "orb", "kind": "orb"}]})";
    auto loaded = scene::Composition::fromJson(nlohmann::json::parse(text), fx.registry);
    REQUIRE(loaded.has_value());
    CHECK((*loaded)->heroes().empty());
    // Not "an empty array": a scene that never mentioned heroes must write back the file it had.
    CHECK_FALSE((*loaded)->toJson().contains("heroes"));
}

TEST_CASE("An invalid hero names itself and refuses the scene rather than vanishing",
          "[scene][composition][json][hero]") {
    Fixture fx;
    auto load = [&](const std::string& heroes) {
        return scene::Composition::fromJson(nlohmann::json::parse(heroScene(heroes)), fx.registry);
    };

    // Inside the stand-off: the hero would never be active on the shot designed for it.
    {
        auto r = load(R"([{"name": "elder", "preferredCameraDistance": 50.0,
                           "activationRadius": 20.0}])");
        REQUIRE_FALSE(r.has_value());
        INFO(r.error().message);
        CHECK(r.error().message.find("elder") != std::string::npos);
        CHECK(r.error().message.find("activationRadius") != std::string::npos);
    }
    // A profile nobody wrote: the hero would load and then react to nothing.
    {
        auto r = load(R"([{"name": "elder", "reactionProfile": "mycelial"}])");
        REQUIRE_FALSE(r.has_value());
        INFO(r.error().message);
        CHECK(r.error().message.find("elder") != std::string::npos);
        CHECK(r.error().message.find("mycelial") != std::string::npos);
    }
    // A name and nothing else is a complete hero: it stands on the node of that name (ADR-107).
    {
        auto r = load(R"([{"name": "elder"}])");
        REQUIRE(r.has_value());
    }
    // Two heroes by the same name: every downstream reference to "elder" would be ambiguous.
    {
        auto r = load(R"([{"name": "elder"}, {"name": "elder"}])");
        REQUIRE_FALSE(r.has_value());
        INFO(r.error().message);
        CHECK(r.error().message.find("elder") != std::string::npos);
    }
    // Shape errors.
    CHECK_FALSE(load(R"({"name": "elder"})").has_value());
    CHECK_FALSE(load(R"([42])").has_value());
    CHECK_FALSE(load(R"([{"radius": 4.0}])").has_value());   // a hero with no name names nothing

    // And the same refusals reach setHeroes directly, so an in-memory scene cannot hold a hero a
    // file would be refused for.
    scene::Composition comp(fx.registry, "direct");
    world::HeroPoint bad;
    bad.name = "elder";
    bad.preferredCameraDistance = 50.0f;
    bad.activationRadius = 20.0f;
    auto rejected = comp.setHeroes({bad});
    REQUIRE_FALSE(rejected.has_value());
    CHECK(rejected.error().message.find("elder") != std::string::npos);
    CHECK(comp.heroes().empty());
}

TEST_CASE("examples/world/glowmere-stylized.scene.json declares the elder as its hero",
          "[scene][composition][json][hero]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    const std::filesystem::path path =
        std::filesystem::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-stylized.scene.json";
    REQUIRE(std::filesystem::exists(path));
    std::ifstream in(path);
    REQUIRE(in.good());
    nlohmann::json j;
    in >> j;
    REQUIRE(j.contains("heroes"));
    REQUIRE(j["heroes"].is_array());
    // The elder specifically, found by name rather than by position. Pinning a count of one was
    // pinning an authoring decision: Glowmere gained three more heroes so a camera director would
    // have somewhere to travel, and adding a hero to a scene should not fail a test about whether
    // the elder is declared correctly.
    REQUIRE(!j["heroes"].empty());
    const auto entry = std::find_if(j["heroes"].begin(), j["heroes"].end(),
                                    [](const nlohmann::json& h) {
                                        return h.value("name", std::string()) == "elder-crown";
                                    });
    REQUIRE(entry != j["heroes"].end());
    auto hero = world::HeroPoint::fromJson(*entry);
    REQUIRE(hero.has_value());
    CHECK(hero->name == "elder-crown");
    // Every hero in the file has to load, or one of them is silently broken.
    for (const nlohmann::json& h : j["heroes"]) {
        INFO("hero '" << h.value("name", std::string("?")) << "'");
        CHECK(world::HeroPoint::fromJson(h).has_value());
    }
    // The warm accent is the elder's alone. A second hero wearing it is the cheapest possible way
    // to lose the one warm light in a cool world.
    const auto warm = std::count_if(j["heroes"].begin(), j["heroes"].end(),
                                    [](const nlohmann::json& h) {
                                        const auto c = h.value("colorAccent", std::vector<float>{});
                                        return c.size() == 3 && c[0] > 0.9f && c[1] < 0.6f && c[2] < 0.3f;
                                    });
    CHECK(warm == 1);
    // One object, and it is really in the scene (ADR-107). The elder used to be declared as an
    // "assembly" -- a hero named `elder` standing for the three `elder-*` nodes by prefix -- which
    // made it a hero no row in the editor was and, because reactions are wired to
    // `nodes/<hero name>/...`, one its `organism` profile could never have reached.
    CHECK(hero->assetId.empty());
    const bool onANode = std::any_of(j.at("nodes").begin(), j.at("nodes").end(),
                                     [&](const nlohmann::json& n) {
                                         return n.at("name").get<std::string>() == hero->name;
                                     });
    CHECK(onANode);
    // Every hero in this file names an object, which is what one-to-one means.
    for (const nlohmann::json& h : j["heroes"]) {
        const std::string name = h.value("name", std::string());
        INFO("hero '" << name << "'");
        CHECK_FALSE(h.contains("assembly"));
        CHECK(std::any_of(j.at("nodes").begin(), j.at("nodes").end(), [&](const nlohmann::json& n) {
            return n.at("name").get<std::string>() == name;
        }));
    }
    // Its root is the stem's base and its accent is the filaments' emissive colour, so the hero
    // describes the geometry rather than sitting next to it.
    checkVec(hero->position, glm::vec3(-1.0f, -9.0f, -46.0f));
    checkVec(hero->colorAccent, glm::vec3(1.0f, 0.47f, 0.15f));
    CHECK(hero->importance > 0.9f);
    CHECK(hero->reactionProfile == "organism");
    // Roughly three times its height, the stand-off the composer frames a hero from, and an
    // activation radius that covers the whole authored camera move (its furthest pose is about 78 m
    // from the elder) so the elder is never inert while it is on screen.
    CHECK_THAT(static_cast<double>(hero->preferredCameraDistance / hero->height), WithinAbs(3.0, 0.1));
    CHECK(hero->activationRadius > 100.0f);
#endif
}

// ---- rebuild invalidation (docs/application-performance.md) -------------------------------------
//
// `dirty_` is not a request to re-read a file. It is a request to flatten the whole world again --
// every terrain chunk, every scatter layer, every procedural cloud -- and on
// examples/world/terrain.scene.json that is about 390 ms of frozen main thread. So anything that
// sets it has to have actually changed something.
//
// The load path set it for nothing. `attach()` flattens once (it fits the camera to the resulting
// bounds), and `Engine::loadComposition` then called `setEnvironmentMap()` with the path the scene
// had just been loaded with, which dirtied it again -- so every scene carrying an environment map
// built its world twice on open, and the second build landed inside the editor's first frame.
//
// The observable here is `scene().meshVersion`, which `rebuild()` increments. Deliberately not a
// getter for the flag: what matters is whether the world is flattened again, and the version is
// what the renderer watches to decide whether to re-upload every buffer.
TEST_CASE("Setting an environment map never re-flattens the world",
          "[scene][composition][performance]") {
    // ADR-346 strengthened this. It used to read "setting the *same* map again does not re-flatten",
    // because a new map did: `setEnvironmentMap` marked the composition dirty and `dirty_` has no
    // granularity, so changing one texture id re-flattened everything -- 273-293 ms for 557 entities
    // and 1,069 meshes on the Tree of Life ocean world, and a day/night cycle paid it twice per
    // revolution. Resolving the map is its own step now, so the answer is never.
    //
    // The observable is `scene().meshVersion`, which `rebuild()` increments and which the renderer
    // watches to decide whether to re-upload every buffer. Deliberately not a getter for the flag:
    // what matters is whether the world was flattened again.
    assets::AssetRegistry registry;
    registry.setBaseDirectory(tempDir());
    params::ParameterSet params;
    params::Modulator modulator;
    scene::Composition comp(registry, "composition");
    comp.attach(params, modulator);

    const FrameTime time{};
    comp.update(time);
    const std::uint64_t settled = comp.scene().meshVersion;

    // A new map, the same map twice, the same map by the absolute name the engine resolves to,
    // a different map, and clearing it. None of them is a reason to rebuild the world.
    comp.setEnvironmentMap("env/studio.hdr");
    comp.update(time);
    CHECK(comp.scene().meshVersion == settled);

    comp.setEnvironmentMap("env/studio.hdr");
    comp.update(time);
    CHECK(comp.scene().meshVersion == settled);

    comp.setEnvironmentMap(registry.resolve("env/studio.hdr"));
    comp.update(time);
    CHECK(comp.scene().meshVersion == settled);
    CHECK(!comp.environmentMap().empty());

    const std::string studio = comp.environmentMap().generic_string();

    comp.setEnvironmentMap("env/sunset.hdr");
    comp.update(time);
    CHECK(comp.scene().meshVersion == settled);

    // THE CONTROL. Every assertion above is satisfied by a `setEnvironmentMap` that does nothing
    // at all, which is precisely the failure mode of making a call cheaper. The composition must
    // actually be holding the map it was handed, and the two names must differ.
    const std::string sunset = comp.environmentMap().generic_string();
    INFO("studio '" << studio << "' vs sunset '" << sunset << "'");
    CHECK(sunset != studio);
    CHECK(sunset.find("sunset") != std::string::npos);

    comp.setEnvironmentMap({});
    comp.update(time);
    CHECK(comp.scene().meshVersion == settled);
    CHECK(comp.environmentMap().empty());
}

// ---- interactive regeneration (docs/application-performance.md) ---------------------------------
//
// Procedural regeneration is real work and must happen; the question is when. Done inside the frame
// on every frame of a drag, the editor's frame rate becomes the regeneration rate -- measured at
// 145-216 ms per frame on Glowmere with `hierarchy/depth` under the pointer.
//
// Two properties matter and both are pinned here. Offline must be untouched, because the deferral
// reads a wall clock; and the live editor must still converge, because geometry that is quietly and
// permanently wrong is worse than geometry that is briefly late.
TEST_CASE("Procedural regeneration is deferred only when a budget is set", "[scene][composition][performance]") {
    assets::AssetRegistry registry;
    registry.setBaseDirectory(tempDir());
    params::ParameterSet params;
    params::Modulator modulator;
    scene::Composition comp(registry, "composition");
    comp.attach(params, modulator);

    scene::CompositionNode node;
    node.name = "grid";
    node.kind = scene::NodeKind::Procedural;
    node.procedural.name = "grid";
    node.procedural.source.kind = scene::PrimitiveKind::Box;
    node.procedural.distribution.kind = scene::DistributionKind::Grid;
    node.procedural.distribution.gridCount = glm::ivec3(6, 1, 6);
    REQUIRE(comp.addNode(std::move(node)));

    FrameTime time{};
    params.resetFinals();
    comp.update(time);
    REQUIRE(comp.scene().procedurals.size() == 1);
    const std::uint32_t settled = comp.scene().procedurals[0].structureVersion;

    // Driven through the parameter, which is what a slider drags. Writing the flattened copy
    // directly would prove nothing: applyParameters() rebuilds that copy from the node's rest state
    // every frame, so the write would be gone before rebuildProcedurals() ever saw it.
    auto* gridX = params.find("procedural/grid/distribution/gridCountX");
    REQUIRE(gridX != nullptr);

    SECTION("no budget: a changed input regenerates on the very next update") {
        CHECK(comp.interactiveRebuildBudget() == 0.0);
        gridX->setBaseComponent(0, 7.0f);
        params.resetFinals(); // the engine does this each frame; a bare composition has no engine
        comp.update(time);
        CHECK(comp.scene().procedurals[0].structureVersion > settled);
        CHECK(comp.proceduralsAwaitingRebuild() == 0);
    }

    SECTION("with a budget, an object cheaper than it is never deferred") {
        // This grid costs microseconds, so the budget leaves it alone. The deferral is for objects
        // whose regeneration takes the frame away; it must not add latency to the ones it was never
        // meant for.
        comp.setInteractiveRebuildBudget(2.0);
        gridX->setBaseComponent(0, 8.0f);
        params.resetFinals(); // the engine does this each frame; a bare composition has no engine
        comp.update(time);
        CHECK(comp.scene().procedurals[0].structureVersion > settled);
        CHECK(comp.proceduralsAwaitingRebuild() == 0);
    }

    SECTION("a structural rebuild forgets the per-object state rather than misapplying it") {
        comp.setInteractiveRebuildBudget(2.0);
        params.resetFinals(); // the engine does this each frame; a bare composition has no engine
        comp.update(time);
        scene::CompositionNode second;
        second.name = "grid2";
        second.kind = scene::NodeKind::Procedural;
        second.procedural.name = "grid2";
        second.procedural.source.kind = scene::PrimitiveKind::Sphere;
        REQUIRE(comp.addNode(std::move(second))); // sets dirty_, so the next update re-flattens
        params.resetFinals(); // the engine does this each frame; a bare composition has no engine
        comp.update(time);
        CHECK(comp.scene().procedurals.size() == 2);
        CHECK(comp.proceduralsAwaitingRebuild() == 0);
    }
}

// ADR-233. A composition that nobody is touching must generate nothing.
//
// It generated *twice per procedural object per frame*, for ever, and the mechanism is worth
// stating because it is invisible from either end on its own. `applyParameters` did two things in
// order: it copied the parameter finals into the flattened object -- which ended by calling
// `rebuild()` against an **empty** GenerationContext and storing that hash -- and then folded the
// node's world transform into the distribution transform. `rebuildProcedurals()` then asked for a
// rebuild against the **real** context, whose hash differed from the stored one for two independent
// reasons, so it regenerated and stored its own hash; next frame the first step disagreed right
// back. Neither call site looked wrong. Only the count did: measured on the editor with
// `--ui-ab idle`, eleven procedural nodes produced twenty-two regenerations per idle frame.
//
// The counter is the assertion, not a timing: a wall clock on this machine is a statement about
// what else was running, and "none where there were two" is not.
TEST_CASE("An untouched composition regenerates no procedural geometry", "[scene][composition][performance]") {
    assets::AssetRegistry registry;
    registry.setBaseDirectory(tempDir());
    params::ParameterSet params;
    params::Modulator modulator;
    scene::Composition comp(registry, "composition");
    comp.attach(params, modulator);

    scene::CompositionNode node;
    node.name = "grid";
    node.kind = scene::NodeKind::Procedural;
    node.procedural.name = "grid";
    node.procedural.source.kind = scene::PrimitiveKind::Box;
    node.procedural.distribution.kind = scene::DistributionKind::Grid;
    node.procedural.distribution.gridCount = glm::ivec3(4, 1, 4);
    // **The node is not at the origin, and that is the point.** The fold is a no-op at identity, so
    // an object sitting at the world origin never showed the defect at all -- which is why it
    // survived two performance passes. Everything a person places is somewhere.
    node.transform.position = glm::vec3(3.0f, 1.0f, -2.0f);
    node.transform.scale = glm::vec3(1.5f);
    REQUIRE(comp.addNode(std::move(node)));

    FrameTime time{};
    params.resetFinals();
    comp.update(time); // the first update flattens and builds: it is allowed to generate
    REQUIRE(comp.scene().procedurals.size() == 1);

    SECTION("ten idle frames generate nothing") {
        const std::uint64_t before = scene::proceduralRebuildCount();
        const std::uint32_t version = comp.scene().procedurals[0].structureVersion;
        for (int frame = 0; frame < 10; ++frame) {
            params.resetFinals(); // the engine does this each frame; a bare composition has no engine
            comp.update(time);
        }
        CHECK(scene::proceduralRebuildCount() == before);
        CHECK(comp.scene().procedurals[0].structureVersion == version);
    }

    SECTION("the arm can fail: with the old path restored the same ten frames generate twenty") {
        // §3 rule 5 of docs/application-performance.md -- break the code and confirm the test
        // fails -- done here rather than by hand, so the assertion above cannot quietly become
        // vacuous if some later change makes regeneration impossible for an unrelated reason.
        comp.setLegacyProceduralGeneration(true);
        const std::uint64_t before = scene::proceduralRebuildCount();
        for (int frame = 0; frame < 10; ++frame) {
            params.resetFinals();
            comp.update(time);
        }
        CHECK(scene::proceduralRebuildCount() == before + 20);
    }

    SECTION("a parameter that moves still regenerates, exactly once") {
        auto* gridX = params.find("procedural/grid/distribution/gridCountX");
        REQUIRE(gridX != nullptr);
        const std::uint64_t before = scene::proceduralRebuildCount();
        gridX->setBaseComponent(0, 7.0f);
        params.resetFinals();
        comp.update(time);
        CHECK(scene::proceduralRebuildCount() == before + 1);
        // And then settles again, rather than regenerating on for ever at the new value.
        const std::uint64_t after = scene::proceduralRebuildCount();
        for (int frame = 0; frame < 5; ++frame) {
            params.resetFinals();
            comp.update(time);
        }
        CHECK(scene::proceduralRebuildCount() == after);
    }
}

// The deferral policy itself, as arithmetic. Pinned here rather than through a composition because
// the two properties that matter are properties of the policy and not of any scene: a drag must
// never reach a regeneration, and a released slider must always reach one. Driving it through a
// real scene would additionally depend on a wall clock and on which of several code paths happened
// to have flattened the object first, neither of which this is about.
TEST_CASE("The interactive rebuild policy defers a drag and always converges",
          "[scene][composition][performance]") {
    using State = scene::Composition::ProceduralRebuildState;

    SECTION("nothing to do when the object is already at the wanted hash") {
        State s;
        s.lastMs = 500.0;
        s.deferring = true;
        CHECK_FALSE(scene::advanceRebuildDeferral(s, 42, 42, 1000.0));
        CHECK_FALSE(s.deferring);
        CHECK(s.heldForMs == 0.0);
    }

    SECTION("a drag does not regenerate per frame, only at the ceiling") {
        State s;
        s.lastMs = 150.0; // expensive: the ceiling is 4 x 150 = 600 ms
        std::uint64_t wanted = 1;
        // Sixty frames of a drag at 60 Hz -- a second of dragging, with a different wanted hash
        // every frame, which is what dragging a slider does. The failing behaviour this replaces
        // regenerated on all sixty, at 145-216 ms each.
        int regenerations = 0;
        int firstAt = -1;
        for (int frame = 0; frame < 60; ++frame) {
            if (scene::advanceRebuildDeferral(s, ++wanted, 0, 16.7)) {
                ++regenerations;
                if (firstAt < 0) {
                    firstAt = frame;
                }
            }
        }
        // Once, at the ceiling: 601 ms in, which at 16.7 ms a frame is frame 36.
        CHECK(regenerations == 1);
        CHECK(firstAt == 36);
    }

    SECTION("letting go regenerates within the settle window") {
        State s;
        s.lastMs = 150.0;
        CHECK_FALSE(scene::advanceRebuildDeferral(s, 7, 0, 16.7)); // the drag's last frame
        // Now still. It must not fire immediately (that would defeat the settling)...
        CHECK_FALSE(scene::advanceRebuildDeferral(s, 7, 0, 16.7));
        // ...and it must fire soon. 90 ms is the window; give it a couple of frames of slack and
        // require that it has happened, rather than pinning the exact frame.
        bool fired = false;
        for (int frame = 0; frame < 8 && !fired; ++frame) {
            fired = scene::advanceRebuildDeferral(s, 7, 0, 16.7);
        }
        CHECK(fired);
        CHECK_FALSE(s.deferring);
    }

    SECTION("a long continuous drag still refreshes, at a rate that scales with the cost") {
        // The ceiling: an object may not be held back forever, or a slow drag would show nothing
        // moving at all. Cheap objects get the floor (90 ms), expensive ones four times their own
        // cost, so neither starves and neither takes the frame back.
        const auto framesUntilRefresh = [](double lastMs) {
            State s;
            s.lastMs = lastMs;
            std::uint64_t wanted = 1;
            for (int frame = 1; frame <= 2000; ++frame) {
                if (scene::advanceRebuildDeferral(s, ++wanted, 0, 1.0)) { // 1 ms per step
                    return frame;
                }
            }
            return 0;
        };
        // A moving target never settles, so only the ceiling can fire. Note both do fire: a drag is
        // deferred, not ignored.
        const int cheap = framesUntilRefresh(5.0);
        const int expensive = framesUntilRefresh(150.0);
        CHECK(cheap > 0);
        CHECK(expensive > 0);
        CHECK(cheap == 91);       // the 90 ms floor
        CHECK(expensive == 601);  // 4 x 150 ms, so under a fifth of the time spent regenerating
        CHECK(expensive > cheap);
    }
}

TEST_CASE("A pick id says which numbering it belongs to", "[scene][pick]") {
    // Three renderers write into one 16-bit object id and each counts from zero. Untagged, the
    // number cannot say what it counts, and the picker read every one as an entity index -- so a
    // click on a scattered tree selected whichever node owned that entity, or nothing.
    for (const auto space : {scene::PickSpace::Entity, scene::PickSpace::Procedural,
                             scene::PickSpace::Sdf}) {
        for (const std::size_t index : {std::size_t{0}, std::size_t{1}, std::size_t{277},
                                        std::size_t{scene::kPickMaxIndex}}) {
            const std::uint32_t id = scene::packPickId(space, index);
            INFO("space " << static_cast<int>(space) << " index " << index);
            CHECK(scene::pickSpaceOf(id) == space);
            CHECK(scene::pickIndexOf(id) == index);
        }
    }

    // The property that was missing: the same index in two numberings is two different ids.
    CHECK(scene::packPickId(scene::PickSpace::Entity, 7) !=
          scene::packPickId(scene::PickSpace::Procedural, 7));
    CHECK(scene::packPickId(scene::PickSpace::Procedural, 7) !=
          scene::packPickId(scene::PickSpace::Sdf, 7));

    // An entity id is still its own index, so everything that already read one is unaffected.
    CHECK(scene::packPickId(scene::PickSpace::Entity, 0) == 0u);
    CHECK(scene::packPickId(scene::PickSpace::Entity, 123) == 123u);

    // Saturates rather than wrapping. An id that wraps names a real and entirely unrelated object,
    // which is worse than one that names nothing -- and it fits in the 16 bits the target has.
    const std::uint32_t over = scene::packPickId(scene::PickSpace::Procedural, 1'000'000);
    CHECK(scene::pickSpaceOf(over) == scene::PickSpace::Procedural);
    CHECK(scene::pickIndexOf(over) == scene::kPickMaxIndex);
    CHECK(over <= 0xFFFFu);
}

TEST_CASE("A procedural resolves to the node that emitted it", "[scene][composition][pick]") {
    assets::AssetRegistry registry;
    registry.setBaseDirectory(tempDir());
    params::ParameterSet params;
    params::Modulator modulator;
    scene::Composition comp(registry, "composition");
    comp.attach(params, modulator);

    const auto addGrid = [&comp](const char* name) {
        scene::CompositionNode node;
        node.name = name;
        node.kind = scene::NodeKind::Procedural;
        node.procedural.name = name;
        node.procedural.source.kind = scene::PrimitiveKind::Box;
        node.procedural.distribution.kind = scene::DistributionKind::Grid;
        node.procedural.distribution.gridCount = glm::ivec3(3, 1, 3);
        REQUIRE(comp.addNode(std::move(node)));
    };
    addGrid("first");
    addGrid("second");

    FrameTime time{};
    params.resetFinals();
    comp.update(time);
    REQUIRE(comp.scene().procedurals.size() == 2);

    // Every procedural resolves, and to the node whose name it carries -- which is the check that
    // would have caught the two id spaces sharing one channel, had it been asked of a procedural.
    for (std::size_t i = 0; i < comp.scene().procedurals.size(); ++i) {
        const scene::CompositionNode* node = comp.nodeForProcedural(i);
        INFO("procedural " << i << " named " << comp.scene().procedurals[i].name);
        REQUIRE(node != nullptr);
        // The emitted procedural's name carries the node's, so the two must agree.
        CHECK(comp.scene().procedurals[i].name.find(node->name) != std::string::npos);
    }

    // Two nodes, two different answers: a resolver that returned the first node for everything
    // would satisfy the loop above.
    CHECK(comp.nodeForProcedural(0) != comp.nodeForProcedural(1));

    // Past the end is nothing, rather than the last node.
    CHECK(comp.nodeForProcedural(comp.scene().procedurals.size()) == nullptr);
    CHECK(comp.nodeForProcedural(9999) == nullptr);
}

// Reported against the world editor: in Glowmere Valley 2 a click on a hero mushroom's cap or gills
// does not select it, and the only way to select one is to click the ground where its root sits.
// Every procedural the scene emits must resolve to the node that owns it, or a click on its pixels
// resolves to nothing and the editor deselects.
TEST_CASE("every procedural in the shipped scenes resolves to its node", "[scene][composition][pick]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    const std::filesystem::path scene =
        std::filesystem::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-valley-2.scene.json";
    if (!std::filesystem::exists(scene)) {
        SKIP("Glowmere Valley 2 is not present");
    }
    Fixture fx;
    auto loaded = scene::Composition::loadFile(scene, fx.registry);
    INFO((loaded ? std::string() : loaded.error().message));
    REQUIRE(loaded.has_value());
    scene::Composition& comp = **loaded;
    comp.update({});

    const std::size_t count = comp.scene().procedurals.size();
    INFO("procedurals emitted: " << count);
    REQUIRE(count > 0);

    std::vector<std::size_t> unresolved;
    for (std::size_t i = 0; i < count; ++i) {
        if (comp.nodeForProcedural(i) == nullptr) {
            unresolved.push_back(i);
        }
    }
    std::string names;
    for (std::size_t i : unresolved) {
        names += (names.empty() ? "" : ", ") + std::to_string(i) + ":" + comp.scene().procedurals[i].name;
    }
    INFO("unresolved " << unresolved.size() << " of " << count << ": " << names);
    CHECK(unresolved.empty());

    // Does a hero mushroom emit any geometry at all? A part with no instances is invisible, and a
    // click that lands on the terrain behind it looks exactly like a picking bug.
    for (const char* part : {"lantern-cap", "lantern-gills", "lantern-stem", "lantern-under"}) {
        const scene::CompositionNode* node = comp.findNode(part);
        REQUIRE(node != nullptr);
        std::string emitted;
        for (std::size_t i = 0; i < count; ++i) {
            if (comp.nodeForProcedural(i) != node) {
                continue;
            }
            emitted += " " + comp.scene().procedurals[i].name + "(instances=" +
                       std::to_string(comp.scene().procedurals[i].instances.size()) + ")";
        }
        INFO("part " << part << " emits:" << (emitted.empty() ? std::string(" NOTHING") : emitted));
        CHECK_FALSE(emitted.empty());
    }

    // And the hero mushrooms specifically, by name -- the thing that was reported. A generated
    // organism is emitted as several parts (cap, underside, stem, gills) and every one of them is
    // a surface somebody will click on.
    for (const char* part : {"lantern-cap", "lantern-gills", "lantern-stem", "lantern-under"}) {
        const scene::CompositionNode* node = comp.findNode(part);
        INFO("part: " << part);
        REQUIRE(node != nullptr);
        bool reachable = false;
        for (std::size_t i = 0; i < count && !reachable; ++i) {
            reachable = comp.nodeForProcedural(i) == node;
        }
        CHECK(reachable); // some procedural index resolves back to this node
    }
#endif
}

// ---- Phase 1.4/5.1: culling writes its own verdict and nothing else -----------------------------
//
// Rendering cannot touch authoritative scene state: every `SceneRenderer` entry point takes the
// scene by const reference and the only `const_cast` under `src/rendering` is on the renderer's own
// LOD bookkeeping, so the invariant is held by the type system rather than by a runtime assertion.
//
// Culling is the half that genuinely does write to the scene, from inside `Composition`. What it is
// allowed to write is `cameraCulled` -- this frame's verdict. What it must never write is the
// authored `visible` flag or a transform: a cull that turned an object off would be an object that
// stayed off after the camera moved away, and one that nudged a transform is `SYM-STATIC-1`.
TEST_CASE("culling writes its verdict and never authored state",
          "[scene][composition][forensics][culling]") {
    Fixture fx;
    const std::string text = std::string(R"({
  "format": "avgen-scene", "version": 1, "name": "cull",
  "camera": {"mode": 1, "position": [0, 1, 8], "target": [0, 1, 0]},
  "nodes": [
    {"name": "near", "kind": "gltf", "asset": ")" + fx.glb.filename().string() + R"(",
     "position": [0, 1, 0]},
    {"name": "far", "kind": "gltf", "asset": ")" + fx.glb.filename().string() + R"(",
     "position": [60, 1, 0]},
    {"name": "hidden", "kind": "gltf", "asset": ")" + fx.glb.filename().string() + R"(",
     "position": [0, 1, -3], "visible": false}
  ]})");
    auto loaded = scene::Composition::fromJson(nlohmann::json::parse(text), fx.registry);
    REQUIRE(loaded.has_value());
    scene::Composition& comp = **loaded;
    params::ParameterSet params;
    params::Modulator modulator;
    comp.attach(params, modulator);
    comp.setViewport(320, 200);

    const auto aim = [&](glm::vec3 eye, glm::vec3 target, double at) {
        params.findAs<int>("camera/mode")->setBase(1);
        params.findAs<glm::vec3>("camera/position")->setBase(eye);
        params.findAs<glm::vec3>("camera/target")->setBase(target);
        params.resetFinals();
        FrameTime time;
        time.renderTime = at;
        time.deltaTime = 1.0 / 60.0;
        comp.update(time);
    };
    aim({0.0f, 1.0f, 8.0f}, {0.0f, 1.0f, 0.0f}, 0.0);

    // What the scene authored, taken once.
    std::vector<std::tuple<std::string, glm::mat4, bool>> authored;
    for (const scene::Entity& e : comp.scene().entities) {
        authored.emplace_back(e.name, e.transform.matrix(), e.visible);
    }
    REQUIRE(authored.size() >= 3);

    // Look away, so the frustum rejects things it was accepting a moment ago.
    std::size_t culledAtSomePoint = 0;
    for (const auto& [eye, target] : {std::pair{glm::vec3(0.0f, 1.0f, 8.0f), glm::vec3(0.0f, 1.0f, 0.0f)},
                                      {glm::vec3(0.0f, 1.0f, 8.0f), glm::vec3(0.0f, 1.0f, 40.0f)},
                                      {glm::vec3(0.0f, 60.0f, 0.0f), glm::vec3(0.0f, 61.0f, 0.0f)},
                                      {glm::vec3(0.0f, 1.0f, 8.0f), glm::vec3(0.0f, 1.0f, 0.0f)}}) {
        aim(eye, target, 1.0);
        for (const scene::Entity& e : comp.scene().entities) {
            culledAtSomePoint += e.cameraCulled ? 1 : 0;
            for (const auto& [name, matrix, visible] : authored) {
                if (e.name != name) {
                    continue;
                }
                INFO("entity " << e.name);
                CHECK(e.transform.matrix() == matrix);   // no transform written by a cull
                CHECK(e.visible == visible);             // and the authored flag is not a verdict
            }
        }
    }
    // The camera really did reject something, or none of the above was tested against anything.
    CHECK(culledAtSomePoint > 0);
    // The authored-invisible node stays invisible throughout, which is the other direction of the
    // same rule: a cull cannot turn something *on* either.
    for (const scene::Entity& e : comp.scene().entities) {
        if (e.name.find("hidden") != std::string::npos) {
            CHECK_FALSE(e.visible);
        }
    }
}

// ---- Phase 1.3 of the renderer forensics: the derived-copy rule ---------------------------------
//
// Two values were found to be re-derived every frame by accident rather than by search, and both cost
// an investigation: `CompositionNode::transform` (a forensic test perturbed it and nothing failed)
// and `Scene::camera` (two forensic tests wrote it and moved no camera, so they asserted nothing).
// The pattern turned out to be uniform rather than two special cases:
//
//   **the parameter is authoritative, the node's `*Rest` struct is the authored baseline, and the
//   object hanging off `Scene` is a per-frame derivation of the two.**
//
// `Composition::applyParameters` rebuilds entities, lights, procedurals, splines, SDFs, fields and
// particle systems from that pair on every update. So writing to any of them directly is a write
// that does not survive one frame -- which is a perfectly good design and a trap for anybody who has
// not been told, including a test author.
//
// This pins the rule for the kinds that can be built cheaply. It is deliberately a *contract* test:
// if a future change makes one of these authoritative it should fail here and the documentation
// should move with it.
TEST_CASE("scene objects are derived from parameters, not authoritative",
          "[scene][composition][forensics][derived]") {
    Fixture fx;
    const std::string text = std::string(R"({
  "format": "avgen-scene", "version": 1, "name": "derived",
  "camera": {"mode": 1, "position": [3, 4, 5], "target": [0, 1, 0]},
  "nodes": [
    {"name": "block", "kind": "gltf", "asset": ")" + fx.glb.filename().string() + R"(",
     "position": [1, 2, 3], "rotation": [0, 30, 0], "scale": [2, 2, 2]},
    {"name": "sparks", "kind": "particles", "position": [0, 0, 1],
     "particles": {"capacity": 256, "seed": 3, "spawnRate": 42.0, "extent": [1, 0.5, 1]}}
  ]})");
    auto loaded = scene::Composition::fromJson(nlohmann::json::parse(text), fx.registry);
    REQUIRE(loaded.has_value());
    scene::Composition& comp = **loaded;

    params::ParameterSet params;
    params::Modulator modulator;
    comp.attach(params, modulator);
    const auto update = [&](double at) {
        // The *final* value is what `applyParameters` reads, and a bare `setBase` does not reach it
        // -- the engine's modulation pass is what normally refreshes finals every frame, and a
        // composition updated on its own has no such pass. Another way for a test's setup to
        // silently do nothing, which is the failure mode this whole investigation keeps meeting.
        params.resetFinals();
        FrameTime time;
        time.renderTime = at;
        time.deltaTime = 1.0 / 60.0;
        comp.update(time);
    };
    update(0.0);

    const auto entityNamed = [&](std::string_view name) -> const scene::Entity* {
        for (const scene::Entity& e : comp.scene().entities) {
            if (e.name.find(name) != std::string::npos) {
                return &e;
            }
        }
        return nullptr;
    };

    SECTION("a node's transform is a cache of its parameters") {
        scene::CompositionNode* node = comp.findNode("block");
        REQUIRE(node != nullptr);
        const glm::vec3 authored = node->transform.position;
        node->transform.position += glm::vec3(0.0f, 5.0f, 0.0f);
        update(1.0 / 60.0);
        CHECK(node->transform.position == authored);   // overwritten, not honoured

        // ...and the parameter is what does survive.
        params::IParameter* p = params.find("nodes/block/position");
        REQUIRE(p != nullptr);
        p->setBaseComponent(1, 9.0f);
        update(2.0 / 60.0);
        CHECK_THAT(static_cast<double>(node->transform.position.y), WithinAbs(9.0, 1e-6));
    }

    SECTION("the flattened entity's transform is a derivation too") {
        const scene::Entity* block = entityNamed("block");
        REQUIRE(block != nullptr);
        const std::string name = block->name;
        const glm::vec3 authored = block->transform.position;
        // Written through the same const_cast a careless caller would reach for.
        const_cast<scene::Entity*>(block)->transform.position += glm::vec3(3.0f, 0.0f, 0.0f);
        update(3.0 / 60.0);
        const scene::Entity* again = entityNamed(name);
        REQUIRE(again != nullptr);
        CHECK(again->transform.position == authored);
    }

    SECTION("the scene camera is a derivation of camera/*") {
        // This is the one that made two forensic tests vacuous: they wrote `scene().camera` and
        // asserted nothing moved, while the camera in fact never moved at all.
        const_cast<scene::Camera&>(comp.scene().camera).position = glm::vec3(100.0f, 100.0f, 100.0f);
        update(4.0 / 60.0);
        CHECK(comp.scene().camera.position != glm::vec3(100.0f, 100.0f, 100.0f));

        params::IParameter* mode = params.find("camera/mode");
        params::IParameter* position = params.find("camera/position");
        REQUIRE(mode != nullptr);
        REQUIRE(position != nullptr);
        mode->setBaseComponent(0, 1.0f);   // free: orbit ignores position entirely
        position->setBaseComponent(0, 7.0f);
        position->setBaseComponent(1, 8.0f);
        position->setBaseComponent(2, 9.0f);
        update(5.0 / 60.0);
        CHECK(comp.scene().camera.position == glm::vec3(7.0f, 8.0f, 9.0f));
    }

    SECTION("a particle system is rebuilt from its rest state and parameters") {
        REQUIRE_FALSE(comp.scene().particles.empty());
        const float authored = comp.scene().particles.front().spawnRate;
        const_cast<scene::ParticleSystem&>(comp.scene().particles.front()).spawnRate = 999.0f;
        update(6.0 / 60.0);
        CHECK_THAT(static_cast<double>(comp.scene().particles.front().spawnRate),
                   WithinAbs(static_cast<double>(authored), 1e-6));

        if (params::IParameter* rate = params.find("nodes/sparks/particles/spawnRate")) {
            rate->setBaseComponent(0, 7.0f);
            update(7.0 / 60.0);
            CHECK_THAT(static_cast<double>(comp.scene().particles.front().spawnRate),
                       WithinAbs(7.0, 1e-6));
        }
    }
}

// `cloneNodeSpec` is a hand-written field list, and its own header says so: "the list of fields here
// is the list a new authored field has to be added to. If a duplicate ever comes back missing
// something, this is the function that forgot it." It had forgotten five -- `waterFlow`,
// `materialAuthored`, `city`, `cityLibrary` and `floats` -- so duplicating a City node silently
// produced default settings, exactly the failure the header warns about.
//
// Checking the five by hand would only pin the five. The serialiser is the better oracle: it *is*
// the definition of what is authored about a node, because it is what the scene file writes. So a
// clone is correct precisely when it serialises to the same object as its source, and a field added
// to `toJson` but forgotten here fails this without anybody editing the test.
TEST_CASE("A duplicated node carries every field the scene file writes", "[scene][composition][clone]") {
    Fixture fx;
    scene::Composition comp(fx.registry, "clone");

    // A terrain, because it is the node kind carrying the most authored state that is not geometry:
    // the water's flow, a city's settings and tiling manifest, a floating layer, and the flag
    // recording that a material block was authored rather than defaulted.
    auto node = makeNode(scene::NodeKind::Terrain, "ground");
    node.transform.position = glm::vec3(3.0f, -1.5f, 7.25f);
    node.visible = false;
    node.locked = true;
    node.emissiveBoost = 2.75f;
    node.roughnessScale = 0.4f;
    node.materialAuthored = true;
    node.waterFlow.speedScale = 1.875f;
    node.waterFlow.speedOverride = 0.625f;
    node.cityLibrary = "kits/tiles.manifest.json";
    scene::FloatSpec floats;
    floats.water = "ground";
    floats.count = 37;
    floats.seed = 4242;
    node.floats = floats;

    REQUIRE(comp.addNode(std::move(node)).has_value());
    const scene::CompositionNode* source = comp.findNode("ground");
    REQUIRE(source != nullptr);

    scene::CompositionNode copy = scene::cloneNodeSpec(*source);
    copy.name = "ground-copy";
    REQUIRE(comp.addNode(std::move(copy)).has_value());

    const nlohmann::json document = comp.toJson();
    REQUIRE(document.contains("nodes"));
    const auto findNodeJson = [&](const std::string& name) {
        for (const auto& entry : document.at("nodes")) {
            if (entry.value("name", std::string()) == name) {
                return entry;
            }
        }
        return nlohmann::json();
    };
    nlohmann::json original = findNodeJson("ground");
    nlohmann::json duplicate = findNodeJson("ground-copy");
    REQUIRE_FALSE(original.is_null());
    REQUIRE_FALSE(duplicate.is_null());
    // The name is the one thing a duplicate must *not* carry over.
    original.erase("name");
    duplicate.erase("name");
    INFO("original:  " << original.dump(2));
    INFO("duplicate: " << duplicate.dump(2));
    CHECK(original == duplicate);
}

// The distance ladder is authored in the scene file, all four rungs of it. Two of them used to be
// compiled into `SkinnedRig` with nothing copying a scene's values over them, so a node could name
// `updateHz` and `cullDistance` and silently keep the engine's `nearDistance` and `farHz` -- which
// is how Glowmere asked for 30 Hz and got 20 beyond fifteen metres.
TEST_CASE("a node's animation ladder reaches the rig and round-trips", "[scene][composition][animation]") {
    Fixture fx;
    scene::Composition comp(fx.registry, "ladder");

    auto node = makeNode(scene::NodeKind::Gltf, "character", fx.glb.filename());
    node.animation.state = "Walk";
    node.animation.updateHz = 30.0f;
    node.animation.nearDistance = 42.0f;
    node.animation.farHz = 7.5f;
    node.animation.cullDistance = 310.0f;
    REQUIRE(comp.addNode(std::move(node)).has_value());
    comp.update({});

    // All four reach the rig the node owns. Checking the rig rather than the node is the point:
    // the node is what the file says and the rig is what the engine poses with, and the defect was
    // entirely in the gap between them.
    const scene::CompositionNode* built = comp.findNode("character");
    REQUIRE(built != nullptr);
    if (!built->rigs.empty()) {
        const scene::SkinnedRig& rig = comp.scene().rigs[built->rigs.front()];
        CHECK(rig.updateHz == 30.0f);
        CHECK(rig.nearDistance == 42.0f);
        CHECK(rig.farHz == 7.5f);
        CHECK(rig.cullDistance == 310.0f);
        // And the ladder behaves: inside the near band every frame, past it the authored far rate.
        CHECK(rig.rateFor(10.0f) == 30.0f);
        CHECK(rig.rateFor(100.0f) == 7.5f);
        CHECK(rig.rateFor(400.0f) < 0.0f);
    }

    // ...and survives a save. A knob that cannot be written back is a knob an author sets once.
    const nlohmann::json document = comp.toJson();
    bool found = false;
    for (const auto& entry : document.at("nodes")) {
        if (entry.value("name", std::string()) != "character") {
            continue;
        }
        found = true;
        REQUIRE(entry.contains("animation"));
        const auto& anim = entry.at("animation");
        CHECK(anim.value("updateHz", 0.0f) == 30.0f);
        CHECK(anim.value("nearDistance", 0.0f) == 42.0f);
        CHECK(anim.value("farHz", 0.0f) == 7.5f);
        CHECK(anim.value("cullDistance", 0.0f) == 310.0f);
    }
    CHECK(found);
}

// Reported against the world editor: selecting a tree's foliage put the move gizmo metres away from
// the foliage, down at the trunk's base. The gizmo sits at the centre of `nodeBounds`, so the fault
// was in the bounds: a procedural node fell back to a one-metre box at the node's own origin, and
// generated geometry is built in a local frame with the node at its base.
//
// Heroes are measured from the same box (`heroFromNode`), so the same defect aimed the camera
// director at the ground under an object rather than at the object.
TEST_CASE("A procedural node is as big as what it placed, not a box at its root",
          "[scene][composition][bounds]") {
    assets::AssetRegistry registry;
    registry.setBaseDirectory(tempDir());
    params::ParameterSet params;
    params::Modulator modulator;
    scene::Composition comp(registry, "composition");
    comp.attach(params, modulator);

    // Instances well away from the node's origin, and along one axis only, so a bounds that is
    // really the origin box cannot pass by accident: the centre is somewhere the root is not.
    scene::CompositionNode node;
    node.name = "canopy";
    node.kind = scene::NodeKind::Procedural;
    node.procedural.name = "canopy";
    node.procedural.source.kind = scene::PrimitiveKind::Box;
    node.procedural.distribution.kind = scene::DistributionKind::Linear;
    node.procedural.distribution.count = 5;
    node.procedural.distribution.start = glm::vec3(10.0f, 6.0f, 0.0f);
    node.procedural.distribution.end = glm::vec3(20.0f, 6.0f, 0.0f);
    REQUIRE(comp.addNode(std::move(node)));

    FrameTime time{};
    params.resetFinals();
    comp.update(time);
    REQUIRE(comp.scene().procedurals.size() == 1);

    const scene::WorldBounds bounds = comp.nodeBounds("canopy");
    REQUIRE(bounds.valid);

    // The middle of the run, not the root. The instances span x = 10..20, so the old one-metre box
    // at the origin was fifteen metres from where the geometry actually is.
    CHECK_THAT(bounds.centre().x, Catch::Matchers::WithinAbs(15.0f, 1.0f));
    CHECK_THAT(bounds.centre().y, Catch::Matchers::WithinAbs(6.0f, 1.0f));
    CHECK(bounds.size().x >= 10.0f);      // it covers the run
    CHECK(bounds.min.x > 5.0f);           // and does not reach back to the root

    // And it is *tight*. Reported: selecting a mushroom stem drew a box many times the stem, because
    // this read the procedural's cull bound -- a sphere of the source's diagonal around every
    // instance, which is right for a test that may never drop something visible and badly wrong for
    // a box a person is shown. The run is along x with no rotation, so the cross-section is the
    // source's own extent and nothing like its diagonal.
    CHECK(bounds.size().y < 3.0f);
    CHECK(bounds.size().z < 3.0f);
}

// ADR-199. Reported twice, with screenshots: a selection box the size of the valley, sitting on the
// ground under a mushroom cap fifteen metres above it.
//
// The tight bounds took the source's half-extent from `sourceHalfExtent`, which returns
// `max(|vertex|)` -- the distance from the *origin* to the furthest vertex. That is a half-extent
// only when the geometry is centred on its own origin. A cap authored up its own stem is not: the
// number comes back as the full height, so the box is twice as tall as the cap AND centred on the
// ground rather than on the cap. Both symptoms, one cause.
// ADR-199 fixed `Mesh` and `Tube`. It did not fix `Generated`, and every hero mushroom in Glowmere
// is a `Generated` source -- which is why the box was reported wrong *again*, with a second
// screenshot, after that ADR shipped ("another example, look how low the yellow box is compared to
// the elder cap").
//
// `primitiveBoxImpl` branches on Mesh, Tube and Cylinder and lets everything else keep
// `centre = 0, half = sourceHalfExtent(s)`. For `Generated`, `sourceHalfExtent` has no case at all,
// so it falls through `case Torus: default:` and returns **torus dimensions** --
// `{majorRadius + minorRadius, minorRadius, majorRadius + minorRadius}` -- for a mushroom. Wrong
// size from the torus fields, wrong place from the zero centre, which is both reported symptoms.
//
// The expected values below are computed from the mesh **this test defines**, not from the function
// under test. That is the point: a bounds test that asks the bounds code what the bounds are cannot
// fail.
TEST_CASE("a generated source gets a box around the geometry it generates",
          "[scene][composition][bounds]") {
    scene::clearGenerators();
    // A slab from y = 10 to y = 12, one metre either side in x and z. Nothing at the origin, and
    // nothing symmetric about it: centre (0, 11, 0), half-extent (1, 1, 1).
    //
    // Deliberately *not* a torus, a tube or a mesh source -- the whole question is what happens to a
    // kind the box code has no branch for.
    scene::registerGenerator("slab", [](const scene::GeneratedSource&, int) -> Result<scene::MeshData> {
        scene::MeshData mesh;
        for (int i = 0; i < 8; ++i) {
            scene::Vertex v;
            v.position = glm::vec3((i & 1) ? 1.0f : -1.0f, (i & 2) ? 12.0f : 10.0f,
                                   (i & 4) ? 1.0f : -1.0f);
            v.normal = glm::vec3(0.0f, 1.0f, 0.0f);
            mesh.vertices.push_back(v);
        }
        // Two triangles are enough to be a mesh; the box is a question about vertices.
        mesh.indices = {0, 1, 2, 1, 3, 2};
        return mesh;
    });

    assets::AssetRegistry registry;
    registry.setBaseDirectory(tempDir());
    params::ParameterSet params;
    params::Modulator modulator;
    scene::Composition comp(registry, "generated-box");
    comp.attach(params, modulator);

    scene::CompositionNode node;
    node.name = "cap";
    node.kind = scene::NodeKind::Procedural;
    node.procedural.name = "cap";
    node.procedural.source.kind = scene::PrimitiveKind::Generated;
    node.procedural.source.generated.generator = "slab";
    // A generated source is a parameter vector with provenance (ADR-175); it is refused without one.
    // The builder above ignores the numbers -- what is being tested is the box, not the generator.
    node.procedural.source.generated.values = {0.5f, 0.5f, 0.5f};
    node.procedural.distribution.kind = scene::DistributionKind::Single;
    const auto added = comp.addNode(std::move(node));
    if (!added.has_value()) {
        INFO(added.error().message);
        FAIL("addNode refused the generated source");
    }

    FrameTime time{};
    params.resetFinals();
    comp.update(time);

    const scene::WorldBounds bounds = comp.nodeBounds("cap");
    REQUIRE(bounds.valid);
    INFO("box centre " << bounds.centre().x << ", " << bounds.centre().y << ", " << bounds.centre().z
                       << "  size " << bounds.size().x << ", " << bounds.size().y << ", "
                       << bounds.size().z);
    // The slab's own numbers, written down independently above.
    CHECK_THAT(bounds.centre().y, Catch::Matchers::WithinAbs(11.0, 0.2));
    CHECK_THAT(bounds.size().y, Catch::Matchers::WithinAbs(2.0, 0.4));
    CHECK_THAT(bounds.size().x, Catch::Matchers::WithinAbs(2.0, 0.4));
    CHECK_THAT(bounds.size().z, Catch::Matchers::WithinAbs(2.0, 0.4));
    // And the symptom in the words it was reported in: the box must not sit on the ground under a
    // thing that is ten metres up.
    CHECK(bounds.min.y > 8.0f);

    scene::clearGenerators();
}

TEST_CASE("a source that is not centred on its origin still gets a box around itself",
          "[scene][composition][bounds]") {
    assets::AssetRegistry registry;
    registry.setBaseDirectory(tempDir());
    params::ParameterSet params;
    params::Modulator modulator;
    scene::Composition comp(registry, "offcentre");
    comp.attach(params, modulator);

    // A tube whose curve runs ten metres above its own origin, with nothing at the origin at all.
    // `max(|vertex|)` for this is about 10.5, so the broken version produced a 21 m box centred on
    // zero -- exactly the screenshot: twice too tall, and sitting on the ground under the thing.
    scene::CompositionNode node;
    node.name = "cap";
    node.kind = scene::NodeKind::Procedural;
    node.procedural.name = "cap";
    node.procedural.source.kind = scene::PrimitiveKind::Tube;
    node.procedural.source.radius = 0.4f;
    node.procedural.source.curve.points = {
        spatial::SplinePoint{{-1.0f, 10.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, 0.0f, 1.0f},
        spatial::SplinePoint{{0.0f, 10.2f, 0.0f}, {1.0f, 0.0f, 0.0f}, 0.0f, 1.0f},
        spatial::SplinePoint{{1.0f, 10.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, 0.0f, 1.0f}};
    node.procedural.distribution.kind = scene::DistributionKind::Single;
    const auto added = comp.addNode(std::move(node));
    if (!added.has_value()) {
        INFO(added.error().message);
        FAIL("addNode refused the mesh source");
    }

    FrameTime time{};
    params.resetFinals();
    comp.update(time);

    const scene::WorldBounds bounds = comp.nodeBounds("cap");
    REQUIRE(bounds.valid);
    INFO("box " << bounds.min.y << " .. " << bounds.max.y);

    // Around the geometry, not around the origin. The cap lives between y = 10 and y = 10.5.
    CHECK(bounds.min.y > 9.0f);
    CHECK(bounds.max.y < 11.5f);
    // And the right size: about a metre tall, not twenty-one.
    CHECK(bounds.size().y < 3.0f);
    // The centre is what the gizmo sits on, and it must be on the cap.
    CHECK_THAT(bounds.centre().y, Catch::Matchers::WithinAbs(10.1, 1.5));
}

// Found by measurement, not by reading: setting `groundGlow` to 5.0 on Glowmere's valley produced a
// byte-identical frame, as did turning `groundMottle` off. The generated ground material carries all
// five of those settings and is built only for a terrain that names no program of its own -- so a
// terrain that authors both gets the program and silently loses the glow.
//
// The engine cannot compose the two (which surface wins is an authoring decision, not an engine
// one), so what it owes is to say so. This asserts the saying: an authored program plus a glow is
// the case that must be reported, and either one alone is not.
TEST_CASE("A terrain that authors a program is told its ground glow does nothing",
          "[composition][terrain][diagnostics]") {
    const auto sceneText = [](const char* program, double glow) {
        return fmt::format(R"({{
          "format": "avgen-scene", "version": 1, "name": "terra",
          "camera": {{ "mode": 1, "position": [0, 30, 60], "target": [0, 0, -60], "fov": 50.0 }},
          "nodes": [
            {{ "name": "ground", "kind": "terrain",
              "world": {{ "name": "small", "size": [160, 160] }},
              "terrain": {{ "chunkSize": 40.0, "resolution": 8, "lodLevels": 2,
                           "lodDistance": 50.0, "viewDistance": 400.0, "groundGlow": {} }},
              "material": {{ "baseColor": [0.2, 0.4, 0.3], "roughness": 0.9{}{}{} }} }}
          ]
        }})", glow, program[0] != '\0' ? ", \"program\": \"" : "", program,
             program[0] != '\0' ? "\"" : "");
    };

    // What the composition *itself* reports, rather than what a log line says: a node that names a
    // program keeps it, and the generated ground material is not installed for it at all. That is
    // the mechanism the warning describes, so it is what the test pins.
    const auto build = [&](const char* program, double glow, Fixture& fx) {
        const auto path = writeJson("terrain-glow", sceneText(program, glow));
        fx.files.push_back(path);
        auto comp = scene::Composition::loadFile(path.filename(), fx.registry);
        REQUIRE(comp.has_value());
        return std::move(*comp);
    };

    SECTION("with no program authored, the generated ground material exists and carries the glow") {
        Fixture fx;
        params::ParameterSet params;
        params::Modulator modulator;
        auto comp = build("", 0.5, fx);
        comp->attach(params, modulator);
        comp->update(FrameTime{});
        const auto& programs = comp->scene().materialPrograms;
        const bool generated = std::ranges::any_of(programs, [](const scene::MaterialProgram& p) {
            return p.name.find("ground_ground") != std::string::npos;
        });
        CHECK(generated);
        // And every chunk draws with it, which is the half that makes the glow visible.
        const auto* node = comp->findNode("ground");
        REQUIRE(node != nullptr);
        const auto chunks = std::ranges::count_if(comp->scene().entities, [](const scene::Entity& e) {
            return e.material.program.find("ground_ground") != std::string::npos;
        });
        CHECK(chunks > 0);
    }

    SECTION("with a program authored, the generated material is never built -- so the glow is inert") {
        Fixture fx;
        params::ParameterSet params;
        params::Modulator modulator;
        auto comp = build("paintedGround", 0.5, fx);
        comp->attach(params, modulator);
        comp->update(FrameTime{});
        const auto& programs = comp->scene().materialPrograms;
        const bool generated = std::ranges::any_of(programs, [](const scene::MaterialProgram& p) {
            return p.name.find("ground_ground") != std::string::npos;
        });
        CHECK_FALSE(generated);
        // The chunks draw with what the author named, which is why the glow reaches nothing.
        const auto chunks = std::ranges::count_if(comp->scene().entities, [](const scene::Entity& e) {
            return e.material.program == "paintedGround";
        });
        CHECK(chunks > 0);
    }
}

// ADR-193: the navigation grid's cell size had a field, a documented "0 disables pathfinding"
// escape hatch, and no way at all to reach either -- no setter, no scene key. A knob nobody can
// turn is the same defect as a knob wired to nothing.
TEST_CASE("the navigation cell size is authorable and takes effect", "[scene][composition][nav]") {
    assets::AssetRegistry registry;
    registry.setBaseDirectory(tempDir());
    params::ParameterSet params;
    params::Modulator modulator;
    scene::Composition comp(registry, "nav");
    comp.attach(params, modulator);

    CHECK(comp.navCellSize() == 4.0f);

    // Clamped, not rejected, for a value inside the plausible band.
    comp.setNavCellSize(1.5f);
    CHECK(comp.navCellSize() == 1.5f);
    comp.setNavCellSize(0.01f);
    CHECK(comp.navCellSize() == 0.5f); // the floor `NavGrid::build` applies anyway
    comp.setNavCellSize(1000.0f);
    CHECK(comp.navCellSize() == 64.0f);

    // Zero is the documented escape hatch and must survive the clamp that guards the rest.
    comp.setNavCellSize(0.0f);
    CHECK(comp.navCellSize() == 0.0f);

    // It survives a save and a load, and an untouched scene does not grow the key -- so a scene
    // written before this existed reloads byte-identical.
    comp.setNavCellSize(2.5f);
    const nlohmann::json saved = comp.toJson();
    REQUIRE(saved.contains("navCellSize"));
    CHECK(saved.at("navCellSize").get<float>() == 2.5f);

    scene::Composition plain(registry, "plain");
    plain.attach(params, modulator);
    CHECK_FALSE(plain.toJson().contains("navCellSize"));

    // And a value nobody could afford is refused with a reason rather than clamped into a stall.
    nlohmann::json bad = saved;
    bad["navCellSize"] = 500.0;
    const auto refused = scene::Composition::fromJson(bad, registry);
    CHECK_FALSE(refused.has_value());
}

// ADR-195: how deep a walker wades is the one thing a scene has to say to turn water from a wall
// into a depth, so it is the one thing that has to be reachable from a scene file.
TEST_CASE("the wade depth is authorable and defaults to the old rule", "[scene][composition][nav]") {
    assets::AssetRegistry registry;
    registry.setBaseDirectory(tempDir());
    params::ParameterSet params;
    params::Modulator modulator;
    scene::Composition comp(registry, "wade");
    comp.attach(params, modulator);

    // 0 is "a body that stops at the waterline", which is every walker this engine had before.
    CHECK(comp.navWadeDepth() == 0.0f);

    comp.setNavWadeDepth(0.6f);
    CHECK(comp.navWadeDepth() == 0.6f);
    comp.setNavWadeDepth(-1.0f);
    CHECK(comp.navWadeDepth() == 0.0f);

    comp.setNavWadeDepth(0.6f);
    const nlohmann::json saved = comp.toJson();
    REQUIRE(saved.contains("navWadeDepth"));
    CHECK(saved.at("navWadeDepth").get<float>() == 0.6f);
    const auto reloaded = scene::Composition::fromJson(saved, registry);
    REQUIRE(reloaded.has_value());
    CHECK((*reloaded)->navWadeDepth() == 0.6f);

    // An untouched scene does not grow the key, so every scene written before this reloads
    // byte-identical and keeps the walkable set it was authored against.
    scene::Composition plain(registry, "plain");
    plain.attach(params, modulator);
    CHECK_FALSE(plain.toJson().contains("navWadeDepth"));

    // Refused rather than clamped: a four-metre wade band is a character walking along the bed of
    // the river, and a number quietly pulled back to something sensible is a scene that does not do
    // what it says.
    nlohmann::json bad = saved;
    bad["navWadeDepth"] = 40.0;
    CHECK_FALSE(scene::Composition::fromJson(bad, registry).has_value());
}

// ---- the texture version (ADR-273) ------------------------------------------------------------

TEST_CASE("a flatten bumps the texture version only when a texture changed",
          "[scene][composition][textures]") {
    // `Scene::textureVersion` is the renderer's "destroy and re-create every GPU texture" signal,
    // and a flatten used to bump it whichever edit caused the flatten. Measured on Glowmere: 528
    // texture uploads over 12 flattens for starring a hero and 704 over 16 for a brush edit -- 44 a
    // flatten in both, because the number was a property of flattening and not of the edit. Neither
    // edit touches an image.
    assets::AssetRegistry registry;
    registry.setBaseDirectory(tempDir());
    params::ParameterSet params;
    params::Modulator modulator;
    scene::Composition comp(registry, "tex");
    comp.attach(params, modulator);

    const auto addOrb = [&](const char* name) {
        scene::CompositionNode node;
        node.name = name;
        node.kind = scene::NodeKind::Orb;
        REQUIRE(comp.addNode(std::move(node)).has_value());
    };

    addOrb("orb1");
    comp.update(FrameTime{}); // flattens
    const std::uint64_t afterFirst = comp.scene().textureVersion;
    const std::uint64_t meshesAfterFirst = comp.scene().meshVersion;

    // A second structural edit, a second flatten, and no texture anywhere near it.
    addOrb("orb2");
    comp.update(FrameTime{});
    CHECK(comp.scene().textureVersion == afterFirst);

    SECTION("control: the mesh version still moves, so the flatten really happened") {
        CHECK(comp.scene().meshVersion != meshesAfterFirst);
    }

    SECTION("control: adding a texture directly still moves it") {
        // The other half of the contract, unchanged: `Scene::addTexture` is what "a texture
        // changed" means, and it has always said so.
        scene::TextureData t;
        t.name = "one";
        t.width = 1;
        t.height = 1;
        t.data = {1, 2, 3, 4};
        const std::uint64_t before = comp.scene().textureVersion;
        comp.scene().addTexture(t);
        CHECK(comp.scene().textureVersion != before);
    }
}

TEST_CASE("the texture digest answers to a single texel", "[scene][composition][textures]") {
    // The arm that fails if the digest above ever hashes nothing. Without it "the version did not
    // move" is indistinguishable from "the version can never move", which is the defect the
    // conditional bump would introduce rather than the one it removes (ADR-182).
    const auto table = [](std::uint8_t last) {
        scene::TextureData t;
        t.name = "albedo";
        t.width = 4;
        t.height = 4;
        t.format = scene::TextureFormat::Rgba8Srgb;
        t.data.assign(4 * 4 * 4, 0u);
        t.data.back() = last;
        return std::vector<scene::TextureData>{t};
    };
    CHECK(scene::textureTableDigest(table(7)) == scene::textureTableDigest(table(7)));
    CHECK(scene::textureTableDigest(table(7)) != scene::textureTableDigest(table(8)));

    // ...and to the metadata, so two images that differ only in size or name are not one image.
    std::vector<scene::TextureData> renamed = table(7);
    renamed.front().name = "normal";
    CHECK(scene::textureTableDigest(table(7)) != scene::textureTableDigest(renamed));
    std::vector<scene::TextureData> resized = table(7);
    resized.front().width = 2;
    resized.front().height = 8;
    CHECK(scene::textureTableDigest(table(7)) != scene::textureTableDigest(resized));
    // An empty table is a legal state and is not confused with a one-texture one.
    CHECK(scene::textureTableDigest({}) != scene::textureTableDigest(table(0)));
}

// ---- runtime LOD on an imported node (ADR-351) ---------------------------------------------------

TEST_CASE("a gltf node builds no LOD chain unless it asks", "[scene][composition][lod]") {
    // The opt-in rule, asserted rather than trusted. Two engine changes landed on this project in
    // one day under "a behaviour that changes under everyone is not a fix", and this is the arm
    // that says the third one did not.
    Fixture fx;
    scene::Composition comp(fx.registry, "opt-in");
    REQUIRE(comp.addNode(makeNode(scene::NodeKind::Gltf, "plain", fx.glb.filename())).has_value());
    comp.update(FrameTime{});
    CHECK(comp.scene().meshLods.empty());
    CHECK_FALSE(comp.scene().meshes.empty()); // ...and the mesh itself did arrive
}

TEST_CASE("a lod block survives a save and a load", "[scene][composition][lod]") {
    Fixture fx;
    scene::Composition comp(fx.registry, "roundtrip");
    auto node = makeNode(scene::NodeKind::Gltf, "tree", fx.glb.filename());
    node.lod.enabled = true;
    node.lod.ratios = {1.0f, 0.4f, 0.1f};
    node.lod.thinning = false;
    node.lod.hysteresis = 0.15f;
    node.lod.maxScreenError = 12.0f;
    REQUIRE(comp.addNode(std::move(node)).has_value());

    const nlohmann::json saved = comp.toJson();
    auto reloaded = scene::Composition::fromJson(saved, fx.registry);
    REQUIRE(reloaded);
    const scene::CompositionNode* back = (*reloaded)->findNode("tree");
    REQUIRE(back != nullptr);
    CHECK(back->lod.enabled);
    CHECK(back->lod.ratios == std::vector<float>{1.0f, 0.4f, 0.1f});
    CHECK_FALSE(back->lod.thinning);
    CHECK_THAT(static_cast<double>(back->lod.hysteresis), WithinAbs(0.15, 1e-6));
    CHECK_THAT(static_cast<double>(back->lod.maxScreenError), WithinAbs(12.0, 1e-6));

    // A node that never asked writes no `lod` key at all, so enabling the feature does not add a
    // line to every scene file in the repository.
    scene::Composition plain(fx.registry, "plain");
    REQUIRE(plain.addNode(makeNode(scene::NodeKind::Gltf, "quiet", fx.glb.filename())).has_value());
    const nlohmann::json quiet = plain.toJson();
    REQUIRE(quiet.contains("nodes"));
    CHECK_FALSE(quiet["nodes"][0].contains("lod"));
}

TEST_CASE("a misspelt lod key is refused rather than ignored", "[scene][composition][lod]") {
    // The failure this closes is named in the parser: `"lodCount"` was accepted for months against
    // a reader that wanted `"count"`, and the ladder read as configured in the file while being one
    // rung in the engine.
    Fixture fx;
    const std::string text = fmt::format(R"({{"format":"avgen-scene","version":1,
"nodes":[{{"name":"tree","kind":"gltf","asset":"{}","lod":{{"enabled":true,"ratio":[1.0,0.5]}}}}]}})",
                                         fx.glb.filename().generic_string());
    const auto refused = scene::Composition::fromJson(nlohmann::json::parse(text), fx.registry);
    CHECK_FALSE(refused);
    // The control: the same document with the key spelt right loads.
    const std::string ok = fmt::format(R"({{"format":"avgen-scene","version":1,
"nodes":[{{"name":"tree","kind":"gltf","asset":"{}","lod":{{"enabled":true,"ratios":[1.0,0.5]}}}}]}})",
                                       fx.glb.filename().generic_string());
    CHECK(scene::Composition::fromJson(nlohmann::json::parse(ok), fx.registry).has_value());
}

TEST_CASE("the Tree of Life's chains reach the scene with LOD0 untouched", "[scene][composition][lod]") {
    const std::filesystem::path asset =
        std::filesystem::path(AVGEN_SOURCE_DIR) / "assets" / "treeisle" / "tree-glowmere-foliage.glb";
    if (!std::filesystem::is_regular_file(asset)) {
        SKIP("assets/treeisle is not present in this checkout");
    }
    assets::AssetRegistry registry{std::filesystem::path(AVGEN_SOURCE_DIR)};
    scene::Composition comp(registry, "tree");
    auto node = makeNode(scene::NodeKind::Gltf, "foliage", asset);
    node.lod.enabled = true;
    node.lod.ratios = {1.0f, 0.5f, 0.2f};
    REQUIRE(comp.addNode(std::move(node)).has_value());
    comp.update(FrameTime{});
    const scene::Scene& s = comp.scene();
    REQUIRE_FALSE(s.meshLods.empty());
    CHECK(s.meshLods.size() == s.meshes.size()); // one chain per part of this asset

    for (const scene::MeshLodChain& chain : s.meshLods) {
        INFO("chain on mesh " << chain.base);
        REQUIRE(chain.base < s.meshes.size());
        // §1: LOD0 *is* the source mesh. The chain carries the rungs below it and nothing else, so
        // an offline render reading scene.meshes gets the asset and cannot be handed a rung.
        CHECK(s.meshes[chain.base].indices.size() / 3 == chain.sourceTriangles);
        REQUIRE(chain.levels.size() == 2);
        CHECK(chain.levels[0].triangles < chain.sourceTriangles);
        CHECK(chain.levels[1].triangles < chain.levels[0].triangles);
        // Non-decreasing error, which is the property the selector's threshold depends on. Not
        // *strictly* increasing: two of this asset's 22 parts hit the shell-growth cap at both
        // rungs and report the same deviation, which the chain builder's monotone floor makes equal
        // rather than inverted.
        CHECK(chain.levels[1].error >= chain.levels[0].error);
        // This layer is the one that cannot be simplified, so every rung of it must be thinned --
        // if this ever reads false the strategy switch has stopped firing and the canopy has
        // silently stopped having a LOD.
        CHECK(chain.levels[0].thinned);
        CHECK(chain.levels[1].thinned);
        CHECK(chain.sourceShells > 0);
    }

    // Built once per (asset, version, ladder): a second node on the same asset and the same ladder
    // does not build a second chain, and does not install a second one either.
    const std::size_t chains = s.meshLods.size();
    auto second = makeNode(scene::NodeKind::Gltf, "foliage-again", asset);
    second.lod.enabled = true;
    second.lod.ratios = {1.0f, 0.5f, 0.2f};
    REQUIRE(comp.addNode(std::move(second)).has_value());
    comp.update(FrameTime{});
    CHECK(comp.scene().meshLods.size() == chains);
}
