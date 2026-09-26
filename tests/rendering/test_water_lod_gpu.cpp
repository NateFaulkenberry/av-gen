// The water's fades count reference pixels (ADR-915), measured from outside.
//
// The ripple layers, the sparkle's band-pass and the foam's break-up all fade by how many pixels a
// cycle spans. Counted in the frame's own pixels, a render at twice the resolution keeps detail out to
// twice the distance: GV3's previews (960x540 at 2x supersampling, 1080 rows internal) rendered its far
// river as a mirror and its final (1920x1080 at 2x, 2160 rows) showed ripple texture across it (GV3
// F37). Counted in a 1080-row frame's pixels, both fade the same world-space detail.
//
// The QA scene's own camera is a grazing view from the bank, so the river runs from the lens to the
// far distance and every layer's fade distance is somewhere in frame. Rendered at 1080 rows and at
// 2160, the second is box-averaged onto the first's grid and the fine structure of the water is
// compared, in the near and far halves of the water separately. The control is the same shader counting
// its own pixels, derived from the live file by changing its one constant.

#include "support/water_bench.hpp"
#include "support/water_shader_variant.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

using namespace avgen;
using namespace avgen::testsupport;
namespace fs = std::filesystem;

namespace {

// The shader counting its own pixels, as it did before ADR-915.
std::string ownPixelWaterShader() {
    return replaceOnce(readWaterShaderSource(), "const kWaterReferenceRows: f32 = 1080.0;",
                       "const kWaterReferenceRows: f32 = 1.0e9;");
}

struct ResolutionPair {
    Fine nearLow, nearHigh, farLow, farHigh;
};

// The bench's view at 1440x1080 and 2880x2160 through `arm`, measured on the 1080-row grid.
ResolutionPair measureResolutions(WaterBench& bench, WaterBench::Arm& arm, double seconds) {
    constexpr std::uint32_t kLowW = 1440;
    constexpr std::uint32_t kLowH = 1080;
    bench.resize(kLowW, kLowH);
    bench.hold(seconds);
    const gpu::ImageF low = bench.render(arm);
    const std::vector<char> lowMask = waterMask(low, bench.renderDry(arm));
    bench.resize(kLowW * 2, kLowH * 2);
    bench.hold(seconds);
    const gpu::ImageF high = bench.render(arm);
    const std::vector<char> highMask = maskDown2(waterMask(high, bench.renderDry(arm)), kLowW * 2, kLowH * 2);
    std::vector<char> mask(lowMask.size());
    for (std::size_t i = 0; i < mask.size(); ++i) {
        mask[i] = (lowMask[i] != 0 && highMask[i] != 0) ? 1 : 0;
    }
    const std::vector<float> lowLum = luminance(low);
    const std::vector<float> highLum = boxDown2(luminance(high), kLowW * 2, kLowH * 2);

    // Where the water is, top to bottom, so "far" and "near" are halves of the water and not of the
    // frame (the sky and the far bank are above it).
    std::uint32_t top = kLowH;
    std::uint32_t bottom = 0;
    for (std::uint32_t y = 0; y < kLowH; ++y) {
        for (std::uint32_t x = 0; x < kLowW; ++x) {
            if (mask[static_cast<std::size_t>(y) * kLowW + x] != 0) {
                top = std::min(top, y);
                bottom = std::max(bottom, y);
            }
        }
    }
    REQUIRE(bottom > top + 40);
    const std::uint32_t middle = (top + bottom) / 2;
    ResolutionPair r;
    r.farLow = fineStructure(lowLum, mask, kLowW, kLowH, top, middle);
    r.farHigh = fineStructure(highLum, mask, kLowW, kLowH, top, middle);
    r.nearLow = fineStructure(lowLum, mask, kLowW, kLowH, middle, bottom + 1);
    r.nearHigh = fineStructure(highLum, mask, kLowW, kLowH, middle, bottom + 1);
    return r;
}

} // namespace

// The near half of the water is the calibration. Nothing near the lens fades at either resolution, so
// the ratio there is what the comparison itself does -- a 2160-row frame box-averaged onto the 1080-row
// grid is smoother than a 1080-row frame that point-samples the same shading (x0.83 here, the same in
// both arms). The far half, relative to that, is what the fades do: x1.08 counting reference pixels,
// x1.44 counting the frame's own.
TEST_CASE("a preview and a final at twice its resolution fade the same water", "[gpu][renderer][water][lod]") {
    if (!fs::is_regular_file(qaWaterScene())) {
        SKIP("the water QA scene is not present");
    }
    WaterBench bench = WaterBench::make(1440, 1080);
    WaterBench::Arm live = bench.arm();
    const fs::path ownDir = writeWaterShaderVariant("own-pixels", ownPixelWaterShader());
    WaterBench::Arm own = bench.arm({ownDir});

    const ResolutionPair now = measureResolutions(bench, live, 12.0);
    const ResolutionPair before = measureResolutions(bench, own, 12.0);
    const double nowNear = now.nearHigh.energy / now.nearLow.energy;
    const double beforeNear = before.nearHigh.energy / before.nearLow.energy;
    const double nowFar = (now.farHigh.energy / now.farLow.energy) / nowNear;
    const double beforeFar = (before.farHigh.energy / before.farLow.energy) / beforeNear;
    INFO(fmt::format("fine structure, 2160 rows over 1080: near half x{:.3f} ({} px) and x{:.3f} counting its own "
                     "pixels; far half relative to near x{:.3f} ({} px), and x{:.3f} counting its own pixels",
                     nowNear, now.nearLow.pixels, beforeNear, nowFar, now.farLow.pixels, beforeFar));
    REQUIRE(now.farLow.pixels > 20000);
    REQUIRE(now.nearLow.pixels > 20000);
    // The calibration is a calibration: near the lens both shaders fade nothing, so they agree.
    REQUIRE(std::fabs(nowNear - beforeNear) < 0.03);
    // THE CONTROL: counting its own pixels, the final keeps ripples the preview fades, so its far water
    // carries measurably more fine structure. If it did not, this scene could not show F37.
    CHECK(beforeFar > 1.15);
    // Counting reference pixels, the two agree within the audit's +-15%.
    CHECK(nowFar > 1.0 / 1.15);
    CHECK(nowFar < 1.15);
    CHECK(bench.ctx->errorCount() == 0);
}
