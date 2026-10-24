// ADR-1650: the water's sky mirror, the GPU half.
//   * at 0 the water is byte-identical to a renderer whose shader has the sky-mirror blocks removed -- on the plain,
//     cinematic and flow-streak paths alike;
//   * on, the aurora overhead appears in the river, in the aurora's own colour, with the screen-space reflection on
//     and with it off (the live ladder's Emergency rung, `waterSsrScale` 0); off, the river shows none of it (the
//     control: neither the march nor the environment cube can see a sky effect);
//   * where every mirror point lands off screen (the camera pitched down, the sky above the frame), it adds nothing:
//     the edge fade hands those pixels back to the environment reflection rather than smearing the frame's edge;
//   * hidden: its cost at 1080p.

#include "support/water_bench.hpp"
#include "support/water_shader_variant.hpp"

#include "rendering/water_renderer.hpp"
#include "world/atmospherics.hpp"
#include "world/effects/effect_instance.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

using namespace avgen;
using namespace avgen::testsupport;
namespace fs = std::filesystem;

namespace {

constexpr const char* kMirrorBegin = "---- sky mirror (ADR-1650) begin ----";
constexpr const char* kMirrorEnd = "---- sky mirror (ADR-1650) end ----";

// Ribbons overhead, world-anchored, bright: the sky effect the river should reflect.
world::AtmosphericFrame auroraSky(double seconds) {
    world::EffectInstance e = world::glowmereAurora("sky");
    e.aurora.shape.anchor = world::SkyAnchor::World;
    e.aurora.shape.anchorPosition = glm::vec3(0.0f);
    e.aurora.shape.curtainHeight = 700.0f;
    e.aurora.shape.ribbons = true;
    e.aurora.shape.ribbonAltitude = 250.0f;
    e.aurora.shape.ribbonScale = 700.0f;
    e.aurora.shape.ribbonWidth = 0.16f;
    e.aurora.shape.ribbonDistance = 8000.0f;
    e.aurora.appearance.intensity = 6.0f;
    e.aurora.audio.sensitivity = 0.0f;
    e.timing.fadeIn = 0.0;
    e.ground.mode = world::GroundGlow::Off; // the only way the aurora reaches the water here is the reflection
    const std::array<world::EffectInstance, 1> set{e};
    world::EffectContext ctx;
    ctx.seconds = seconds;
    world::AtmosphericFrame frame;
    world::buildAtmosphericFrame(set, ctx, frame);
    return frame;
}

struct Rgb {
    double r = 0.0, g = 0.0, b = 0.0;
    [[nodiscard]] double sum() const { return r + g + b; }
};

// The mean of (a - b) over the pixels `mask` selects.
Rgb meanDiff(const gpu::ImageF& a, const gpu::ImageF& b, const std::vector<char>& mask) {
    Rgb out;
    std::size_t n = 0;
    for (std::size_t i = 0; i < mask.size(); ++i) {
        if (!mask[i]) continue;
        out.r += a.rgba[i * 4] - b.rgba[i * 4];
        out.g += a.rgba[i * 4 + 1] - b.rgba[i * 4 + 1];
        out.b += a.rgba[i * 4 + 2] - b.rgba[i * 4 + 2];
        ++n;
    }
    if (n > 0) {
        out.r /= static_cast<double>(n);
        out.g /= static_cast<double>(n);
        out.b /= static_cast<double>(n);
    }
    return out;
}

bool allFinite(const gpu::ImageF& img) {
    return std::all_of(img.rgba.begin(), img.rgba.end(), [](float v) { return std::isfinite(v); });
}

} // namespace

// The packing half: its own lane, zero by default, and the ladder's SSR lever leaves it alone (at Emergency, scale 0,
// the sky stays in the river).
TEST_CASE("the sky mirror packs into its own lane, untouched by the SSR lever", "[gpu][water][adr1650]") {
    scene::WaterSettings on;
    on.skyMirror = 0.8f;
    CHECK(rendering::waterUniformsFrom(on, 0.0f, 1.0f, true).mirror == glm::vec4(0.8f, 0.0f, 0.0f, 0.0f));
    CHECK(rendering::waterUniformsFrom(scene::WaterSettings{}, 0.0f, 1.0f, true).mirror == glm::vec4(0.0f));
    rendering::WaterUniforms u = rendering::waterUniformsFrom(on, 0.0f, 1.0f, true);
    rendering::applyWaterSsrScale(u, 0.0f);
    CHECK(u.cineA.x == 0.0f);
    CHECK(u.mirror.x == 0.8f);
}

TEST_CASE("with the sky mirror at 0 the water is byte-identical to the shader with no sky-mirror code",
          "[gpu][renderer][water][adr1650][identity]") {
    if (!fs::is_regular_file(qaWaterScene())) {
        SKIP("the water QA scene is not present");
    }
    int blocks = 0;
    const std::string stripped = stripMarkedBlocks(readWaterShaderSource(), kMirrorBegin, kMirrorEnd, &blocks);
    INFO(blocks << " marked sky-mirror blocks removed");
    REQUIRE(blocks >= 2);
    REQUIRE(stripped.find("water.mirror") == std::string::npos);
    REQUIRE(stripped.find("mirror: vec4") == std::string::npos);
    const fs::path strippedDir = writeWaterShaderVariant("no-sky-mirror", stripped);

    WaterBench bench = WaterBench::make(256, 176);
    WaterBench::Arm live = bench.arm();
    WaterBench::Arm none = bench.arm({strippedDir});
    bench.aim({40.0f, 6.0f, -86.0f}, {12.0f, 7.0f, -108.0f});
    bench.hold(12.0);
    bench.scene().atmospherics = auroraSky(12.0);
    scene::WaterSettings& w = bench.scene().waters.at(0).settings;
    REQUIRE(w.skyMirror == 0.0f);

    REQUIRE_FALSE(w.cinematic()); // the plain path
    const gpu::ImageF plain = bench.render(live);
    REQUIRE(countMask(waterMask(plain, bench.renderDry(live))) > 500);
    CHECK(gpu::hashImage(plain) == gpu::hashImage(bench.render(none)));

    w.ssr = 0.5f; // the cinematic path
    w.tintPatches = 0.4f;
    REQUIRE(w.cinematic());
    CHECK(gpu::hashImage(bench.render(live)) == gpu::hashImage(bench.render(none)));

    w.flowStreaks = 1.5f; // the streaks path
    CHECK(gpu::hashImage(bench.render(live)) == gpu::hashImage(bench.render(none)));

    // THE CONTROL: on, it reaches the picture
    w.skyMirror = 1.0f;
    CHECK(gpu::hashImage(bench.render(live)) != gpu::hashImage(bench.render(none)));
    CHECK(bench.ctx->errorCount() == 0);
}

TEST_CASE("the sky mirror puts the aurora overhead in the river, with the water's SSR on and off",
          "[gpu][renderer][water][adr1650]") {
    if (!fs::is_regular_file(qaWaterScene())) {
        SKIP("the water QA scene is not present");
    }
    WaterBench bench = WaterBench::make(320, 200);
    WaterBench::Arm live = bench.arm();
    // Level with the horizon: the sky fills the top half, the river the bottom, and every water pixel's mirror
    // direction (as far below the horizon as it is above) is on screen.
    bench.aim({40.0f, 6.0f, -86.0f}, {12.0f, 6.5f, -108.0f});
    bench.hold(12.0);
    scene::WaterSettings& w = bench.scene().waters.at(0).settings;
    w.ssr = 0.5f;
    w.ssrSteps = 16.0f;
    w.reflectionRays = 1.0f;
    w.reflection = 1.0f;
    w.fresnel = 0.6f; // a mirror-like river, as the scene will author it, so the reflection carries the colour
    w.roughness = 0.05f;

    const world::AtmosphericFrame sky = auroraSky(12.0);
    const auto frame = [&](bool aurora, float mirror) {
        bench.scene().atmospherics = aurora ? sky : world::AtmosphericFrame{};
        w.skyMirror = mirror;
        return bench.render(live);
    };

    // which pixels are water, and which are sky above the horizon (for the aurora's own colour)
    bench.scene().atmospherics = sky;
    w.skyMirror = 1.0f;
    const gpu::ImageF wetRef = bench.render(live);
    const auto water = waterMask(wetRef, bench.renderDry(live));
    REQUIRE(countMask(water) > 2000);

    const auto measure = [&](const char* label) {
        const gpu::ImageF onMirror = frame(true, 1.0f);
        const gpu::ImageF offMirror = frame(false, 1.0f);
        const gpu::ImageF onPlain = frame(true, 0.0f);
        const gpu::ImageF offPlain = frame(false, 0.0f);
        dumpWater(onMirror, std::string("sky-mirror-") + label + "-aurora");
        dumpWater(onPlain, std::string("sky-mirror-") + label + "-none");
        CHECK(allFinite(onMirror));
        // the aurora's own colour, from the sky it is drawn in: what the aurora adds above the river
        std::vector<char> skyMask(water.size(), 0);
        for (std::uint32_t y = 0; y < bench.height / 2; ++y) {
            for (std::uint32_t x = 0; x < bench.width; ++x) {
                const std::size_t i = static_cast<std::size_t>(y) * bench.width + x;
                skyMask[i] = water[i] ? 0 : 1;
            }
        }
        const Rgb auroraSkyAdds = meanDiff(onMirror, offMirror, skyMask);
        const Rgb mirrored = meanDiff(onMirror, offMirror, water);  // what the aurora adds to the river, mirrored
        const Rgb unmirrored = meanDiff(onPlain, offPlain, water);  // and without the mirror
        INFO(label << ": the aurora adds to the sky " << auroraSkyAdds.r << ", " << auroraSkyAdds.g << ", "
                   << auroraSkyAdds.b << "; to the river with the mirror " << mirrored.r << ", " << mirrored.g
                   << ", " << mirrored.b << "; without " << unmirrored.r << ", " << unmirrored.g << ", "
                   << unmirrored.b);
        REQUIRE(auroraSkyAdds.sum() > 0.05); // the control: there is an aurora up there to reflect
        CHECK(mirrored.sum() > 0.04 * auroraSkyAdds.sum()); // through the fresnel and the surface's alpha
        CHECK(mirrored.sum() > 20.0 * std::max(unmirrored.sum(), 1e-4));
        // in the aurora's colour: each channel's share of what it adds within a few percent of the sky's
        for (const auto& [m, s] : {std::pair{mirrored.r, auroraSkyAdds.r}, std::pair{mirrored.g, auroraSkyAdds.g},
                                   std::pair{mirrored.b, auroraSkyAdds.b}}) {
            CHECK(std::abs(m / mirrored.sum() - s / auroraSkyAdds.sum()) < 0.12);
        }
    };
    SECTION("the screen-space reflection on (16 steps, 1 ray)") {
        measure("ssr");
    }
    SECTION("the screen-space reflection off by the live ladder (Emergency: waterSsrScale 0)") {
        rendering::QualitySettings q = live.renderer->qualitySettings();
        q.waterSsrScale = 0.0f;
        live.renderer->setQualitySettings(q);
        measure("nossr");
    }
    CHECK(bench.ctx->errorCount() == 0);
}

TEST_CASE("the sky mirror adds nothing where its lookups land off screen", "[gpu][renderer][water][adr1650]") {
    if (!fs::is_regular_file(qaWaterScene())) {
        SKIP("the water QA scene is not present");
    }
    WaterBench bench = WaterBench::make(256, 176);
    WaterBench::Arm live = bench.arm();
    // Pitched well down: the horizon is above the top of the frame, so every mirror point is off screen.
    bench.aim({40.0f, 6.0f, -86.0f}, {28.0f, -3.0f, -95.0f});
    bench.hold(12.0);
    bench.scene().atmospherics = auroraSky(12.0);
    scene::WaterSettings& w = bench.scene().waters.at(0).settings;
    w.ssr = 0.5f;
    w.fresnel = 0.6f;
    const gpu::ImageF off = bench.render(live);
    const auto water = waterMask(off, bench.renderDry(live));
    REQUIRE(countMask(water) > 2000);
    w.skyMirror = 1.0f;
    const gpu::ImageF on = bench.render(live);
    CHECK(allFinite(on));
    double worst = 0.0;
    for (std::size_t i = 0; i < on.rgba.size(); ++i) {
        worst = std::max(worst, static_cast<double>(std::abs(on.rgba[i] - off.rgba[i])));
    }
    INFO("largest change anywhere: " << worst);
    CHECK(worst < 1e-3);
    CHECK(bench.ctx->errorCount() == 0);
}

// Hidden ([.]): the sky mirror's cost on the QA river at 1080p (honest timestamps, ADR-1600), the camera level so
// about half the frame is water: cinematic water with SSR 16x1, the mirror off and on, then the SSR off (Emergency)
// with the mirror on; interleaved over 6 reps of 60 frames; the scene label's median.
TEST_CASE("water sky mirror: cost (measure)", "[.][gpu][water][measure][adr1650]") {
    if (!fs::is_regular_file(qaWaterScene())) {
        SKIP("the water QA scene is not present");
    }
    WaterBench bench = WaterBench::make(1920, 1080);
    WaterBench::Arm live = bench.arm();
    if (!live.renderer->timeline().available()) {
        SKIP("timestamp queries unavailable");
    }
    bench.aim({40.0f, 6.0f, -86.0f}, {12.0f, 6.5f, -108.0f});
    bench.hold(12.0);
    scene::WaterSettings& w = bench.scene().waters.at(0).settings;
    w.ssr = 0.5f;
    w.ssrSteps = 16.0f;
    w.reflectionRays = 1.0f;
    const world::AtmosphericFrame sky = auroraSky(12.0);
    const auto measure = [&](float mirror, float ssrScale) {
        w.skyMirror = mirror;
        rendering::QualitySettings q = live.renderer->qualitySettings();
        q.waterSsrScale = ssrScale;
        live.renderer->setQualitySettings(q);
        std::vector<double> ms;
        for (int i = 0; i < 80; ++i) {
            bench.time.renderTime = 12.0 + i / 60.0;
            bench.scene().atmospherics = sky;
            REQUIRE(live.renderer->renderFrame(bench.scene(), bench.time, 1920, 1080).has_value());
            if (i >= 20) {
                ms.push_back(live.renderer->timeline().msFor("scene"));
            }
        }
        std::sort(ms.begin(), ms.end());
        return ms[ms.size() / 2];
    };
    const std::array<std::pair<float, float>, 4> arms{std::pair{0.0f, 1.0f}, std::pair{1.0f, 1.0f},
                                                      std::pair{0.0f, 0.0f}, std::pair{1.0f, 0.0f}};
    for (const auto& [m, s] : arms) {
        static_cast<void>(measure(m, s));
    }
    std::array<std::vector<double>, 4> ms;
    for (int rep = 0; rep < 6; ++rep) {
        for (std::size_t k = 0; k < arms.size(); ++k) {
            ms[k].push_back(measure(arms[k].first, arms[k].second));
        }
    }
    const auto list = [](const std::vector<double>& v) {
        std::string out;
        for (const double x : v) {
            out += fmt::format(" {:.3f}", x);
        }
        return out;
    };
    WARN("1080p QA river (level camera, ~half water), aurora ribbons overhead, the scene label's median (ms), 6 "
         "interleaved reps: SSR 16x1 mirror off"
         << list(ms[0]) << " | SSR 16x1 mirror on" << list(ms[1]) << " | no SSR (Emergency) mirror off" << list(ms[2])
         << " | no SSR mirror on" << list(ms[3]));
    CHECK(ms[1][0] > 0.0);
}
