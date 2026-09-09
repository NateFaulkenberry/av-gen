#include "core/time.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/orb_scene.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>

using namespace avgen;
using namespace avgen::scene;
using Catch::Matchers::WithinAbs;

namespace {
double d(float v) {
    return static_cast<double>(v);
}

void checkNormalsUnit(const MeshData& mesh) {
    for (const Vertex& v : mesh.vertices) {
        CHECK_THAT(d(glm::length(v.normal)), WithinAbs(1.0, 1e-5));
    }
}

// Every triangle's geometric normal points the same way as its vertex normals (CCW = outward).
void checkWindingMatchesNormals(const MeshData& mesh) {
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const Vertex& a = mesh.vertices[mesh.indices[i]];
        const Vertex& b = mesh.vertices[mesh.indices[i + 1]];
        const Vertex& c = mesh.vertices[mesh.indices[i + 2]];
        const glm::vec3 faceNormal = glm::cross(b.position - a.position, c.position - a.position);
        const glm::vec3 average = a.normal + b.normal + c.normal;
        REQUIRE(glm::dot(faceNormal, average) > 0.0f);
    }
}

std::size_t triangleCount(const MeshData& mesh) {
    return mesh.indices.size() / 3;
}
} // namespace

TEST_CASE("Icosphere generator", "[scene][mesh]") {
    const MeshData ico = makeIcosphere(1.0f, 0);
    CHECK(ico.valid());
    CHECK(ico.vertices.size() == 12);
    CHECK(triangleCount(ico) == 20);
    checkNormalsUnit(ico);
    checkWindingMatchesNormals(ico);

    for (int n = 1; n <= 3; ++n) {
        const float radius = 2.5f;
        const MeshData sphere = makeIcosphere(radius, n);
        INFO("subdivisions " << n);
        CHECK(sphere.valid());
        CHECK(triangleCount(sphere) == static_cast<std::size_t>(20 * std::pow(4, n)));
        CHECK(sphere.vertices.size() ==
              static_cast<std::size_t>(10 * std::pow(4, n) + 2)); // shared midpoints
        checkNormalsUnit(sphere);
        checkWindingMatchesNormals(sphere);
        for (const Vertex& v : sphere.vertices) {
            REQUIRE_THAT(d(glm::length(v.position)), WithinAbs(d(radius), 1e-5));
            REQUIRE(v.uv.x >= 0.0f);
            REQUIRE(v.uv.x <= 1.0f);
            REQUIRE(v.uv.y >= 0.0f);
            REQUIRE(v.uv.y <= 1.0f);
        }
    }
    // Subdivisions are clamped to a sane range.
    CHECK(triangleCount(makeIcosphere(1.0f, -3)) == 20);
    CHECK(triangleCount(makeIcosphere(1.0f, 99)) == triangleCount(makeIcosphere(1.0f, 6)));
}

TEST_CASE("Cube generator", "[scene][mesh]") {
    const MeshData cube = makeCube(0.5f);
    CHECK(cube.valid());
    CHECK(cube.vertices.size() == 24);
    CHECK(cube.indices.size() == 36);
    checkNormalsUnit(cube);
    checkWindingMatchesNormals(cube);
    for (const Vertex& v : cube.vertices) {
        CHECK_THAT(d(std::fabs(v.position.x) + std::fabs(v.position.y) + std::fabs(v.position.z)),
                   WithinAbs(1.5, 1e-6));
        CHECK_THAT(d(glm::dot(v.position, v.normal)), WithinAbs(0.5, 1e-6)); // normal faces outward
    }
}

TEST_CASE("Plane generator", "[scene][mesh]") {
    const MeshData plane = makePlane(3.0f, 4);
    CHECK(plane.valid());
    CHECK(plane.vertices.size() == 25);
    CHECK(triangleCount(plane) == 32);
    checkNormalsUnit(plane);
    checkWindingMatchesNormals(plane);
    for (const Vertex& v : plane.vertices) {
        CHECK(v.position.y == 0.0f);
        CHECK(v.normal == glm::vec3(0.0f, 1.0f, 0.0f));
        CHECK(std::fabs(v.position.x) <= 3.0f + 1e-6f);
        CHECK(std::fabs(v.position.z) <= 3.0f + 1e-6f);
        CHECK(v.uv.x >= 0.0f);
        CHECK(v.uv.x <= 1.0f);
        CHECK(v.uv.y >= 0.0f);
        CHECK(v.uv.y <= 1.0f);
    }
    CHECK(plane.vertices.front().position == glm::vec3(-3.0f, 0.0f, -3.0f));
    CHECK(plane.vertices.back().position == glm::vec3(3.0f, 0.0f, 3.0f));
    CHECK(plane.vertices.front().uv == glm::vec2(0.0f));
    CHECK(plane.vertices.back().uv == glm::vec2(1.0f));
    CHECK(makePlane(1.0f, 0).valid()); // clamps to at least one division
}

TEST_CASE("MeshData::valid rejects malformed meshes", "[scene][mesh]") {
    MeshData empty;
    CHECK_FALSE(empty.valid());
    MeshData cube = makeCube(1.0f);
    cube.indices.push_back(0);
    CHECK_FALSE(cube.valid()); // not a multiple of 3
    cube.indices.pop_back();
    cube.indices[0] = 24;
    CHECK_FALSE(cube.valid()); // out of range
    cube.indices.clear();
    CHECK_FALSE(cube.valid());
}

TEST_CASE("Transform::matrix applies scale, then rotation, then translation", "[scene][transform]") {
    Transform t;
    t.position = glm::vec3(10.0f, 0.0f, 0.0f);
    t.rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f)); // 90 deg about +Y
    t.scale = glm::vec3(2.0f);
    const glm::vec4 p = t.matrix() * glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    // scale -> (2,0,0); rotate 90deg about Y -> (0,0,-2); translate -> (10,0,-2)
    CHECK_THAT(d(p.x), WithinAbs(10.0, 1e-5));
    CHECK_THAT(d(p.y), WithinAbs(0.0, 1e-5));
    CHECK_THAT(d(p.z), WithinAbs(-2.0, 1e-5));
    CHECK_THAT(d(p.w), WithinAbs(1.0, 1e-6));
    CHECK(Transform{}.matrix() == glm::mat4(1.0f));
}

TEST_CASE("Camera view looks down -Z from the position", "[scene][camera]") {
    Camera cam;
    cam.position = glm::vec3(0.0f, 0.0f, 5.0f);
    cam.target = glm::vec3(0.0f);
    const glm::vec4 origin = cam.view() * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    CHECK_THAT(d(origin.z), WithinAbs(-5.0, 1e-5)); // in front of the camera
    const glm::vec4 self = cam.view() * glm::vec4(cam.position, 1.0f);
    CHECK_THAT(d(glm::length(glm::vec3(self))), WithinAbs(0.0, 1e-5));
}

TEST_CASE("Camera projection maps near to depth 0 and far to depth 1", "[scene][camera]") {
    Camera cam;
    cam.nearPlane = 0.5f;
    cam.farPlane = 100.0f;
    const glm::mat4 proj = cam.projection(16.0f / 9.0f);
    auto depthAt = [&](float z) {
        const glm::vec4 clip = proj * glm::vec4(0.0f, 0.0f, z, 1.0f);
        return clip.z / clip.w;
    };
    CHECK_THAT(d(depthAt(-cam.nearPlane)), WithinAbs(0.0, 1e-5));
    CHECK_THAT(d(depthAt(-cam.farPlane)), WithinAbs(1.0, 1e-5));
    const float mid = depthAt(-10.0f);
    CHECK(mid > 0.0f);
    CHECK(mid < 1.0f);
    // A point at +Y appears above the centre.
    const glm::vec4 up = proj * glm::vec4(0.0f, 1.0f, -10.0f, 1.0f);
    CHECK(up.y / up.w > 0.0f);
}

TEST_CASE("Scene::addMesh bumps meshVersion and addEntity references it", "[scene]") {
    Scene scene;
    CHECK(scene.meshVersion == 0);
    const MeshId a = scene.addMesh(makeCube(1.0f));
    const MeshId b = scene.addMesh(makePlane(1.0f, 1));
    CHECK(a == 0);
    CHECK(b == 1);
    CHECK(scene.meshVersion == 2);
    Entity& e = scene.addEntity("thing", b);
    CHECK(e.name == "thing");
    CHECK(e.mesh == b);
    CHECK(e.visible);
    CHECK(scene.entities.size() == 1);
}

TEST_CASE("OrbScene registers all parameters and default routes", "[scene][orb]") {
    params::ParameterSet params;
    params::Modulator modulator;
    OrbScene orb(params, modulator);

    for (const char* path : {"orb/scale", "orb/rotationSpeed", "orb/emissive", "orb/impulse", "orb/baseColor",
                             "orb/emissiveColor", "scene/brightness", "scene/gridIntensity",
                             "camera/distance", "camera/height", "camera/orbitSpeed"}) {
        INFO(path);
        CHECK(params.find(path) != nullptr);
    }
    CHECK(params.size() == 33); // 11 orb/scene/camera + 22 particles/sparks
    CHECK(orb.scale().value() == 1.0f);
    CHECK(orb.scale().softMax(0) == 3.0f);
    CHECK(orb.scale().hardMax(0) == 8.0f);
    CHECK(orb.baseColor().kind() == params::ParamKind::Color);
    CHECK(orb.cameraDistance().base() == 7.0f);

    REQUIRE(modulator.routes().size() == 8); // five orb routes + three sparks routes
    for (const params::ModRoute& route : modulator.routes()) {
        INFO(route.source << " -> " << route.target);
        CHECK(params.find(route.target) != nullptr);
    }
    CHECK(modulator.routes()[0].source == "audio.bass");
    CHECK(modulator.routes()[0].target == "orb/scale");
    CHECK(modulator.routes()[0].chain.curve == params::CurveType::Power);
    CHECK(modulator.routes()[4].source == "audio.onset");
    CHECK(modulator.routes()[4].chain.envelope == params::EnvelopeMode::PeakHold);

    // Idempotent by target.
    OrbScene::addDefaultRoutes(modulator);
    CHECK(modulator.routes().size() == 8); // idempotent
    modulator.clearRoutes();
    params::ModRoute custom;
    custom.source = "audio.rms";
    custom.target = "orb/scale";
    modulator.addRoute(custom);
    OrbScene::addDefaultRoutes(modulator);
    CHECK(modulator.routes().size() == 8); // idempotent // existing orb/scale route kept, four added
    CHECK(modulator.routes()[0].source == "audio.rms");

    // Scene content.
    const Scene& scene = orb.scene();
    REQUIRE(scene.meshes.size() == 2);
    CHECK(scene.meshes[0].valid());
    CHECK(scene.meshes[1].valid());
    REQUIRE(scene.entities.size() == 2);
    CHECK(scene.entities[0].name == "orb");
    CHECK(scene.entities[0].style == MeshStyle::Lit);
    CHECK(scene.entities[0].transform.position.y == 1.5f); // 0.5 above the grid + default scale 1
    CHECK(scene.entities[1].name == "grid");
    CHECK(scene.entities[1].style == MeshStyle::Grid);
    CHECK(scene.entities[1].transform.position.y == 0.0f);
}

TEST_CASE("OrbScene update is deterministic with a FixedStepClock", "[scene][orb]") {
    auto runScene = [](int frames) {
        params::ParameterSet params;
        params::Modulator modulator;
        OrbScene orb(params, modulator);
        FixedStepClock clock(60.0);
        for (int i = 0; i < frames; ++i) {
            params.resetFinals();
            orb.update(clock.tick());
        }
        return std::make_pair(orb.scene().entities[0].transform.matrix(), orb.scene().camera.view());
    };
    const auto a = runScene(120);
    const auto b = runScene(120);
    CHECK(a.first == b.first);
    CHECK(a.second == b.second);
    const auto c = runScene(121);
    CHECK(c.first != a.first);
}

TEST_CASE("OrbScene integrates rotation from rotationSpeed", "[scene][orb]") {
    params::ParameterSet params;
    params::Modulator modulator;
    OrbScene orb(params, modulator);
    orb.rotationSpeed().setBase(2.0f);
    orb.cameraOrbitSpeed().setBase(0.5f);
    params.resetFinals();
    FixedStepClock clock(100.0);
    for (int i = 0; i <= 50; ++i) { // first tick has dt = 0, then 50 steps of 0.01 s
        orb.update(clock.tick());
    }
    CHECK_THAT(d(orb.currentAngle()), WithinAbs(1.0, 1e-4));
    CHECK_THAT(d(orb.currentCameraAngle()), WithinAbs(0.25, 1e-4));
    const glm::vec3 pos = orb.scene().camera.position;
    CHECK_THAT(d(pos.x), WithinAbs(std::sin(0.25) * 7.0, 1e-4));
    CHECK_THAT(d(pos.z), WithinAbs(std::cos(0.25) * 7.0, 1e-4));
    CHECK_THAT(d(pos.y), WithinAbs(2.2, 1e-6));
    CHECK(orb.scene().camera.target == glm::vec3(0.0f, 1.2f, 0.0f)); // orb centre (0.5 + scale 1) - 0.3
}

TEST_CASE("OrbScene writes parameters into the scene", "[scene][orb]") {
    params::ParameterSet params;
    params::Modulator modulator;
    OrbScene orb(params, modulator);
    orb.scale().setBase(2.0f);
    orb.impulse().setBase(0.5f);
    orb.brightness().setBase(2.5f);
    orb.gridIntensity().setBase(1.5f);
    orb.emissive().setBase(3.0f);
    orb.baseColor().setBase(glm::vec3(0.1f, 0.2f, 0.3f));
    orb.emissiveColor().setBase(glm::vec3(0.4f, 0.5f, 0.6f));
    orb.cameraHeight().setBase(4.0f);
    params.resetFinals();
    orb.update(FrameTime{});

    const Scene& scene = orb.scene();
    CHECK(scene.entities[0].transform.scale == glm::vec3(2.5f));
    CHECK(scene.environment.brightness == 2.5f);
    CHECK(scene.environment.gridIntensity == 1.5f);
    CHECK(scene.entities[0].material.emissiveIntensity == 3.0f);
    CHECK(scene.entities[0].material.baseColor == glm::vec3(0.1f, 0.2f, 0.3f));
    CHECK(scene.entities[0].material.emissiveColor == glm::vec3(0.4f, 0.5f, 0.6f));
    CHECK(scene.camera.position.y == 4.0f);

    // Modulated finals (not base) drive the scene.
    signals::SignalBus bus;
    const auto bass = bus.declare("audio.bass");
    bus.declare("audio.mid");
    bus.declare("audio.treble");
    bus.declare("audio.rms");
    bus.declare("audio.onset", 0.0f, 1.0f, true);
    REQUIRE(modulator.bind(bus, params).has_value());
    bus.set(bass, 1.0f);
    modulator.evaluate(bus, params, 1.0); // long dt: smoothing settles in one step
    orb.update(FrameTime{});
    CHECK(orb.scene().entities[0].transform.scale.x > 2.5f);
    CHECK(orb.scale().base() == 2.0f);
}
