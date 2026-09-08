// GPU particle system: emission, simulation and indirect drawing produce visible, bounded output.
#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <memory>

using namespace avgen;

namespace {
std::unique_ptr<gpu::Context> makeContext() {
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

int brightness(const gpu::Image8& img) {
    long sum = 0;
    for (std::size_t i = 0; i < img.rgba.size(); i += 4) {
        sum += img.rgba[i] + img.rgba[i + 1] + img.rgba[i + 2];
    }
    return static_cast<int>(sum / static_cast<long>(img.width * img.height));
}
} // namespace

TEST_CASE("Particles emit, live, and die according to their parameters", "[gpu][particles]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    scene::Scene s;
    s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    s.camera.position = {0.0f, 0.0f, 6.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    scene::ParticleSystem sys;
    sys.name = "test";
    sys.capacity = 4096;
    sys.shape = scene::EmitterShape::Sphere;
    sys.position = {0.0f, 0.0f, 0.0f};
    sys.extent = {1.0f, 1.0f, 1.0f};
    sys.spawnRate = 20000.0f;
    sys.lifetimeMin = 0.5f;
    sys.lifetimeMax = 0.5f;
    sys.speedMin = 1.0f;
    sys.speedMax = 2.0f;
    sys.spread = 1.0f;
    sys.gravity = {0.0f, 0.0f, 0.0f};
    sys.turbulence = 0.0f;
    sys.sizeStart = 0.15f;
    sys.sizeEnd = 0.15f;
    sys.colorStart = {1.0f, 0.5f, 0.1f, 1.0f};
    sys.colorEnd = {1.0f, 0.5f, 0.1f, 1.0f};
    sys.emissive = 2.0f;
    s.particles.push_back(sys);

    FixedStepClock clock(60.0);
    // Frame 0: nothing alive yet (first tick has dt = 0 -> no emission).
    auto first = renderer.renderToImage(s, clock.tick(), 64, 64);
    REQUIRE(first.has_value());
    CHECK(ctx->errorCount() == 0);
    const int b0 = brightness(*first);

    int bMid = 0;
    for (int i = 0; i < 12; ++i) {
        auto img = renderer.renderToImage(s, clock.tick(), 64, 64);
        REQUIRE(img.has_value());
        bMid = brightness(*img);
    }
    CHECK(bMid > b0 + 5);
    CHECK(renderer.stats().particles.systems == 1);
    CHECK(renderer.stats().particles.capacity == 4096);
    CHECK(renderer.stats().particles.emittedThisFrame > 0);

    // Stop emitting: after more than one lifetime everything is dead again.
    s.particles[0].spawnRate = 0.0f;
    int bEnd = bMid;
    for (int i = 0; i < 45; ++i) { // 0.75 s > 0.5 s lifetime
        auto img = renderer.renderToImage(s, clock.tick(), 64, 64);
        REQUIRE(img.has_value());
        bEnd = brightness(*img);
    }
    CHECK(bEnd <= b0 + 1);
    CHECK(ctx->errorCount() == 0);

    // Disabled systems draw nothing and are not simulated.
    s.particles[0].spawnRate = 20000.0f;
    s.particles[0].enabled = false;
    auto off = renderer.renderToImage(s, clock.tick(), 64, 64);
    REQUIRE(off.has_value());
    CHECK(renderer.stats().particles.systems == 0);
}

TEST_CASE("Particle burst produces an immediate flash and capacity bounds emission", "[gpu][particles]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    scene::Scene s;
    s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    s.camera.position = {0.0f, 0.0f, 6.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    scene::ParticleSystem sys;
    sys.capacity = 256;
    sys.shape = scene::EmitterShape::Sphere;
    sys.extent = {0.5f, 0.5f, 0.5f};
    sys.spawnRate = 0.0f;
    sys.lifetimeMin = sys.lifetimeMax = 2.0f;
    sys.turbulence = 0.0f;
    sys.gravity = {0.0f, 0.0f, 0.0f};
    sys.sizeStart = sys.sizeEnd = 0.2f;
    sys.emissive = 3.0f;
    s.particles.push_back(sys);
    FixedStepClock clock(60.0);
    auto quiet = renderer.renderToImage(s, clock.tick(), 48, 48);
    REQUIRE(quiet.has_value());
    s.particles[0].burst = 100000.0f; // far beyond capacity: clamped, no GPU errors
    auto flash = renderer.renderToImage(s, clock.tick(), 48, 48);
    REQUIRE(flash.has_value());
    CHECK(renderer.stats().particles.emittedThisFrame == 256);
    CHECK(brightness(*flash) > brightness(*quiet) + 10);
    s.particles[0].burst = 0.0f;
    auto after = renderer.renderToImage(s, clock.tick(), 48, 48);
    REQUIRE(after.has_value());
    CHECK(renderer.stats().particles.emittedThisFrame == 0);
    CHECK(brightness(*after) > brightness(*quiet) + 10); // still alive
    CHECK(ctx->errorCount() == 0);
    if (const char* dumpDir = std::getenv("AVGEN_DUMP_DIR")) {
        REQUIRE(gpu::writePpm(*after, std::filesystem::path(dumpDir) / "particles.ppm").has_value());
    }
}

// Hidden performance probe: run with `avgen_render_tests "[.perf]"`. Reports GPU time per frame
// for a one-million-particle pool at 1280x720; no assertions beyond validity.
TEST_CASE("One million particles simulate and draw", "[.perf][particles]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    scene::Scene s;
    s.camera.position = {0.0f, 0.0f, 8.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    scene::ParticleSystem sys;
    sys.capacity = 1u << 20;
    sys.shape = scene::EmitterShape::Sphere;
    sys.extent = {2.0f, 2.0f, 2.0f};
    sys.spawnRate = 400000.0f;
    sys.lifetimeMin = 2.0f;
    sys.lifetimeMax = 3.0f;
    sys.turbulence = 1.0f;
    sys.sizeStart = 0.01f;
    sys.sizeEnd = 0.0f;
    s.particles.push_back(sys);
    FixedStepClock clock(60.0);
    double gpuSum = 0.0;
    int counted = 0;
    for (int i = 0; i < 90; ++i) {
        auto img = renderer.renderToImage(s, clock.tick(), 1280, 720);
        REQUIRE(img.has_value());
        if (i >= 30 && renderer.stats().gpuFrameMs >= 0.0) {
            gpuSum += renderer.stats().gpuFrameMs;
            ++counted;
        }
    }
    CHECK(ctx->errorCount() == 0);
    WARN("1M particles: mean GPU " << (counted ? gpuSum / counted : -1.0) << " ms/frame at 1280x720 (steady state ~"
                                    << std::min<double>(sys.capacity, 400000.0 * 2.5) << " alive)");
}
