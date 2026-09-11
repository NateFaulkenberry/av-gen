// The tilt-shift band (ADR-079), driven straight through PostProcessor rather than through a
// scene. The band is a purely screen-space circle-of-confusion function, so geometry proves
// nothing here and a synthetic HDR source proves a great deal: the input values are known exactly,
// which is what turns "the band is untouched" and "the highlight is still above 1.0" into
// assertions rather than eyeballing.
#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "gpu/transient_pool.hpp"
#include "rendering/post_processor.hpp"
#include "scene/post_settings.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <vector>

using namespace avgen;

namespace {

constexpr std::uint32_t kWidth = 192;
constexpr std::uint32_t kHeight = 128;
// Radii are authored in pixels at 720p and scale with the frame height (as dofMaxRadius does), so
// at 128 pixels tall an authored 16 would come out at under three. The tests want the radius they
// ask for, not the one a small frame implies.
constexpr float kRadiusPx = 12.0f;
constexpr float kAuthoredRadius = kRadiusPx * 720.0f / static_cast<float>(kHeight);

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

// float -> IEEE 754 binary16. Only finite, in-range values are fed to it, so the subnormal and
// overflow paths are deliberately absent rather than wrong.
std::uint16_t floatToHalf(float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    const std::uint32_t sign = (bits >> 16) & 0x8000u;
    const std::int32_t exponent = static_cast<std::int32_t>((bits >> 23) & 0xFFu) - 127 + 15;
    const std::uint32_t mantissa = (bits >> 13) & 0x3FFu;
    if (exponent <= 0) {
        return static_cast<std::uint16_t>(sign);
    }
    return static_cast<std::uint16_t>(sign | (static_cast<std::uint32_t>(std::min(exponent, 30)) << 10) | mantissa);
}

float hash01(std::uint32_t n) {
    n = (n ^ 61u) ^ (n >> 16);
    n *= 9u;
    n = n ^ (n >> 4);
    n *= 0x27d4eb2du;
    n = n ^ (n >> 15);
    return static_cast<float>(n & 0xFFFFFFu) / static_cast<float>(0x1000000u);
}

// Column noise at two scales, plus a much brighter block down the left edge.
//
// Two scales on purpose. A single stripe frequency makes contrast a jinc of the blur radius, which
// has zero crossings: a wider blur can then measure as *more* contrast than a narrower one, and a
// test asserting that defocus increases with distance from the band would be asserting an artefact
// of the pattern. Broadband content falls off monotonically. The bright block is there to catch a
// clamp to 0..1 anywhere in the path.
float sourceValue(std::uint32_t x) {
    if (x < 6) {
        return 60.0f;
    }
    return 0.25f + 8.0f * hash01(x / 2u) + 16.0f * hash01(x / 16u + 977u);
}

struct Source {
    wgpu::Texture texture;
    wgpu::TextureView view;
};

Source makeSource(gpu::Context& ctx) {
    wgpu::TextureDescriptor desc{};
    desc.label = "tilt-shift-source";
    desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
    desc.dimension = wgpu::TextureDimension::e2D;
    desc.size = {kWidth, kHeight, 1};
    desc.format = rendering::PostProcessor::kHdrFormat;
    Source source;
    source.texture = ctx.device().CreateTexture(&desc);
    source.view = source.texture.CreateView();

    std::vector<std::uint16_t> texels(static_cast<std::size_t>(kWidth) * kHeight * 4);
    for (std::uint32_t y = 0; y < kHeight; ++y) {
        for (std::uint32_t x = 0; x < kWidth; ++x) {
            const std::uint16_t v = floatToHalf(sourceValue(x));
            const std::size_t i = (static_cast<std::size_t>(y) * kWidth + x) * 4;
            texels[i + 0] = v;
            texels[i + 1] = v;
            texels[i + 2] = v;
            texels[i + 3] = floatToHalf(1.0f);
        }
    }
    wgpu::TexelCopyTextureInfo destination{};
    destination.texture = source.texture;
    wgpu::TexelCopyBufferLayout layout{};
    layout.bytesPerRow = kWidth * 4 * sizeof(std::uint16_t);
    layout.rowsPerImage = kHeight;
    const wgpu::Extent3D extent{kWidth, kHeight, 1};
    ctx.queue().WriteTexture(&destination, texels.data(), texels.size() * sizeof(std::uint16_t), &layout, &extent);
    return source;
}

// Everything but the defocus pass off, so what the assertions see is the band and nothing else.
scene::PostSettings quietSettings() {
    scene::PostSettings s;
    s.bloomEnabled = false;
    s.bloomIntensity = 0.0f;
    return s;
}

scene::PostSettings bandSettings() {
    scene::PostSettings s = quietSettings();
    s.tiltShiftEnabled = true;
    s.tiltShiftCentre = {0.5f, 0.5f};
    s.tiltShiftRotation = 0.0f;
    s.tiltShiftBandWidth = 0.2f; // +/- 0.1 of the frame height about the middle
    s.tiltShiftFalloff = 0.15f;
    s.tiltShiftMaxRadius = kAuthoredRadius;
    return s;
}

struct Harness {
    std::unique_ptr<gpu::Context> ctx = makeContext();
    gpu::ShaderLibrary shaders{*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)}};
    rendering::PostProcessor post{*ctx, shaders};
    gpu::TransientPool pool{*ctx};
    Source source = makeSource(*ctx);

    gpu::ImageF render(const scene::PostSettings& settings) {
        rendering::PostFrameInputs inputs;
        inputs.sceneHdr = source.view;
        inputs.width = kWidth;
        inputs.height = kHeight;
        inputs.settings = &settings;
        wgpu::CommandEncoder encoder = ctx->device().CreateCommandEncoder();
        post.run(encoder, inputs, pool);
        wgpu::CommandBuffer commands = encoder.Finish();
        ctx->queue().Submit(1, &commands);
        auto image = gpu::readTextureF16(*ctx, post.outputTexture(), kWidth, kHeight);
        REQUIRE(image.has_value());
        pool.endFrame();
        return *image;
    }
};

// Mean absolute difference between neighbouring pixels along a row: high while the noise is sharp,
// collapsing as the gather spreads it.
float rowContrast(const gpu::ImageF& image, std::uint32_t y) {
    float sum = 0.0f;
    // From x = 9 so the bright left block, which has an edge of its own, stays out of it.
    for (std::uint32_t x = 9; x < image.width; ++x) {
        sum += std::abs(image.pixel(x, y)[0] - image.pixel(x - 1, y)[0]);
    }
    return sum / static_cast<float>(image.width - 9);
}

// The largest single step along a row. Mean absolute difference is a total-variation measure and
// survives a blur far better than intuition suggests - an isolated step becomes a ramp of the same
// total height - so it is the *peak* step that says whether detail has actually gone.
float rowMaxStep(const gpu::ImageF& image, std::uint32_t y) {
    float worst = 0.0f;
    for (std::uint32_t x = 9; x < image.width; ++x) {
        worst = std::max(worst, std::abs(image.pixel(x, y)[0] - image.pixel(x - 1, y)[0]));
    }
    return worst;
}

bool rowIsIdentical(const gpu::ImageF& a, const gpu::ImageF& b, std::uint32_t y) {
    for (std::uint32_t x = 0; x < a.width; ++x) {
        for (int c = 0; c < 3; ++c) {
            if (a.pixel(x, y)[c] != b.pixel(x, y)[c]) {
                return false;
            }
        }
    }
    return true;
}

float regionMean(const gpu::ImageF& image, std::uint32_t x0, std::uint32_t x1) {
    float sum = 0.0f;
    for (std::uint32_t y = 0; y < image.height; ++y) {
        for (std::uint32_t x = x0; x < x1; ++x) {
            sum += image.pixel(x, y)[0];
        }
    }
    return sum / static_cast<float>((x1 - x0) * image.height);
}

} // namespace

TEST_CASE("Tilt-shift off leaves the image exactly as it was", "[gpu][post][tiltshift]") {
    Harness h;
    REQUIRE(h.post.init().has_value());
    auto settings = quietSettings();
    const gpu::ImageF off = h.render(settings);
    const std::uint64_t offHash = gpu::hashImage(off);
    const std::uint32_t quietPasses = h.post.stats().passes;

    // Enabled but with no radius to give: skipped, rather than run and doing nothing.
    settings = bandSettings();
    settings.tiltShiftMaxRadius = 0.0f;
    CHECK(gpu::hashImage(h.render(settings)) == offHash);
    CHECK(h.post.stats().passes == quietPasses);

    // And a band fully described but switched off is the same image again.
    settings = bandSettings();
    settings.tiltShiftEnabled = false;
    CHECK(gpu::hashImage(h.render(settings)) == offHash);
    CHECK(h.post.stats().passes == quietPasses);

    // One extra pass, and only one, when it is actually asked for.
    (void)h.render(bandSettings());
    CHECK(h.post.stats().passes == quietPasses + 1);
    CHECK(h.ctx->errorCount() == 0);
}

TEST_CASE("The tilt-shift band stays sharp and the frame beyond it blurs", "[gpu][post][tiltshift]") {
    Harness h;
    REQUIRE(h.post.init().has_value());
    const gpu::ImageF sharp = h.render(quietSettings());
    const gpu::ImageF tilted = h.render(bandSettings());
    CHECK(h.ctx->errorCount() == 0);

    // Inside the band the circle of confusion is zero, the gather takes its early exit, and the
    // pixel is the one that went in - not approximately, exactly.
    CHECK(rowIsIdentical(sharp, tilted, kHeight / 2));
    CHECK(rowIsIdentical(sharp, tilted, kHeight / 2 - 5));

    // Well past the falloff it is fully defocused: no step in the row survives at anything like its
    // original height, and the row's total variation drops with it.
    const float sharpContrast = rowContrast(sharp, 4);
    const float blurredContrast = rowContrast(tilted, 4);
    INFO("row 4 contrast sharp=" << sharpContrast << " tilted=" << blurredContrast
                                 << " max step sharp=" << rowMaxStep(sharp, 4)
                                 << " tilted=" << rowMaxStep(tilted, 4));
    CHECK(sharpContrast > 1.0f);
    CHECK(rowMaxStep(tilted, 4) < rowMaxStep(sharp, 4) * 0.2f);
    CHECK(blurredContrast < sharpContrast * 0.4f);

    // Monotone across the transition: nearer the band is sharper than further from it. This is the
    // assertion that catches a coverage function which is merely *different* outside the band
    // rather than increasing with distance from it.
    const float nearBand = rowContrast(tilted, kHeight / 2 + 14);
    const float midBand = rowContrast(tilted, kHeight / 2 + 24);
    INFO("near=" << nearBand << " mid=" << midBand << " far=" << blurredContrast);
    CHECK(nearBand > midBand);
    CHECK(midBand > blurredContrast);

    // HDR-correct: this runs before the tone map on scene-linear values, so the bright block must
    // still be far above 1.0 after being blurred, and the blurred noise must average out to roughly
    // what it averaged to before. A gather that normalised or clamped would show here.
    INFO("bright block after blur=" << tilted.pixel(2, 4)[0]);
    CHECK(tilted.pixel(2, 4)[0] > 8.0f);
    float sharpMean = 0.0f;
    float blurredMean = 0.0f;
    for (std::uint32_t x = 40; x < 152; ++x) {
        sharpMean += sharp.pixel(x, 4)[0];
        blurredMean += tilted.pixel(x, 4)[0];
    }
    sharpMean /= 112.0f;
    blurredMean /= 112.0f;
    INFO("mean sharp=" << sharpMean << " blurred=" << blurredMean);
    CHECK(sharpMean > 3.0f);
    CHECK(blurredMean > sharpMean * 0.7f);
    CHECK(blurredMean < sharpMean * 1.3f);
}

TEST_CASE("Rotating the tilt-shift band moves the blur onto the other axis", "[gpu][post][tiltshift]") {
    Harness h;
    REQUIRE(h.post.init().has_value());
    const gpu::ImageF sharp = h.render(quietSettings());

    auto settings = bandSettings();
    const gpu::ImageF horizontal = h.render(settings);
    settings.tiltShiftRotation = 90.0f;
    const gpu::ImageF vertical = h.render(settings);
    CHECK(h.ctx->errorCount() == 0);

    // A horizontal band leaves the middle row sharp and blurs the top; turned through 90 degrees it
    // does the opposite, and the middle row is now sharp only where it crosses the band.
    CHECK(rowIsIdentical(sharp, horizontal, kHeight / 2));
    CHECK_FALSE(rowIsIdentical(sharp, horizontal, 4));
    CHECK_FALSE(rowIsIdentical(sharp, vertical, kHeight / 2));
    CHECK(rowContrast(vertical, kHeight / 2) < rowContrast(sharp, kHeight / 2) * 0.6f);
    // The vertical band blurs the top row no more than the horizontal one does, because it covers
    // that row's middle: the blur has moved, it has not simply grown.
    CHECK(rowContrast(vertical, 4) > rowContrast(horizontal, 4));

    // The bright left block sits outside a vertical band at every height, but inside a horizontal
    // one for the rows the band covers, so it bleeds rightwards in one case and not the other.
    const float horizontalBleed = regionMean(horizontal, 6, 20);
    const float verticalBleed = regionMean(vertical, 6, 20);
    INFO("bleed horizontal=" << horizontalBleed << " vertical=" << verticalBleed);
    CHECK(verticalBleed > horizontalBleed);
    CHECK(horizontalBleed > regionMean(sharp, 6, 20));
}
