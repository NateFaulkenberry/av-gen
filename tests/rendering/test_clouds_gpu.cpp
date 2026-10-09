// ADR-1289: the cloud Environment draws a lobe where it is, behind what is in front of it, and nothing without a block.

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <memory>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

std::unique_ptr<gpu::Context> makeCloudContext() {
    log::init(log::Level::Warn);
    auto ctx = gpu::Context::create(gpu::ContextDesc{});
    if (!ctx) {
        SKIP("no GPU adapter available: " << ctx.error().message);
    }
    return std::move(*ctx);
}

scene::Scene skyScene() {
    scene::Scene s;
    s.camera.position = {0.0f, 0.0f, 0.0f};
    s.camera.target = {0.0f, 0.0f, 100.0f};
    s.camera.fovYRadians = 0.87f;
    s.camera.nearPlane = 0.5f;
    s.camera.farPlane = 20000.0f;
    return s;
}

double meanLuma(const gpu::Image8& img, std::uint32_t x0, std::uint32_t y0, std::uint32_t x1, std::uint32_t y1) {
    double sum = 0.0;
    std::size_t n = 0;
    for (std::uint32_t y = y0; y < y1; ++y) {
        for (std::uint32_t x = x0; x < x1; ++x) {
            const auto* p = img.pixel(x, y);
            sum += 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];
            ++n;
        }
    }
    return sum / static_cast<double>(n);
}

} // namespace

TEST_CASE("a cloud lobe is drawn where it is, and a scene with no cloud block draws none",
          "[gpu][renderer][clouds][adr1289]") {
    auto ctx = makeCloudContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    constexpr std::uint32_t W = 160, H = 96;
    FrameTime t{};

    scene::Scene none = skyScene();
    auto a = renderer.renderToImage(none, t, W, H);
    REQUIRE(a.has_value());

    scene::Scene clouded = skyScene();
    scene::CloudLobe lobe;
    lobe.centre = {0.0f, 0.0f, 2000.0f};   // dead ahead, 2 km
    lobe.radius = 400.0f;
    clouded.clouds.lobes = {lobe};
    clouded.clouds.extinction = 0.02f;
    clouded.clouds.noiseAmount = 0.0f;
    clouded.clouds.lightIntensity = 3.0f;
    clouded.clouds.ambientTop = {0.6f, 0.6f, 0.6f};
    clouded.clouds.ambientBottom = {0.6f, 0.6f, 0.6f};
    REQUIRE(clouded.clouds.active());
    auto b = renderer.renderToImage(clouded, t, W, H);
    REQUIRE(b.has_value());

    // the centre of the frame brightens; a corner, outside the lobe's disc, does not
    const double centreA = meanLuma(*a, W / 2 - 6, H / 2 - 6, W / 2 + 6, H / 2 + 6);
    const double centreB = meanLuma(*b, W / 2 - 6, H / 2 - 6, W / 2 + 6, H / 2 + 6);
    const double cornerA = meanLuma(*a, 0, 0, 10, 10);
    const double cornerB = meanLuma(*b, 0, 0, 10, 10);
    INFO("centre " << centreA << " -> " << centreB << ", corner " << cornerA << " -> " << cornerB);
    CHECK(centreB > centreA + 20.0);
    CHECK(std::abs(cornerB - cornerA) < 2.0);

    // and with the block disabled the frame is the cloudless one, byte for byte
    clouded.clouds.enabled = false;
    auto c = renderer.renderToImage(clouded, t, W, H);
    REQUIRE(c.has_value());
    CHECK(gpu::hashImage(*c) == gpu::hashImage(*a));
    CHECK(ctx->errorCount() == 0);
}
