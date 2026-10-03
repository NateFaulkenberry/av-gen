// ADR-1094..1098, 1102: the live levers on a real device. Each test proves its lever reaches the frame (a counter or
// the pixels move) and that a hero, or the neutral default, is untouched.

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/resource_stats.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "rendering/sdf_renderer.hpp"
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
    gpu::ContextDesc desc{};
    auto ctx = gpu::Context::create(desc);
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

// A ground, one big caster near the camera and a field of small casters far away.
scene::Scene castersScene(bool unlit = false) {
    scene::Scene s;
    s.environment.showSkybox = false;
    s.environment.sky.enabled = false;
    const auto ground = s.addMesh(box(1.0f));
    auto& g = s.addEntity("ground", ground);
    g.transform.scale = {60.0f, 0.1f, 60.0f};
    g.material.unlit = unlit;
    const auto cube = s.addMesh(box(1.0f));
    auto& big = s.addEntity("big", cube);
    big.transform.position = {0.0f, 1.0f, 0.0f};
    big.material.unlit = unlit;
    for (int k = 0; k < 12; ++k) {
        auto& e = s.addEntity("small" + std::to_string(k), cube);
        e.transform.position = {-11.0f + 2.0f * static_cast<float>(k), 0.2f, -45.0f};
        e.transform.scale = glm::vec3(0.1f);
        e.material.unlit = unlit;
    }
    s.camera.position = {0.0f, 2.0f, 6.0f};
    s.camera.target = {0.0f, 1.0f, 0.0f};
    scene::PunctualLight key;
    key.direction = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.6f));
    key.intensity = 3.0f;
    key.castsShadow = true;
    s.addLight(key);
    return s;
}

} // namespace

TEST_CASE("the caster floor keeps small non-hero casters out of the shadow views", "[gpu][live-optimizer]") {
    auto ctx = makeCtx();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    auto scene = castersScene();
    FrameTime time{};
    REQUIRE(renderer.renderToImage(scene, time, 160, 96).has_value());
    const std::uint32_t all = renderer.stats().shadowCasters;
    CHECK(renderer.stats().shadows.castersBelowFloor == 0);
    rendering::QualitySettings q = renderer.qualitySettings();
    q.shadowCasterMinPixels = 24.0f;
    renderer.setQualitySettings(q);
    REQUIRE(renderer.renderToImage(scene, time, 160, 96).has_value());
    CHECK(renderer.stats().shadows.castersBelowFloor > 0);
    CHECK(renderer.stats().shadowCasters < all);
    // A hero is exempt.
    for (auto& e : scene.entities) {
        e.importance = scene::Importance::Hero;
    }
    REQUIRE(renderer.renderToImage(scene, time, 160, 96).has_value());
    CHECK(renderer.stats().shadows.castersBelowFloor == 0);
    CHECK(renderer.stats().shadowCasters == all);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("an all-unlit frame renders no shadow maps, and a lit one does", "[gpu][live-optimizer]") {
    auto ctx = makeCtx();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    FrameTime time{};
    auto lit = castersScene(false);
    auto litImage = renderer.renderToImage(lit, time, 128, 80);
    REQUIRE(litImage.has_value());
    CHECK_FALSE(renderer.stats().shadows.skippedNothingLit);
    CHECK(renderer.stats().shadowCasters > 0);
    auto unlit = castersScene(true);
    auto first = renderer.renderToImage(unlit, time, 128, 80);
    REQUIRE(first.has_value());
    CHECK(renderer.stats().shadows.skippedNothingLit);
    CHECK(renderer.stats().shadowCasters == 0);
    // The picture is the one a renderer with shadows switched off entirely draws: nothing read the maps.
    rendering::SceneRenderer control(*ctx, shaders);
    REQUIRE(control.init().has_value());
    auto toggles = control.passToggles();
    toggles.shadows = false;
    control.setPassToggles(toggles);
    auto reference = control.renderToImage(unlit, time, 128, 80);
    REQUIRE(reference.has_value());
    CHECK(gpu::hashImage(*first) == gpu::hashImage(*reference));
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("the live levers at their defaults leave the frame exactly as it was", "[gpu][live-optimizer]") {
    auto ctx = makeCtx();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer a(*ctx, shaders);
    REQUIRE(a.init().has_value());
    rendering::SceneRenderer b(*ctx, shaders);
    REQUIRE(b.init().has_value());
    auto scene = castersScene();
    scene.post.motionBlurAmount = 1.0f;
    FrameTime time{};
    rendering::QualitySettings q = b.qualitySettings();
    q.lodBias = 1.0f;
    q.drawDistanceScale = 1.0f;
    q.shadowCasterMinPixels = 0.0f;
    q.postEffectQuality = 1.0f;
    q.particleCullDistance = 0.0f;
    b.setQualitySettings(q);
    auto x = a.renderToImage(scene, time, 128, 80);
    auto y = b.renderToImage(scene, time, 128, 80);
    REQUIRE(x.has_value());
    REQUIRE(y.has_value());
    CHECK(gpu::hashImage(*x) == gpu::hashImage(*y));
    // And the taps lever does reach the motion blur: half the samples, a different picture of a moving camera.
    q.postEffectQuality = 0.25f;
    b.setQualitySettings(q);
    ++time.frameIndex;
    scene.camera.position.x += 0.5f;
    auto x2 = a.renderToImage(scene, time, 128, 80);
    auto y2 = b.renderToImage(scene, time, 128, 80);
    REQUIRE(x2.has_value());
    REQUIRE(y2.has_value());
    CHECK(gpu::hashImage(*x2) != gpu::hashImage(*y2));
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("far particle emitters stop under the cull distance, a hero's never", "[gpu][live-optimizer]") {
    auto ctx = makeCtx();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    auto scene = castersScene();
    scene::ParticleSystem near;
    near.name = "near";
    near.capacity = 1024;
    near.position = {0.0f, 1.0f, 0.0f};
    scene::ParticleSystem far = near;
    far.name = "far";
    far.position = {0.0f, 1.0f, -150.0f};
    scene.particles = {near, far};
    FrameTime time{};
    time.deltaTime = 1.0 / 60.0;
    REQUIRE(renderer.renderToImage(scene, time, 96, 64).has_value());
    CHECK(renderer.stats().particles.culledByDistance == 0);
    CHECK(renderer.stats().particles.simulationSteps == 2);
    rendering::QualitySettings q = renderer.qualitySettings();
    q.particleCullDistance = 60.0f;
    renderer.setQualitySettings(q);
    ++time.frameIndex;
    REQUIRE(renderer.renderToImage(scene, time, 96, 64).has_value());
    CHECK(renderer.stats().particles.culledByDistance == 1);
    CHECK(renderer.stats().particles.simulationSteps == 1);
    scene.particles[1].importance = scene::Importance::Hero;
    ++time.frameIndex;
    REQUIRE(renderer.renderToImage(scene, time, 96, 64).has_value());
    CHECK(renderer.stats().particles.culledByDistance == 0);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("pipelines are counted where they are created and Dawn reports memory", "[gpu][live-optimizer]") {
    auto ctx = makeCtx();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    const gpu::PipelineCounters before = gpu::pipelineCounters();
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    auto scene = castersScene();
    REQUIRE(renderer.renderToImage(scene, FrameTime{}, 96, 64).has_value());
    const gpu::PipelineCounters after = gpu::pipelineCounters();
    CHECK(after.renderPipelines > before.renderPipelines);
    CHECK(after.shaderModules > before.shaderModules);
    // A second identical frame compiles nothing: a steady frame must not.
    REQUIRE(renderer.renderToImage(scene, FrameTime{}, 96, 64).has_value());
    CHECK(gpu::pipelineCounters().compiles() == after.compiles());
    const gpu::MemoryReport m = gpu::memoryReport(ctx->device(), 4);
    REQUIRE(m.available);
    CHECK(m.textureBytes > 0);
    CHECK(m.bufferBytes > 0);
    CHECK(m.textures > 0);
    REQUIRE_FALSE(m.largest.empty());
    CHECK(m.largest.front().bytes >= m.largest.back().bytes);
}
