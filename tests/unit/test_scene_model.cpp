#include "scene/mesh_generators.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <glm/gtc/matrix_transform.hpp>

using namespace avgen::scene;
using Catch::Matchers::WithinAbs;

TEST_CASE("Transform::fromMatrix round-trips TRS", "[scene][model]") {
    Transform t;
    t.position = {1.0f, -2.0f, 3.0f};
    t.rotation = glm::angleAxis(0.7f, glm::normalize(glm::vec3(0.2f, 1.0f, 0.3f)));
    t.scale = {2.0f, 0.5f, 1.5f};
    const Transform back = Transform::fromMatrix(t.matrix());
    const glm::mat4 a = t.matrix();
    const glm::mat4 b = back.matrix();
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            CHECK_THAT(static_cast<double>(b[c][r]), WithinAbs(static_cast<double>(a[c][r]), 1e-4));
        }
    }
    CHECK_THAT(static_cast<double>(back.position.y), WithinAbs(-2.0, 1e-5));
}

TEST_CASE("MeshData bounds and normals", "[scene][model]") {
    MeshData cube = makeCube(1.5f);
    const auto [lo, hi] = cube.bounds();
    CHECK(lo == glm::vec3(-1.5f));
    CHECK(hi == glm::vec3(1.5f));
    for (auto& v : cube.vertices) {
        v.normal = glm::vec3(0.0f);
    }
    cube.computeNormals();
    for (const auto& v : cube.vertices) {
        CHECK_THAT(static_cast<double>(glm::length(v.normal)), WithinAbs(1.0, 1e-5));
        // Flat-shaded cube: the smooth normal of each face vertex still points out of the cube.
        CHECK(glm::dot(v.normal, glm::normalize(v.position)) > 0.5f);
    }
    MeshData empty;
    CHECK(empty.bounds().first == glm::vec3(0.0f));
}

TEST_CASE("Scene bounds, textures, lights and clear", "[scene][model]") {
    Scene scene;
    const auto mesh = scene.addMesh(makeCube(1.0f));
    auto& a = scene.addEntity("a", mesh);
    a.transform.position = {10.0f, 0.0f, 0.0f};
    auto& b = scene.addEntity("b", mesh);
    b.transform.scale = glm::vec3(2.0f);
    scene.addEntity("hidden", mesh).visible = false;
    const auto [lo, hi] = scene.bounds();
    CHECK(lo == glm::vec3(-2.0f));
    CHECK(hi == glm::vec3(11.0f, 2.0f, 2.0f));

    TextureData tex;
    tex.width = 2;
    tex.height = 2;
    tex.data.assign(16, 255);
    CHECK(tex.valid());
    tex.format = TextureFormat::Rgba32Float;
    CHECK_FALSE(tex.valid());
    tex.format = TextureFormat::Rgba8Srgb;
    const auto before = scene.textureVersion;
    CHECK(scene.addTexture(tex) == 0);
    CHECK(scene.textureVersion == before + 1);

    PunctualLight light;
    light.type = PunctualLight::Type::Point;
    light.intensity = 5.0f;
    CHECK(scene.addLight(light).intensity == 5.0f);
    CHECK(scene.lights.size() == 1);

    scene.clear();
    CHECK(scene.entities.empty());
    CHECK(scene.meshes.empty());
    CHECK(scene.textures.empty());
    CHECK(scene.lights.empty());
    CHECK(scene.bounds().second == glm::vec3(0.0f));
}
