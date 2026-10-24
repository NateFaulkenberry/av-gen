// ADR-1643: the water's flow streaks, the GPU half.
//   * at 0 (with every other streak setting off its default) the water is byte-identical to a renderer whose shader
//     has the streak blocks removed -- on the plain path and on the cinematic path alike;
//   * above 0 they reach the river, as lit lines over a part of it;
//   * they move with the water and do not flicker: a thirtieth of a second later only a little of the river changed;
//   * hidden: their cost.

#include "support/water_bench.hpp"
#include "support/water_shader_variant.hpp"

#include "rendering/water_renderer.hpp"

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

constexpr const char* kStreakBegin = "---- flow streaks (ADR-1643) begin ----";
constexpr const char* kStreakEnd = "---- flow streaks (ADR-1643) end ----";

double luma(const gpu::ImageF& img, std::size_t i) {
    return 0.2126 * img.rgba[i * 4] + 0.7152 * img.rgba[i * 4 + 1] + 0.0722 * img.rgba[i * 4 + 2];
}

// Every streak setting but the switch off its default.
void shapeStreaks(scene::WaterSettings& w) {
    w.streakDensity = 0.6f;
    w.streakLength = 30.0f;
    w.streakLines = 90.0f;
    w.streakSpeed = 2.0f;
    w.streakHighlight = 2.0f;
    w.streakColorScale = 0.3f;
    w.streakColor1 = {1.0f, 0.0f, 0.0f};
    w.streakColor2 = {0.0f, 1.0f, 0.0f};
    w.streakColor3 = {0.0f, 0.0f, 1.0f};
}

} // namespace

TEST_CASE("with the flow streaks at 0 the water is byte-identical to the shader with no streak code",
          "[gpu][renderer][water][adr1643][identity]") {
    if (!fs::is_regular_file(qaWaterScene())) {
        SKIP("the water QA scene is not present");
    }
    int blocks = 0;
    const std::string stripped = stripMarkedBlocks(readWaterShaderSource(), kStreakBegin, kStreakEnd, &blocks);
    INFO(blocks << " marked streak blocks removed");
    REQUIRE(blocks >= 5);
    for (const char* name : {"waterFlowStreaks", "streakA", "streakLight", "streakSide"}) {
        INFO(name);
        REQUIRE(stripped.find(name) == std::string::npos);
    }
    const fs::path strippedDir = writeWaterShaderVariant("no-streaks", stripped);

    WaterBench bench = WaterBench::make(256, 176);
    WaterBench::Arm live = bench.arm();
    WaterBench::Arm none = bench.arm({strippedDir});
    bench.aim({40.0f, 6.0f, -86.0f}, {12.0f, -1.0f, -108.0f});
    bench.hold(12.0);
    scene::WaterSettings& w = bench.scene().waters.at(0).settings;
    shapeStreaks(w);
    REQUIRE(w.flowStreaks == 0.0f);

    // the plain path
    REQUIRE_FALSE(w.cinematic());
    const gpu::ImageF plain = bench.render(live);
    const auto mask = waterMask(plain, bench.renderDry(live));
    REQUIRE(countMask(mask) > 500);
    CHECK(gpu::hashImage(plain) == gpu::hashImage(bench.render(none)));

    // the cinematic path, streaks still off: drawn by the cinematic pipeline with them compiled out
    w.ssr = 0.5f;
    w.tintPatches = 0.4f;
    REQUIRE(w.cinematic());
    CHECK(gpu::hashImage(bench.render(live)) == gpu::hashImage(bench.render(none)));

    // THE CONTROL: on, they reach the picture
    w.flowStreaks = 1.5f;
    CHECK(gpu::hashImage(bench.render(live)) != gpu::hashImage(bench.render(none)));
    CHECK(bench.ctx->errorCount() == 0);
}

TEST_CASE("the flow streaks light lines over part of the river, and move without flickering",
          "[gpu][renderer][water][adr1643]") {
    if (!fs::is_regular_file(qaWaterScene())) {
        SKIP("the water QA scene is not present");
    }
    WaterBench bench = WaterBench::make(320, 200);
    WaterBench::Arm live = bench.arm();
    bench.aim({40.0f, 10.0f, -86.0f}, {12.0f, -1.0f, -108.0f});
    bench.hold(12.0);
    scene::WaterSettings& w = bench.scene().waters.at(0).settings;
    const gpu::ImageF off = bench.render(live);
    const auto mask = waterMask(off, bench.renderDry(live));
    REQUIRE(countMask(mask) > 800);
    w.flowStreaks = 2.0f;
    w.streakLines = 60.0f; // broad enough to resolve at this size
    w.streakDensity = 0.5f;
    const gpu::ImageF on = bench.render(live);
    dumpWater(on, "streaks-on");
    std::size_t brighter = 0;
    std::size_t water = 0;
    for (std::size_t i = 0; i < mask.size(); ++i) {
        if (!mask[i]) continue;
        ++water;
        brighter += luma(on, i) > luma(off, i) + 0.05 ? 1 : 0;
    }
    INFO(brighter << " of " << water << " water pixels lit by the streaks");
    CHECK(brighter > water / 20); // lines...
    CHECK(brighter < water * 9 / 10); // ...not a wash

    // a thirtieth of a second later: the streaks have travelled a little; nothing flickered
    bench.hold(12.0 + 1.0 / 30.0);
    const gpu::ImageF next = bench.render(live);
    std::size_t jumped = 0;
    for (std::size_t i = 0; i < mask.size(); ++i) {
        if (mask[i] && std::abs(luma(next, i) - luma(on, i)) > 0.25) ++jumped;
    }
    INFO(jumped << " water pixels changed by more than 0.25 in 1/30 s");
    CHECK(jumped < water / 50);
    CHECK(bench.ctx->errorCount() == 0);
}

// Hidden ([.]): the streaks' cost on the QA river at 1080p (honest timestamps, ADR-1600): the cinematic water with
// streaks off and on, interleaved over 6 reps of 60 frames; the scene label's median.
TEST_CASE("water flow streaks: cost (measure)", "[.][gpu][water][measure][adr1643]") {
    if (!fs::is_regular_file(qaWaterScene())) {
        SKIP("the water QA scene is not present");
    }
    WaterBench bench = WaterBench::make(1920, 1080);
    WaterBench::Arm live = bench.arm();
    if (!live.renderer->timeline().available()) {
        SKIP("timestamp queries unavailable");
    }
    bench.aim({40.0f, 6.0f, -86.0f}, {12.0f, -1.0f, -108.0f});
    bench.hold(12.0);
    scene::WaterSettings& w = bench.scene().waters.at(0).settings;
    w.ssr = 0.5f; // the cinematic path either way
    const auto measure = [&](float streaks) {
        w.flowStreaks = streaks;
        std::vector<double> ms;
        for (int i = 0; i < 80; ++i) {
            bench.time.renderTime = 12.0 + i / 60.0;
            REQUIRE(live.renderer->renderFrame(bench.scene(), bench.time, 1920, 1080).has_value());
            if (i >= 20) {
                ms.push_back(live.renderer->timeline().msFor("scene"));
            }
        }
        std::sort(ms.begin(), ms.end());
        return ms[ms.size() / 2];
    };
    static_cast<void>(measure(0.0f));
    static_cast<void>(measure(2.0f));
    std::vector<double> off;
    std::vector<double> on;
    for (int rep = 0; rep < 6; ++rep) {
        off.push_back(measure(0.0f));
        on.push_back(measure(2.0f));
    }
    const auto list = [](const std::vector<double>& v) {
        std::string out;
        for (const double x : v) {
            out += fmt::format(" {:.3f}", x);
        }
        return out;
    };
    WARN("1080p QA river, cinematic water, the scene label's median (ms), 6 interleaved reps: streaks off"
         << list(off) << " | on" << list(on));
    CHECK(on[0] > 0.0);
}
