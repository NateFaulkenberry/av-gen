// ADR-918: aerial perspective takes the sky's radiance, aurora included.
//
// The surface fog used to fade every surface towards one constant colour, so a far ridge seen
// against a bright horizon faded towards navy and stood as a dark cut-out against the sky it should
// have been dissolving into -- Glowmere Valley 3's rim against its aurora (reports/render-post.md,
// "Draw-distance root cause" 4). `Environment::fogSky` takes the fog's colour from the sky along the
// ray instead.
//
// The measure: a ridge 300 m out, fogged to about 5% of itself, is compared with the sky that stands
// in the same pixels when the ridge is removed. Fogged towards the sky, the ridge is that sky to
// within what the air lets through; fogged towards the constant colour -- the control, and every
// scene before ADR-918 -- it is a dark shape against it.

#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/scene.hpp"
#include "world/atmospherics.hpp"
#include "world/effects/effect_instance.hpp"

#include "support/post_bench.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

constexpr std::uint32_t kWidth = 320;
constexpr std::uint32_t kHeight = 200;

// Eye level, looking along the horizon at a long dark ridge 300 m away whose crest stands about 5
// degrees above the horizon. The sky is procedural and drawn behind the world, with a warm, bright
// horizon nothing like the navy fog colour -- the arrangement in which the old fog's cut-out was
// worst.
scene::Scene ridgeShot(bool withRidge) {
    scene::Scene s;
    s.environment.showSkybox = true;
    s.environment.environmentIntensity = 1.0f;
    s.environment.sky.enabled = true;
    s.environment.sky.showBackground = true;
    s.environment.sky.zenithColor = {0.02f, 0.03f, 0.08f};
    s.environment.sky.horizonColor = {0.9f, 0.45f, 0.2f};
    s.environment.sky.sunIntensity = 0.0f; // no disc: the gradient is the whole sky
    s.environment.backgroundColor = glm::vec3(0.0f);
    s.environment.fogColor = {0.02f, 0.04f, 0.12f};
    s.environment.volumeDensity = 0.01f;
    s.environment.volumeAbsorption = 1.0f; // extinction 0.01 / m: 5% of the ridge survives 300 m
    s.environment.volumeMaxDistance = 0.0f; // no march: the surface pass carries the whole ray
    s.camera.position = {0.0f, 8.0f, 0.0f};
    s.camera.target = {0.0f, 8.0f, -100.0f};
    s.camera.fovYRadians = glm::radians(40.0f);
    // No bloom: the ridge is brighter in one arm than the other, and a glow spreading that into the
    // open sky would make "the sky is not fogged" untestable.
    s.post.bloomEnabled = false;
    s.post.bloomIntensity = 0.0f;
    if (withRidge) {
        const auto mesh = s.addMesh(scene::makeCube(1.0f));
        auto& ridge = s.addEntity("ridge", mesh);
        ridge.transform.position = {0.0f, 5.0f, -300.0f};
        ridge.transform.scale = {1500.0f, 30.0f, 20.0f}; // y from -25 to 35: the crest at +5.1 degrees
        ridge.material.baseColor = glm::vec3(0.02f);
        ridge.material.roughness = 1.0f;
    }
    return s;
}

// The standard Glowmere aurora, anchored on the eye, so its curtains ring the horizon behind the
// ridge (atmospherics_gpu's fixture, the same numbers).
world::AtmosphericFrame auroraFrame() {
    world::EffectInstance e = world::glowmereAurora("sky");
    e.aurora.shape.anchor = world::SkyAnchor::World;
    e.aurora.shape.anchorPosition = glm::vec3(0.0f);
    e.aurora.shape.radius = 900.0f;
    e.aurora.shape.baseHeight = -60.0f;
    e.aurora.shape.curtainHeight = 700.0f;
    e.aurora.appearance.intensity = 3.0f;
    e.aurora.audio.spectrumShape = 0.0f;
    e.timing.fadeIn = 0.0;
    e.ground.mode = world::GroundGlow::Off;
    const std::array<world::EffectInstance, 1> set{e};
    world::EffectContext ctx;
    ctx.seconds = 3.0;
    world::AtmosphericFrame frame;
    world::buildAtmosphericFrame(set, ctx, frame);
    return frame;
}

struct Shot {
    gpu::ImageF image;
    gpu::ImageF map; // the fog's sky map, read back after the frame (all zero if it was not built)
    bool mapBuilt = false;
};

Shot render(gpu::Context& ctx, gpu::ShaderLibrary& shaders, const scene::Scene& s) {
    rendering::SceneRenderer renderer(ctx, shaders);
    REQUIRE(renderer.init().has_value());
    const FrameTime time{.renderTime = 1.0, .deltaTime = 1.0 / 60.0, .frameIndex = 1};
    auto image = renderer.renderToImageFloat(s, time, kWidth, kHeight);
    REQUIRE(image.has_value());
    Shot out;
    out.image = std::move(*image);
    out.mapBuilt = renderer.stats().fogSkyMap;
    auto map = gpu::readTextureF16(ctx, renderer.fogSkyMapTexture(), rendering::SceneRenderer::kFogSkyWidth,
                                   rendering::SceneRenderer::kFogSkyHeight);
    REQUIRE(map.has_value());
    out.map = std::move(*map);
    return out;
}

double luminance(const float* p) {
    return 0.2126 * static_cast<double>(p[0]) + 0.7152 * static_cast<double>(p[1]) +
           0.0722 * static_cast<double>(p[2]);
}

// The mean luminance of a band of rows, as fractions of the height from the top.
double bandLuminance(const gpu::ImageF& image, double top, double bottom) {
    const auto y0 = static_cast<std::uint32_t>(top * image.height);
    const auto y1 = static_cast<std::uint32_t>(bottom * image.height);
    double sum = 0.0;
    std::size_t n = 0;
    for (std::uint32_t y = y0; y < y1; ++y) {
        for (std::uint32_t x = 0; x < image.width; ++x) {
            sum += luminance(image.pixel(x, y));
            ++n;
        }
    }
    return n > 0 ? sum / static_cast<double>(n) : 0.0;
}

// sum |a - b| over RGB in a band of rows, relative to sum |b|.
double bandDifference(const gpu::ImageF& a, const gpu::ImageF& b, double top, double bottom) {
    const auto y0 = static_cast<std::uint32_t>(top * a.height);
    const auto y1 = static_cast<std::uint32_t>(bottom * a.height);
    double num = 0.0;
    double den = 0.0;
    for (std::uint32_t y = y0; y < y1; ++y) {
        for (std::uint32_t x = 0; x < a.width; ++x) {
            for (int c = 0; c < 3; ++c) {
                num += std::abs(static_cast<double>(a.pixel(x, y)[c]) - static_cast<double>(b.pixel(x, y)[c]));
                den += std::abs(static_cast<double>(b.pixel(x, y)[c]));
            }
        }
    }
    return den > 0.0 ? num / den : 0.0;
}

// The ridge's crest is at about 0.37 of the height and its foot at 0.66; the band just under the
// crest is where the rim meets the sky, and where the cut-out was.
constexpr double kRidgeTop = 0.40;
constexpr double kRidgeBottom = 0.60;
constexpr double kSkyTop = 0.05;
constexpr double kSkyBottom = 0.30;

} // namespace

TEST_CASE("a far ridge fogged towards the sky dissolves into the sky behind it", "[gpu][fog][fogsky]") {
    auto ctx = testsupport::gpuContextOrSkip();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});

    const Shot skyOnly = render(*ctx, shaders, ridgeShot(false));
    scene::Scene constant = ridgeShot(true); // fogSky 0: the control, and every scene before ADR-918
    scene::Scene fromSky = ridgeShot(true);
    fromSky.environment.fogSky = 1.0f;
    const Shot a = render(*ctx, shaders, constant);
    const Shot b = render(*ctx, shaders, fromSky);

    // How far the ridge's pixels are from the sky that is there when the ridge is not.
    const double controlGap = bandDifference(a.image, skyOnly.image, kRidgeTop, kRidgeBottom);
    const double treatmentGap = bandDifference(b.image, skyOnly.image, kRidgeTop, kRidgeBottom);
    UNSCOPED_INFO("the ridge against the sky behind it: constant fog colour " << controlGap << ", fog from the sky "
                                                                              << treatmentGap);
    UNSCOPED_INFO("ridge luminance: constant " << bandLuminance(a.image, kRidgeTop, kRidgeBottom) << ", from sky "
                                               << bandLuminance(b.image, kRidgeTop, kRidgeBottom) << ", sky behind "
                                               << bandLuminance(skyOnly.image, kRidgeTop, kRidgeBottom));
    // About 5% of the ridge survives 300 m of this air, and the map is a low-pass of the sky, so the
    // treatment is the sky to within a few tenths of that; the control is most of the sky away.
    CHECK(treatmentGap < 0.15);
    CHECK(controlGap > 0.6);

    // The option reaches the map and the map reaches the output: it was built in the treatment and
    // not in the control, and the two frames differ on the ridge...
    CHECK(b.mapBuilt);
    CHECK_FALSE(a.mapBuilt);
    CHECK(bandDifference(b.image, a.image, kRidgeTop, kRidgeBottom) > 0.5);
    // ...and nowhere else: the sky is not fogged, so the rows of open sky are identical to the bit.
    CHECK(bandDifference(b.image, a.image, kSkyTop, kSkyBottom) == 0.0);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("the fog carries the aurora into the air in front of it", "[gpu][fog][fogsky][atmospherics]") {
    auto ctx = testsupport::gpuContextOrSkip();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});

    auto shot = [&](bool aurora, float fogSky) {
        scene::Scene s = ridgeShot(true);
        s.environment.fogSky = fogSky;
        if (aurora) {
            s.atmospherics = auroraFrame();
            REQUIRE(s.atmospherics.auroraCount == 1);
        }
        return render(*ctx, shaders, s);
    };
    const Shot plainFromSky = shot(false, 1.0f);
    const Shot auroraFromSky = shot(true, 1.0f);
    const Shot plainConstant = shot(false, 0.0f);
    const Shot auroraConstant = shot(true, 0.0f);

    // The aurora behind the ridge moves the ridge's colour when the fog takes the sky's...
    const double withSky = bandDifference(auroraFromSky.image, plainFromSky.image, kRidgeTop, kRidgeBottom);
    // ...and does not when the fog is a constant colour -- which is the control, and the reason
    // the rim read as a cut-out: the air in front of the aurora never knew it was there.
    const double withConstant = bandDifference(auroraConstant.image, plainConstant.image, kRidgeTop, kRidgeBottom);
    UNSCOPED_INFO("the aurora's effect on the fogged ridge: fog from the sky " << withSky << ", constant fog colour "
                                                                                << withConstant);
    CHECK(withSky > 0.05);
    CHECK(withConstant < 0.01);

    // It gets there through the map: the map's horizon rows carry the aurora's light.
    const double mapPlain = bandLuminance(plainFromSky.map, 0.0, 0.25);
    const double mapAurora = bandLuminance(auroraFromSky.map, 0.0, 0.25);
    UNSCOPED_INFO("the map's horizon rows: plain sky " << mapPlain << ", with the aurora " << mapAurora);
    CHECK(mapAurora > mapPlain * 1.05);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("the fog's sky map is a pure function of the frame", "[gpu][fog][fogsky][determinism]") {
    // ADR-091's promise at the level this pass could break it: two fresh renderers, one frame, the
    // same bits -- the map carries no history, so a seek lands on the frame play did.
    auto ctx = testsupport::gpuContextOrSkip();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    scene::Scene s = ridgeShot(true);
    s.environment.fogSky = 0.7f;
    s.atmospherics = auroraFrame();
    const Shot first = render(*ctx, shaders, s);
    const Shot second = render(*ctx, shaders, s);
    REQUIRE(first.image.rgba.size() == second.image.rgba.size());
    CHECK(std::memcmp(first.image.rgba.data(), second.image.rgba.data(), first.image.rgba.size() * sizeof(float)) == 0);
    CHECK(std::memcmp(first.map.rgba.data(), second.map.rgba.data(), first.map.rgba.size() * sizeof(float)) == 0);
    CHECK(ctx->errorCount() == 0);
}
