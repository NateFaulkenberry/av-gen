// The scene pass's group 0 after the particle draw (115 Stage A, the Neon Dreams headless live-profile bug).
//
// `ParticleRenderer::draw` binds each system's own render group at group 0, so the blended draws after it need the
// frame group bound again. That rebind used to be asked of `particles_->stats().systems`, which counts the systems
// update() stepped -- and the live distance cull (ADR-1098) skips a far system in update() without stopping draw()
// from binding for it. A frame whose only particle systems were all culled therefore drew its blended meshes through
// the particles' group: `avgen --live-profile --mode headless` on Neon Dreams logged "Bind group layout frame-layout
// of pipeline layout skinned-pipeline-layout does not match layout particles-render-layout ... set at group index 0"
// and submitted an invalid command buffer every frame. A plain blended entity takes the same rebind as a blended
// skinned one, so it is the fixture here.

#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <memory>

using namespace avgen;

namespace {

std::unique_ptr<gpu::Context> makeCtx() {
    static bool logInit = false;
    if (!logInit) {
        log::init(log::Level::Warn);
        logInit = true;
    }
    auto ctx = gpu::Context::create(gpu::ContextDesc{});
    if (!ctx) {
        SKIP("no GPU adapter available: " << ctx.error().message);
    }
    return std::move(*ctx);
}

scene::MeshData box(float h) {
    scene::MeshData m;
    const glm::vec3 n[6] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    for (const glm::vec3& normal : n) {
        const glm::vec3 u = std::abs(normal.y) > 0.5f ? glm::vec3(1, 0, 0) : glm::cross(glm::vec3(0, 1, 0), normal);
        const glm::vec3 v = glm::cross(normal, u);
        const auto base = static_cast<std::uint32_t>(m.vertices.size());
        m.vertices.push_back({normal * h - u * h - v * h, normal, {0, 0}});
        m.vertices.push_back({normal * h + u * h - v * h, normal, {1, 0}});
        m.vertices.push_back({normal * h + u * h + v * h, normal, {1, 1}});
        m.vertices.push_back({normal * h - u * h + v * h, normal, {0, 1}});
        m.indices.insert(m.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    return m;
}

// A ground, an opaque cube, a blended cube in front of it, and ONE particle system, far away.
scene::Scene blendedWithFarParticles() {
    scene::Scene s;
    s.environment.showSkybox = false;
    s.environment.sky.enabled = false;
    const auto cube = s.addMesh(box(1.0f));
    auto& g = s.addEntity("ground", cube);
    g.transform.scale = {30.0f, 0.1f, 30.0f};
    auto& solid = s.addEntity("solid", cube);
    solid.transform.position = {0.0f, 1.0f, -2.0f};
    auto& glass = s.addEntity("glass", cube);
    glass.transform.position = {0.0f, 1.0f, 1.0f};
    glass.transform.scale = glm::vec3(0.6f);
    glass.material.alphaMode = scene::AlphaMode::Blend;
    glass.material.opacity = 0.4f;
    s.camera.position = {0.0f, 2.0f, 6.0f};
    s.camera.target = {0.0f, 1.0f, 0.0f};
    scene::PunctualLight key;
    key.direction = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.6f));
    key.intensity = 3.0f;
    s.addLight(key);
    scene::ParticleSystem far;
    far.name = "far";
    far.capacity = 1024;
    far.position = {0.0f, 1.0f, -150.0f};
    s.particles = {far};
    return s;
}

} // namespace

TEST_CASE("a blended draw after a particle pass whose every system was distance-culled binds the frame group",
          "[gpu][particles][live-optimizer]") {
    auto ctx = makeCtx();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    const scene::Scene scene = blendedWithFarParticles();
    FrameTime time{};
    time.deltaTime = 1.0 / 60.0;

    // The control: no cull, the system is stepped and counted, and the rebind was always taken.
    REQUIRE(renderer.renderToImage(scene, time, 96, 64).has_value());
    CHECK(renderer.stats().particles.systems == 1);
    CHECK(ctx->errorCount() == 0);

    // The arm: the live cull skips the only system, so update() counts none -- and draw() still binds for it.
    rendering::QualitySettings q = renderer.qualitySettings();
    q.particleCullDistance = 60.0f;
    renderer.setQualitySettings(q);
    for (int i = 0; i < 3; ++i) {
        ++time.frameIndex;
        REQUIRE(renderer.renderToImage(scene, time, 96, 64).has_value());
        CHECK(renderer.stats().particles.culledByDistance == 1);
        CHECK(renderer.stats().particles.systems == 0);
    }
    CHECK(ctx->errorCount() == 0);
}
