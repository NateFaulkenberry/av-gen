// ADR-917: one set of post parameters gives the same look at any resolution.
//
// The bloom pyramid, the halation pyramid, the anamorphic streak and the motion-blur tiles used to
// be counted in pixels of whatever frame the chain was handed (ADR-279 measured the bloom), so a
// 960x540 preview and a 3840x2160 x2 final of one project showed two different looks. They now
// scale with `post/referenceHeight`, the frame height the values were tuned at.
//
// The measurement is the one the brief asks for: the same shot rendered at two resolutions, the
// larger box-filtered down to the smaller, and the POST CONTRIBUTION compared -- the frame with the
// effect minus the same frame without it, so what rasterisation does differently at the two sizes
// cancels and what is left is the effect.
//
// The control arm is the chain before ADR-917, expressed through the same build: each frame gets
// its own height as its reference, so every pyramid, streak and tile is a pixel count of the frame
// in hand -- and the one radius that did scale before (motion blur's) is given the height / 720
// it scaled by then. The control must fail the same criterion by a wide margin, or the test could
// not have caught the defect it was written for.

#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/post_settings.hpp"
#include "scene/scene.hpp"

#include "support/post_bench.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

constexpr std::uint32_t kLowW = 320;
constexpr std::uint32_t kLowH = 180;
constexpr std::uint32_t kFactor = 4;

// Black, no sky, no lights: three self-lit boxes of fixed WORLD size, so each is a fixed fraction of
// the frame at every resolution. One small and warm (halation reads warm highlights), one small and
// cool, one larger. All well above the bloom threshold, so the soft knee sees the same thing at
// both sizes and what is compared is reach, not which pixels crossed a threshold.
scene::Scene glowShot() {
    scene::Scene s;
    s.environment.backgroundColor = glm::vec3(0.0f);
    s.environment.showSkybox = false;
    s.environment.environmentIntensity = 0.0f;
    s.camera.position = {0.0f, 0.0f, 12.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    auto emitter = [&](const char* name, glm::vec3 at, float half, glm::vec3 colour, float intensity) {
        const auto mesh = s.addMesh(scene::makeCube(half));
        auto& e = s.addEntity(name, mesh);
        e.transform.position = at;
        e.material.baseColor = glm::vec3(0.0f);
        e.material.emissiveColor = colour;
        e.material.emissiveIntensity = intensity;
        e.material.unlit = true;
    };
    emitter("warm", {-2.6f, 0.9f, 0.0f}, 0.16f, {1.0f, 0.45f, 0.15f}, 24.0f);
    emitter("cool", {2.4f, -0.8f, 0.0f}, 0.14f, {0.3f, 0.6f, 1.0f}, 30.0f);
    emitter("large", {0.4f, 0.2f, 0.0f}, 0.45f, {0.9f, 0.85f, 1.0f}, 12.0f);

    scene::PostSettings& p = s.post;
    p.bloomEnabled = false;
    p.bloomIntensity = 1.0f;
    p.bloomThreshold = 1.0f;
    p.bloomKnee = 0.5f;
    p.bloomRadius = 1.0f;
    p.bloomLevels = 6;
    p.halationEnabled = false;
    p.halationIntensity = 0.8f;
    p.halationThreshold = 2.0f;
    p.anamorphicEnabled = false;
    p.anamorphicIntensity = 0.6f;
    p.anamorphicStretch = 8.0f;
    p.tonemap = scene::TonemapOperator::Clamp;
    return s;
}

struct Rendered {
    gpu::ImageF image;
    rendering::PostStats post;
};

// A fresh renderer per frame, so no history of any kind carries from one arm to the next.
Rendered render(gpu::Context& ctx, gpu::ShaderLibrary& shaders, const scene::Scene& s, std::uint32_t w,
                std::uint32_t h) {
    rendering::SceneRenderer renderer(ctx, shaders);
    REQUIRE(renderer.init().has_value());
    FrameTime time{};
    auto image = renderer.renderToImageFloat(s, time, w, h);
    REQUIRE(image.has_value());
    return {std::move(*image), renderer.stats().post};
}

// A box filter by an integer factor: what a supersampled frame's resolve does, and the fair way to
// bring the larger render onto the smaller's grid.
gpu::ImageF boxDown(const gpu::ImageF& in, std::uint32_t factor) {
    gpu::ImageF out;
    out.width = in.width / factor;
    out.height = in.height / factor;
    out.rgba.assign(static_cast<std::size_t>(out.width) * out.height * 4, 0.0f);
    const float norm = 1.0f / static_cast<float>(factor * factor);
    for (std::uint32_t y = 0; y < out.height; ++y) {
        for (std::uint32_t x = 0; x < out.width; ++x) {
            float* dst = out.rgba.data() + (static_cast<std::size_t>(y) * out.width + x) * 4;
            for (std::uint32_t dy = 0; dy < factor; ++dy) {
                for (std::uint32_t dx = 0; dx < factor; ++dx) {
                    const float* src = in.pixel(x * factor + dx, y * factor + dy);
                    for (int c = 0; c < 3; ++c) {
                        dst[c] += src[c] * norm;
                    }
                }
            }
            dst[3] = 1.0f;
        }
    }
    return out;
}

gpu::ImageF minus(const gpu::ImageF& a, const gpu::ImageF& b) {
    REQUIRE(a.width == b.width);
    REQUIRE(a.height == b.height);
    gpu::ImageF out = a;
    for (std::size_t i = 0; i < out.rgba.size(); ++i) {
        out.rgba[i] = (i % 4 == 3) ? 1.0f : a.rgba[i] - b.rgba[i];
    }
    return out;
}

// sum |a - b| / sum |a| over RGB: how much of the reference contribution the other one gets wrong.
double relativeL1(const gpu::ImageF& reference, const gpu::ImageF& other) {
    double num = 0.0;
    double den = 0.0;
    for (std::size_t i = 0; i < reference.rgba.size(); ++i) {
        if (i % 4 == 3) {
            continue;
        }
        num += std::abs(static_cast<double>(reference.rgba[i]) - static_cast<double>(other.rgba[i]));
        den += std::abs(static_cast<double>(reference.rgba[i]));
    }
    return den > 0.0 ? num / den : 0.0;
}

// The radius, in frame heights, that holds `fraction` of a contribution's energy around (u, v): the
// resolution-free statement of how far an effect reaches (ADR-279's measure).
double energyRadius(const gpu::ImageF& image, double u, double v, double fraction) {
    const double cx = u * image.width;
    const double cy = v * image.height;
    std::vector<std::pair<double, double>> samples;
    samples.reserve(static_cast<std::size_t>(image.width) * image.height);
    double total = 0.0;
    for (std::uint32_t y = 0; y < image.height; ++y) {
        for (std::uint32_t x = 0; x < image.width; ++x) {
            const float* p = image.pixel(x, y);
            const double e =
                std::max(0.0, static_cast<double>(p[0]) + static_cast<double>(p[1]) + static_cast<double>(p[2]));
            if (e <= 0.0) {
                continue;
            }
            samples.emplace_back(std::hypot(x + 0.5 - cx, y + 0.5 - cy) / image.height, e);
            total += e;
        }
    }
    std::sort(samples.begin(), samples.end());
    double run = 0.0;
    for (const auto& [d, e] : samples) {
        run += e;
        if (run >= fraction * total) {
            return d;
        }
    }
    return 0.0;
}

struct Arms {
    double treatment = 0.0;
    double control = 0.0;
    double treatmentR90 = 0.0; // high / low, around the warm emitter
    double controlR90 = 0.0;
    rendering::PostStats highStats; // the treatment's high-resolution frame
};

using Configure = std::function<void(scene::PostSettings&)>;

// One effect's contribution at the two sizes, compared after the box filter: once with a fixed
// reference (the treatment), once with each frame its own reference (the chain before ADR-917).
Arms compareEffect(gpu::Context& ctx, gpu::ShaderLibrary& shaders, const Configure& enable) {
    Arms out;
    for (const bool fixedReference : {true, false}) {
        gpu::ImageF contribution[2];
        const std::uint32_t sizes[2][2] = {{kLowW, kLowH}, {kLowW * kFactor, kLowH * kFactor}};
        for (int i = 0; i < 2; ++i) {
            scene::Scene off = glowShot();
            off.post.referenceHeight = fixedReference ? static_cast<float>(kLowH) : static_cast<float>(sizes[i][1]);
            scene::Scene on = off;
            enable(on.post);
            const Rendered withEffect = render(ctx, shaders, on, sizes[i][0], sizes[i][1]);
            const Rendered without = render(ctx, shaders, off, sizes[i][0], sizes[i][1]);
            contribution[i] = minus(withEffect.image, without.image);
            if (fixedReference && i == 1) {
                out.highStats = withEffect.post;
            }
        }
        const gpu::ImageF down = boxDown(contribution[1], kFactor);
        const double err = relativeL1(contribution[0], down);
        // The warm emitter sits at about (0.30, 0.37) of this frame; a window around it keeps the
        // other two emitters' halos out of its radius.
        const double r90Low = energyRadius(contribution[0], 0.30, 0.37, 0.9);
        const double r90High = energyRadius(down, 0.30, 0.37, 0.9);
        const double ratio = r90Low > 0.0 ? r90High / r90Low : 0.0;
        if (fixedReference) {
            out.treatment = err;
            out.treatmentR90 = ratio;
        } else {
            out.control = err;
            out.controlR90 = ratio;
        }
    }
    return out;
}

} // namespace

TEST_CASE("bloom, halation and the anamorphic streak keep their look across a fourfold resolution change",
          "[gpu][post][resolution]") {
    auto ctx = testsupport::gpuContextOrSkip();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});

    const Arms bloom = compareEffect(*ctx, shaders, [](scene::PostSettings& p) { p.bloomEnabled = true; });
    const Arms halation = compareEffect(*ctx, shaders, [](scene::PostSettings& p) { p.halationEnabled = true; });
    const Arms streak = compareEffect(*ctx, shaders, [](scene::PostSettings& p) {
        p.bloomEnabled = true;
        p.bloomIntensity = 0.0f; // the streak reads the bloom pyramid, not the bloom's own add
        p.anamorphicEnabled = true;
    });
    UNSCOPED_INFO("relative L1 of the downsampled 1280x720 contribution against the 320x180 one, and the "
                  "ratio of the warm emitter's 90% energy radius (1 = the same reach)");
    UNSCOPED_INFO("bloom     treatment " << bloom.treatment << " (r90 x" << bloom.treatmentR90 << ")  control "
                                         << bloom.control << " (r90 x" << bloom.controlR90 << ")");
    UNSCOPED_INFO("halation  treatment " << halation.treatment << " (r90 x" << halation.treatmentR90
                                         << ")  control " << halation.control << " (r90 x" << halation.controlR90 << ")");
    UNSCOPED_INFO("streak    treatment " << streak.treatment << " (r90 x" << streak.treatmentR90 << ")  control "
                                         << streak.control << " (r90 x" << streak.controlR90 << ")");

    // What reached the chain at four times the reference height: two more pyramid levels, a streak
    // four times as many quarter-resolution texels long. The halation pyramid starts at a quarter of
    // the frame, so at both sizes its last authored level would be one texel tall and is not built
    // (5 of 6 at 180 lines, 7 of 8 at 720): the same level lost at both, which is the point.
    CHECK(bloom.highStats.pixelScale == 4.0f);
    CHECK(bloom.highStats.bloomLevels == 8u);
    CHECK(halation.highStats.halationLevels == 7u);
    CHECK(streak.highStats.anamorphicReach == 8.0f * 8.0f * 4.0f);

    CHECK(bloom.treatment < 0.15);
    CHECK(halation.treatment < 0.15);
    CHECK(streak.treatment < 0.15);
    CHECK(bloom.control > 0.4);
    CHECK(halation.control > 0.4);
    CHECK(streak.control > 0.4);
    CHECK(ctx->errorCount() == 0);
}

// ---- motion blur: the tiles follow the radius ------------------------------------------------------

namespace {

// One small self-lit box on black, moved between two frames so the second has a real velocity.
scene::Scene movingBox() {
    scene::Scene s;
    s.environment.backgroundColor = glm::vec3(0.0f);
    s.environment.showSkybox = false;
    s.environment.environmentIntensity = 0.0f;
    s.camera.position = {0.0f, 0.0f, 6.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    s.camera.lens.shutterAngle = 180.0f;
    const auto box = s.addMesh(scene::makeCube(0.2f));
    auto& e = s.addEntity("mover", box);
    e.material.baseColor = glm::vec3(0.0f);
    e.material.emissiveColor = glm::vec3(1.0f);
    e.material.emissiveIntensity = 6.0f;
    e.material.unlit = true;
    s.post.bloomEnabled = false;
    s.post.bloomIntensity = 0.0f;
    s.post.tonemap = scene::TonemapOperator::Clamp;
    s.post.motionBlurSamples = 32;
    return s;
}

Rendered renderMoved(gpu::Context& ctx, gpu::ShaderLibrary& shaders, scene::Scene s, std::uint32_t w,
                     std::uint32_t h) {
    rendering::SceneRenderer renderer(ctx, shaders);
    REQUIRE(renderer.init().has_value());
    FrameTime time;
    time.deltaTime = 1.0 / 30.0;
    s.entities[0].transform.position = {-0.9f, 0.0f, 0.0f};
    time.frameIndex = 0;
    time.renderTime = 0.0;
    REQUIRE(renderer.renderToImageFloat(s, time, w, h).has_value());
    s.entities[0].transform.position = {0.0f, 0.0f, 0.0f};
    time.frameIndex = 1;
    time.renderTime = 1.0 / 30.0;
    auto second = renderer.renderToImageFloat(s, time, w, h);
    REQUIRE(second.has_value());
    return {std::move(*second), renderer.stats().post};
}

} // namespace

TEST_CASE("motion blur keeps its smear across a fourfold resolution change", "[gpu][post][resolution]") {
    auto ctx = testsupport::gpuContextOrSkip();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});

    // A long smear: 0.9 m of travel in a frame is a third of this frame's height, and a 180 degree
    // shutter blurs half of it -- about 30 px at 180 lines and 120 at 720. The authored tile (20 px
    // at the reference) and radius (30 px at the reference, 180 lines) are chosen so the smear
    // fits the tiles at the reference, as the defaults do at 720.
    const std::uint32_t sizes[2][2] = {{kLowW, kLowH}, {kLowW * kFactor, kLowH * kFactor}};
    double error[2] = {0.0, 0.0};
    rendering::PostStats highStats;
    for (const bool fixedReference : {true, false}) {
        gpu::ImageF contribution[2];
        for (int i = 0; i < 2; ++i) {
            const auto h = static_cast<float>(sizes[i][1]);
            scene::Scene on = movingBox();
            on.post.motionBlurAmount = 1.0f;
            on.post.motionBlurTileSize = 20;
            if (fixedReference) {
                on.post.referenceHeight = static_cast<float>(kLowH);
                on.post.motionBlurMaxRadius = 30.0f;
            } else {
                // The chain before ADR-917: the tile a pixel count of this frame, the radius scaled
                // by height / 720 from the same 120 px it reaches at 720 lines.
                on.post.referenceHeight = h;
                on.post.motionBlurMaxRadius = 120.0f * h / 720.0f;
            }
            scene::Scene off = on;
            off.post.motionBlurAmount = 0.0f;
            const Rendered blurred = renderMoved(*ctx, shaders, on, sizes[i][0], sizes[i][1]);
            const Rendered sharp = renderMoved(*ctx, shaders, off, sizes[i][0], sizes[i][1]);
            contribution[i] = minus(blurred.image, sharp.image);
            if (fixedReference && i == 1) {
                highStats = blurred.post;
            }
        }
        error[fixedReference ? 0 : 1] = relativeL1(contribution[0], boxDown(contribution[1], kFactor));
    }
    UNSCOPED_INFO("relative L1 of the downsampled smear: treatment " << error[0] << ", control " << error[1]);
    // What reached the chain at 720 lines: the tile and the radius both four times their authored
    // pixels.
    CHECK(highStats.motionBlurTile == 80u);
    CHECK(highStats.motionBlurRadius == 120.0f);
    CHECK(error[0] < 0.25);
    CHECK(error[1] > 2.0 * error[0]);
    CHECK(ctx->errorCount() == 0);
}

// ---- the reference height reaches the picture ------------------------------------------------------

TEST_CASE("post/referenceHeight moves the glow's reach at a fixed frame size", "[gpu][post][resolution]") {
    // The other half of reaching the output: at ONE frame size, halving the reference doubles every
    // pixel-sized value, so the bloom's reach must grow. Without this a reference that bound and
    // did nothing would pass every test above that compares two sizes at the same reference.
    auto ctx = testsupport::gpuContextOrSkip();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    double r90[2] = {0.0, 0.0};
    std::uint32_t levels[2] = {0, 0};
    for (int i = 0; i < 2; ++i) {
        scene::Scene off = glowShot();
        off.post.referenceHeight = i == 0 ? 720.0f : 360.0f;
        scene::Scene on = off;
        on.post.bloomEnabled = true;
        const Rendered withBloom = render(*ctx, shaders, on, 1280, 720);
        const Rendered without = render(*ctx, shaders, off, 1280, 720);
        r90[i] = energyRadius(minus(withBloom.image, without.image), 0.30, 0.37, 0.9);
        levels[i] = withBloom.post.bloomLevels;
    }
    UNSCOPED_INFO("r90 at reference 720: " << r90[0] << ", at reference 360: " << r90[1]);
    CHECK(levels[0] == 6u);
    CHECK(levels[1] == 7u);
    CHECK(r90[1] > 1.4 * r90[0]);
    CHECK(ctx->errorCount() == 0);
}

// ---- what it costs at Glowmere Valley 3's final size ---------------------------------------------

TEST_CASE("perf: the post chain at 7680x4320, scaled for a 1080-line reference against unscaled",
          "[.perf][post][resolution]") {
    // Hidden: it prints a measurement rather than asserting one. Run it under the GPU lock:
    //   tools/gpu-lock.sh build/release/tests/avgen_render_tests "perf: the post chain at 7680x4320*"
    // A 3840x2160 output at supersample 2 is a 7680x4320 chain. Reference 4320 is pixel scale 1 --
    // the chain before ADR-917 at this size; reference 1080 is a 960x540 x2 preview's, scale 4: two
    // more pyramid levels, a streak and tiles four times as long, the look stage two octaves down.
    // GV3's own post settings (render-post.md), with a full-frame moving surface so motion blur
    // reconstructs everywhere.
    auto ctx = testsupport::gpuContextOrSkip();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    for (const float reference : {4320.0f, 1080.0f}) {
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        rendering::QualitySettings q = renderer.qualitySettings();
        q.renderScale = 2.0f;
        renderer.setQualitySettings(q);
        scene::Scene s = glowShot();
        const auto wall = s.addMesh(scene::makeCube(1.0f));
        auto& backdrop = s.addEntity("backdrop", wall);
        backdrop.transform.position = {0.0f, 0.0f, -30.0f};
        backdrop.transform.scale = {80.0f, 50.0f, 1.0f};
        backdrop.material.baseColor = glm::vec3(0.4f);
        backdrop.material.emissiveColor = glm::vec3(0.2f, 0.25f, 0.3f);
        backdrop.material.emissiveIntensity = 1.0f;
        backdrop.material.unlit = true;
        scene::PostSettings& p = s.post;
        p.referenceHeight = reference;
        p.bloomEnabled = true;
        p.bloomIntensity = 0.912f;
        p.bloomThreshold = 1.761f;
        p.bloomKnee = 0.5f;
        p.bloomRadius = 1.15f;
        p.bloomLevels = 6;
        p.anamorphicEnabled = true;
        p.anamorphicIntensity = 0.25f;
        p.anamorphicStretch = 10.386f;
        p.motionBlurAmount = 0.954f;
        p.motionBlurSamples = 16;
        p.motionBlurTileSize = 20;
        p.motionBlurMaxRadius = 60.0f;
        p.antialias = 0.963f;
        p.look.colour = 0.12f;      // the audit's recommendation, so the look stage runs
        p.look.lightWrap = 0.15f;
        p.look.localContrastRadius = 36.0f;
        s.camera.lens.shutterAngle = 172.0f;
        std::vector<double> post;
        rendering::PostStats last;
        for (std::uint64_t frame = 0; frame < 48; ++frame) {
            s.camera.position.x = 0.02f * static_cast<float>(frame);
            s.camera.target.x = s.camera.position.x + 0.01f * static_cast<float>(frame % 7);
            FrameTime time;
            time.frameIndex = frame;
            time.renderTime = static_cast<double>(frame) / 60.0;
            time.deltaTime = 1.0 / 60.0;
            REQUIRE(renderer.renderFrame(s, time, 3840, 2160).has_value());
            last = renderer.stats().post;
            if (frame >= 16 && last.postMs > 0.0) {
                post.push_back(last.postMs);
            }
        }
        std::sort(post.begin(), post.end());
        const double median = post.empty() ? -1.0 : post[post.size() / 2];
        fmt::print("reference {:.0f} (pixel scale {:.2f}): post chain median {:.3f} ms over {} frames; "
                   "bloom levels {}, streak reach {:.0f}, motion-blur tile {} px, look octaves {}, passes {}\n",
                   reference, last.pixelScale, median, post.size(), last.bloomLevels, last.anamorphicReach,
                   last.motionBlurTile, last.lookOctaves, last.passes);
    }
    CHECK(ctx->errorCount() == 0);
}

// ---- the look stage's local mean holds its radius where the tap budget runs out ------------------

namespace {

// A dark card with a bright block and a mid-grey block, both a fixed FRACTION of the frame, as an
// HDR image written straight into the chain (tests/support/post_bench.hpp): the look stage's local
// mean, its local contrast and its light wrap all act at the blocks' edges.
testsupport::SyntheticHdr lookCard(gpu::Context& ctx, std::uint32_t w, std::uint32_t h) {
    testsupport::Canvas canvas(w, h);
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            canvas.set(x, y, 0.05f);
        }
    }
    canvas.fillRect(w * 3 / 16, h * 3 / 8, w / 8, h / 4, 6.0f);
    canvas.fillRect(w * 9 / 16, h * 3 / 8, w / 6, h / 4, 0.9f);
    return testsupport::makeHdr(ctx, w, h, canvas.rgba);
}

// The look stage's contribution: what it adds to the composite it was handed.
gpu::ImageF lookContribution(testsupport::PostBench& bench, std::uint32_t w, std::uint32_t h,
                             const scene::PostSettings& settings, std::uint32_t& octaves) {
    const testsupport::SyntheticHdr hdr = lookCard(*bench.ctx, w, h);
    const std::vector<testsupport::CapturedStage> stages = bench.run(hdr, settings);
    octaves = bench.post->stats().lookOctaves;
    const gpu::ImageF* composite = nullptr;
    const gpu::ImageF* look = nullptr;
    for (const testsupport::CapturedStage& s : stages) {
        if (s.name == "composite") composite = &s.image;
        if (s.name == "look") look = &s.image;
    }
    REQUIRE(composite != nullptr);
    REQUIRE(look != nullptr);
    return minus(*look, *composite);
}

} // namespace

TEST_CASE("the look stage's local mean keeps its radius where its tap budget runs out",
          "[gpu][post][resolution]") {
    // At four times the reference height the default 24 px radius is a 24-texel sigma at quarter
    // resolution, 72 taps a side against a budget of 32. Before ADR-917 the gaussian was cut off
    // there -- at 1.3 sigma -- so the local mean, and the local contrast and light wrap built on it,
    // narrowed at high resolution. Now the low-pass goes down two octaves instead. The control arm
    // below is the high-resolution frame with half the radius, which is roughly what the cut-off
    // did, and it must miss by far more than the treatment.
    testsupport::PostBench bench = testsupport::PostBench::make();
    scene::PostSettings settings;
    settings.bloomEnabled = false;
    settings.bloomIntensity = 0.0f;
    settings.referenceHeight = 270.0f;
    settings.look.localContrast = 0.6f;
    settings.look.lightWrap = 0.5f;
    settings.look.localContrastRadius = 24.0f;
    std::uint32_t lowOctaves = 0;
    std::uint32_t highOctaves = 0;
    const gpu::ImageF low = lookContribution(bench, 480, 270, settings, lowOctaves);
    const gpu::ImageF high = lookContribution(bench, 1920, 1080, settings, highOctaves);
    scene::PostSettings narrow = settings;
    narrow.look.localContrastRadius = 12.0f;
    std::uint32_t narrowOctaves = 0;
    const gpu::ImageF highNarrow = lookContribution(bench, 1920, 1080, narrow, narrowOctaves);

    const double treatment = relativeL1(low, boxDown(high, 4));
    const double control = relativeL1(low, boxDown(highNarrow, 4));
    UNSCOPED_INFO("look contribution, relative L1 against 480x270: 1920x1080 " << treatment
                                                                             << ", the same with half the radius "
                                                                             << control);
    CHECK(lowOctaves == 0u);  // 6 texels of sigma at 270 lines: within the budget, nothing changes
    CHECK(highOctaves == 2u); // 24 at 1080: two octaves down to 6
    CHECK(treatment < 0.2);
    CHECK(control > 2.0 * treatment);
    CHECK(bench.ctx->errorCount() == 0);
}
