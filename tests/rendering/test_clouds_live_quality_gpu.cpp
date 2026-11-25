// ADR-2134: the cloud march follows the live quality ladder. Neutral levers draw the frame they always drew; the
// ladder's lowest level draws a cheaper march of the same cloud, in fewer ADR-2131 bands.

#include "app/interactive_resolution.hpp"
#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/cloud_renderer.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <memory>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

std::unique_ptr<gpu::Context> makeContext() {
    log::init(log::Level::Warn);
    auto ctx = gpu::Context::create(gpu::ContextDesc{});
    if (!ctx) {
        SKIP("no GPU adapter available: " << ctx.error().message);
    }
    return std::move(*ctx);
}

// a broken cloud sea under a low sun, eroded lobes over it (the v2 march with every lever in play)
scene::Scene cloudScene() {
    scene::Scene s;
    s.camera.position = {0.0f, 300.0f, 0.0f};
    s.camera.target = {0.0f, 150.0f, -2000.0f};
    s.camera.fovYRadians = 0.87f;
    s.camera.nearPlane = 0.5f;
    s.camera.farPlane = 60000.0f;
    s.clouds.sea.enabled = true;
    s.clouds.sea.base = -300.0f;
    s.clouds.sea.top = 0.0f;
    s.clouds.sea.coverage = 0.6f;
    s.clouds.sea.density = 2.0f;
    s.clouds.extinction = 0.02f;
    s.clouds.lightIntensity = 0.5f;
    s.clouds.resolution = 0.5f;
    s.clouds.steps = 128;
    s.clouds.lightSteps = 6;
    s.clouds.stepGrowth = 0.02f;
    s.clouds.erosion = 0.8f;
    s.clouds.erosionOctaves = 3;
    s.clouds.temporal = 0.0f;
    for (int i = 0; i < 5; ++i) {
        scene::CloudLobe l;
        l.centre = {-400.0f + 200.0f * static_cast<float>(i), 150.0f, -1500.0f};
        l.radius = 120.0f;
        l.density = 1.0f;
        s.clouds.lobes.push_back(l);
    }
    return s;
}

double meanLuma(const gpu::Image8& img) {
    double sum = 0.0;
    for (std::size_t i = 0; i < img.rgba.size(); i += 4) {
        sum += 0.2126 * img.rgba[i] + 0.7152 * img.rgba[i + 1] + 0.0722 * img.rgba[i + 2];
    }
    return sum / static_cast<double>(img.rgba.size() / 4);
}

} // namespace

TEST_CASE("neutral cloud levers draw the authored march; the lowest live level a cheaper one of the same cloud",
          "[gpu][renderer][clouds][adr2134]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    constexpr std::uint32_t W = 256, H = 144;
    const scene::Scene s = cloudScene();
    FrameTime t{};
    t.renderTime = 1.0;
    t.frameIndex = 30;
    // a band budget that splits the authored march into five, so the band count reads the march's cost
    const std::uint64_t authoredUnits = std::uint64_t{W / 2} * (H / 2) * 128 * 7;
    const std::uint64_t budget = authoredUnits / 5 + 1;

    const auto render = [&](const rendering::QualitySettings& q, std::uint32_t& bands) {
        rendering::SceneRenderer r(*ctx, shaders);
        REQUIRE(r.init().has_value());
        r.setQualitySettings(q);
        r.clouds().setMarchUnitsPerSubmit(budget);
        auto img = r.renderToImage(s, t, W, H);
        REQUIRE(img.has_value());
        bands = r.clouds().lastMarchBands();
        return std::move(*img);
    };

    const rendering::QualitySettings tier = rendering::QualitySettings::forTier(rendering::QualityTier::Realtime);
    std::uint32_t bandsDefault = 0;
    const gpu::Image8 def = render(tier, bandsDefault);
    // every lever written explicitly at its neutral value: the same frame, bit for bit
    rendering::QualitySettings neutral = tier;
    neutral.cloudResolutionScale = 1.0f;
    neutral.cloudStepScale = 1.0f;
    neutral.cloudLightStepScale = 1.0f;
    neutral.cloudErosionOctaves = 3;
    std::uint32_t bandsNeutral = 0;
    const gpu::Image8 same = render(neutral, bandsNeutral);
    CHECK(gpu::hashImage(def) == gpu::hashImage(same));
    CHECK(bandsDefault == 5);
    CHECK(bandsNeutral == 5);

    // the Balanced ladder's Emergency rung over the same tier
    const auto& ladder = app::liveQualityLadder(app::LiveQualityStrategy::Balanced);
    const rendering::QualitySettings emergency = app::applyLiveRung(tier, tier, ladder.back(), 1.0f);
    REQUIRE(emergency.renderScale < 1.0f);
    rendering::QualitySettings cheap = tier; // only the cloud levers, so the frames compare at one size
    cheap.cloudResolutionScale = emergency.cloudResolutionScale;
    cheap.cloudStepScale = emergency.cloudStepScale;
    cheap.cloudLightStepScale = emergency.cloudLightStepScale;
    cheap.cloudErosionOctaves = emergency.cloudErosionOctaves;
    std::uint32_t bandsCheap = 0;
    const gpu::Image8 low = render(cheap, bandsCheap);
    CHECK(gpu::hashImage(low) != gpu::hashImage(def));
    // the band count follows the march it runs (ADR-2131's guarantee is on the cheaper march too)
    CHECK(bandsCheap == 1);
    // the same cloud: there is cloud in the frame, and its brightness is within a fifth of the authored march's
    const double a = meanLuma(def);
    const double b = meanLuma(low);
    INFO("mean luma authored " << a << ", lowest level " << b);
    CHECK(a > 10.0);
    CHECK(std::abs(b - a) < 0.2 * a);
    CHECK(ctx->errorCount() == 0);
}
