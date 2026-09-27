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

#include "assets/exr.hpp"
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
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <memory>
#include <string>
#include <utility>
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
// both sizes and what is compared is reach, not which pixels crossed a threshold. `warmOnly` keeps
// the warm one alone, for measuring one glow's reach without its neighbours' halos in the sum.
scene::Scene glowShot(bool warmOnly = false) {
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
    if (!warmOnly) {
        emitter("cool", {2.4f, -0.8f, 0.0f}, 0.14f, {0.3f, 0.6f, 1.0f}, 30.0f);
        emitter("large", {0.4f, 0.2f, 0.0f}, 0.45f, {0.9f, 0.85f, 1.0f}, 12.0f);
    }

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
// resolution-free statement of how far an effect reaches (ADR-279's measure). Meaningful on a frame
// with ONE source: every pixel of the frame is in the sum.
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

// Where a frame's light is, as a fraction of its size: the energy-weighted centroid. Taken from the
// frame without the effect, so the reach is measured about the source itself.
glm::dvec2 centroid(const gpu::ImageF& image) {
    glm::dvec2 sum(0.0);
    double total = 0.0;
    for (std::uint32_t y = 0; y < image.height; ++y) {
        for (std::uint32_t x = 0; x < image.width; ++x) {
            const float* p = image.pixel(x, y);
            const double e = std::max(0.0, static_cast<double>(p[0]) + static_cast<double>(p[1]) + static_cast<double>(p[2]));
            sum += e * glm::dvec2((x + 0.5) / image.width, (y + 0.5) / image.height);
            total += e;
        }
    }
    return total > 0.0 ? sum / total : glm::dvec2(0.5);
}

// Diagnostics: AVGEN_POST_RESOLUTION_DUMP=<dir> writes each compared contribution as an EXR.
void dumpIfAsked(const std::string& name, const gpu::ImageF& image) {
    const char* dir = std::getenv("AVGEN_POST_RESOLUTION_DUMP");
    if (dir == nullptr || *dir == '\0') {
        return;
    }
    std::filesystem::create_directories(dir);
    static_cast<void>(assets::writeExr(fs::path(dir) / (name + ".exr"), image.width, image.height, image.rgba, false));
}

struct Arms {
    double treatment = 0.0;
    double control = 0.0;
    double treatmentR90 = 0.0; // high / low: the warm emitter's 90% energy radius, alone in its frame
    double controlR90 = 0.0;
    rendering::PostStats highStats; // the treatment's high-resolution frame
};

using Configure = std::function<void(scene::PostSettings&)>;

// One effect's contribution -- the frame with it minus the frame without -- at the two sizes,
// compared after the box filter: once with a fixed reference (the treatment), once with each frame
// its own reference (the chain before ADR-917). The three-emitter shot gives the relative L1; the
// warm emitter alone gives the reach, whose sum would otherwise include its neighbours' halos.
Arms compareEffect(gpu::Context& ctx, gpu::ShaderLibrary& shaders, const Configure& enable, const char* name) {
    Arms out;
    const std::uint32_t sizes[2][2] = {{kLowW, kLowH}, {kLowW * kFactor, kLowH * kFactor}};
    for (const bool fixedReference : {true, false}) {
        gpu::ImageF contribution[2];
        double r90[2] = {0.0, 0.0};
        for (int i = 0; i < 2; ++i) {
            const float reference = fixedReference ? static_cast<float>(kLowH) : static_cast<float>(sizes[i][1]);
            for (const bool warmOnly : {false, true}) {
                scene::Scene off = glowShot(warmOnly);
                off.post.referenceHeight = reference;
                scene::Scene on = off;
                enable(on.post);
                const Rendered withEffect = render(ctx, shaders, on, sizes[i][0], sizes[i][1]);
                const Rendered without = render(ctx, shaders, off, sizes[i][0], sizes[i][1]);
                gpu::ImageF c = minus(withEffect.image, without.image);
                if (i == 1) {
                    c = boxDown(c, kFactor);
                }
                if (warmOnly) {
                    const glm::dvec2 at = centroid(i == 1 ? boxDown(without.image, kFactor) : without.image);
                    r90[i] = energyRadius(c, at.x, at.y, 0.9);
                } else {
                    dumpIfAsked(fmt::format("{}-{}-{}", name, fixedReference ? "treatment" : "control",
                                            i == 0 ? "low" : "high-down"),
                                c);
                    contribution[i] = std::move(c);
                    if (fixedReference && i == 1) {
                        out.highStats = withEffect.post;
                    }
                }
            }
        }
        const double err = relativeL1(contribution[0], contribution[1]);
        const double ratio = r90[0] > 0.0 ? r90[1] / r90[0] : 0.0;
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

    const Arms bloom = compareEffect(*ctx, shaders, [](scene::PostSettings& p) { p.bloomEnabled = true; }, "bloom");
    const Arms halation =
        compareEffect(*ctx, shaders, [](scene::PostSettings& p) { p.halationEnabled = true; }, "halation");
    const Arms streak = compareEffect(
        *ctx, shaders,
        [](scene::PostSettings& p) {
            p.bloomEnabled = true;
            p.bloomIntensity = 0.0f; // the streak reads the bloom pyramid, not the bloom's own add
            p.anamorphicEnabled = true;
        },
        "streak");
    WARN("relative L1 of the downsampled 1280x720 contribution against the 320x180 one, and the "
         "ratio of the warm emitter's 90% energy radius (1 = the same reach)");
    WARN("bloom     treatment " << bloom.treatment << " (r90 x" << bloom.treatmentR90 << ")  control "
                                << bloom.control << " (r90 x" << bloom.controlR90 << ")");
    WARN("halation  treatment " << halation.treatment << " (r90 x" << halation.treatmentR90 << ")  control "
                                << halation.control << " (r90 x" << halation.controlR90 << ")");
    WARN("streak    treatment " << streak.treatment << " (r90 x" << streak.treatmentR90 << ")  control "
                                << streak.control << " (r90 x" << streak.controlR90 << ")");

    // What reached the chain at four times the reference height: the frame boxed down two octaves
    // to the reference's size, then the reference's own pyramids -- six bloom levels, and five of
    // six halation levels at both sizes (its last authored level would be one texel tall at 180
    // lines, and is not built at either) -- and a streak four times as many quarter-resolution
    // texels long.
    CHECK(bloom.highStats.pixelScale == 4.0f);
    CHECK(bloom.highStats.pyramidBoxOctaves == 2u);
    CHECK(bloom.highStats.bloomLevels == 6u);
    CHECK(halation.highStats.halationLevels == 5u);
    CHECK(streak.highStats.anamorphicReach == 8.0f * 8.0f * 4.0f);

    // Measured on the first run with the frame boxed to the reference before the pyramids (M-series,
    // 2026-09-27): relative L1 bloom 0.073, halation 0.136, streak 0.051 against the unscaled
    // chain's 0.498, 0.782 and 1.439; the warm emitter's reach x1.003, x1.005 and x1.000 against
    // x0.286, x0.295 and x0.325. What remains is rasterisation: a box 5 px across at 180 lines is
    // drawn with a stair at its edge that the 720-line frame resolves. The bars leave that headroom
    // and sit far below every control.
    CHECK(bloom.treatment < 0.15);
    CHECK(halation.treatment < 0.2);
    CHECK(streak.treatment < 0.15);
    CHECK(bloom.control > 0.4);
    CHECK(halation.control > 0.4);
    CHECK(streak.control > 0.4);
    for (const Arms* arms : {&bloom, &halation, &streak}) {
        CHECK(std::abs(arms->treatmentR90 - 1.0) < 0.05); // the same reach, as a fraction of the frame
        CHECK(arms->controlR90 < 0.5);                     // the unscaled chain's: under half of it
    }
    CHECK(ctx->errorCount() == 0);
}

// ---- motion blur: the tiles follow the radius ------------------------------------------------------

namespace {

// How far the box moves between the two frames, in metres. At 6 m behind a 50 degree lens a frame
// is 5.6 m tall, so this is 77 px at 180 lines and 309 at 720; a 180 degree shutter halves that, and
// the radius clamp (30 px at the 180-line reference) saturates it at both sizes -- a 30 px smear and
// a 120 px one, the same fraction of the frame.
constexpr float kTravel = 2.4f;

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
    s.entities[0].transform.position = {-kTravel, 0.0f, 0.0f};
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

    // A long smear, saturated at the radius clamp at both sizes (kTravel). The reconstruction
    // (McGuire 2012) gathers along a pixel's 3x3 tile neighbourhood's velocity, so a pixel more
    // than a tile or two from the moving box never learns it moved: the smear reaches only as far
    // as the tiles do. At the 180-line reference a 20 px tile holds the 15 px half-smear. At 720
    // lines the treatment's tiles are 80 px and hold the 60 px half-smear; the control's stay
    // 20 px -- the chain before ADR-917 -- and cut the smear's outer part off.
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
    WARN("relative L1 of the downsampled smear: treatment " << std::setprecision(12) << error[0] << ", control "
                                                            << error[1]);
    // What reached the chain at 720 lines: the tile and the radius both four times their authored
    // pixels.
    CHECK(highStats.motionBlurTile == 80u);
    CHECK(highStats.motionBlurRadius == 120.0f);
    // Measured: treatment 0.094, control 0.355. The separable tile maximum that takes the 80 px
    // tiles gave the single pass's error to all twelve printed digits (a throwaway build, ADR-917).
    CHECK(error[0] < 0.15);
    CHECK(error[1] > 2.5 * error[0]);
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
    std::uint32_t boxed[2] = {0, 0};
    for (int i = 0; i < 2; ++i) {
        scene::Scene off = glowShot(true); // one source, so every pixel of the sum is its glow
        off.post.referenceHeight = i == 0 ? 720.0f : 360.0f;
        scene::Scene on = off;
        on.post.bloomEnabled = true;
        const Rendered withBloom = render(*ctx, shaders, on, 1280, 720);
        const Rendered without = render(*ctx, shaders, off, 1280, 720);
        const glm::dvec2 at = centroid(without.image);
        r90[i] = energyRadius(minus(withBloom.image, without.image), at.x, at.y, 0.9);
        levels[i] = withBloom.post.bloomLevels;
        boxed[i] = withBloom.post.pyramidBoxOctaves;
    }
    WARN("r90 at reference 720: " << r90[0] << ", at reference 360: " << r90[1]);
    // At reference 360 the 720-line frame is an octave finer: boxed down one octave, then the
    // reference's six levels, which now reach twice as far across the frame.
    CHECK(levels[0] == 6u);
    CHECK(boxed[0] == 0u);
    CHECK(levels[1] == 6u);
    CHECK(boxed[1] == 1u);
    CHECK(r90[1] > 1.6 * r90[0]); // measured x1.81
    CHECK(ctx->errorCount() == 0);
}

// ---- what it costs at Glowmere Valley 3's final size ---------------------------------------------

TEST_CASE("perf: the post chain at 7680x4320, scaled for a 1080-line reference against unscaled",
          "[.perf][post][resolution]") {
    // Hidden: it prints a measurement rather than asserting one. Run it under the GPU lock:
    //   tools/gpu-lock.sh build/release/tests/avgen_render_tests "perf: the post chain at 7680x4320*"
    // A 3840x2160 output at supersample 2 is a 7680x4320 chain, GV3's final. Two arms, both with
    // GV3's own post settings (render-post.md) and the same 240 px motion-blur radius -- which is
    // what GV3's 40 px at 720 lines became at 4320 before ADR-917, and what its recommended 60 px at
    // a 1080-line reference becomes now:
    //   reference 4320, radius 240: the chain before ADR-917 at this size (pixel scale 1);
    //   reference 1080, radius 60:  GV3's preview reference, scale 4 -- the pyramid two octaves
    //                               finer, streak and tiles four times as long, the look stage two
    //                               octaves down.
    // A full-frame moving backdrop, so the motion blur reconstructs everywhere.
    auto ctx = testsupport::gpuContextOrSkip();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    const char* stages[] = {"post/exposure", "post/motionblur", "post/lens", "post/bloom", "post/anamorphic",
                            "post/composite", "post/look", "post/fxaa"};
    for (const auto& [reference, radius] : {std::pair{4320.0f, 240.0f}, std::pair{1080.0f, 60.0f}}) {
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
        p.motionBlurMaxRadius = radius;
        p.antialias = 0.963f;
        p.look.colour = 0.12f; // the audit's recommendation, so the look stage runs
        p.look.lightWrap = 0.15f;
        p.look.localContrastRadius = 36.0f * reference / 1080.0f; // 36 px at 1080 lines in both arms
        s.camera.lens.shutterAngle = 172.0f;
        std::vector<double> post;
        std::vector<std::vector<double>> perStage(std::size(stages));
        rendering::PostStats last;
        std::uint64_t seen = 0;
        for (std::uint64_t frame = 0; frame < 64; ++frame) {
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
            const gpu::FrameTimeline& timeline = renderer.timeline();
            if (frame >= 16 && timeline.completedFrames() != seen) {
                seen = timeline.completedFrames();
                for (std::size_t k = 0; k < std::size(stages); ++k) {
                    perStage[k].push_back(timeline.msFor(stages[k]));
                }
            }
        }
        auto median = [](std::vector<double> v) {
            std::sort(v.begin(), v.end());
            return v.empty() ? -1.0 : v[v.size() / 2];
        };
        std::string breakdown;
        for (std::size_t k = 0; k < std::size(stages); ++k) {
            breakdown += fmt::format(" {} {:.2f}", stages[k] + 5, median(perStage[k]));
        }
        fmt::print("reference {:.0f}, radius {:.0f} (pixel scale {:.2f}): post chain median {:.3f} ms over {} frames; "
                   "bloom levels {}, streak reach {:.0f}, motion-blur tile {} px and radius {:.0f} px, look octaves "
                   "{}, passes {}\n  per stage (median ms):{}\n",
                   reference, radius, last.pixelScale, median(post), post.size(), last.bloomLevels,
                   last.anamorphicReach, last.motionBlurTile, last.motionBlurRadius, last.lookOctaves, last.passes,
                   breakdown);
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
    WARN("look contribution, relative L1 against 480x270: 1920x1080 " << treatment
                                                                             << ", the same with half the radius "
                                                                             << control);
    CHECK(lowOctaves == 0u);  // 6 texels of sigma at 270 lines: within the budget, nothing changes
    CHECK(highOctaves == 2u); // 24 at 1080: two octaves down to 6
    // Measured: 0.029 with the octave descent; 0.310 from a throwaway build of the chain before it
    // (the gaussian truncated at the tap budget), which fails this bar; 0.540 for the half-radius arm.
    CHECK(treatment < 0.08);
    CHECK(control > 2.0 * treatment);
    CHECK(bench.ctx->errorCount() == 0);
}
