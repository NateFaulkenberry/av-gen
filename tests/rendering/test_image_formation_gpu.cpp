// Image formation (ADR-037, ADR-039): exposure before bloom, tone-map operators at known input
// values, energy-conserving bloom, red-weighted halation, determinism of the whole chain, and the
// Hyperspace frame that used to blow out to solid white.

#include "app/engine.hpp"
#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/camera.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <vector>

using namespace avgen;
using Catch::Approx;
namespace fs = std::filesystem;

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

gpu::ShaderLibrary makeShaders(gpu::Context& ctx) {
    return gpu::ShaderLibrary(ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
}

// ---- CPU mirrors of shaders/tonemap.wgsl, so the operators can be checked against numbers ------

glm::vec3 clamp01(glm::vec3 c) { return glm::clamp(c, glm::vec3(0.0f), glm::vec3(1.0f)); }

glm::vec3 acesFitted(glm::vec3 x) {
    const float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
    return clamp01((x * (a * x + b)) / (x * (c * x + d) + e));
}

glm::vec3 agxContrast(glm::vec3 x) {
    const glm::vec3 x2 = x * x;
    const glm::vec3 x4 = x2 * x2;
    return 15.5f * x4 * x2 - 40.14f * x4 * x + 31.96f * x4 - 6.868f * x2 * x + 0.4298f * x2 + 0.1191f * x -
           0.00232f;
}

glm::vec3 agx(glm::vec3 val) {
    // The WGSL mat3x3 constructor takes columns, and glm::mat3 does too.
    const glm::mat3 inset(glm::vec3(0.842479062253094f, 0.0423282422610123f, 0.0423756549057051f),
                          glm::vec3(0.0784335999999992f, 0.878468636469772f, 0.0784336f),
                          glm::vec3(0.0792237451477643f, 0.0791661274605434f, 0.879142973793104f));
    const glm::mat3 outset(glm::vec3(1.19687900512017f, -0.0528968517574562f, -0.0529716355144438f),
                           glm::vec3(-0.0980208811401368f, 1.15190312990417f, -0.0980434501171241f),
                           glm::vec3(-0.0990297440797205f, -0.0989611768448433f, 1.15107367264116f));
    const float minEv = -12.47393f;
    const float maxEv = 4.026069f;
    glm::vec3 v = inset * val;
    v = glm::clamp(glm::log2(glm::max(v, glm::vec3(1e-10f))), glm::vec3(minEv), glm::vec3(maxEv));
    v = (v - minEv) / (maxEv - minEv);
    v = agxContrast(v);
    v = outset * v;
    return glm::pow(clamp01(v), glm::vec3(2.2f));
}

glm::vec3 reinhardExtended(glm::vec3 c) {
    const float white = 4.0f;
    const float l = glm::dot(c, glm::vec3(0.2126f, 0.7152f, 0.0722f));
    const float lm = l * (1.0f + l / (white * white)) / (1.0f + l);
    return clamp01(c * (lm / std::max(l, 1e-5f)));
}

glm::vec3 pbrNeutral(glm::vec3 color) {
    const float startCompression = 0.8f - 0.04f;
    const float desaturation = 0.15f;
    const float x = std::min(color.r, std::min(color.g, color.b));
    const float offset = x < 0.08f ? x - 6.25f * x * x : 0.04f;
    color -= glm::vec3(offset);
    const float peak = std::max(color.r, std::max(color.g, color.b));
    if (peak < startCompression) {
        return clamp01(color);
    }
    const float d = 1.0f - startCompression;
    const float newPeak = 1.0f - d * d / (peak + d - startCompression);
    color *= newPeak / peak;
    const float g = 1.0f - 1.0f / (desaturation * (peak - newPeak) + 1.0f);
    return clamp01(glm::mix(color, glm::vec3(newPeak), g));
}

glm::vec3 tonemapReference(scene::TonemapOperator op, glm::vec3 hdr) {
    switch (op) {
    case scene::TonemapOperator::AgX: return agx(hdr);
    case scene::TonemapOperator::Reinhard: return reinhardExtended(hdr);
    case scene::TonemapOperator::PbrNeutral: return pbrNeutral(hdr);
    case scene::TonemapOperator::Clamp: return clamp01(hdr);
    case scene::TonemapOperator::AcesFitted: break;
    }
    return acesFitted(hdr);
}

int srgbByte(float linear) {
    const float encoded = linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
    return static_cast<int>(std::lround(std::clamp(encoded, 0.0f, 1.0f) * 255.0f));
}

// A scene whose whole frame is one known scene-linear colour, with every post stage neutral.
scene::Scene flatScene(glm::vec3 colour) {
    scene::Scene s;
    s.environment.backgroundColor = colour;
    s.environment.brightness = 1.0f;
    s.post.bloomEnabled = false;
    s.post.bloomIntensity = 0.0f;
    return s;
}

// A small very bright emissive cube on black.
scene::Scene brightCubeScene() {
    scene::Scene s;
    s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    s.camera.position = {0.0f, 0.0f, 8.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    const auto mesh = s.addMesh(scene::makeCube(0.4f));
    auto& e = s.addEntity("bright", mesh);
    e.material.baseColor = {0.0f, 0.0f, 0.0f};
    e.material.emissiveColor = {1.0f, 0.9f, 0.6f};
    e.material.emissiveIntensity = 30.0f;
    s.post.bloomEnabled = false;
    s.post.bloomIntensity = 0.0f;
    return s;
}

double totalEnergy(const gpu::ImageF& img) {
    double sum = 0.0;
    for (std::uint32_t y = 0; y < img.height; ++y) {
        for (std::uint32_t x = 0; x < img.width; ++x) {
            const float* p = img.pixel(x, y);
            sum += static_cast<double>(p[0]) + static_cast<double>(p[1]) + static_cast<double>(p[2]);
        }
    }
    return sum;
}

int sum3(const std::uint8_t* px) { return px[0] + px[1] + px[2]; }

// Max horizontal gradient along a row: sharp edges score high, blurred ones low.
int edgeSharpness(const gpu::Image8& img, std::uint32_t y) {
    int best = 0;
    for (std::uint32_t x = 1; x < img.width; ++x) {
        best = std::max(best, std::abs(sum3(img.pixel(x, y)) - sum3(img.pixel(x - 1, y))));
    }
    return best;
}

} // namespace

TEST_CASE("Tone-map operators map known inputs to known outputs", "[gpu][post][tonemap]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    FrameTime time{};
    // Mid grey, a bright saturated colour and a heavily over-range one.
    for (const glm::vec3 input : {glm::vec3(0.18f), glm::vec3(0.6f, 0.15f, 0.05f), glm::vec3(3.0f, 1.2f, 0.4f)}) {
        for (const int op : {0, 1, 2, 3, 4}) {
            auto s = flatScene(input);
            s.post.tonemap = static_cast<scene::TonemapOperator>(op);
            auto img = renderer.renderToImage(s, time, 32, 32);
            REQUIRE(img.has_value());
            const auto* px = img->pixel(16, 16);
            const glm::vec3 expected = tonemapReference(s.post.tonemap, input);
            INFO("operator " << op << " input " << input.r << "," << input.g << "," << input.b);
            for (int c = 0; c < 3; ++c) {
                INFO("channel " << c << " got " << int(px[c]) << " want " << srgbByte(expected[c]));
                CHECK(std::abs(int(px[c]) - srgbByte(expected[c])) <= 2);
            }
        }
    }
    // AgX is the default operator now (ADR-039). Two properties justify that and the re-tuning of
    // the shipped grades: it is flatter through the midtones than the ACES fit, and it does not
    // crush the off-hue channels of a saturated over-range colour the way the ACES fit does.
    auto greyAt = [&](float v, scene::TonemapOperator op) {
        auto grey = flatScene(glm::vec3(v));
        grey.post.tonemap = op;
        auto img = renderer.renderToImage(grey, time, 16, 16);
        REQUIRE(img.has_value());
        return int(img->pixel(8, 8)[1]);
    };
    const int agxRamp = greyAt(0.72f, scene::TonemapOperator::AgX) - greyAt(0.045f, scene::TonemapOperator::AgX);
    const int acesRamp =
        greyAt(0.72f, scene::TonemapOperator::AcesFitted) - greyAt(0.045f, scene::TonemapOperator::AcesFitted);
    INFO("four-stop grey ramp: agx " << agxRamp << " aces " << acesRamp);
    CHECK(agxRamp < acesRamp - 20);

    auto agxScene = flatScene(glm::vec3(8.0f, 1.0f, 0.2f));
    agxScene.post.tonemap = scene::TonemapOperator::AgX;
    auto agxImage = renderer.renderToImage(agxScene, time, 32, 32);
    REQUIRE(agxImage.has_value());
    auto acesScene = agxScene;
    acesScene.post.tonemap = scene::TonemapOperator::AcesFitted;
    auto acesImage = renderer.renderToImage(acesScene, time, 32, 32);
    REQUIRE(acesImage.has_value());
    const auto* a = agxImage->pixel(16, 16);
    const auto* b = acesImage->pixel(16, 16);
    INFO("agx " << int(a[0]) << "," << int(a[1]) << "," << int(a[2]) << " aces " << int(b[0]) << "," << int(b[1])
                << "," << int(b[2]));
    CHECK(int(a[2]) > int(b[2]) + 10); // AgX keeps the blue channel; the ACES fit skews the hue
    CHECK(int(a[1]) > int(b[1]) - 40);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("Manual exposure is applied before bloom, one stop at a time", "[gpu][post][exposure]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    FrameTime time{};

    // The default exposure block is a no-op, so a scene that never mentions it is unchanged.
    auto base = flatScene(glm::vec3(0.25f, 0.1f, 0.05f));
    base.post.tonemap = scene::TonemapOperator::Clamp;
    auto plain = renderer.renderToImage(base, time, 32, 32);
    REQUIRE(plain.has_value());
    CHECK(std::abs(int(plain->pixel(16, 16)[0]) - srgbByte(0.25f)) <= 2);

    // One stop down (aperture 5.6 -> 8) halves the linear value.
    auto stopped = base;
    stopped.post.exposure.aperture = 8.0f; // (8/5.6)^2 = 2.04x less light
    auto dark = renderer.renderToImage(stopped, time, 32, 32);
    REQUIRE(dark.has_value());
    const float scale = scene::manualExposureScale(stopped.post.exposure);
    CHECK(scale == Approx(0.49f).epsilon(0.01));
    CHECK(std::abs(int(dark->pixel(16, 16)[0]) - srgbByte(0.25f * scale)) <= 2);

    // Exposure compensation is the same knob from the other side.
    auto lifted = base;
    lifted.post.exposure.compensation = 1.0f;
    auto bright = renderer.renderToImage(lifted, time, 32, 32);
    REQUIRE(bright.has_value());
    CHECK(std::abs(int(bright->pixel(16, 16)[0]) - srgbByte(0.5f)) <= 2);

    // And it happens *before* bloom: with exposure pushed two stops down, a threshold of 1.0 that
    // the raw scene would clear is no longer met, so the halo disappears.
    auto glow = brightCubeScene();
    glow.post.bloomEnabled = true;
    glow.post.bloomIntensity = 1.0f;
    glow.post.bloomThreshold = 1.0f;
    glow.post.tonemap = scene::TonemapOperator::Clamp;
    auto litHalo = renderer.renderToImage(glow, time, 128, 128);
    REQUIRE(litHalo.has_value());
    glow.post.exposure.compensation = -8.0f; // 1/256
    auto darkHalo = renderer.renderToImage(glow, time, 128, 128);
    REQUIRE(darkHalo.has_value());
    INFO("halo bright=" << sum3(litHalo->pixel(64, 40)) << " dark=" << sum3(darkHalo->pixel(64, 40)));
    CHECK(sum3(darkHalo->pixel(64, 40)) < sum3(litHalo->pixel(64, 40)) / 4);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("Automatic metering converges and reproduces exactly across two runs", "[gpu][post][exposure]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    scene::Scene s = flatScene(glm::vec3(4.0f, 3.6f, 3.2f)); // ~20x mid grey: badly over-exposed
    s.post.tonemap = scene::TonemapOperator::Clamp;
    s.post.exposure.mode = scene::ExposureSettings::Mode::Automatic;
    s.post.exposure.speedUp = 6.0f;
    s.post.exposure.speedDown = 6.0f;
    s.post.exposureDeltaSeconds = 1.0f / 30.0f;

    auto run = [&](std::vector<float>& scales) {
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        FrameTime time{};
        std::vector<std::uint64_t> hashes;
        for (int i = 0; i < 40; ++i) {
            auto img = renderer.renderToImage(s, time, 48, 48);
            REQUIRE(img.has_value());
            scales.push_back(renderer.post().stats().exposureScale);
            hashes.push_back(gpu::hashImage(*img));
        }
        return hashes;
    };
    std::vector<float> scalesA;
    std::vector<float> scalesB;
    const auto a = run(scalesA);
    const auto b = run(scalesB);

    // It starts at the manual exposure (nothing metered yet) and walks down toward mid grey.
    CHECK(scalesA.front() == Approx(1.0f).margin(1e-4));
    CHECK(scalesA.back() < 0.2f);
    CHECK(scalesA.back() > 0.02f);
    // 4.0 scene-linear metered onto 0.18 wants a scale of about 0.18 / 3.66 = 0.049.
    CHECK(scalesA.back() == Approx(0.18f / 3.664f).epsilon(0.15));
    // Every frame is bit-identical between the two runs.
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        INFO("frame " << i << " scale " << scalesA[i] << " vs " << scalesB[i]);
        CHECK(a[i] == b[i]);
        CHECK(scalesA[i] == scalesB[i]);
    }
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("Bloom conserves energy and the whole chain is deterministic", "[gpu][post][bloom]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    FrameTime time{};

    auto s = brightCubeScene();
    auto off = renderer.renderToImageFloat(s, time, 256, 256);
    REQUIRE(off.has_value());
    const double base = totalEnergy(*off);
    REQUIRE(base > 1.0);

    // With the threshold at 0 the prefilter is the identity, so the pyramid carries the whole
    // image's energy and `scene + bloom * intensity` must total (1 + intensity) times as much.
    for (const float intensity : {0.25f, 0.5f, 1.0f}) {
        s.post.bloomEnabled = true;
        s.post.bloomThreshold = 0.0f;
        s.post.bloomKnee = 0.0f;
        s.post.bloomIntensity = intensity;
        auto on = renderer.renderToImageFloat(s, time, 256, 256);
        REQUIRE(on.has_value());
        const double ratio = totalEnergy(*on) / base;
        const double want = 1.0 + static_cast<double>(intensity);
        INFO("intensity " << intensity << " energy ratio " << ratio << " want " << want);
        CHECK(ratio == Approx(want).epsilon(0.12));
    }

    // A raised threshold takes energy out rather than adding it: the spread halo is dimmer, and
    // adding levels does not multiply the energy the way the old additive upsample did.
    s.post.bloomThreshold = 4.0f;
    s.post.bloomKnee = 0.5f;
    s.post.bloomIntensity = 1.0f;
    auto few = renderer.renderToImageFloat(s, time, 256, 256);
    REQUIRE(few.has_value());
    const double sixLevels = totalEnergy(*few);
    s.post.bloomLevels = 3;
    auto three = renderer.renderToImageFloat(s, time, 256, 256);
    REQUIRE(three.has_value());
    INFO("6 levels " << sixLevels << " vs 3 levels " << totalEnergy(*three));
    CHECK(totalEnergy(*three) == Approx(sixLevels).epsilon(0.10));
    s.post.bloomLevels = 6;

    // Determinism: the same scene twice, through fresh renderers, is bit-identical.
    s.post.halationEnabled = true;
    s.post.anamorphicEnabled = true;
    s.post.anamorphicGhosts = 0.4f;
    s.post.dofEnabled = true;
    s.post.dofPhysical = true;
    s.post.sharpen = 0.4f;
    s.post.exposure.mode = scene::ExposureSettings::Mode::Automatic;
    auto chainHash = [&] {
        rendering::SceneRenderer r(*ctx, shaders);
        REQUIRE(r.init().has_value());
        std::uint64_t h = 0;
        for (int i = 0; i < 6; ++i) {
            auto img = r.renderToImage(s, time, 128, 128);
            REQUIRE(img.has_value());
            h = gpu::hashImage(*img);
        }
        return h;
    };
    CHECK(chainHash() == chainHash());
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("Halation is a wide, red-weighted tier and anamorphic streaks are horizontal",
          "[gpu][post][halation]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    FrameTime time{};

    // A neutral-white bright cube, so any colour in the halo comes from the halation tint.
    auto s = brightCubeScene();
    s.entities[0].material.emissiveColor = {1.0f, 1.0f, 1.0f};
    s.post.tonemap = scene::TonemapOperator::Clamp;
    auto plain = renderer.renderToImage(s, time, 128, 128);
    REQUIRE(plain.has_value());

    s.post.halationEnabled = true;
    s.post.halationIntensity = 1.5f;
    s.post.halationThreshold = 1.0f;
    s.post.halationWarmth = 0.0f; // a white highlight still haloes; the tint supplies the colour
    auto halo = renderer.renderToImage(s, time, 128, 128);
    REQUIRE(halo.has_value());
    const auto* off = plain->pixel(64, 34);
    const auto* on = halo->pixel(64, 34);
    INFO("halo off " << int(off[0]) << "," << int(off[1]) << "," << int(off[2]) << " on " << int(on[0]) << ","
                     << int(on[1]) << "," << int(on[2]));
    CHECK(sum3(on) > sum3(off) + 10);           // it added something
    CHECK(int(on[0]) - int(off[0]) > int(on[2]) - int(off[2]) + 8); // and it is red-weighted
    CHECK(int(on[0]) - int(off[0]) > int(on[1]) - int(off[1]));
    if (const char* dumpDir = std::getenv("AVGEN_DUMP_DIR")) {
        REQUIRE(gpu::writePpm(*halo, fs::path(dumpDir) / "halation.ppm").has_value());
    }

    // Anamorphic: the streak reaches further sideways than upward.
    auto ana = brightCubeScene();
    ana.post.tonemap = scene::TonemapOperator::Clamp;
    ana.post.bloomEnabled = true;
    ana.post.bloomIntensity = 0.2f;
    ana.post.bloomThreshold = 1.0f;
    auto anaOff = renderer.renderToImage(ana, time, 128, 128);
    REQUIRE(anaOff.has_value());
    ana.post.anamorphicEnabled = true;
    ana.post.anamorphicIntensity = 2.0f;
    ana.post.anamorphicStretch = 20.0f;
    auto anaOn = renderer.renderToImage(ana, time, 128, 128);
    REQUIRE(anaOn.has_value());
    const int sideways = sum3(anaOn->pixel(110, 64)) - sum3(anaOff->pixel(110, 64));
    const int upward = sum3(anaOn->pixel(64, 110)) - sum3(anaOff->pixel(64, 110));
    INFO("anamorphic sideways " << sideways << " upward " << upward);
    CHECK(sideways > upward);
    CHECK(sideways > 3);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("The anamorphic streak falls off smoothly rather than in a comb", "[gpu][post][anamorphic]") {
    // ADR-156. A gaussian whose taps do not overlap in its source does not blur -- it *copies*, once
    // per tap, so a compact bright thing prints a row of evenly spaced dots. That is a ripple in the
    // streak's profile, and a ripple is what this measures: walking outward from the highlight, the
    // streak may only get dimmer.
    //
    // Deliberately not a comparison of two renders of the streak: both paths would carry the same
    // defect and agree with each other. The reference is the shape a blur has.
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    FrameTime time{};

    auto scene = brightCubeScene();
    scene.post.tonemap = scene::TonemapOperator::Clamp;
    scene.post.bloomEnabled = true;
    scene.post.bloomIntensity = 0.2f;
    scene.post.bloomThreshold = 1.0f;
    auto off = renderer.renderToImage(scene, time, 256, 256);
    REQUIRE(off.has_value());
    scene.post.anamorphicEnabled = true;
    scene.post.anamorphicIntensity = 2.0f;
    scene.post.anamorphicStretch = 8.0f;
    auto on = renderer.renderToImage(scene, time, 256, 256);
    REQUIRE(on.has_value());

    // The streak alone: everything else in the row is identical between the two renders.
    std::vector<int> profile;
    for (std::uint32_t x = 160; x < 254; ++x) {
        profile.push_back(sum3(on->pixel(x, 128)) - sum3(off->pixel(x, 128)));
    }
    REQUIRE(profile.size() > 20);
    REQUIRE(profile.front() > 6); // there is a streak here at all to have a shape

    // A local maximum away from the highlight is a copy of it. The margin is above 8-bit noise on a
    // sum of three channels; the comb this was written against rippled by tens of levels.
    int bumps = 0;
    std::size_t worst = 0;
    for (std::size_t i = 1; i + 1 < profile.size(); ++i) {
        if (profile[i] > profile[i - 1] + 3 && profile[i] > profile[i + 1] + 3) {
            ++bumps;
            worst = i;
        }
    }
    INFO("streak profile bumps " << bumps << " first at x=" << (160 + worst));
    CHECK(bumps == 0);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("Depth of field takes its radius from the lens's circle of confusion", "[gpu][post][lens]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    FrameTime time{};

    scene::Scene s;
    s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    s.camera.position = {0.0f, 0.0f, 6.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    const auto mesh = s.addMesh(scene::makeCube(1.0f));
    auto& e = s.addEntity("cube", mesh);
    e.material.baseColor = {1.0f, 1.0f, 1.0f};
    e.material.emissiveColor = {1.0f, 1.0f, 1.0f};
    e.material.emissiveIntensity = 1.0f;
    s.post.bloomEnabled = false;
    s.post.tonemap = scene::TonemapOperator::Clamp;
    auto sharp = renderer.renderToImage(s, time, 256, 256);
    REQUIRE(sharp.has_value());
    const int sharpEdge = edgeSharpness(*sharp, 128);

    // An 85 mm lens wide open at f/1, focused at 20 m; the cube's near face is about 5 m away.
    s.post.dofEnabled = true;
    s.post.dofPhysical = true;
    s.post.dofMaxRadius = 32.0f; // only a clamp now
    s.post.focusDistance = 20.0f;
    s.post.lens.focalLength = 85.0f;
    s.post.lens.aperture = 1.0f;
    s.post.lens.sensorHeight = 24.0f;
    s.post.lens.focusDistance = 20.0f;
    // The analytic prediction, so the test says what it expects rather than only "blurrier".
    const float cocPixels = s.post.lens.circleOfConfusionPixels(5.0f, 256.0f);
    INFO("circle of confusion at 5 m: " << s.post.lens.circleOfConfusion(5.0f) << " mm = " << cocPixels << " px");
    CHECK(cocPixels > 8.0f);
    auto open = renderer.renderToImage(s, time, 256, 256);
    REQUIRE(open.has_value());
    const int openEdge = edgeSharpness(*open, 128);

    // Stopping down to f/8 shrinks the circle eightfold and brings the edge back.
    s.post.lens.aperture = 8.0f;
    auto stopped = renderer.renderToImage(s, time, 256, 256);
    REQUIRE(stopped.has_value());
    const int stoppedEdge = edgeSharpness(*stopped, 128);
    INFO("edge sharp=" << sharpEdge << " f/1=" << openEdge << " f/8=" << stoppedEdge);
    CHECK(openEdge < sharpEdge / 2);
    CHECK(stoppedEdge > openEdge * 2);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("The Hyperspace core frame is no longer blown out to white", "[gpu][post][examples]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    const fs::path project = fs::path(AVGEN_SOURCE_DIR) / "examples" / "hyperspace" / "hyperspace.json";
    if (!fs::exists(project)) {
        SKIP("hyperspace example not present");
    }
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    // The failing frame: the camera has flown into the core (t = 50 s on the project's own
    // timeline). Run the last two seconds so the exposure meter has settled, exactly as the
    // headless capture does.
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(project).has_value());
    FixedStepClock clock(30.0);
    engine.seekSeconds(48.0);
    gpu::Image8 frame;
    for (int i = 0; i < 60; ++i) {
        const FrameTime time = engine.tick(clock);
        engine.update(time);
        const rendering::ShaderFrameInputs inputs{&engine.shaderLayers(),
                                                  engine.hasFrame() ? &engine.latestFrame() : nullptr};
        auto img = renderer.renderToImage(engine.scene(), time, 320, 180, &inputs);
        REQUIRE(img.has_value());
        frame = *img;
    }
    if (const char* dumpDir = std::getenv("AVGEN_DUMP_DIR")) {
        REQUIRE(gpu::writePpm(frame, fs::path(dumpDir) / "hyperspace_core.ppm").has_value());
    }

    // Sample a grid over the frame. Before ADR-037/039 essentially every pixel of the lower two
    // thirds was 255,255,255; now nothing may be saturated and the frame must still have contrast.
    int saturated = 0;
    int samples = 0;
    int brightest = 0;
    long long total = 0;
    for (std::uint32_t y = 8; y < frame.height; y += 8) {
        for (std::uint32_t x = 8; x < frame.width; x += 8) {
            const auto* p = frame.pixel(x, y);
            const int s = sum3(p);
            brightest = std::max(brightest, s);
            total += s;
            if (p[0] >= 250 && p[1] >= 250 && p[2] >= 250) {
                ++saturated;
            }
            ++samples;
        }
    }
    const double mean = static_cast<double>(total) / (samples * 3);
    INFO("samples " << samples << " saturated " << saturated << " brightest " << brightest << " mean " << mean);
    CHECK(samples > 400);
    CHECK(saturated == 0);          // nothing clipped to white
    CHECK(brightest < 3 * 250);     // not even close
    CHECK(mean > 4.0);              // and it did not simply go black
    CHECK(mean < 200.0);
    // The guard is about the image, not the mechanism: the shot may meter automatically or carry
    // an authored manual exposure, but either way the frame must hold a range rather than clip.
    CHECK(brightest > 90);          // the core is still clearly the brightest thing
    CHECK(mean < brightest / 3.0);  // and it is not the whole frame
    CHECK(ctx->errorCount() == 0);
#endif
}

// Hidden performance probe: `avgen_render_tests "[.perf][post]"` (Release build). Reports GPU time
// per frame for the post chain at 1080p and 4K with bloom, halation and depth of field on and off.
TEST_CASE("Image formation chain cost", "[.perf][post]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    struct Config {
        const char* name;
        bool bloom;
        bool halation;
        bool anamorphic;
        bool dof;
        bool autoExposure;
    };
    const std::vector<Config> configs = {
        {"chain off", false, false, false, false, false},
        {"bloom", true, false, false, false, false},
        {"bloom + auto exposure", true, false, false, false, true},
        {"bloom + halation", true, true, false, false, false},
        {"bloom + halation + anamorphic", true, true, true, false, false},
        {"bloom + DOF", true, false, false, true, false},
        {"everything", true, true, true, true, true},
    };
    struct Size {
        const char* name;
        std::uint32_t width;
        std::uint32_t height;
    };
    for (const Size size : {Size{"1080p", 1920, 1080}, Size{"4K", 3840, 2160}}) {
        for (const auto& cfg : configs) {
            auto s = brightCubeScene();
            s.post.tonemap = scene::TonemapOperator::AgX;
            s.post.bloomEnabled = cfg.bloom;
            s.post.bloomIntensity = cfg.bloom ? 0.3f : 0.0f;
            s.post.halationEnabled = cfg.halation;
            s.post.anamorphicEnabled = cfg.anamorphic;
            s.post.dofEnabled = cfg.dof;
            s.post.dofPhysical = cfg.dof;
            s.post.dofMaxRadius = 12.0f;
            s.post.exposure.mode =
                cfg.autoExposure ? scene::ExposureSettings::Mode::Automatic : scene::ExposureSettings::Mode::Manual;
            FixedStepClock clock(60.0);
            double gpuSum = 0.0;
            int counted = 0;
            std::uint32_t passes = 0;
            for (int i = 0; i < 45; ++i) {
                auto img = renderer.renderToImage(s, clock.tick(), size.width, size.height);
                REQUIRE(img.has_value());
                if (i >= 15 && renderer.stats().gpuFrameMs >= 0.0) {
                    gpuSum += renderer.stats().gpuFrameMs;
                    ++counted;
                }
                passes = renderer.stats().post.passes;
            }
            CHECK(ctx->errorCount() == 0);
            WARN("post " << size.name << " " << cfg.name << ": GPU " << (counted ? gpuSum / counted : -1.0)
                         << " ms/frame over " << passes << " post passes");
        }
    }
}

// Depth layers (ADR-038), image side: the composite pass grades contrast and saturation by the
// distance of each pixel, which is atmospheric perspective. Half of this feature -- the instance
// thinning -- lives in cull.wgsl; this is the half a viewer sees.
TEST_CASE("Depth layers desaturate distance without touching the foreground", "[gpu][post][composition]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    // Two saturated cubes on black, one at 6 units and one at 40, framed so the near one occupies
    // the left of frame and the far one the right.
    scene::Scene s;
    s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    s.environment.showSkybox = false;
    s.camera.position = {0.0f, 0.0f, 0.0f};
    s.camera.target = {0.0f, 0.0f, -1.0f};
    s.post.bloomEnabled = false;
    s.post.bloomIntensity = 0.0f;
    const auto nearMesh = s.addMesh(scene::makeCube(0.7f));
    const auto farMesh = s.addMesh(scene::makeCube(5.0f));
    auto& nearCube = s.addEntity("near", nearMesh);
    nearCube.transform.position = {-1.4f, 0.0f, -6.0f};
    nearCube.material.baseColor = {0.0f, 0.0f, 0.0f};
    nearCube.material.emissiveColor = {1.0f, 0.15f, 0.05f};
    nearCube.material.emissiveIntensity = 3.0f;
    auto& farCube = s.addEntity("far", farMesh);
    farCube.transform.position = {9.0f, 0.0f, -40.0f};
    farCube.material.baseColor = {0.0f, 0.0f, 0.0f};
    farCube.material.emissiveColor = {1.0f, 0.15f, 0.05f};
    farCube.material.emissiveIntensity = 3.0f;

    FrameTime time{};
    // Mean saturation over the pixels a cube actually covers.
    const auto saturationOf = [](const gpu::Image8& img, bool leftHalf) {
        double sum = 0.0;
        int count = 0;
        for (std::uint32_t y = 0; y < img.height; ++y) {
            for (std::uint32_t x = 0; x < img.width; ++x) {
                const bool left = x < img.width / 2;
                if (left != leftHalf) {
                    continue;
                }
                const auto* p = img.pixel(x, y);
                const int hi = std::max({p[0], p[1], p[2]});
                const int lo = std::min({p[0], p[1], p[2]});
                if (hi < 24) {
                    continue; // background, where saturation is meaningless
                }
                sum += static_cast<double>(hi - lo) / static_cast<double>(hi);
                ++count;
            }
        }
        REQUIRE(count > 20);
        return sum / count;
    };

    auto plain = renderer.renderToImage(s, time, 128, 96);
    REQUIRE(plain.has_value());
    const double nearPlain = saturationOf(*plain, true);
    const double farPlain = saturationOf(*plain, false);

    scene::Scene layered = s;
    layered.composition.layers.push_back(
        scene::DepthLayer{.name = "near", .start = 0.0f, .end = 12.0f, .density = 1.0f, .contrast = 1.0f, .saturation = 1.0f});
    // The far band ends at 60, so a cube at 40 sits past its midpoint and receives the band's
    // value in full. The grade interpolates between band midpoints -- a per-pixel value that
    // switched at a band edge would draw a line across the image -- so a subject in the middle of
    // a band is graded partway, which is the point.
    layered.composition.layers.push_back(
        scene::DepthLayer{.name = "far", .start = 12.0f, .end = 60.0f, .density = 1.0f, .contrast = 1.0f, .saturation = 0.1f});
    auto graded = renderer.renderToImage(layered, time, 128, 96);
    REQUIRE(graded.has_value());
    const double nearGraded = saturationOf(*graded, true);
    const double farGraded = saturationOf(*graded, false);

    INFO("near " << nearPlain << " -> " << nearGraded << ", far " << farPlain << " -> " << farGraded);
    CHECK(farGraded < farPlain * 0.6);            // distance lost its colour
    CHECK(nearGraded > nearPlain * 0.9);          // the foreground kept its own
    CHECK(nearGraded > farGraded * 1.5);          // and the two now read as different distances

    // A scene with no layers is bit-identical to one whose only layer is the identity, so adding
    // the feature cannot have moved any existing frame.
    scene::Scene identity = s;
    identity.composition.layers.push_back(
        scene::DepthLayer{.name = "all", .start = 0.0f, .end = 1000.0f, .density = 1.0f, .contrast = 1.0f, .saturation = 1.0f});
    auto same = renderer.renderToImage(identity, time, 128, 96);
    REQUIRE(same.has_value());
    CHECK(gpu::hashImage(*same) == gpu::hashImage(*plain));
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("Chroma retention keeps bright narrow-band light coloured", "[gpu][post][tonemap]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    FrameTime time{};

    // Saturation as the eye reads it: how far apart the brightest and dimmest channels sit.
    // 0 is white, 1 is fully saturated. No channel of the source is exactly zero, because a
    // hard zero survives every clamp and would score 1.0 whatever the curve did.
    const auto saturationOf = [&](glm::vec3 colour, float retention) {
        auto s = flatScene(colour);
        s.post.tonemap = scene::TonemapOperator::AgX;
        s.post.bloomEnabled = false;
        s.post.chromaRetention = retention;
        auto img = renderer.renderToImage(s, time, 16, 16);
        REQUIRE(img.has_value());
        const auto* px = img->pixel(8, 8);
        const float mx = std::max({px[0], px[1], px[2]}) / 255.0f;
        const float mn = std::min({px[0], px[1], px[2]}) / 255.0f;
        return mx > 1e-3f ? (mx - mn) / mx : 0.0f;
    };

    const glm::vec3 cyan(0.04f, 0.85f, 1.0f);

    SECTION("a filmic curve alone turns a bright emitter white") {
        // The failure this feature exists to fix: at 25x scene white AgX has spent nearly all
        // the chroma. If this ever stops holding, the guard below is no longer measuring anything.
        REQUIRE(saturationOf(cyan * 25.0f, 0.0f) < 0.12f);
    }

    SECTION("retention holds the hue through the highlight") {
        REQUIRE(saturationOf(cyan * 25.0f, 0.6f) > 0.3f);
        REQUIRE(saturationOf(cyan * 50.0f, 0.6f) > 0.3f);
    }

    SECTION("the operator's own look stands below scene white") {
        // The effect ramps in above scene white, so an ordinary exposure is untouched and
        // enabling retention cannot restyle the rest of the image.
        const float plain = saturationOf(cyan, 0.0f);
        REQUIRE(saturationOf(cyan, 0.6f) == Catch::Approx(plain).margin(0.02f));
    }
}
