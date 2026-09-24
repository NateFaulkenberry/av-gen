// ADR-369: the comet's coma had infinite support inside a half-space test through the eye.
//
// The owner reported a hard straight line across the sky. The mechanism: the head's contribution
// sits inside `if (th > 0.0)` where `th = dot(head - ro, rd)`, and the coma inside it is
// `((hr*hr)/(perp*perp + hr*hr))^2` -- an inverse-square wash squared, which is small at distance
// but never zero. A half-space through the camera projects to an EXACTLY straight line, so the
// wash was stepping from a finite value to nothing along it.
//
// This guards the property that fixes it: the comet's sky contribution has no hard edge in it. The
// test measures the sharpest adjacent-pixel step anywhere in the sky and holds it well below the
// comet's own peak. Before the window the ratio was about 1 in 4; after it, far smaller.

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/scene.hpp"
#include "world/atmospherics.hpp"
#include "world/effects/effect_instance.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <vector>

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

float luma(const gpu::Image8& img, std::uint32_t x, std::uint32_t y) {
    const std::size_t i = (static_cast<std::size_t>(y) * img.width + x) * 4;
    return 0.2126f * img.rgba[i] + 0.7152f * img.rgba[i + 1] + 0.0722f * img.rgba[i + 2];
}

struct SkyStats {
    float peak = 0.0f;
    float sharpestStep = 0.0f;
};

SkyStats skyStats(const gpu::Image8& img) {
    SkyStats s;
    for (std::uint32_t y = 0; y < img.height; ++y) {
        for (std::uint32_t x = 0; x + 1 < img.width; ++x) {
            const float a = luma(img, x, y);
            const float b = luma(img, x + 1, y);
            s.peak = std::max(s.peak, a);
            s.sharpestStep = std::max(s.sharpestStep, std::abs(b - a));
        }
    }
    return s;
}

// Empty sky with one comet, framed so the head is off to one side -- which is exactly the geometry
// that puts the `th = 0` plane across the frame.
scene::Scene cometSky(double seconds) {
    scene::Scene s;
    s.environment.backgroundColor = {0.004f, 0.006f, 0.018f};
    s.camera.position = {0.0f, 0.0f, 0.0f};
    s.camera.target = {1.0f, 0.0f, 0.0f};

    world::EffectInstance e = world::bioluminescentComet("probe");
    e.comet.path.distance = 400.0f;
    e.comet.appearance.haloSize = 95.0f;
    e.comet.appearance.haloIntensity = 3.8f;
    e.comet.appearance.coreIntensity = 20.0f;
    e.comet.appearance.tailLength = 2000.0f;
    e.activation = world::Activation::Always;

    const std::vector<world::EffectInstance> effects{e};
    world::EffectContext ctx;
    ctx.seconds = seconds;
    ctx.cameraPosition = s.camera.position;
    world::buildAtmosphericFrame(effects, ctx, s.atmospherics);
    return s;
}

} // namespace

TEST_CASE("The comet's coma has no hard edge in the sky", "[gpu][atmospherics][comet]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    // Several seconds along the arc, because the defect was intermittent in exactly the way a
    // moving boundary is: it only shows while the plane crosses the frame. One frame could pass by
    // luck, and a probe that can pass by luck is not a probe.
    bool sawComet = false;
    for (const double t : {1.0, 2.0, 3.0, 4.0, 5.0}) {
        const scene::Scene s = cometSky(t);
        FixedStepClock clock(30.0);
        clock.restartAt(t);
        auto img = renderer.renderToImage(s, clock.tick(), 160, 90);
        REQUIRE(img.has_value());
        const SkyStats st = skyStats(*img);
        INFO("t=" << t << " peak " << st.peak << " sharpest step " << st.sharpestStep);
        if (st.peak > 8.0f) {
            sawComet = true;
        }
        // A smooth wash across a 160-wide frame cannot step by a large fraction of its own peak
        // between two adjacent pixels. The pre-fix frame did exactly that along the `th = 0` line.
        CHECK(st.sharpestStep < std::max(0.25f * st.peak, 6.0f));
    }
    // THE CONTROL that stops the loop above passing vacuously: if the comet never drew anything,
    // every frame is flat, every step is zero, and the assertion is meaningless.
    CHECK(sawComet);
    CHECK(ctx->errorCount() == 0);
}
