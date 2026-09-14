// Forensics for the Glowmere water artifact: the lattice of dots and the banded streaks that
// appear once `post/anamorphic/enabled` is on over sparkling water.
//
// Three fixes for this were shipped and reverted without anyone knowing which stage produced the
// pattern, because every one of them was judged on the final frame. The final frame is the worst
// possible evidence here: bloom, the wide tier and the grade all land on it, and a change anywhere
// upstream moves it. So this file does the opposite -- it feeds the *production* post chain inputs
// the CPU knows exactly, captures every intermediate target it renders (PostProcessor::armCapture),
// and measures each one against the one before it.
//
// Two halves:
//
//   1. Synthetic inputs (docs/post-artifact-forensics.md §8). A single bright texel, a sparse grid,
//      a line, a rectangle and a checkerboard go into the chain in place of a rendered scene. No
//      camera, no clock, no noise, no water: if the chain prints a lattice from an input that has
//      none, the mechanism is the chain's and the water shader is not on trial. This is the only
//      arrangement in which "the post pipeline creates the pattern" is falsifiable.
//
//   2. The water matrix. The QA water scene, top-down, with the clock pinned, driven through the
//      same parameters the Glowmere UI drives (`post/bloom/enabled`, `post/anamorphic/enabled`,
//      `post/anamorphic/ghosts`, `nodes/.../water/sparkle`) -- no diagnostic toggles of its own,
//      because a second control path is a second thing that can disagree with the application.
//
// The detector is in `artifact.hpp`-free local code below: out-of-region energy against a measured
// water mask, an autocorrelation lag for periodic structure, and an isolated-peak count. None of
// them compares a render against a second render of the same code path.

#include "app/engine.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "gpu/texture.hpp"
#include "gpu/transient_pool.hpp"
#include "rendering/post_processor.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/composition.hpp"
#include "scene/post_settings.hpp"
#include "scene/scene.hpp"

#include <fmt/format.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

using namespace avgen;
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

// ---- the detector -------------------------------------------------------------------------------
//
// Luminance only. Every measurement below is on one scalar field so that a stage-to-stage
// comparison is a comparison of the same quantity at two resolutions, and not of two tints.

struct Field {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<float> v;
    [[nodiscard]] float at(std::uint32_t x, std::uint32_t y) const {
        return v[static_cast<std::size_t>(y) * width + x];
    }
};

float luminance(const float* rgb) { return 0.2126f * rgb[0] + 0.7152f * rgb[1] + 0.0722f * rgb[2]; }

Field toField(const gpu::ImageF& image) {
    Field f;
    f.width = image.width;
    f.height = image.height;
    f.v.resize(static_cast<std::size_t>(image.width) * image.height);
    for (std::size_t i = 0; i < f.v.size(); ++i) {
        const float* px = image.rgba.data() + i * 4;
        f.v[i] = luminance(px);
    }
    return f;
}

float peak(const Field& f) { return f.v.empty() ? 0.0f : *std::max_element(f.v.begin(), f.v.end()); }

double total(const Field& f) { return std::accumulate(f.v.begin(), f.v.end(), 0.0); }

double mean(const Field& f) { return f.v.empty() ? 0.0 : total(f) / static_cast<double>(f.v.size()); }

std::size_t countAbove(const Field& f, float threshold) {
    return static_cast<std::size_t>(std::count_if(f.v.begin(), f.v.end(), [&](float x) { return x > threshold; }));
}

// How much of the field's energy sits outside a region. `mask` is in the field's own resolution.
struct OutOfRegion {
    std::size_t pixels = 0;   // above `threshold` and outside the mask
    float peak = 0.0f;
    double energy = 0.0;      // summed luminance outside the mask, every pixel
    std::uint32_t minX = 0, maxX = 0, minY = 0, maxY = 0;
};

OutOfRegion outsideMask(const Field& f, const std::vector<char>& mask, float threshold) {
    OutOfRegion out;
    bool any = false;
    for (std::uint32_t y = 0; y < f.height; ++y) {
        for (std::uint32_t x = 0; x < f.width; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * f.width + x;
            if (mask[i] != 0) {
                continue;
            }
            out.energy += f.v[i];
            out.peak = std::max(out.peak, f.v[i]);
            if (f.v[i] > threshold) {
                ++out.pixels;
                if (!any) {
                    out.minX = out.maxX = x;
                    out.minY = out.maxY = y;
                    any = true;
                } else {
                    out.minX = std::min(out.minX, x);
                    out.maxX = std::max(out.maxX, x);
                    out.minY = std::min(out.minY, y);
                    out.maxY = std::max(out.maxY, y);
                }
            }
        }
    }
    return out;
}

// Periodic structure along a row, as an autocorrelation of the mean-removed column profile.
//
// A comb -- one source point resampled at a spacing wider than the filter it is meant to be --
// prints copies of itself at a fixed lag, and that is exactly what an autocorrelation peak away
// from zero is. Reported as (lag, score) with the score normalised to the zero lag, so 1.0 is a
// perfectly periodic signal and 0 is none. Lags below 2 are the signal's own width.
struct Periodicity {
    std::uint32_t lag = 0;
    double score = 0.0;
};

Periodicity horizontalPeriodicity(const Field& f, std::uint32_t maxLag = 0) {
    // The column profile: summing down y keeps horizontal repetition and throws away everything
    // that only varies vertically, which is what a horizontal streak stage would produce.
    std::vector<double> profile(f.width, 0.0);
    for (std::uint32_t y = 0; y < f.height; ++y) {
        for (std::uint32_t x = 0; x < f.width; ++x) {
            profile[x] += f.at(x, y);
        }
    }
    const double m = std::accumulate(profile.begin(), profile.end(), 0.0) / static_cast<double>(f.width);
    for (double& p : profile) {
        p -= m;
    }
    const double zero = std::inner_product(profile.begin(), profile.end(), profile.begin(), 0.0);
    Periodicity best;
    if (zero <= 1e-12) {
        return best;
    }
    const std::uint32_t limit = maxLag > 0 ? maxLag : f.width / 3;
    std::vector<double> corr(limit, 0.0);
    for (std::uint32_t lag = 2; lag < limit; ++lag) {
        double acc = 0.0;
        for (std::uint32_t x = 0; x + lag < f.width; ++x) {
            acc += profile[x] * profile[x + lag];
        }
        corr[lag] = acc / zero;
        if (corr[lag] > best.score) {
            best.score = corr[lag];
            best.lag = lag;
        }
    }
    // A periodic signal correlates with itself at every multiple of its period, and with a finite
    // kernel the strongest of those peaks is not always the first. Report the *fundamental*: the
    // smallest lag that is a local maximum and within half the best score. Reporting the argmax
    // instead had an impulse's comb come back as its own third harmonic, which reads as a spacing
    // the chain does not have.
    for (std::uint32_t lag = 2; lag + 1 < limit; ++lag) {
        if (corr[lag] >= 0.5 * best.score && corr[lag] > corr[lag - 1] && corr[lag] >= corr[lag + 1]) {
            best.lag = lag;
            best.score = corr[lag];
            break;
        }
    }
    return best;
}

// The autocorrelation at one chosen lag, rather than at its best. This is what a regression test
// wants: the undersampling defect predicts a peak at a *known* lag -- the tap step -- so the check
// is "is there a spike where the comb would be", not "is there a spike anywhere". The second
// question has no stable answer on real content, which is full of legitimate structure.
double correlationAt(const Field& f, std::uint32_t lag) {
    std::vector<double> profile(f.width, 0.0);
    for (std::uint32_t y = 0; y < f.height; ++y) {
        for (std::uint32_t x = 0; x < f.width; ++x) {
            profile[x] += f.at(x, y);
        }
    }
    const double m = std::accumulate(profile.begin(), profile.end(), 0.0) / static_cast<double>(f.width);
    for (double& p : profile) {
        p -= m;
    }
    const double zero = std::inner_product(profile.begin(), profile.end(), profile.begin(), 0.0);
    if (zero <= 1e-12 || lag >= f.width) {
        return 0.0;
    }
    double acc = 0.0;
    for (std::uint32_t x = 0; x + lag < f.width; ++x) {
        acc += profile[x] * profile[x + lag];
    }
    return acc / zero;
}

// Whether the autocorrelation has a *tooth* at a given period, rather than merely a high value
// there. This distinction is the whole detector: a wide smooth blur correlates strongly with itself
// at every small lag -- 0.69 at lag 10, for the streak as it stands -- so "correlation at the comb's
// period" cannot tell a comb from a blur, and a threshold on it fails in both directions. A comb
// can, though: a periodic signal's autocorrelation has a maximum at its period and a minimum half a
// period either side of it, while a blur's decays monotonically and is very nearly straight over
// three consecutive samples. So the statistic is the prominence -- how far the correlation at the
// period stands above the line through its anti-phase neighbours. Zero or below for any blur;
// strongly positive only for repeated copies.
double combProminence(const Field& f, std::uint32_t period) {
    if (period < 2 || period * 3 / 2 >= f.width / 2) {
        return 0.0;
    }
    const double here = correlationAt(f, period);
    const double before = correlationAt(f, period / 2);
    const double after = correlationAt(f, period * 3 / 2);
    return here - 0.5 * (before + after);
}

// How elongated the field is, as the ratio of the energy's horizontal to its vertical standard
// deviation about its own centroid. An anamorphic streak is anisotropic *by definition*; a fix that
// removes the comb by blurring both axes equally has turned a streak into a blob, and this is the
// number that says so. Measured on the energy above a floor so that a frame's ambient level does
// not drag the centroid to the middle.
double elongation(const Field& f, float floorLevel) {
    double w = 0.0, sx = 0.0, sy = 0.0;
    for (std::uint32_t y = 0; y < f.height; ++y) {
        for (std::uint32_t x = 0; x < f.width; ++x) {
            const double v = std::max(0.0f, f.at(x, y) - floorLevel);
            w += v;
            sx += v * x;
            sy += v * y;
        }
    }
    if (w <= 1e-12) {
        return 0.0;
    }
    const double cx = sx / w, cy = sy / w;
    double vx = 0.0, vy = 0.0;
    for (std::uint32_t y = 0; y < f.height; ++y) {
        for (std::uint32_t x = 0; x < f.width; ++x) {
            const double v = std::max(0.0f, f.at(x, y) - floorLevel);
            vx += v * (x - cx) * (x - cx);
            vy += v * (y - cy) * (y - cy);
        }
    }
    return std::sqrt(vx / w) / std::max(std::sqrt(vy / w), 1e-6);
}

// Isolated peaks: a pixel far brighter than the ring around it. A sparse point field scores high
// by construction, which is the point -- the interesting number is how the count changes from one
// stage to the next, because a blur should destroy isolation and a resample should preserve it.
std::size_t isolatedPeaks(const Field& f, float factor, float floorLevel) {
    std::size_t count = 0;
    for (std::uint32_t y = 2; y + 2 < f.height; ++y) {
        for (std::uint32_t x = 2; x + 2 < f.width; ++x) {
            const float centre = f.at(x, y);
            if (centre <= floorLevel) {
                continue;
            }
            double ring = 0.0;
            int n = 0;
            for (int dy = -2; dy <= 2; ++dy) {
                for (int dx = -2; dx <= 2; ++dx) {
                    if (std::abs(dx) < 2 && std::abs(dy) < 2) {
                        continue;
                    }
                    ring += f.at(static_cast<std::uint32_t>(static_cast<int>(x) + dx),
                                 static_cast<std::uint32_t>(static_cast<int>(y) + dy));
                    ++n;
                }
            }
            if (centre > factor * static_cast<float>(ring / std::max(n, 1))) {
                ++count;
            }
        }
    }
    return count;
}

std::string describe(const char* name, const Field& f) {
    const Periodicity p = horizontalPeriodicity(f);
    return fmt::format("{:<18} {:>4}x{:<4} peak {:>10.4f} mean {:>10.6f} lag {:>3} score {:.3f} peaks {}", name,
                       f.width, f.height, peak(f), mean(f), p.lag, p.score,
                       isolatedPeaks(f, 4.0f, static_cast<float>(mean(f)) * 2.0f + 1e-6f));
}

// ---- synthetic inputs ---------------------------------------------------------------------------

// An RGBA16Float texture the CPU wrote, standing in for the scene's HDR colour.
struct SyntheticHdr {
    wgpu::Texture texture;
    wgpu::TextureView view;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

SyntheticHdr makeHdr(gpu::Context& ctx, std::uint32_t width, std::uint32_t height,
                     const std::vector<float>& rgba) {
    SyntheticHdr out;
    out.width = width;
    out.height = height;
    wgpu::TextureDescriptor desc{};
    desc.label = "synthetic-hdr";
    desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst | wgpu::TextureUsage::CopySrc;
    desc.dimension = wgpu::TextureDimension::e2D;
    desc.size = {width, height, 1};
    desc.format = rendering::PostProcessor::kHdrFormat;
    out.texture = ctx.device().CreateTexture(&desc);
    out.view = out.texture.CreateView();

    std::vector<std::uint16_t> halves(rgba.size());
    for (std::size_t i = 0; i < rgba.size(); ++i) {
        halves[i] = gpu::floatToHalf(rgba[i]);
    }
    wgpu::TexelCopyTextureInfo dst{};
    dst.texture = out.texture;
    wgpu::TexelCopyBufferLayout layout{};
    layout.bytesPerRow = width * 8;
    layout.rowsPerImage = height;
    wgpu::Extent3D extent{width, height, 1};
    ctx.queue().WriteTexture(&dst, halves.data(), halves.size() * sizeof(std::uint16_t), &layout, &extent);
    return out;
}

// A blank HDR frame with a plotting helper, so each pattern below reads as what it is.
struct Canvas {
    std::uint32_t width, height;
    std::vector<float> rgba;
    Canvas(std::uint32_t w, std::uint32_t h) : width(w), height(h), rgba(static_cast<std::size_t>(w) * h * 4, 0.0f) {
        for (std::size_t i = 3; i < rgba.size(); i += 4) {
            rgba[i] = 1.0f;
        }
    }
    void set(std::uint32_t x, std::uint32_t y, float value) {
        if (x >= width || y >= height) {
            return;
        }
        float* px = rgba.data() + (static_cast<std::size_t>(y) * width + x) * 4;
        px[0] = px[1] = px[2] = value;
    }
};

// The Glowmere post settings, as authored in the user's project (`glowmere-edit.json`) with
// `post/anamorphic/enabled` turned on -- which is the state the artifact was reported in. Only the
// stages under investigation are on: depth of field, the tilt-shift band and motion blur all need
// targets the synthetic arm has no business inventing, and none of them is accused.
scene::PostSettings glowmerePost() {
    scene::PostSettings s;
    s.bloomEnabled = true;
    s.bloomIntensity = 0.18f;
    s.bloomThreshold = 1.0f;
    s.bloomKnee = 0.5f;
    s.bloomRadius = 1.15f;
    s.bloomEmissionWeight = 0.0f; // no emission target in the synthetic arm; luminance only
    s.anamorphicEnabled = true;
    s.anamorphicIntensity = 0.47f;
    s.anamorphicStretch = 10.386f;
    s.anamorphicGhosts = 0.223f;
    s.anamorphicTint = {0.35f, 0.55f, 1.0f};
    s.halationEnabled = false;
    s.dofEnabled = false;
    s.dofMaxRadius = 0.0f;
    s.tiltShiftEnabled = false;
    s.tiltShiftMaxRadius = 0.0f;
    s.motionBlurAmount = 0.0f;
    s.antialias = 0.0f;
    s.sharpen = 0.0f;
    s.contrast = 1.0f;
    s.saturation = 1.0f;
    return s;
}

// The post chain on its own: no scene renderer, no camera, no clock. One synthetic HDR frame in,
// every intermediate the chain rendered out.
struct PostBench {
    std::unique_ptr<gpu::Context> ctx;
    std::unique_ptr<gpu::ShaderLibrary> shaders;
    std::unique_ptr<rendering::PostProcessor> post;
    std::unique_ptr<gpu::TransientPool> pool;

    static PostBench make() {
        PostBench b;
        b.ctx = makeContext();
        b.shaders = std::make_unique<gpu::ShaderLibrary>(*b.ctx, std::vector{fs::path(AVGEN_SHADER_SOURCE_DIR)});
        b.post = std::make_unique<rendering::PostProcessor>(*b.ctx, *b.shaders);
        REQUIRE(b.post->init().has_value());
        b.pool = std::make_unique<gpu::TransientPool>(*b.ctx);
        return b;
    }

    // Runs the chain over `hdr` and reads back every captured stage. The readback happens before
    // anything else renders, which is what keeps the transient-pool handles meaningful.
    std::vector<std::pair<std::string, Field>> run(const SyntheticHdr& hdr, const scene::PostSettings& settings) {
        post->armCapture();
        wgpu::CommandEncoder encoder = ctx->device().CreateCommandEncoder();
        rendering::PostFrameInputs in;
        in.sceneHdr = hdr.view;
        in.width = hdr.width;
        in.height = hdr.height;
        in.settings = &settings;
        post->run(encoder, in, *pool);
        wgpu::CommandBuffer commands = encoder.Finish();
        ctx->queue().Submit(1, &commands);
        ctx->waitForQueue();

        rendering::PostCapture capture = post->takeCapture();
        std::vector<std::pair<std::string, Field>> out;
        // The input itself, so a stage-to-stage comparison starts where the chain does.
        auto source = gpu::readTextureF16(*ctx, hdr.texture, hdr.width, hdr.height);
        REQUIRE(source.has_value());
        out.emplace_back("scene-hdr", toField(*source));
        for (const rendering::PostCaptureStage& stage : capture.stages) {
            auto image = gpu::readTextureF16(*ctx, stage.texture.texture, stage.texture.width, stage.texture.height);
            REQUIRE(image.has_value());
            out.emplace_back(stage.name, toField(*image));
        }
        pool->endFrame();
        return out;
    }
};

const Field& stage(const std::vector<std::pair<std::string, Field>>& stages, std::string_view name) {
    for (const auto& [key, field] : stages) {
        if (key == name) {
            return field;
        }
    }
    FAIL("no captured stage named " << name);
    return stages.front().second;
}

bool has(const std::vector<std::pair<std::string, Field>>& stages, std::string_view name) {
    return std::any_of(stages.begin(), stages.end(), [&](const auto& e) { return e.first == name; });
}

void report(const char* title, const std::vector<std::pair<std::string, Field>>& stages) {
    fmt::print("\n== {} ==\n", title);
    for (const auto& [name, field] : stages) {
        fmt::print("  {}\n", describe(name.c_str(), field));
    }
}

constexpr std::uint32_t kSynthW = 512;
constexpr std::uint32_t kSynthH = 288;

} // namespace

// ---- §8: what the production post chain does to inputs the CPU knows exactly ---------------------
//
// One texel, lit to 40 (well over the 1.0 threshold), in the middle of an otherwise black frame.
//
// Everything the chain does to it is a *filter*, and a filter's response to an impulse is the
// filter. If the output is one blob the chain is a blur; if it is a row of blobs the chain is a
// comb, and a comb over a field of sparkles is a lattice whatever the sparkles look like.
TEST_CASE("an impulse through the anamorphic chain comes out as a comb, not a streak",
          "[gpu][post][forensics][waterfx]") {
    PostBench bench = PostBench::make();
    Canvas canvas(kSynthW, kSynthH);
    canvas.set(kSynthW / 2, kSynthH / 2, 40.0f);
    SyntheticHdr hdr = makeHdr(*bench.ctx, kSynthW, kSynthH, canvas.rgba);

    const scene::PostSettings settings = glowmerePost();
    const auto stages = bench.run(hdr, settings);
    report("impulse: one texel at 40", stages);
    CHECK(bench.ctx->errorCount() == 0);

    // The chain's shape, measured rather than read off the source: the pyramid starts at half the
    // frame and the wide tier renders at a quarter of it. That mismatch is the subject of the next
    // test; here it only has to be recorded.
    const Field& prefilter = stage(stages, "bloom/prefilter");
    const Field& bloom = stage(stages, "bloom/up0");
    const Field& wide = stage(stages, "wide");
    CHECK(prefilter.width == kSynthW / 2);
    CHECK(bloom.width == kSynthW / 2);
    CHECK(wide.width == kSynthW / 4);

    // The impulse survives the pyramid as an impulse: the finest level is blended, not replaced,
    // so `bloom` still carries a sharp point. This is what the wide tier has to filter.
    INFO(describe("bloom", bloom));
    CHECK(isolatedPeaks(bloom, 4.0f, 1e-4f) >= 1);

    // And the wide tier must return a *filtered* version of it. Before the fix this pass sampled
    // its gaussian every `stretch` texels and returned a row of copies instead:
    //
    //   wide 128x72  peak 0.1382  lag 11  score 0.253  isolated peaks 13
    //
    // The two numbers that say "copies" rather than "blur" are the isolated-peak count and the
    // correlation at the tap step. Both are now zero-ish; the recorded values are in
    // docs/post-artifact-forensics.md.
    INFO(describe("wide", wide));
    INFO("comb prominence at lag 10: " << combProminence(wide, 10));
    CHECK(isolatedPeaks(wide, 4.0f, 1e-6f) == 0);
    // The comb's period was the tap step in wide texels, which at this stretch is 10.
    CHECK(combProminence(wide, 10) < 0.02);

    // And it must still be a *streak*: removing a comb by blurring both axes alike would turn the
    // anamorphic tier into a blob, which is a different artifact and not a fix.
    const double aspect = elongation(wide, 0.0f);
    INFO("wide elongation " << aspect);
    CHECK(aspect > 3.0);
}

// The same impulse, with the *only* difference being the ghost strength. If the lattice were the
// ghosts', it would appear here and not before; if the comb is the streak's, the ghosts change the
// count of copies and not their spacing.
TEST_CASE("the streak's comb spacing is set by the source-to-output texel mismatch, not by ghosts",
          "[gpu][post][forensics][waterfx]") {
    PostBench bench = PostBench::make();
    Canvas canvas(kSynthW, kSynthH);
    canvas.set(kSynthW / 2, kSynthH / 2, 40.0f);
    SyntheticHdr hdr = makeHdr(*bench.ctx, kSynthW, kSynthH, canvas.rgba);

    scene::PostSettings settings = glowmerePost();
    settings.anamorphicGhosts = 0.0f;
    const Field noGhosts = stage(bench.run(hdr, settings), "wide");
    settings.anamorphicGhosts = 0.223f;
    const Field withGhosts = stage(bench.run(hdr, settings), "wide");
    CHECK(bench.ctx->errorCount() == 0);

    const Periodicity a = horizontalPeriodicity(noGhosts);
    const Periodicity b = horizontalPeriodicity(withGhosts);
    fmt::print("\n== ghosts off vs on (impulse) ==\n  {}\n  {}\n", describe("wide ghosts=0", noGhosts),
               describe("wide ghosts=0.223", withGhosts));
    // This is the experiment that acquitted the ghosts. Before the fix the comb was there with the
    // ghosts off *entirely* -- lag 31 (the third harmonic of the period 10.4), score 0.516 -- so
    // whatever the two mirrored taps do, they were never what turned an impulse into a row. The
    // ghosts added structure of their own on top, at their own spacing (lag 11, score 0.253).
    //
    // Now neither arm has any. Both are checked, because a fix that quietened the streak and left
    // the ghosts combing would pass a test that only looked at one of them.
    INFO("streak lag " << a.lag << " score " << a.score << "; with ghosts lag " << b.lag << " score " << b.score);
    CHECK(isolatedPeaks(noGhosts, 4.0f, 1e-6f) == 0);
    CHECK(isolatedPeaks(withGhosts, 4.0f, 1e-6f) == 0);
    CHECK(combProminence(noGhosts, 10) < 0.02);
    CHECK(combProminence(withGhosts, 10) < 0.02);
}

// The same chain over a *sparse grid* of bright points, which is the shape the water's sparkle
// field actually has. If the chain's comb response is the mechanism, a sparse input must come out
// with structure at the comb's lag and not at the input's own spacing.
TEST_CASE("a sparse point field acquires the chain's own spacing, not its own",
          "[gpu][post][forensics][waterfx]") {
    PostBench bench = PostBench::make();
    constexpr std::uint32_t kInputSpacing = 23; // deliberately not a divisor of anything downstream
    Canvas canvas(kSynthW, kSynthH);
    for (std::uint32_t y = 40; y < kSynthH - 40; y += kInputSpacing) {
        for (std::uint32_t x = 40; x < kSynthW - 40; x += kInputSpacing) {
            canvas.set(x, y, 30.0f);
        }
    }
    SyntheticHdr hdr = makeHdr(*bench.ctx, kSynthW, kSynthH, canvas.rgba);
    const auto stages = bench.run(hdr, glowmerePost());
    report("sparse grid: spacing 23 px, value 30", stages);
    CHECK(bench.ctx->errorCount() == 0);

    const Periodicity input = horizontalPeriodicity(stage(stages, "scene-hdr"));
    const Field& wide = stage(stages, "wide");
    INFO("input lag=" << input.lag << " " << describe("wide", wide));
    CHECK(input.lag == kInputSpacing);
    // The input's own 23-pixel spacing is legitimate content and survives into the bloom, which is
    // correct. What must not survive is the *chain's* spacing: before the fix the wide tier came
    // back with 390 isolated peaks of its own over this input.
    CHECK(isolatedPeaks(wide, 4.0f, 1e-6f) == 0);
    CHECK(combProminence(wide, 10) < 0.02);
}

// A compact bright rectangle: an input with no high frequency in it at all. The same chain, the
// same settings. If the comb appears here too the mechanism is not about sparseness; if it does
// not, the chain only misbehaves on inputs whose energy is concentrated in isolated texels -- which
// is the distinction that decides whether the water shader is implicated.
TEST_CASE("a compact bright rectangle does not acquire a comb",
          "[gpu][post][forensics][waterfx]") {
    PostBench bench = PostBench::make();
    Canvas canvas(kSynthW, kSynthH);
    for (std::uint32_t y = kSynthH / 2 - 12; y < kSynthH / 2 + 12; ++y) {
        for (std::uint32_t x = kSynthW / 2 - 24; x < kSynthW / 2 + 24; ++x) {
            canvas.set(x, y, 8.0f);
        }
    }
    SyntheticHdr hdr = makeHdr(*bench.ctx, kSynthW, kSynthH, canvas.rgba);
    const auto stages = bench.run(hdr, glowmerePost());
    report("rectangle: 48x24 at 8.0", stages);
    CHECK(bench.ctx->errorCount() == 0);
}

// Where does the ghost put its energy? One point, and the two mirrored taps in `fs_wide` place
// copies of it elsewhere in the frame. This measures how far, and whether what lands there is a
// copy of the input (transport) or something the sampling invented (creation).
TEST_CASE("the ghosts place energy far from the source, and undersample it getting there",
          "[gpu][post][forensics][waterfx]") {
    PostBench bench = PostBench::make();
    Canvas canvas(kSynthW, kSynthH);
    // Off centre, so a mirrored ghost cannot be confused with the source.
    canvas.set(kSynthW / 4, kSynthH / 4, 40.0f);
    SyntheticHdr hdr = makeHdr(*bench.ctx, kSynthW, kSynthH, canvas.rgba);

    scene::PostSettings settings = glowmerePost();
    settings.anamorphicGhosts = 0.0f;
    const Field off = stage(bench.run(hdr, settings), "wide");
    settings.anamorphicGhosts = 0.223f;
    const Field on = stage(bench.run(hdr, settings), "wide");
    CHECK(bench.ctx->errorCount() == 0);

    // The ghost contribution is the difference of two runs that differ in one parameter.
    Field ghost = on;
    for (std::size_t i = 0; i < ghost.v.size(); ++i) {
        ghost.v[i] = std::max(0.0f, on.v[i] - off.v[i]);
    }
    fmt::print("\n== ghost contribution (impulse at quarter frame) ==\n  {}\n  {}\n  {}\n",
               describe("wide ghosts=0", off), describe("wide ghosts=on", on), describe("ghost delta", ghost));
    CHECK(peak(ghost) > 0.0f);
}

// Experiment 4, the one that turns the impulse response into an arithmetic claim. `fs_wide` steps
// its taps by `post.texelSize.x * stretch`, and `post.texelSize` in that pass is *the wide target's*
// texel -- the wide target is a quarter of the frame while the bloom pyramid it samples is a half of
// it, so the step is one number expressed in one resolution and applied to another.
//
// Whatever the confusion, the measurable consequence is a spacing, and a spacing can be predicted:
// if the comb's period is the tap step, then the period in wide texels must equal `stretch` exactly
// and must track it linearly. Nothing else in the chain has that signature -- a blur's width scales
// with stretch but a blur has no period at all, and an aliasing artifact of the source would keep
// the source's spacing while stretch moved.
TEST_CASE("the streak no longer prints a copy of its input at every tap step",
          "[gpu][post][forensics][waterfx]") {
    PostBench bench = PostBench::make();
    Canvas canvas(kSynthW, kSynthH);
    canvas.set(kSynthW / 2, kSynthH / 2, 40.0f);
    SyntheticHdr hdr = makeHdr(*bench.ctx, kSynthW, kSynthH, canvas.rgba);

    scene::PostSettings settings = glowmerePost();
    settings.anamorphicGhosts = 0.0f; // the streak alone, so the period measured is the streak's
    // Before the fix the comb's period equalled `stretch` exactly, over a fivefold sweep:
    //
    //   stretch  4.000 -> lag  4 score 0.843     stretch 10.386 -> lag 10 score 0.394
    //   stretch  6.000 -> lag  6 score 0.814     stretch 14.000 -> lag 14 score 0.777
    //   stretch  8.000 -> lag  8 score 0.803     stretch 20.000 -> lag 20 score 0.722
    //
    // So the regression check is not "is there structure somewhere" -- real content has structure
    // everywhere -- but "is there a spike at the one lag the defect predicts". Reintroducing the
    // undersampling in any form puts it back, whatever else changes.
    fmt::print("\n== correlation at the old comb period (impulse, ghosts off) ==\n");
    for (const float stretch : {4.0f, 6.0f, 8.0f, 10.386f, 14.0f, 20.0f}) {
        settings.anamorphicStretch = stretch;
        const Field wide = stage(bench.run(hdr, settings), "wide");
        const auto oldPeriod = static_cast<std::uint32_t>(std::lround(stretch));
        const double prominence = combProminence(wide, oldPeriod);
        fmt::print("  stretch {:>6.3f} -> prominence at lag {:>3} = {:+.4f}; correlation {:.3f}; "
                   "isolated peaks {}; elongation {:.1f}\n",
                   stretch, oldPeriod, prominence, correlationAt(wide, oldPeriod),
                   isolatedPeaks(wide, 4.0f, 1e-6f), elongation(wide, 0.0f));
        CHECK(isolatedPeaks(wide, 4.0f, 1e-6f) == 0);
        CHECK(prominence < 0.02);
        CHECK(elongation(wide, 0.0f) > 3.0);
    }
    CHECK(bench.ctx->errorCount() == 0);
}

// ---- the water matrix ---------------------------------------------------------------------------
//
// Everything above is the post chain with the water taken out of it. This is the other direction:
// the real water shader, the real sparkle field, and the post stages switched with the parameters
// the application's own UI writes -- `post/bloom/enabled`, `post/anamorphic/enabled`,
// `post/anamorphic/ghosts`, `nodes/<node>/water/sparkle`. No diagnostic toggles: a second control
// path is a second thing that can disagree with the application, and the point of the matrix is
// that every arm is a state a user can reach.

namespace {

// The sparkle is band-passed in *screen* space (water.wgsl `sparkleBandFade`): a cell shows only
// between about five and twenty-eight pixels across. That makes the resolution and the camera's
// distance part of the reproduction rather than incidental to it -- at 640x360 from the QA scene's
// authored bank view the cells are one pixel wide and the sparkle is, correctly, not drawn at all.
// 960x540 with the camera eight metres off the water puts the cells near the middle of the band.
constexpr std::uint32_t kWaterW = 960;
constexpr std::uint32_t kWaterH = 540;

// The QA water scene through the Engine, as `test_water_depth_forensics_gpu.cpp` drives it: a real
// generated river with a real shoreline, so the sparkle field is the shader's own and not a
// stand-in. Its water settings are then set to Glowmere's through the same parameters.
struct WaterBench {
    std::unique_ptr<gpu::Context> ctx;
    std::unique_ptr<gpu::ShaderLibrary> shaders;
    std::unique_ptr<rendering::SceneRenderer> renderer;
    std::unique_ptr<app::Engine> engine;
    FixedStepClock clock{60.0};
    FrameTime time{};

    static fs::path scenePath() {
        return fs::path(AVGEN_SOURCE_DIR) / "examples" / "qa" / "renderer-qa-water.scene.json";
    }

    static WaterBench make() {
        WaterBench b;
        b.ctx = makeContext();
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
        engine->setViewport(kWaterW, kWaterH);
        engine->update(time);
    }

    // Every source of variation the brief names, pinned in one place: the camera forced free and
    // placed, the clock held at a fixed second, and the frame index fixed so anything that jitters
    // on it stands still. Auto-exposure is the one piece of history the chain keeps, and it is
    // reset before each arm so an arm never inherits the previous arm's metering.
    void pin(glm::vec3 position, glm::vec3 target, double seconds) {
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
        p.resetFinals();
        step();
        REQUIRE(scene().camera.position == position);
        time.renderTime = seconds;
        time.deltaTime = 1.0 / 60.0;
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
    void apply() {
        engine->params().resetFinals();
        engine->update(time);
    }

    scene::Scene& scene() { return engine->composition()->scene(); }

    // One frame, with every post intermediate the chain rendered. The readbacks happen before the
    // next frame, which is what keeps the transient-pool handles pointing at this frame's content.
    struct Frame {
        std::vector<std::pair<std::string, Field>> stages;
        Field final;
    };

    Frame capture() {
        renderer->post().resetExposure();
        renderer->post().armCapture();
        auto image = renderer->renderToImageFloat(scene(), time, kWaterW, kWaterH);
        REQUIRE(image.has_value());
        rendering::PostCapture capture = renderer->post().takeCapture();
        Frame frame;
        for (const rendering::PostCaptureStage& s : capture.stages) {
            auto tex = gpu::readTextureF16(*ctx, s.texture.texture, s.texture.width, s.texture.height);
            REQUIRE(tex.has_value());
            frame.stages.emplace_back(s.name, toField(*tex));
        }
        frame.final = toField(*image);
        return frame;
    }

    // Glowmere's authored water, on the QA scene's river.
    void glowmereWater(float sparkle) {
        setFloat("nodes/ground/water/glow", 1.2f);
        setFloat("nodes/ground/water/sparkle", sparkle);
        setFloat("nodes/ground/water/ripple", 1.0f);
        setFloat("nodes/ground/water/flowSpeed", 1.0f);
        setFloat("nodes/ground/water/foam", 0.6f);
    }

    // Glowmere's authored post, with each accused stage switchable.
    void glowmerePostParams(bool bloom, bool anamorphic, float ghosts) {
        setBool("post/bloom/enabled", bloom);
        setFloat("post/bloom/intensity", 0.18f);
        setFloat("post/bloom/threshold", 1.0f);
        setFloat("post/bloom/knee", 0.5f);
        setFloat("post/bloom/radius", 1.15f);
        setFloat("post/bloom/emissionWeight", 0.75f);
        setBool("post/anamorphic/enabled", anamorphic);
        setFloat("post/anamorphic/intensity", 0.47f);
        setFloat("post/anamorphic/stretch", 10.386f);
        setFloat("post/anamorphic/ghosts", ghosts);
        setBool("post/halation/enabled", false);
        // The stages nobody has accused, held off so an arm differs only in what it says it does.
        setFloat("post/output/sharpen", 0.0f);
        setFloat("post/output/grain", 0.0f);
        setFloat("post/motionBlur/amount", 0.0f);
        setBool("post/dof/enabled", false);
        setBool("post/tiltShift/enabled", false);
    }
};

// The mask this investigation needs is not "where is the water" but "where did the sparkle put
// light", because the accusation is that post copies that light somewhere it does not belong. It
// is measured, from the one difference that isolates it: the same frame with the sparkle amount at
// zero. Both arms have post off, so the mask is the shader's own output and nothing else.
std::vector<char> sparkleSupport(const Field& withSparkle, const Field& without, float epsilon) {
    std::vector<char> mask(withSparkle.v.size(), 0);
    for (std::size_t i = 0; i < mask.size(); ++i) {
        mask[i] = std::fabs(withSparkle.v[i] - without.v[i]) > epsilon ? 1 : 0;
    }
    return mask;
}

// Grown by `radius` pixels, so "outside the sparkle" means genuinely away from it rather than one
// pixel past a point the shader antialiased. A bloom is *supposed* to spread; the question is how
// far, and whether what it leaves out there is a blur or a copy.
std::vector<char> dilate(const std::vector<char>& mask, std::uint32_t w, std::uint32_t h, int radius) {
    std::vector<char> out(mask.size(), 0);
    for (int y = 0; y < static_cast<int>(h); ++y) {
        for (int x = 0; x < static_cast<int>(w); ++x) {
            bool any = false;
            for (int dy = -radius; dy <= radius && !any; ++dy) {
                for (int dx = -radius; dx <= radius && !any; ++dx) {
                    const int sx = x + dx, sy = y + dy;
                    if (sx >= 0 && sy >= 0 && sx < static_cast<int>(w) && sy < static_cast<int>(h)) {
                        any = mask[static_cast<std::size_t>(sy) * w + sx] != 0;
                    }
                }
            }
            out[static_cast<std::size_t>(y) * w + x] = any ? 1 : 0;
        }
    }
    return out;
}

// The sparkle's whole contribution to a frame: the same configuration twice, differing only in the
// sparkle amount. Every other thing in the frame -- the glint, the foam, the bank, the fog, the
// grade -- subtracts out, so what is left is what the post chain did to the sparkle and nothing
// else. This is the quantity the matrix compares across arms.
Field sparkleContribution(const Field& on, const Field& off) {
    Field d = on;
    for (std::size_t i = 0; i < d.v.size(); ++i) {
        d.v[i] = std::fabs(on.v[i] - off.v[i]);
    }
    return d;
}

// The view the matrix runs from, chosen by measurement rather than by eye: the `[.probe]` test at
// the bottom of this file renders the same river from six distances and counts the pixels the
// sparkle actually lights. At 960x540 the band pass puts a live sparkle field between roughly four
// and eight metres and nothing outside that, so the authored bank view -- thirty-six metres out --
// has no sparkle in it at all and would have made every arm of this matrix a comparison of two
// frames with nothing in them. Four metres at forty-five degrees is the strongest of the six.
constexpr struct {
    glm::vec3 position;
    glm::vec3 target;
} kSparkleView{{12.0f, 4.0f, -104.0f}, {12.0f, 0.0f, -108.0f}};

} // namespace

// The experiment matrix: eight configurations over one scene, one camera and one clock, differing
// only in the parameters named on each line. For each, the sparkle's own contribution is isolated
// by a second render with the sparkle at zero, and measured for periodic structure and for energy
// outside the region the sparkle actually lit.
TEST_CASE("the water matrix: which post stage the pattern first appears in",
          "[gpu][post][forensics][waterfx]") {
    if (!fs::exists(WaterBench::scenePath())) {
        SKIP("the water QA scene is not present");
    }
    WaterBench bench = WaterBench::make();
    bench.pin(kSparkleView.position, kSparkleView.target, 20.0);

    struct Arm {
        const char* name;
        bool bloom;
        bool anamorphic;
        float ghosts;
    };
    const Arm arms[] = {
        {"2 sparkle, no post", false, false, 0.0f},
        {"3 sparkle + bloom", true, false, 0.0f},
        {"4 sparkle + bloom + streaks", true, true, 0.0f},
        {"5 sparkle + bloom + streaks + ghosts", true, true, 0.223f},
    };

    std::vector<char> support;
    for (const Arm& arm : arms) {
        bench.glowmerePostParams(arm.bloom, arm.anamorphic, arm.ghosts);
        bench.glowmereWater(0.6f);
        bench.apply();
        const WaterBench::Frame on = bench.capture();
        bench.glowmereWater(0.0f);
        bench.apply();
        const WaterBench::Frame off = bench.capture();

        const Field contribution = sparkleContribution(on.final, off.final);
        if (support.empty()) {
            // Arm 1 of the brief's matrix, in the only form that is measurable: the sparkle's
            // support with no post at all. Every later arm is judged against this region.
            support = dilate(sparkleSupport(on.final, off.final, 1e-4f), contribution.width, contribution.height, 3);
            const std::size_t covered =
                static_cast<std::size_t>(std::count(support.begin(), support.end(), 1));
            fmt::print("\n== sparkle support: {} of {} pixels ==\n", covered, support.size());
            REQUIRE(covered > 200); // the camera has to actually be looking at sparkling water
        }
        const OutOfRegion out = outsideMask(contribution, support, 1e-3f);
        const Periodicity p = horizontalPeriodicity(contribution);
        fmt::print("\n== {} ==\n", arm.name);
        fmt::print("  {}\n", describe("sparkle delta", contribution));
        fmt::print("  outside support: {} px above 1e-3, peak {:.5f}, energy {:.4f}, bbox {},{}..{},{}\n",
                   out.pixels, out.peak, out.energy, out.minX, out.minY, out.maxX, out.maxY);
        fmt::print("  periodicity lag {} score {:.3f}\n", p.lag, p.score);
        for (const auto& [name, field] : on.stages) {
            fmt::print("    {}\n", describe(name.c_str(), field));
        }
    }
    CHECK(bench.ctx->errorCount() == 0);
}

// The ghost sweep the brief asks for: everything else byte-identical, the ghost strength alone
// moving. An artifact that does not scale with ghost strength is not the ghosts', whatever it
// looks like -- and this is the measurement that decides it.
TEST_CASE("the ghost sweep: does out-of-region energy scale with ghost strength",
          "[gpu][post][forensics][waterfx]") {
    if (!fs::exists(WaterBench::scenePath())) {
        SKIP("the water QA scene is not present");
    }
    WaterBench bench = WaterBench::make();
    bench.pin(kSparkleView.position, kSparkleView.target, 20.0);

    std::vector<char> support;
    for (const float ghosts : {0.0f, 0.02f, 0.06f, 0.12f, 0.223f}) {
        bench.glowmerePostParams(true, true, ghosts);
        bench.glowmereWater(0.6f);
        bench.apply();
        const WaterBench::Frame on = bench.capture();
        bench.glowmereWater(0.0f);
        bench.apply();
        const WaterBench::Frame off = bench.capture();
        const Field contribution = sparkleContribution(on.final, off.final);
        if (support.empty()) {
            support = dilate(sparkleSupport(on.final, off.final, 1e-4f), contribution.width, contribution.height, 3);
        }
        const OutOfRegion out = outsideMask(contribution, support, 1e-3f);
        const Periodicity p = horizontalPeriodicity(contribution);
        const Field& wide = stage(on.stages, "wide");
        fmt::print("  ghosts {:.3f}: outside {} px peak {:.5f} energy {:.4f} | lag {} score {:.3f} | {}\n", ghosts,
                   out.pixels, out.peak, out.energy, p.lag, p.score, describe("wide", wide));
    }
    CHECK(bench.ctx->errorCount() == 0);
}

// A probe, not an assertion: where does this scene's water actually sparkle? The band pass makes
// that a property of the camera distance and the frame's resolution together, so it has to be
// measured before any arm of the matrix means anything.
TEST_CASE("probe: which views of the QA river have a live sparkle field",
          "[.probe][gpu][post][forensics][waterfx]") {
    if (!fs::exists(WaterBench::scenePath())) {
        SKIP("the water QA scene is not present");
    }
    WaterBench bench = WaterBench::make();
    struct View {
        const char* name;
        glm::vec3 position;
        glm::vec3 target;
    };
    const View views[] = {
        {"authored bank", {40.0f, 6.0f, -86.0f}, {12.0f, -1.0f, -108.0f}},
        {"4 m, 45 deg", {12.0f, 4.0f, -104.0f}, {12.0f, 0.0f, -108.0f}},
        {"8 m, 45 deg", {12.0f, 8.0f, -100.0f}, {12.0f, 0.0f, -108.0f}},
        {"16 m, 45 deg", {12.0f, 16.0f, -92.0f}, {12.0f, 0.0f, -108.0f}},
        {"8 m, top down", {12.0f, 8.0f, -107.0f}, {12.0f, 0.0f, -108.0f}},
        {"30 m, 45 deg", {12.0f, 30.0f, -78.0f}, {12.0f, 0.0f, -108.0f}},
    };
    for (const View& v : views) {
        bench.pin(v.position, v.target, 20.0);
        bench.glowmerePostParams(false, false, 0.0f);
        bench.glowmereWater(0.6f);
        bench.apply();
        const Field on = bench.capture().final;
        bench.glowmereWater(0.0f);
        bench.apply();
        const Field off = bench.capture().final;
        const std::vector<char> mask = sparkleSupport(on, off, 1e-4f);
        const Field delta = sparkleContribution(on, off);
        fmt::print("  {:<16} sparkle pixels {:>6} of {}  peak {:.5f}  energy {:.4f}\n", v.name,
                   std::count(mask.begin(), mask.end(), 1), mask.size(), peak(delta), total(delta));
    }
    CHECK(bench.ctx->errorCount() == 0);
}
