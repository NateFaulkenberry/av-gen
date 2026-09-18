// HDR / Exposure / Bloom Lab -- the GPU arms. See docs/hdr-lab/README.md.
//
// Two harnesses, because this lab's subject spans a boundary no single one crosses:
//
//   1. `testsupport::PostBench` -- the post chain alone, over an RGBA16Float texture the CPU
//      wrote. Every intermediate the chain renders comes back named and at the resolution the
//      chain chose. This is where the bloom pyramid, the bright pass and the wide tier are
//      measured, because the input provably has no structure in it.
//
//   2. `HdrBench` -- the whole renderer over the lab's fixture, read back TWICE: once as the
//      scene-linear HDR the tonemap pass was handed (`renderToImageFloat`) and once as the display
//      bytes it produced (`renderToImage`). The pair is the tone curve itself, measured on the same
//      frame rather than modelled on the CPU. A CPU model of AgX would be a second opinion about
//      the operator, and ADR-182 is explicit that an instrument which cannot disagree with the
//      thing it measures has measured nothing.
//
// The fixture is deliberately UNLIT (`material.unlit`, `pbr_shade.wgsl`'s `object.flags.z` branch),
// with no lights, no sky and no fog, so a patch's scene-linear radiance is exactly the number in
// the scene file: `color = baseColor + emissive` and nothing else. Every calibrated value below is
// therefore a value this lab controls, not one it has to infer from a BRDF -- which is the boundary
// between this lab and the Lighting Lab, drawn in the fixture rather than argued about.

#include "app/engine.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "params/parameter_set.hpp"
#include "rendering/post_processor.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/composition.hpp"
#include "scene/post_settings.hpp"
#include "scene/scene.hpp"
#include "support/post_bench.hpp"

#include <fmt/format.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;
using testsupport::Canvas;
using testsupport::CapturedStage;
using testsupport::makeHdr;
using testsupport::PostBench;
using testsupport::SyntheticHdr;

namespace {

float luminance(float r, float g, float b) { return 0.2126f * r + 0.7152f * g + 0.0722f * b; }

const gpu::ImageF& stageNamed(const std::vector<CapturedStage>& stages, std::string_view name) {
    for (const CapturedStage& s : stages) {
        if (s.name == name) {
            return s.image;
        }
    }
    FAIL("no captured stage named " << name);
    return stages.front().image;
}

struct Stat {
    float peak = 0.0f;
    double mean = 0.0;
    double total = 0.0;
};

Stat statOf(const gpu::ImageF& image) {
    Stat s;
    const std::size_t n = static_cast<std::size_t>(image.width) * image.height;
    for (std::size_t i = 0; i < n; ++i) {
        const float* px = image.rgba.data() + i * 4;
        const float l = luminance(px[0], px[1], px[2]);
        s.peak = std::max(s.peak, l);
        s.total += static_cast<double>(l);
    }
    s.mean = n == 0 ? 0.0 : s.total / static_cast<double>(n);
    return s;
}

// The luminance-weighted centroid, in *normalised frame coordinates* so two levels at different
// resolutions are comparable. This is the measurement the pyramid's alignment question is about:
// a halo that is not centred on the thing that made it is a halo that has been resampled wrong.
struct Centroid {
    double u = 0.0;
    double v = 0.0;
    double weight = 0.0;
};

Centroid centroidOf(const gpu::ImageF& image, double u, double v, double radiusTexels);
Centroid centroidOf(const gpu::ImageF& image) { return centroidOf(image, 0.5, 0.5, 1e9); }

// The centroid of the energy WITHIN `radiusTexels` of (u, v). The unwindowed version is the wrong
// statistic for a halo wider than the frame: at bloomRadius 1.9 the tent reaches +/- 120 frame
// pixels from the coarsest level, ClampToEdge truncates the far side against the frame edge, and
// the centroid of a clipped symmetric bump moves toward the frame centre by construction. That is
// the instrument moving, not the halo, and it cost a run to tell them apart.
Centroid centroidOf(const gpu::ImageF& image, double u, double v, double radiusTexels) {
    Centroid c;
    const double cx = u * image.width - 0.5;
    const double cy = v * image.height - 0.5;
    for (std::uint32_t y = 0; y < image.height; ++y) {
        for (std::uint32_t x = 0; x < image.width; ++x) {
            if (std::hypot(static_cast<double>(x) - cx, static_cast<double>(y) - cy) > radiusTexels) {
                continue;
            }
            const float* px = image.rgba.data() + (static_cast<std::size_t>(y) * image.width + x) * 4;
            const double w = static_cast<double>(luminance(px[0], px[1], px[2]));
            if (w <= 0.0) {
                continue;
            }
            c.u += w * ((static_cast<double>(x) + 0.5) / image.width);
            c.v += w * ((static_cast<double>(y) + 0.5) / image.height);
            c.weight += w;
        }
    }
    if (c.weight > 0.0) {
        c.u /= c.weight;
        c.v /= c.weight;
    }
    return c;
}

// Post settings with every optional stage off and the grade at identity, so a measurement of the
// chain is a measurement of the one stage a case turns on. The composite always runs; with these
// values it is arithmetically the identity (contrast 1, saturation 1, gain 1, lift 0, gamma 1).
scene::PostSettings neutralPost() {
    scene::PostSettings s;
    s.bloomEnabled = false;
    s.bloomIntensity = 0.0f;
    s.halationEnabled = false;
    s.anamorphicEnabled = false;
    s.dofEnabled = false;
    s.dofMaxRadius = 0.0f;
    s.tiltShiftEnabled = false;
    s.tiltShiftMaxRadius = 0.0f;
    s.motionBlurAmount = 0.0f;
    s.antialias = 0.0f;
    s.sharpen = 0.0f;
    s.chromaticAberration = 0.0f;
    s.distortion = 0.0f;
    s.contrast = 1.0f;
    s.saturation = 1.0f;
    return s;
}

} // namespace

// ---- the pyramid's alignment, at frame sizes whose levels do not halve exactly -------------------
//
// `PostProcessor::run` sizes each pyramid level with integer division: `w = max(1, w / 2)`. At a
// frame whose dimension is not a multiple of 2^levels that truncation makes a level's resolution
// slightly MORE than half the one above it, while the pass that reads it addresses it in
// normalised uv -- which assumes exactly half. The question this arm asks is whether that
// difference is visible as a displacement of a highlight's halo away from the highlight.
//
// The measurement is the luminance centroid in normalised frame coordinates, which is comparable
// across resolutions by construction, taken on every captured level of a single impulse.
TEST_CASE("the bloom pyramid's levels agree on where a highlight is", "[hdr][lab][gpu][.probe]") {
    PostBench bench = PostBench::make();
    scene::PostSettings settings = neutralPost();
    settings.bloomEnabled = true;
    settings.bloomIntensity = 1.0f;
    settings.bloomThreshold = 1.0f;
    settings.bloomKnee = 0.5f;
    settings.bloomRadius = 1.0f;
    settings.bloomLevels = 6;

    struct Size {
        std::uint32_t w, h;
        const char* why;
    };
    // 512x288: 288 -> 144 -> 72 -> 36 -> 18 -> 9, every step exact until the last.
    // 1280x720: 720 -> 360 -> 180 -> 90 -> 45 -> 22, the truncation lands at level 4.
    // 1920x1080: 1080 -> 540 -> 270 -> 135 -> 67 -> 33, the truncation lands at level 3.
    const Size sizes[] = {
        {512, 288, "exact until the last level"},
        {1280, 720, "truncates at level 4 (45 -> 22)"},
        {1920, 1080, "truncates at level 3 (135 -> 67)"},
    };
    for (const Size& size : sizes) {
        Canvas canvas(size.w, size.h);
        // Three quarters of the way down and across: the drift, if there is one, accumulates with
        // distance from the origin of the uv mapping, so the centre would be the one place it
        // cannot be seen. That is ADR-182's centred box, and it is why the impulse is not centred.
        const std::uint32_t px = size.w * 3 / 4;
        const std::uint32_t py = size.h * 3 / 4;
        canvas.set(px, py, 64.0f);
        SyntheticHdr hdr = makeHdr(*bench.ctx, size.w, size.h, canvas.rgba);
        const auto stages = bench.run(hdr, settings);
        const double sourceU = (static_cast<double>(px) + 0.5) / size.w;
        const double sourceV = (static_cast<double>(py) + 0.5) / size.h;
        fmt::print("\n== {}x{} -- {} ==\n  impulse at u={:.5f} v={:.5f}\n", size.w, size.h, size.why, sourceU, sourceV);
        for (const CapturedStage& s : stages) {
            const Centroid c = centroidOf(s.image);
            if (c.weight <= 0.0) {
                fmt::print("  {:18} {:4}x{:<4} (empty)\n", s.name, s.image.width, s.image.height);
                continue;
            }
            fmt::print("  {:18} {:4}x{:<4} u={:.5f} ({:+.2f} px) v={:.5f} ({:+.2f} px)\n", s.name, s.image.width,
                       s.image.height, c.u, (c.u - sourceU) * size.w, c.v, (c.v - sourceV) * size.h);
        }
        bench.pool->endFrame();
    }
    CHECK(bench.ctx->errorCount() == 0);
}

// ---- the bright pass is a luminance test, and the tone curve is per channel ----------------------
//
// `fs_prefilter` thresholds `luminance(c)` -- 0.2126 R + 0.7152 G + 0.0722 B -- while every tone
// curve in `tonemap.wgsl` compresses each channel on its own. The two disagree about what "bright"
// means by the ratio of the luminance weights, which is 9.9x between green and blue. This arm
// measures the disagreement rather than asserting it: for a set of single-channel emitters, what
// radiance does each need to cross the bloom threshold, and what does each need to clip on screen?
TEST_CASE("what the bright pass calls bright, by channel", "[hdr][lab][gpu][.probe]") {
    PostBench bench = PostBench::make();
    scene::PostSettings settings = neutralPost();
    settings.bloomEnabled = true;
    settings.bloomIntensity = 1.0f;
    settings.bloomThreshold = 1.0f;
    settings.bloomKnee = 0.5f;
    settings.bloomLevels = 6;

    constexpr std::uint32_t kW = 256;
    constexpr std::uint32_t kH = 128;
    struct Emitter {
        const char* name;
        float r, g, b;
    };
    const Emitter emitters[] = {
        {"neutral", 1.0f, 1.0f, 1.0f}, {"red", 1.0f, 0.0f, 0.0f},  {"green", 0.0f, 1.0f, 0.0f},
        {"blue", 0.0f, 0.0f, 1.0f},    {"cyan", 0.0f, 1.0f, 1.0f}, {"violet", 0.5f, 0.0f, 1.0f},
    };
    fmt::print("\n== the radiance each hue needs to cross a bloom threshold of 1.0 ==\n");
    for (const Emitter& e : emitters) {
        const float unitLum = luminance(e.r, e.g, e.b);
        // A 16 x 16 block, so the prefilter's four-tap box is not measuring an edge.
        float crossed = 0.0f;
        for (float scale = 0.25f; scale <= 64.0f && crossed == 0.0f; scale *= 1.05f) {
            Canvas canvas(kW, kH);
            for (std::uint32_t y = kH / 2 - 8; y < kH / 2 + 8; ++y) {
                for (std::uint32_t x = kW / 2 - 8; x < kW / 2 + 8; ++x) {
                    canvas.set(x, y, e.r * scale, e.g * scale, e.b * scale);
                }
            }
            SyntheticHdr hdr = makeHdr(*bench.ctx, kW, kH, canvas.rgba);
            const auto stages = bench.run(hdr, settings);
            const Stat pre = statOf(stageNamed(stages, "bloom/prefilter"));
            if (pre.peak > 1e-3f) {
                crossed = scale;
            }
            bench.pool->endFrame();
        }
        fmt::print("  {:8} unit luminance {:.4f}  crosses at radiance {:.3f}  (peak channel {:.3f})\n", e.name,
                   unitLum, crossed, crossed * std::max({e.r, e.g, e.b}));
    }
    CHECK(bench.ctx->errorCount() == 0);
}

// ---- the meter's coverage: can auto-exposure see a small bright source wherever it sits? ---------
//
// `fs_meter_prefilter` renders to a QUARTER-resolution target and takes four bilinear taps one
// full-resolution texel apart. Whether those four taps between them cover all sixteen source texels
// of the block, or only four of them, decides whether a small bright source is metered at all --
// and "a small bright source" is one of the fixtures this lab's specification names.
//
// The control is the same energy spread over the whole block: if the meter is blind to twelve
// texels in sixteen, a single texel's metered luminance swings by position and the spread block's
// does not.
TEST_CASE("the exposure meter's coverage of a single texel", "[hdr][lab][gpu][.probe]") {
    PostBench bench = PostBench::make();
    scene::PostSettings settings = neutralPost();
    settings.exposure.mode = scene::ExposureSettings::Mode::Automatic;
    settings.exposure.meterCenterWeight = 0.0f; // a flat average: position must not matter at all

    constexpr std::uint32_t kW = 64;
    constexpr std::uint32_t kH = 64;
    fmt::print("\n== one texel at 4096, by its position inside a 4x4 meter block ==\n");
    for (std::uint32_t dy = 0; dy < 4; ++dy) {
        std::string row;
        for (std::uint32_t dx = 0; dx < 4; ++dx) {
            Canvas canvas(kW, kH);
            canvas.set(kW / 2 + dx, kH / 2 + dy, 4096.0f);
            SyntheticHdr hdr = makeHdr(*bench.ctx, kW, kH, canvas.rgba);
            bench.post->resetExposure();
            bench.run(hdr, settings); // frame 1 encodes the metering
            bench.pool->endFrame();
            bench.run(hdr, settings); // frame 2 reads it back
            bench.pool->endFrame();
            row += fmt::format(" {:.6f}", bench.post->stats().meteredLuminance);
        }
        fmt::print("  dy={}:{}\n", dy, row);
    }
    CHECK(bench.ctx->errorCount() == 0);
}

// ---- the fixture, and the tone curve measured on both sides of the same frame -------------------

namespace {

constexpr std::uint32_t kFixW = 1280;
constexpr std::uint32_t kFixH = 720;

// The whole renderer over examples/labs/hdr-lab.scene.json, read back twice: the scene-linear HDR
// the tonemap pass was handed, and the display bytes it produced. Nothing here models a tone
// operator; the operator is the difference between the two readbacks.
struct HdrBench {
    std::unique_ptr<gpu::Context> ctx;
    std::unique_ptr<gpu::ShaderLibrary> shaders;
    std::unique_ptr<rendering::SceneRenderer> renderer;
    std::unique_ptr<app::Engine> engine;
    FixedStepClock clock{60.0};
    FrameTime time{};

    static fs::path scenePath() {
        return fs::path(AVGEN_SOURCE_DIR) / "examples" / "labs" / "hdr-lab.scene.json";
    }

    static HdrBench make() {
        HdrBench b;
        b.ctx = testsupport::gpuContextOrSkip();
        b.shaders = std::make_unique<gpu::ShaderLibrary>(*b.ctx, std::vector{fs::path(AVGEN_SHADER_SOURCE_DIR)});
        b.renderer = std::make_unique<rendering::SceneRenderer>(*b.ctx, *b.shaders);
        REQUIRE(b.renderer->init().has_value());
        b.engine = std::make_unique<app::Engine>(app::EngineMode::Offline);
        REQUIRE(b.engine->loadComposition(scenePath()).has_value());
        b.step();
        return b;
    }

    void step() {
        time = engine->tick(clock);
        engine->setViewport(kFixW, kFixH);
        engine->update(time);
    }

    // ADR-091's order, and the frame index pinned so anything that jitters on it stands still.
    void pin(glm::vec3 position, glm::vec3 target) {
        params::ParameterSet& p = engine->params();
        auto* mode = p.findAs<int>("camera/mode");
        auto* pos = p.findAs<glm::vec3>("camera/position");
        auto* at = p.findAs<glm::vec3>("camera/target");
        REQUIRE(mode != nullptr);
        REQUIRE(pos != nullptr);
        REQUIRE(at != nullptr);
        mode->setBase(1);
        pos->setBase(position);
        at->setBase(target);
        apply();
        REQUIRE(scene().camera.position == position);
        time.frameIndex = 0;
        engine->update(time);
    }

    void setFloat(const char* name, float value) {
        auto* p = engine->params().findAs<float>(name);
        REQUIRE(p != nullptr);
        p->setBase(value);
    }
    void setBool(const char* name, bool value) {
        auto* p = engine->params().findAs<bool>(name);
        REQUIRE(p != nullptr);
        p->setBase(value);
    }
    void setInt(const char* name, int value) {
        auto* p = engine->params().findAs<int>(name);
        REQUIRE(p != nullptr);
        p->setBase(value);
    }
    void apply() {
        engine->params().resetFinals();
        engine->update(time);
    }

    scene::Scene& scene() { return engine->composition()->scene(); }

    gpu::ImageF hdr() {
        renderer->post().resetExposure();
        auto image = renderer->renderToImageFloat(scene(), time, kFixW, kFixH);
        REQUIRE(image.has_value());
        return std::move(*image);
    }
    gpu::Image8 display() {
        renderer->post().resetExposure();
        auto image = renderer->renderToImage(scene(), time, kFixW, kFixH);
        REQUIRE(image.has_value());
        return std::move(*image);
    }
};

// The median of a small square, so one stray texel cannot become the reading. `u`/`v` are
// normalised screen coordinates with (0,0) at the top left, the post chain's own convention.
glm::vec3 sampleF(const gpu::ImageF& image, double u, double v, int half = 3) {
    const int cx = static_cast<int>(u * image.width);
    const int cy = static_cast<int>(v * image.height);
    std::vector<float> r, g, b;
    for (int y = cy - half; y <= cy + half; ++y) {
        for (int x = cx - half; x <= cx + half; ++x) {
            if (x < 0 || y < 0 || x >= static_cast<int>(image.width) || y >= static_cast<int>(image.height)) {
                continue;
            }
            const float* px = image.rgba.data() + (static_cast<std::size_t>(y) * image.width + x) * 4;
            r.push_back(px[0]);
            g.push_back(px[1]);
            b.push_back(px[2]);
        }
    }
    auto med = [](std::vector<float>& v) {
        std::nth_element(v.begin(), v.begin() + static_cast<long>(v.size() / 2), v.end());
        return v[v.size() / 2];
    };
    return {med(r), med(g), med(b)};
}

glm::vec3 sample8(const gpu::Image8& image, double u, double v, int half = 3) {
    const int cx = static_cast<int>(u * image.width);
    const int cy = static_cast<int>(v * image.height);
    std::vector<float> r, g, b;
    for (int y = cy - half; y <= cy + half; ++y) {
        for (int x = cx - half; x <= cx + half; ++x) {
            if (x < 0 || y < 0 || x >= static_cast<int>(image.width) || y >= static_cast<int>(image.height)) {
                continue;
            }
            const std::uint8_t* px = image.rgba.data() + (static_cast<std::size_t>(y) * image.width + x) * 4;
            r.push_back(px[0]);
            g.push_back(px[1]);
            b.push_back(px[2]);
        }
    }
    auto med = [](std::vector<float>& v) {
        std::nth_element(v.begin(), v.begin() + static_cast<long>(v.size() / 2), v.end());
        return v[v.size() / 2];
    };
    return {med(r), med(g), med(b)};
}

// The fixture's 4 x 4 wedge, in the order the scene file writes it. `authored` is the
// scene-linear radiance the file asks for, which an unlit surface delivers unchanged.
struct Patch {
    const char* name;
    int col, row;
    glm::vec3 authored;
};

const Patch kWedge[] = {
    {"grey-000", 0, 0, {0.0f, 0.0f, 0.0f}},      {"grey-018", 1, 0, {0.18f, 0.18f, 0.18f}},
    {"grey-050", 2, 0, {0.5f, 0.5f, 0.5f}},      {"grey-100", 3, 0, {1.0f, 1.0f, 1.0f}},
    {"grey-200", 0, 1, {2.0f, 2.0f, 2.0f}},      {"grey-400", 1, 1, {4.0f, 4.0f, 4.0f}},
    {"grey-1600", 2, 1, {16.0f, 16.0f, 16.0f}},  {"grey-5000", 3, 1, {50.0f, 50.0f, 50.0f}},
    {"hue-red-200", 0, 2, {2.0f, 0.0f, 0.0f}},   {"hue-green-200", 1, 2, {0.0f, 2.0f, 0.0f}},
    {"hue-blue-200", 2, 2, {0.0f, 0.0f, 2.0f}},  {"hue-neutral-200", 3, 2, {2.0f, 2.0f, 2.0f}},
    {"hue-cyan-200", 0, 3, {0.0f, 2.0f, 2.0f}},  {"hue-violet-200", 1, 3, {1.0f, 0.0f, 2.0f}},
    {"hue-blue-800", 2, 3, {0.0f, 0.0f, 8.0f}},  {"hue-neutral-800", 3, 3, {8.0f, 8.0f, 8.0f}},
};

double cellU(int col) { return (col + 0.5) / 4.0; }
double cellV(int row) { return (row + 0.5) / 4.0; }

} // namespace

// ADR-182 applied to the fixture itself, before anything is measured through it: a calibrated card
// whose patches are not the values it claims is a card that cannot calibrate anything.
TEST_CASE("the fixture delivers the radiance it authors", "[hdr][lab][gpu][.probe]") {
    HdrBench bench = HdrBench::make();
    // Post off entirely: `--disable post` is the boundary the Lighting Lab drew in code, and this
    // is the reading it protects -- the scene-linear radiance with no chain between it and the
    // readback at all.
    rendering::SceneRenderer::PassToggles toggles;
    toggles.post = false;
    bench.renderer->setPassToggles(toggles);
    const gpu::ImageF raw = bench.hdr();
    fmt::print("\n== the wedge, post disabled: authored vs the HDR target ==\n");
    for (const Patch& p : kWedge) {
        const glm::vec3 got = sampleF(raw, cellU(p.col), cellV(p.row));
        fmt::print("  {:16} authored ({:8.3f} {:8.3f} {:8.3f})  measured ({:8.4f} {:8.4f} {:8.4f})\n", p.name,
                   p.authored.r, p.authored.g, p.authored.b, got.r, got.g, got.b);
    }
    CHECK(bench.ctx->errorCount() == 0);
}

// The lab's central instrument. The same frame, read on both sides of the operator: what the
// tonemap pass was handed, and what it wrote. Every claim this lab makes about "a step the curve
// can still separate" is read off this table and nothing else.
TEST_CASE("the transfer curve, measured on both sides of one frame", "[hdr][lab][gpu][.probe]") {
    HdrBench bench = HdrBench::make();
    const gpu::ImageF linear = bench.hdr();
    const gpu::Image8 shown = bench.display();
    fmt::print("\n== AgX, the default operator, chromaRetention 0 ==\n");
    fmt::print("  {:16} {:>10} {:>26} {:>22}\n", "patch", "authored", "scene-linear into tonemap", "display bytes");
    for (const Patch& p : kWedge) {
        const glm::vec3 lin = sampleF(linear, cellU(p.col), cellV(p.row));
        const glm::vec3 out = sample8(shown, cellU(p.col), cellV(p.row));
        fmt::print("  {:16} {:10.3f}   ({:9.4f} {:9.4f} {:9.4f})   ({:5.0f} {:5.0f} {:5.0f})\n", p.name,
                   std::max({p.authored.r, p.authored.g, p.authored.b}), lin.r, lin.g, lin.b, out.r, out.g, out.b);
    }
    CHECK(bench.ctx->errorCount() == 0);
}

// ---- the metering state a reset is supposed to have discarded ------------------------------------
//
// `PostProcessor::resetExposure()` is documented as the call that makes an offline render reproduce
// a live one exactly: "it is part of render state: reset it when a render job seeks or a scene is
// swapped". It clears `exposureState_`, `haveMeasurement_` and `measuredLuminance_` -- and not
// `meterPending_`, which is the flag saying a copy of the PREVIOUS frame's metered luminance is
// still in flight to the readback buffer. The next `run()` maps that copy and adopts it.
//
// The control is a bench that has never metered anything: the same call sequence with no copy in
// flight must report "nothing metered" (-1), which is what says the arm below is measuring a
// leak rather than the default state.
TEST_CASE("a reset meter reports the frame it was reset for", "[hdr][lab][gpu]") {
    PostBench bench = PostBench::make();
    scene::PostSettings settings = neutralPost();
    settings.exposure.mode = scene::ExposureSettings::Mode::Automatic;
    settings.exposure.meterCenterWeight = 0.0f;

    constexpr std::uint32_t kW = 64;
    constexpr std::uint32_t kH = 64;
    Canvas bright(kW, kH);
    bright.fillRect(0, 0, kW, kH, 8.0f);
    Canvas dark(kW, kH);
    dark.fillRect(0, 0, kW, kH, 0.0f);
    SyntheticHdr brightHdr = makeHdr(*bench.ctx, kW, kH, bright.rgba);
    SyntheticHdr darkHdr = makeHdr(*bench.ctx, kW, kH, dark.rgba);

    // The control: a chain that has metered nothing reports nothing, whatever the reset does.
    bench.post->resetExposure();
    bench.run(darkHdr, settings);
    bench.pool->endFrame();
    CHECK(bench.post->stats().meteredLuminance == -1.0f);

    // Two frames of the bright image: the first encodes the reduction, the second reads it back.
    bench.run(brightHdr, settings);
    bench.pool->endFrame();
    bench.run(brightHdr, settings);
    bench.pool->endFrame();
    const float metered = bench.post->stats().meteredLuminance;
    CHECK(metered > 7.0f); // the arm has teeth only if the meter saw the bright frame at all
    CHECK(metered < 9.0f);

    // Now the reset, and a black frame. Nothing about this frame or any frame the chain will be
    // asked about again has any luminance in it, so a metered luminance here is the previous
    // scene's, adopted across the boundary a reset exists to draw.
    bench.post->resetExposure();
    bench.run(darkHdr, settings);
    bench.pool->endFrame();
    INFO("metered luminance on the first frame after resetExposure(): " << bench.post->stats().meteredLuminance);
    CHECK(bench.post->stats().meteredLuminance == -1.0f);
    CHECK(bench.ctx->errorCount() == 0);
}

// ---- the pyramid's alignment, with a control for the border ---------------------------------------
//
// The probe above cannot tell two mechanisms apart: a level whose resolution is not exactly half
// the one above it (integer truncation in `PostProcessor::run`) and a highlight close enough to the
// frame edge that ClampToEdge pulls the tent's centroid inward. 512x256 halves exactly six times
// and 512x288 does not, the impulse sits at the same normalised position in both, and the frames
// are the same width -- so a v displacement that appears in one and not the other is the
// truncation, and a u displacement in both would be the border.
TEST_CASE("truncated pyramid levels displace the halo they carry", "[hdr][lab][gpu][.probe]") {
    PostBench bench = PostBench::make();
    struct Size {
        std::uint32_t w, h;
        const char* why;
    };
    const Size sizes[] = {
        {512, 256, "control: 256 halves exactly six times"},
        {512, 288, "arm: 9 -> 4 at the last level"},
    };
    for (float radius : {1.0f, 1.9f}) {
        scene::PostSettings settings = neutralPost();
        settings.bloomEnabled = true;
        settings.bloomIntensity = 1.0f;
        settings.bloomThreshold = 1.0f;
        settings.bloomKnee = 0.5f;
        settings.bloomRadius = radius;
        settings.bloomLevels = 6;
        fmt::print("\n== bloomRadius {:.2f} (upsample blend {:.2f}) ==\n", radius,
                   std::clamp(radius * 0.5f, 0.05f, 0.95f));
        for (const Size& size : sizes) {
            for (double frac : {0.5, 0.75}) {
                Canvas canvas(size.w, size.h);
                const auto px = static_cast<std::uint32_t>(size.w * frac);
                const auto py = static_cast<std::uint32_t>(size.h * frac);
                canvas.set(px, py, 64.0f);
                SyntheticHdr hdr = makeHdr(*bench.ctx, size.w, size.h, canvas.rgba);
                const auto stages = bench.run(hdr, settings);
                const gpu::ImageF& up0 = stageNamed(stages, "bloom/up0");
                const Centroid c = centroidOf(up0, (px + 0.5) / size.w, (py + 0.5) / size.h, 12.0);
                // The half-texel the half-resolution grid owes a point mass: the prefilter's box
                // spans two source texels and its centroid is their boundary, so +0.5 px is the
                // correct answer and a departure FROM it is the displacement.
                const double su = (static_cast<double>(px) + 0.5) / size.w;
                const double sv = (static_cast<double>(py) + 0.5) / size.h;
                fmt::print("  {:4}x{:<4} impulse at {:.2f}: du={:+.3f} px  dv={:+.3f} px   ({})\n", size.w, size.h,
                           frac, (c.u - su) * size.w - 0.5, (c.v - sv) * size.h - 0.5, size.why);
                (void)up0;
                bench.pool->endFrame();
            }
        }
    }
    CHECK(bench.ctx->errorCount() == 0);
}

// ---- does bloom itself produce a displaced copy? ------------------------------------------------
//
// "Ghosting" in a bloom is a second, offset image of the highlight. A pyramid cannot make one from
// a symmetric filter, but a pyramid whose levels are misaligned can, and the anamorphic tier makes
// two on purpose. This is the measurement that separates the two claims: the radial profile of an
// impulse's halo, which a blur makes monotonic and a replica does not.
TEST_CASE("the bloom halo of an impulse falls monotonically", "[hdr][lab][gpu][.probe]") {
    PostBench bench = PostBench::make();
    scene::PostSettings settings = neutralPost();
    settings.bloomEnabled = true;
    settings.bloomIntensity = 1.0f;
    settings.bloomThreshold = 1.0f;
    settings.bloomKnee = 0.5f;
    settings.bloomLevels = 6;
    constexpr std::uint32_t kW = 512;
    constexpr std::uint32_t kH = 288;
    for (float radius : {1.0f, 1.9f}) {
        settings.bloomRadius = radius;
        Canvas canvas(kW, kH);
        canvas.set(kW / 2, kH / 2, 64.0f);
        SyntheticHdr hdr = makeHdr(*bench.ctx, kW, kH, canvas.rgba);
        const auto stages = bench.run(hdr, settings);
        const gpu::ImageF& up0 = stageNamed(stages, "bloom/up0");
        // Radial means about the halo's own centre, in texels of up0.
        const auto cx = static_cast<int>(up0.width / 2);
        const auto cy = static_cast<int>(up0.height / 2);
        std::vector<double> sum(64, 0.0), count(64, 0.0);
        for (int y = 0; y < static_cast<int>(up0.height); ++y) {
            for (int x = 0; x < static_cast<int>(up0.width); ++x) {
                const int r = static_cast<int>(std::lround(std::hypot(x - cx, y - cy)));
                if (r >= 64) {
                    continue;
                }
                const float* px = up0.rgba.data() + (static_cast<std::size_t>(y) * up0.width + x) * 4;
                sum[static_cast<std::size_t>(r)] += static_cast<double>(luminance(px[0], px[1], px[2]));
                count[static_cast<std::size_t>(r)] += 1.0;
            }
        }
        int rises = 0;
        double previous = 1e30;
        std::string profile;
        for (std::size_t r = 0; r < sum.size(); ++r) {
            if (count[r] <= 0.0) {
                continue;
            }
            const double m = sum[r] / count[r];
            if (r % 4 == 0) {
                profile += fmt::format(" r{}={:.5f}", r, m);
            }
            if (m > previous * 1.02) {
                ++rises;
            }
            previous = m;
        }
        fmt::print("\n== bloom/up0 radial profile, radius {:.2f}: {} rise(s) ==\n {}\n", radius, rises, profile);
        bench.pool->endFrame();
    }
    CHECK(bench.ctx->errorCount() == 0);
}

namespace {

// The normalised radius, in fractions of the frame HEIGHT, inside which `fraction` of a field's
// energy sits. Resolution-free by construction, which is what makes two frame sizes comparable
// without anyone choosing a scale factor.
double energyRadius(const gpu::ImageF& image, double u, double v, double fraction) {
    const double cx = u * image.width - 0.5;
    const double cy = v * image.height - 0.5;
    const auto bins = static_cast<std::size_t>(image.height); // one bin per output texel of radius
    std::vector<double> ring(bins + 1, 0.0);
    double total = 0.0;
    for (std::uint32_t y = 0; y < image.height; ++y) {
        for (std::uint32_t x = 0; x < image.width; ++x) {
            const float* px = image.rgba.data() + (static_cast<std::size_t>(y) * image.width + x) * 4;
            const double w = static_cast<double>(luminance(px[0], px[1], px[2]));
            if (w <= 0.0) {
                continue;
            }
            const double r = std::hypot(static_cast<double>(x) - cx, static_cast<double>(y) - cy);
            ring[std::min(bins, static_cast<std::size_t>(r))] += w;
            total += w;
        }
    }
    double run = 0.0;
    for (std::size_t r = 0; r <= bins; ++r) {
        run += ring[r];
        if (run >= fraction * total) {
            return static_cast<double>(r) / static_cast<double>(image.height);
        }
    }
    return 1.0;
}

} // namespace

// ---- the two variations the specification names: the frame's size, and where the bright thing is -
//
// Bloom is expressed in texels of each pyramid level, and every level is a fixed fraction of the
// frame, so the halo SHOULD be the same size relative to the frame at any resolution and the same
// shape wherever the highlight sits. Both are invariants rather than opinions, and both are
// measured here as the normalised radius containing 90% of the halo's energy -- a number that is
// resolution-free by construction and therefore comparable without anyone choosing a scale factor.
TEST_CASE("the bloom halo keeps its size across resolutions and its shape across the frame",
          "[hdr][lab][gpu][.probe]") {
    PostBench bench = PostBench::make();
    scene::PostSettings settings = neutralPost();
    settings.bloomEnabled = true;
    settings.bloomIntensity = 1.0f;
    settings.bloomThreshold = 1.0f;
    settings.bloomKnee = 0.5f;
    settings.bloomRadius = 1.0f;
    settings.bloomLevels = 6;

    fmt::print("\n== one impulse at the frame centre, by resolution ==\n");
    const std::pair<std::uint32_t, std::uint32_t> sizes[] = {
        {640, 360}, {1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}};
    for (const auto& [w, h] : sizes) {
        Canvas canvas(w, h);
        // A source of fixed NORMALISED size -- 1/36 of the frame height square -- and not a single
        // texel. A one-texel impulse is a different physical object at every resolution, so its
        // halo comes back a constant twenty pixels wide at all five sizes and says nothing about
        // whether bloom scales; that reading cost a run and is why the source is sized this way.
        const std::uint32_t side = std::max(2u, h / 36);
        canvas.fillRect(w / 2 - side / 2, h / 2 - side / 2, side, side, 64.0f);
        SyntheticHdr hdr = makeHdr(*bench.ctx, w, h, canvas.rgba);
        const auto stages = bench.run(hdr, settings);
        const gpu::ImageF& up0 = stageNamed(stages, "bloom/up0");
        fmt::print("  {:4}x{:<4}  levels {}  source {:2} px  r50={:.5f}  r90={:.5f}  r99={:.5f}  (frame heights)\n",
                   w, h, bench.post->stats().bloomLevels, side, energyRadius(up0, 0.5, 0.5, 0.5),
                   energyRadius(up0, 0.5, 0.5, 0.9), energyRadius(up0, 0.5, 0.5, 0.99));
        bench.pool->endFrame();
    }

    fmt::print("\n== one impulse at 1280x720, by position ==\n");
    const std::pair<double, double> places[] = {{0.5, 0.5}, {0.25, 0.5}, {0.75, 0.75}, {0.1, 0.1}, {0.9, 0.5}};
    for (const auto& [u, v] : places) {
        Canvas canvas(1280, 720);
        const auto px = static_cast<std::uint32_t>(1280 * u);
        const auto py = static_cast<std::uint32_t>(720 * v);
        canvas.set(px, py, 64.0f);
        SyntheticHdr hdr = makeHdr(*bench.ctx, 1280, 720, canvas.rgba);
        const auto stages = bench.run(hdr, settings);
        const gpu::ImageF& up0 = stageNamed(stages, "bloom/up0");
        const double su = (px + 0.5) / 1280.0;
        const double sv = (py + 0.5) / 720.0;
        const Stat s = statOf(up0);
        fmt::print("  u={:.2f} v={:.2f}  r50={:.5f}  r90={:.5f}  peak={:.5f}  total={:.4f}\n", u, v,
                   energyRadius(up0, su, sv, 0.5), energyRadius(up0, su, sv, 0.9), s.peak, s.total);
        bench.pool->endFrame();
    }
    CHECK(bench.ctx->errorCount() == 0);
}

namespace {

// A square window of `half` fractions-of-frame-HEIGHT about (u, v), so a measurement about one
// bright source cannot pick up the other one 0.76 frame heights away.
gpu::ImageF window(const gpu::ImageF& image, double u, double v, double half) {
    const auto side = static_cast<int>(std::lround(half * 2.0 * image.height));
    const int x0 = static_cast<int>(std::lround(u * image.width)) - side / 2;
    const int y0 = static_cast<int>(std::lround(v * image.height)) - side / 2;
    gpu::ImageF out;
    out.width = static_cast<std::uint32_t>(side);
    out.height = static_cast<std::uint32_t>(side);
    out.rgba.assign(static_cast<std::size_t>(side) * side * 4, 0.0f);
    for (int y = 0; y < side; ++y) {
        for (int x = 0; x < side; ++x) {
            const int sx = x0 + x;
            const int sy = y0 + y;
            if (sx < 0 || sy < 0 || sx >= static_cast<int>(image.width) || sy >= static_cast<int>(image.height)) {
                continue;
            }
            const float* src = image.rgba.data() + (static_cast<std::size_t>(sy) * image.width + sx) * 4;
            float* dst = out.rgba.data() + (static_cast<std::size_t>(y) * side + x) * 4;
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
            dst[3] = 1.0f;
        }
    }
    return out;
}

// The fixture's page-2 sources, in normalised screen coordinates. The camera is 20 m back with a
// 40 degree vertical field of view, so the visible plane is 25.8824 m wide and a source at x metres
// from the page's centre sits at u = 0.5 + x / 25.8824.
constexpr double kSmallSourceU = 0.5 - 6.0 / 25.8824;
constexpr double kLargeSourceU = 0.5 + 5.0 / 25.8824;

} // namespace

// ---- the bloom pyramid's reach is a pixel count, so supersampling halves it -----------------------
//
// Six levels from half resolution means the coarsest level's texel is 2^6 = 64 OUTPUT pixels
// whatever the frame's size, so the halo's reach is a fixed number of pixels rather than a fixed
// fraction of the picture. `--supersample 2` runs the whole post chain at twice the deliverable's
// resolution (`render_job.cpp`: `quality.renderScale = min(supersample, 2)`), so the same shot
// delivers a halo half as wide across the frame with supersampling on as with it off.
//
// This is the renderer's own path, not the synthetic one: the fixture's 4 m bright source, through
// SceneRenderer, at one output size and two render scales. The control is the source itself -- its
// own width in the frame must NOT change, or the two frames are not the same shot.
TEST_CASE("supersampling changes how much of the frame a highlight glows over", "[hdr][lab][gpu][.probe]") {
    HdrBench bench = HdrBench::make();
    bench.pin({100.0f, 0.0f, 20.0f}, {100.0f, 0.0f, 0.0f});
    bench.setBool("post/bloom/enabled", true);
    bench.setFloat("post/bloom/intensity", 1.0f);
    bench.setFloat("post/bloom/threshold", 1.0f);
    bench.setFloat("post/bloom/radius", 1.0f);
    bench.apply();
    fmt::print("\n== the 4 m source at a 1280x720 output, by render scale ==\n");
    for (float scale : {1.0f, 2.0f}) {
        rendering::QualitySettings q = bench.renderer->qualitySettings();
        q.renderScale = scale;
        bench.renderer->setQualitySettings(q);
        const gpu::ImageF image = bench.hdr();
        for (const auto& [name, u, half] : {std::tuple{"0.3 m (15 px at 720p)", kSmallSourceU, 0.30},
                                            std::tuple{"4.0 m (198 px at 720p)", kLargeSourceU, 0.35}}) {
            const gpu::ImageF around = window(image, u, 0.5, half);
            const double toFrame = static_cast<double>(around.height) / image.height;
            fmt::print("  renderScale {:.1f} target {:4}x{:<4}  {:22}  r50={:.5f}  r90={:.5f}  r99={:.5f}\n", scale,
                       image.width, image.height, name, energyRadius(around, 0.5, 0.5, 0.5) * toFrame,
                       energyRadius(around, 0.5, 0.5, 0.9) * toFrame,
                       energyRadius(around, 0.5, 0.5, 0.99) * toFrame);
        }
    }
    rendering::QualitySettings q = bench.renderer->qualitySettings();
    q.renderScale = 1.0f;
    bench.renderer->setQualitySettings(q);
    CHECK(bench.ctx->errorCount() == 0);
}
