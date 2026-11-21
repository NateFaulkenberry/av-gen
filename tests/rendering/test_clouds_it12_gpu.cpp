// ADR-2119/2120 (Neon Dreams iteration 12, owner msg 149): the cloud churns in place (a pure function of the second),
// mist sheets draw in their own band, and the vortex opens an eye in the relief sea and lights its funnel.

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/clouds.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

std::unique_ptr<gpu::Context> makeContext12() {
    log::init(log::Level::Warn);
    auto ctx = gpu::Context::create(gpu::ContextDesc{});
    if (!ctx) {
        SKIP("no GPU adapter available: " << ctx.error().message);
    }
    return std::move(*ctx);
}

scene::Scene greyClouds12() {
    scene::Scene s;
    s.camera.position = {0.0f, 0.0f, 0.0f};
    s.camera.target = {0.0f, 0.0f, -100.0f};
    s.camera.fovYRadians = 0.9f;
    s.camera.nearPlane = 0.5f;
    s.camera.farPlane = 60000.0f;
    s.clouds.extinction = 0.05f;
    s.clouds.lightIntensity = 0.0f;
    s.clouds.ambientTop = {0.6f, 0.6f, 0.6f};
    s.clouds.ambientBottom = {0.6f, 0.6f, 0.6f};
    s.clouds.hazeColor = {0.6f, 0.6f, 0.6f};
    s.clouds.hazeDistance = 100000.0f;
    s.clouds.temporal = 0.0f;
    s.clouds.resolution = 1.0f;
    s.clouds.minStep = 1.0f;
    s.clouds.stepGrowth = 0.01f;
    s.clouds.steps = 192;
    s.clouds.noiseAmount = 0.0f;
    s.clouds.detailAmount = 0.0f;
    return s;
}

double luma(const gpu::Image8& img, std::uint32_t x, std::uint32_t y) {
    const auto* p = img.pixel(x, y);
    return 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];
}

double meanLuma12(const gpu::Image8& img, std::uint32_t x0, std::uint32_t y0, std::uint32_t x1, std::uint32_t y1) {
    double s = 0.0;
    for (std::uint32_t y = y0; y < y1; ++y) {
        for (std::uint32_t x = x0; x < x1; ++x) s += luma(img, x, y);
    }
    return s / static_cast<double>((x1 - x0) * (y1 - y0));
}

double meanAbsDiff12(const gpu::Image8& a, const gpu::Image8& b) {
    double sum = 0.0;
    for (std::uint32_t y = 0; y < a.height; ++y) {
        for (std::uint32_t x = 0; x < a.width; ++x) {
            for (int c = 0; c < 3; ++c) sum += std::fabs(double(a.pixel(x, y)[c]) - double(b.pixel(x, y)[c]));
        }
    }
    return sum / (3.0 * a.width * a.height);
}

gpu::Image8 renderAt(gpu::Context& ctx, gpu::ShaderLibrary& shaders, const scene::Scene& s, double seconds,
                     std::uint32_t w, std::uint32_t h, const char* dump = nullptr) {
    rendering::SceneRenderer renderer(ctx, shaders);
    REQUIRE(renderer.init().has_value());
    FrameTime t{};
    t.renderTime = seconds;
    auto img = renderer.renderToImage(s, t, w, h);
    REQUIRE(img.has_value());
    if (dump && std::getenv("AVGEN_TEST_DUMP")) {
        (void)gpu::writePpm(*img, std::string(std::getenv("AVGEN_TEST_DUMP")) + "/" + dump + ".ppm");
    }
    return std::move(*img);
}

} // namespace

TEST_CASE("churned cloud evolves in place with no wind, and is a pure function of the second",
          "[gpu][renderer][clouds][adr2119]") {
    auto ctx = makeContext12();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    constexpr std::uint32_t W = 160, H = 100;
    scene::Scene s = greyClouds12();
    s.clouds.erosion = 1.0f;
    s.clouds.erosionScale = 30.0f;
    for (int i = 0; i < 3; ++i) {
        scene::CloudLobe l;
        l.centre = {-50.0f + 50.0f * static_cast<float>(i), 0.0f, -120.0f};
        l.radius = 32.0f;
        s.clouds.lobes.push_back(l);
    }
    scene::Scene churned = s;
    churned.clouds.churn.amount = 1.0f;
    churned.clouds.churn.period = 4.0f;
    churned.clouds.churn.boil = 3.0f;

    const gpu::Image8 still1 = renderAt(*ctx, shaders, s, 1.0, W, H);
    const gpu::Image8 still4 = renderAt(*ctx, shaders, s, 4.0, W, H);
    const gpu::Image8 c1 = renderAt(*ctx, shaders, churned, 1.0, W, H, "churn1");
    const gpu::Image8 c4 = renderAt(*ctx, shaders, churned, 4.0, W, H, "churn4");
    const gpu::Image8 c4again = renderAt(*ctx, shaders, churned, 4.0, W, H);
    INFO("no churn, 1 s vs 4 s: " << meanAbsDiff12(still1, still4) << "; churned: " << meanAbsDiff12(c1, c4));
    // no wind, no churn: only the march's per-frame step jitter differs (ADR-2111)
    CHECK(meanAbsDiff12(still1, still4) < 0.5);
    CHECK(meanAbsDiff12(c1, c4) > 10.0 * std::max(meanAbsDiff12(still1, still4), 0.05)); // the churn moves it
    CHECK(meanAbsDiff12(c4, c4again) == 0.0);    // the same second, the same picture (seek = play)
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("a mist sheet draws cloud in its own band and disc", "[gpu][renderer][clouds][adr2119]") {
    auto ctx = makeContext12();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    constexpr std::uint32_t W = 160, H = 90;
    scene::Scene s = greyClouds12();
    s.camera.position = {0.0f, 12.0f, 0.0f};
    s.camera.target = {0.0f, 4.0f, -60.0f};
    s.clouds.extinction = 0.08f;
    scene::CloudLobe far;
    far.centre = {0.0f, 0.0f, 5000.0f};
    far.radius = 10.0f;
    s.clouds.lobes = {far};
    s.clouds.wisps.enabled = true;
    scene::Scene sheet = s;
    scene::CloudWisps::Layer l;
    l.centre = {0.0f, -60.0f};
    l.radius = 60.0f;
    l.base = 0.0f;
    l.top = 8.0f;
    l.density = 1.0f;
    l.scale = 12.0f;
    l.coverage = 0.6f;
    sheet.clouds.wisps.layers = {l};
    const gpu::Image8 off = renderAt(*ctx, shaders, s, 2.0, W, H);
    const gpu::Image8 on = renderAt(*ctx, shaders, sheet, 2.0, W, H, "sheet");
    const double a = meanLuma12(off, W / 4, H / 3, 3 * W / 4, 5 * H / 6);
    const double b = meanLuma12(on, W / 4, H / 3, 3 * W / 4, 5 * H / 6);
    INFO("without the sheet " << a << ", with " << b);
    CHECK(b > a + 5.0);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("the vortex opens an eye in the relief sea, lights its funnel and turns with the second",
          "[gpu][renderer][clouds][adr2120]") {
    auto ctx = makeContext12();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    constexpr std::uint32_t W = 160, H = 160;
    scene::Scene s = greyClouds12();
    // straight down at a relief sea from 900 m up
    s.camera.position = {0.0f, 900.0f, 0.01f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    s.camera.fovYRadians = 1.1f;
    s.clouds.extinction = 0.05f;
    s.clouds.sea.enabled = true;
    s.clouds.sea.base = -260.0f;
    s.clouds.sea.top = -40.0f;
    s.clouds.sea.relief = 40.0f;
    s.clouds.sea.billow = 10.0f;
    s.clouds.sea.coverage = 1.0f;
    s.clouds.ambientBottom = {0.3f, 0.3f, 0.3f};
    scene::Scene v = s;
    v.clouds.vortex.enabled = true;
    v.clouds.vortex.centre = {0.0f, 0.0f};
    v.clouds.vortex.radius = 450.0f;
    v.clouds.vortex.eye = 120.0f;
    v.clouds.vortex.depth = 150.0f;
    v.clouds.vortex.spin = 0.3f;
    v.clouds.vortex.glowColor = {1.0f, 0.2f, 1.0f};
    v.clouds.vortex.glow = 4.0f;
    v.clouds.vortex.glowHeight = 80.0f;

    const gpu::Image8 plain = renderAt(*ctx, shaders, s, 2.0, W, H, "vortex_off");
    const gpu::Image8 whirl = renderAt(*ctx, shaders, v, 2.0, W, H, "vortex_on");
    const gpu::Image8 later = renderAt(*ctx, shaders, v, 3.0, W, H);
    const std::uint32_t c0 = W / 2 - 6, c1 = W / 2 + 6;
    const double eyeOff = meanLuma12(plain, c0, c0, c1, c1);
    const double eyeOn = meanLuma12(whirl, c0, c0, c1, c1);
    // the funnel's wall: a ring between the eye and the rim
    double wallOn = 0.0, wallOff = 0.0;
    int n = 0;
    for (std::uint32_t y = 0; y < H; ++y) {
        for (std::uint32_t x = 0; x < W; ++x) {
            const double r = std::hypot(double(x) - W / 2.0, double(y) - H / 2.0);
            if (r > 20.0 && r < 34.0) {
                wallOn += luma(whirl, x, y);
                wallOff += luma(plain, x, y);
                ++n;
            }
        }
    }
    wallOn /= n;
    wallOff /= n;
    INFO("eye: sea " << eyeOff << " -> vortex " << eyeOn << "; wall ring: " << wallOff << " -> " << wallOn
                     << "; 2 s vs 3 s " << meanAbsDiff12(whirl, later));
    CHECK(eyeOn < eyeOff - 20.0);              // the eye is open (the black sky below)
    CHECK(wallOn > wallOff * 0.6);             // the wall is still cloud
    CHECK(meanAbsDiff12(whirl, later) > 0.2);  // and it turns
    CHECK(ctx->errorCount() == 0);
}
