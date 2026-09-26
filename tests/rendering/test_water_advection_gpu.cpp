// The water surface's advection is bounded (ADR-914), measured from outside.
//
// Every travelling field on the water sampled its noise at `p - v * t`, an offset with no bound.
// Wherever the baked flow `v` differs between neighbouring vertices -- the nearest-segment tangent at
// every node of a smoothed river, a still body's rim, the river/pool switch, and the bank shear that
// every river has -- the noise across the triangle between them was compressed by `|dv| * t / cell`,
// so a film looked right at its start and streaked, then aliased, as it went on. The fix is a
// two-sample crossfade whose travel is bounded by one period.
//
// The claim tested is the one a viewer would notice: the surface at 200 s is as fine as it was at
// 10 s. The control is the same shader with the bound taken out -- derived from the live file by
// replacing the one marked function body, so it cannot drift -- which has to fail the same
// measurement, or the measurement cannot see the defect it exists for.

#include "support/water_bench.hpp"
#include "support/water_shader_variant.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>

#include <filesystem>
#include <string>
#include <vector>

using namespace avgen;
using namespace avgen::testsupport;
namespace fs = std::filesystem;

namespace {

// ADR-914's bound taken out: each field's travel is the whole timeline second and its second sample
// carries no weight, which is exactly the `p - v * t` the surface sampled before.
std::string unboundedWaterShader() {
    return replaceMarkedBody(readWaterShaderSource(), "---- flow phases (ADR-914) begin ----",
                             "---- flow phases (ADR-914) end ----",
                             "    out.travel = vec2<f32>(t, t);\n"
                             "    out.weight = vec2<f32>(1.0, 0.0);\n"
                             "    out.jump0 = vec2<f32>(0.0);\n"
                             "    out.jump1 = vec2<f32>(0.0);\n");
}

} // namespace

// A view down onto the QA river, with the bank shear -- the loss of speed toward each bank -- in frame
// across the whole channel. The QA river runs at 0.91 m/s over a 7 m half width, so unbounded, the
// bank shear alone compresses the ripples near the banks by `1 + |dv/dx| * t`: about 3-fold at 10 s and
// over 30-fold at 200 s, where the water is a field of fine axis-aligned streaks. Bounded, the
// compression is at most `1 + |dv/dx| * period` at every second.
//
// The measure is the water's fine structure relative to its contrast (`Fine::relative`), with the
// moon's glint and the sparkle off: both are sparse bright points whose count swings a mean by a
// factor of two from one second to the next, which measured the glints rather than the ripples. Each
// window is averaged over four seconds, because one frame of a travelling pattern is one sample of it.
TEST_CASE("the water surface at 200 s is as fine as it was at 10 s", "[gpu][renderer][water][advection]") {
    if (!fs::is_regular_file(qaWaterScene())) {
        SKIP("the water QA scene is not present");
    }
    WaterBench bench = WaterBench::make(480, 320);
    bench.aim({20.0f, 34.0f, -96.0f}, {14.0f, 0.0f, -110.0f});
    WaterBench::Arm live = bench.arm();
    const fs::path unboundedDir = writeWaterShaderVariant("unbounded", unboundedWaterShader());
    WaterBench::Arm unbounded = bench.arm({unboundedDir});

    // One mask for every arm and second: the water is where it is whatever the ripples are doing.
    bench.hold(10.0);
    const std::vector<char> mask = waterMask(bench.render(live), bench.renderDry(live));
    REQUIRE(countMask(mask) > 5000);

    const auto windowAround = [&](WaterBench::Arm& arm, double from, const char* name) {
        double sum = 0.0;
        for (const double offset : {0.0, 1.3, 2.7, 4.1}) {
            bench.hold(from + offset);
            scene::WaterSettings& w = bench.scene().waters.at(0).settings;
            w.specular = 0.0f;
            w.sparkle = 0.0f;
            const gpu::ImageF image = bench.render(arm);
            dumpWater(image, fmt::format("advection-{}-{:.1f}", name, from + offset), 4.0f);
            const Fine fine = fineStructure(luminance(image), mask, bench.width, bench.height);
            REQUIRE(fine.pixels > 5000);
            sum += fine.relative();
        }
        return sum / 4.0;
    };
    const double live10 = windowAround(live, 10.0, "bounded");
    const double live200 = windowAround(live, 200.0, "bounded");
    const double old10 = windowAround(unbounded, 10.0, "unbounded");
    const double old200 = windowAround(unbounded, 200.0, "unbounded");
    const double liveRatio = live200 / live10;
    const double oldRatio = old200 / old10;
    INFO(fmt::format("relative fine structure, 10 s -> 200 s: bounded {:.4f} -> {:.4f} (x{:.3f}), "
                     "unbounded {:.4f} -> {:.4f} (x{:.3f})",
                     live10, live200, liveRatio, old10, old200, oldRatio));
    REQUIRE(live10 > 0.0);
    // THE CONTROL: without the bound the same measurement sees the late film streak. If it did not,
    // the passing lines below would be a statement about a measurement that cannot see the defect.
    CHECK(oldRatio > 1.15);
    // Bounded: the same surface at both seconds, within the audit's +-15%.
    CHECK(liveRatio > 1.0 / 1.15);
    CHECK(liveRatio < 1.15);
    // And finer than the unbounded surface already was at 10 s, where its offset had been growing for
    // ten seconds: the bound is a property of every second, not only of late ones.
    CHECK(live10 < old10);
    CHECK(bench.ctx->errorCount() == 0);
}

// The bound is not a freeze. The surface still travels -- two frames a second apart differ -- and it
// is still a pure function of the second: the same second rendered twice is the same image, which is
// what a seek landing on the frame a play reached depends on.
TEST_CASE("bounded water still travels and is a pure function of the second",
          "[gpu][renderer][water][advection]") {
    if (!fs::is_regular_file(qaWaterScene())) {
        SKIP("the water QA scene is not present");
    }
    WaterBench bench = WaterBench::make(320, 224);
    bench.aim({20.0f, 34.0f, -96.0f}, {14.0f, 0.0f, -110.0f});
    WaterBench::Arm live = bench.arm();

    bench.hold(57.0);
    const gpu::ImageF a = bench.render(live);
    const gpu::ImageF again = bench.render(live);
    bench.hold(58.0);
    const gpu::ImageF b = bench.render(live);
    const bool repeatable = gpu::hashImage(a) == gpu::hashImage(again);
    const bool moved = gpu::hashImage(a) != gpu::hashImage(b);
    CHECK(repeatable);
    CHECK(moved);
    CHECK(bench.ctx->errorCount() == 0);
}
