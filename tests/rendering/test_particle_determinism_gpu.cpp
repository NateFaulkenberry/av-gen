// ADR-360's particle determinism relaxation, and the two mitigations it traded for.
//
// ADR-091 says scrub must equal play. ADR-360 relaxed that for particle systems only, on the
// owner's decision, and named two things that had to be true in exchange:
//
//   1. The spawn RNG must be keyed to a position on the TIMELINE, not to how many frames have been
//      drawn since this render started. `FrameTime::frameNonce()` exists for exactly that. While
//      the key was `frameIndex`, the same second of the same scene seeded different spawns in a
//      full render (frame 104), a render of the last five seconds (frame 4), and the live
//      application (frame 17 431, because RealtimeClock never resets the counter) -- so an offline
//      render did not agree with the frame the owner was looking at when they pressed render.
//   2. A render whose range starts at t > 0 must be able to begin with populated pools instead of
//      blooming in from nothing: a bounded, opt-in warm-up.
//
// These tests are the probes for both. Each one is built so that it fails if the mitigation is
// absent AND fails if nothing ran at all (ADR-182): every "these two agree" assertion is paired
// with a control that must disagree, over the same pixels of the same scene.

#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/particle_renderer.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/scene.hpp"
#include "support/image_diff.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
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

constexpr double kFps = 60.0;
constexpr double kDt = 1.0 / kFps;

// A drizzle: spawned across a wide box, falling, living exactly `life` seconds. Nothing here is
// stochastic except the spawn randomness itself -- no turbulence, no field forces, no wind -- so a
// difference between two frames of this scene is a difference in which particles exist and where,
// and cannot be anything else.
scene::Scene drizzle(float life = 0.5f, float rate = 6000.0f) {
    scene::Scene s;
    s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    s.environment.showSkybox = false;
    s.environment.environmentIntensity = 0.0f;
    s.camera.position = {0.0f, 0.0f, 7.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    scene::ParticleSystem sys;
    sys.name = "drizzle";
    sys.enabled = true;
    sys.capacity = 8192;
    sys.shape = scene::EmitterShape::Box;
    sys.position = {0.0f, 2.0f, 0.0f};
    sys.extent = {3.0f, 0.05f, 0.05f};
    sys.spawnRate = rate;
    sys.burst = 0.0f;
    sys.lifetimeMin = life;
    sys.lifetimeMax = life;
    sys.speedMin = 0.8f;
    sys.speedMax = 1.4f;
    sys.direction = {0.0f, -1.0f, 0.0f};
    sys.spread = 0.35f;
    sys.gravity = {0.0f, -1.5f, 0.0f};
    sys.turbulence = 0.0f;
    sys.drag = 0.0f;
    sys.sizeStart = 0.05f;
    sys.sizeEnd = 0.05f;
    sys.colorStart = {1.0f, 1.0f, 1.0f, 1.0f};
    sys.colorEnd = {1.0f, 1.0f, 1.0f, 1.0f};
    sys.emissive = 1.0f;
    sys.trailStride = 1;
    s.particles.push_back(sys);
    return s;
}

FrameTime at(double renderTime, std::uint64_t frameIndex, double dt = kDt) {
    FrameTime t{};
    t.renderTime = renderTime;
    t.deltaTime = dt;
    t.frameIndex = frameIndex;
    return t;
}

// Lit pixels, so "the two frames agree" can be told apart from "both frames are black".
std::size_t litPixels(const gpu::Image8& img) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < img.rgba.size(); i += 4) {
        if (std::max({img.rgba[i], img.rgba[i + 1], img.rgba[i + 2]}) > 24) {
            ++n;
        }
    }
    return n;
}

gpu::Image8 oneFrame(gpu::Context& ctx, const scene::Scene& s, const FrameTime& t, std::uint32_t size = 192) {
    gpu::ShaderLibrary shaders(ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(ctx, shaders);
    REQUIRE(renderer.init().has_value());
    auto img = renderer.renderToImage(s, t, size, size);
    REQUIRE(img.has_value());
    return *img;
}

} // namespace

TEST_CASE("The spawn RNG is keyed to the timeline, not to frames since the render started",
          "[gpu][particles][determinism]") {
    auto ctx = makeContext();
    const scene::Scene s = drizzle();

    // Three first frames of three FRESH renderers. A fresh renderer has empty pools and no history,
    // so the only thing that can differ between these arms is what the uniforms carry.
    //
    //   a and b are the SAME timeline second with different frame indices. That is precisely the
    //   difference between a live frame and the same second of an offline render, and between a
    //   full render and a re-render of a section.
    const gpu::Image8 a = oneFrame(*ctx, s, at(1.75, 0));
    const gpu::Image8 b = oneFrame(*ctx, s, at(1.75, 97));
    //   c is a DIFFERENT timeline second at the same frame index: the control.
    const gpu::Image8 c = oneFrame(*ctx, s, at(1.75 + 0.37, 0));

    // Neither-ran guard. If the emitter is silent every comparison below is a comparison of two
    // black frames, which agree for a reason that has nothing to do with this test.
    INFO("lit pixels: a=" << litPixels(a) << " b=" << litPixels(b) << " c=" << litPixels(c));
    REQUIRE(litPixels(a) > 200);
    REQUIRE(litPixels(b) > 200);
    REQUIRE(litPixels(c) > 200);

    // The claim: the frame index no longer reaches the spawn hash.
    {
        const auto d = testing::byteDiff(a.rgba, b.rgba);
        INFO("same second, frame 0 vs frame 97: " << d.describe());
        REQUIRE(d.identical());
    }
    // The control that makes the claim falsifiable: the spawn hash is still keyed to SOMETHING that
    // moves along the timeline. Without this, a shader that returned a constant would pass the
    // assertion above, and so would a scene in which nothing is emitted at all.
    {
        const auto d = testing::byteDiff(a.rgba, c.rgba);
        INFO("1.75 s vs 2.12 s, both frame 0: " << d.describe());
        REQUIRE_FALSE(d.identical());
    }
}

TEST_CASE("A bounded warm-up gives a partial render the pool a full render would have had",
          "[gpu][particles][determinism]") {
    auto ctx = makeContext();
    // Lifetime 0.5 s = 30 frames at 60. A warm-up of 36 frames therefore covers every particle that
    // can still be alive at the head of the range, which is what makes the two arms comparable at
    // all: a warm-up shorter than the lifetime would be expected to fall short and would prove
    // nothing about whether it ran.
    constexpr float kLife = 0.5f;
    constexpr std::uint32_t kWarmFrames = 36;
    constexpr double kHead = 2.0;
    const scene::Scene s = drizzle(kLife);

    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});

    // Arm 1 -- the full render: play from 0 to kHead, one frame at a time, in one renderer.
    std::uint32_t fullAlive = 0;
    {
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        const auto frames = static_cast<std::uint64_t>(kHead * kFps + 0.5);
        for (std::uint64_t f = 1; f <= frames; ++f) {
            auto img = renderer.renderToImage(s, at(static_cast<double>(f) * kDt, f), 64, 64);
            REQUIRE(img.has_value());
        }
        auto counts = renderer.particles().readCounts(0);
        REQUIRE(counts.has_value());
        fullAlive = counts->alive;
    }

    // Arm 2 -- a partial render whose range starts at kHead, with no warm-up: today's behaviour.
    std::uint32_t coldAlive = 0;
    {
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        renderer.setParticleWarmUpFrames(0);
        auto img = renderer.renderToImage(s, at(kHead, 0), 64, 64);
        REQUIRE(img.has_value());
        auto counts = renderer.particles().readCounts(0);
        REQUIRE(counts.has_value());
        coldAlive = counts->alive;
    }

    // Arm 3 -- the same partial render with the warm-up switched on.
    std::uint32_t warmAlive = 0;
    {
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        renderer.setParticleWarmUpFrames(kWarmFrames);
        auto img = renderer.renderToImage(s, at(kHead, 0), 64, 64);
        REQUIRE(img.has_value());
        auto counts = renderer.particles().readCounts(0);
        REQUIRE(counts.has_value());
        warmAlive = counts->alive;
    }

    INFO("alive at t=" << kHead << ": full=" << fullAlive << " cold=" << coldAlive << " warm=" << warmAlive);

    // The full render is in steady state: roughly spawnRate * lifetime particles. If it is not, the
    // scene is wrong and the rest of this test compares two numbers that mean nothing.
    REQUIRE(fullAlive > 2000);

    // The defect, as a number: a render starting at the head of its range carries one frame's worth
    // of spawns instead of a lifetime's.
    REQUIRE(coldAlive * 10 < fullAlive);

    // The mitigation, as a number. Exactly equal, not approximately: the emitted count per frame is
    // a pure function of (renderTime, deltaTime, spawnRate), and every particle alive at kHead was
    // born inside the warm-up window, so the two arms ran the same emissions and the same kills.
    REQUIRE(warmAlive == fullAlive);
}
