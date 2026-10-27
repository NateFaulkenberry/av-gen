// ADR-1806: Scene's per-mesh cache -- bounds and index validity -- and Scene::bounds(), which reads both every frame.

#include "scene/mesh_generators.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdio>

using namespace avgen::scene;

namespace {

// A scene whose meshes are valid except one whose last index points past its vertices.
Scene mixedScene() {
    Scene s;
    for (int i = 0; i < 4; ++i) {
        const MeshId m = s.addMesh(makePlane(1.0f + static_cast<float>(i), 8));
        s.addEntity("plane" + std::to_string(i), m).transform.position = glm::vec3(static_cast<float>(i) * 10.0f, 0, 0);
    }
    MeshData broken = makeCube(1.0f);
    broken.indices.back() = static_cast<std::uint32_t>(broken.vertices.size() + 5);
    const MeshId b = s.addMesh(std::move(broken));
    s.addEntity("broken", b).transform.position = glm::vec3(1000.0f, 0, 0); // would widen bounds if counted
    return s;
}

} // namespace

TEST_CASE("cached mesh validity agrees with MeshData::valid for every mesh, and out of range is false",
          "[scene][adr1806]") {
    const Scene s = mixedScene();
    for (MeshId m = 0; m < s.meshes.size(); ++m) {
        INFO("mesh " << m);
        CHECK(s.meshValid(m) == s.meshes[m].valid());
        CHECK(s.meshBounds(m) == s.meshes[m].bounds());
    }
    CHECK_FALSE(s.meshValid(static_cast<MeshId>(s.meshes.size())));
    CHECK_FALSE(s.meshValid(kInvalidMesh));
    // the invalid mesh is still excluded from the scene's bounds, exactly as before
    const auto [lo, hi] = s.bounds();
    CHECK(hi.x < 100.0f);
}

TEST_CASE("Scene::bounds asks each mesh's validity once per meshVersion, not once per frame",
          "[scene][adr1806]") {
    Scene s = mixedScene();
    (void)s.bounds();
    const std::uint64_t before = s.meshBoundsRebuilds();
    for (int frame = 0; frame < 100; ++frame) {
        (void)s.bounds();
    }
    CHECK(s.meshBoundsRebuilds() == before);
}

TEST_CASE("an edit invalidates: version bump, valid to invalid in place, and resizes without a bump",
          "[scene][adr1806]") {
    Scene s;
    const MeshId m = s.addMesh(makeCube(1.0f));
    s.addEntity("cube", m);
    REQUIRE(s.meshValid(m));

    // valid -> invalid in place, said the way the engine says it (a meshVersion bump)
    s.meshes[m].indices[0] = static_cast<std::uint32_t>(s.meshes[m].vertices.size());
    ++s.meshVersion;
    CHECK_FALSE(s.meshValid(m));
    CHECK(s.meshValid(m) == s.meshes[m].valid());
    const auto [lo0, hi0] = s.bounds();
    CHECK(lo0 == hi0); // nothing valid and lit: the empty bounds, as before

    // invalid -> valid again
    s.meshes[m].indices[0] = 0;
    ++s.meshVersion;
    CHECK(s.meshValid(m));

    // an edit that skips the bump but changes the mesh's size is still seen (the per-mesh fingerprint)
    s.meshes[m].indices.push_back(static_cast<std::uint32_t>(s.meshes[m].vertices.size() + 1));
    s.meshes[m].indices.push_back(0);
    s.meshes[m].indices.push_back(1);
    CHECK_FALSE(s.meshValid(m));
    CHECK(s.meshValid(m) == s.meshes[m].valid());

    // and so is a mesh replaced wholesale without a bump
    s.meshes[m] = makePlane(3.0f, 4);
    CHECK(s.meshValid(m));
    CHECK(s.meshBounds(m) == s.meshes[m].bounds());

    // a new mesh added (with or without addMesh) is covered
    s.meshes.push_back(makeCube(2.0f));
    CHECK(s.meshValid(static_cast<MeshId>(s.meshes.size() - 1)));
}

TEST_CASE("Scene::bounds per-frame cost no longer scales with index count", "[scene][adr1806][performance]") {
    Scene s;
    for (int i = 0; i < 16; ++i) {
        const MeshId m = s.addMesh(makePlane(100.0f, 400)); // 320k triangles each, 5.1M total
        s.addEntity("chunk" + std::to_string(i), m);
    }
    (void)s.bounds(); // the one rebuild
    const auto t0 = std::chrono::steady_clock::now();
    constexpr int kFrames = 200;
    for (int f = 0; f < kFrames; ++f) {
        (void)s.bounds();
    }
    const double perFrameMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / kFrames;
    // uncached: what bounds() paid per frame before -- every index of every mesh
    const auto u0 = std::chrono::steady_clock::now();
    std::size_t valid = 0;
    for (const MeshData& m : s.meshes) valid += m.valid() ? 1u : 0u;
    const double uncachedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - u0).count();
    std::printf("ADR-1806: Scene::bounds over 5.1M triangles: cached %.4f ms/frame, uncached validity scan %.3f ms\n",
                perFrameMs, uncachedMs);
    CHECK(valid == 16u);
    CHECK(perFrameMs < 0.05); // 16 entities: microseconds, not the milliseconds the index walk cost
}
