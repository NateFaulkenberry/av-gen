// The sky and the water, measured against each other across a day (ADR-400).
//
// ADR-345 §Consequences records the defect in its own words: *"Reflections still sample the cube,
// not the analytic sky. The water therefore mirrors the HDRI while the visible sky is procedural,
// and they disagree at dawn and sunset."* ADR-347 turned the reflection down at the warm ends to
// work around the worst of it, and `src/scene/day_night.cpp` carries the same explanation as a
// source comment beside the curve that does it.
//
// Three accepted documents say the same thing and **nothing measured it in a test**. ADR-347's own
// assessment is that whoever fixes this builds the first regression harness; this is that harness,
// built before the fix rather than after, so the decision in ADR-400 rests on a number somebody can
// re-run rather than on three comments agreeing with each other.
//
// It is deliberately NOT a tripwire. It asserts only what must hold whether the defect is present
// or fixed -- that it is reading real sky and real water, and that the sky is on the day cycle --
// and it PRINTS the agreement table. A test that asserted the current gap would fail the day
// somebody closed it, which is a landmine rather than a harness. Hidden behind `[.probe]`, like the
// HDR Lab's printing arms, so it is run deliberately:
//
//   tools/gpu-lock.sh ./build/release/tests/avgen_render_tests "[water][sky][.probe]"

#include "app/engine.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/composition.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>

namespace fs = std::filesystem;
using namespace avgen;

namespace {

constexpr std::uint32_t kW = 640;
constexpr std::uint32_t kH = 360;

// The phases `src/scene/day_night.cpp` names, and the two that matter: sunrise and sunset are where
// the analytic sky is warmest and the baked map is least like it. NOON IS THE CONTROL -- it is the
// phase at which the procedural sky and the daylight map agree about what the sky looks like, so a
// gap that is as large at noon as at dawn is not a day/night failure at all but a constant offset
// between two regions of the frame, and this harness would be measuring nothing.
struct Phase {
    const char* name;
    float value;
};
constexpr std::array<Phase, 4> kPhases{
    {{"dawn", 0.25f}, {"noon", 0.50f}, {"sunset", 0.75f}, {"twilight", 0.82f}}};

struct Rgb {
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    [[nodiscard]] double rMinusB() const { return r - b; }
};

Rgb meanOf(const gpu::Image8& img, std::uint32_t x0, std::uint32_t x1, std::uint32_t y0, std::uint32_t y1) {
    Rgb out;
    std::size_t n = 0;
    for (std::uint32_t y = y0; y < std::min(y1, img.height); ++y) {
        for (std::uint32_t x = x0; x < std::min(x1, img.width); ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * img.width + x) * 4;
            out.r += img.rgba[i];
            out.g += img.rgba[i + 1];
            out.b += img.rgba[i + 2];
            ++n;
        }
    }
    REQUIRE(n > 0);
    out.r /= static_cast<double>(n);
    out.g /= static_cast<double>(n);
    out.b /= static_cast<double>(n);
    return out;
}

Rgb average(const Rgb& a, const Rgb& b) { return Rgb{(a.r + b.r) * 0.5, (a.g + b.g) * 0.5, (a.b + b.b) * 0.5}; }

// Two bands well clear of the horizon and of the island, taken on BOTH sides of frame so a
// left-right gradient in either the sky or the water cancels rather than being read as colour.
Rgb skyBand(const gpu::Image8& img) {
    return average(meanOf(img, 20, 180, 15, 70), meanOf(img, 460, 620, 15, 70));
}
Rgb waterBand(const gpu::Image8& img) {
    return average(meanOf(img, 20, 180, 285, 345), meanOf(img, 460, 620, 285, 345));
}

} // namespace

TEST_CASE("the sky and the water across a day", "[water][sky][gpu][.probe]") {
    static bool logInit = false;
    if (!logInit) {
        log::init(log::Level::Warn);
        logInit = true;
    }
    const fs::path project =
        fs::path(AVGEN_SOURCE_DIR) / "examples" / "treeisland" / "tree-of-life-ocean-world.json";
    REQUIRE(fs::is_regular_file(project));
    // The environment maps are gitignored and a fresh worktree does not have them. Skipping is
    // right here and a failure would be wrong: it would be a failure about the checkout.
    const fs::path dayMap =
        fs::path(AVGEN_SOURCE_DIR) / "assets" / "environments" / "tree_of_life_day.exr";
    if (!fs::exists(dayMap)) {
        SKIP("assets/environments is not populated in this worktree: " << dayMap.string());
    }

    auto ctx = gpu::Context::create(gpu::ContextDesc{});
    if (!ctx) {
        SKIP("no GPU adapter available: " << ctx.error().message);
    }
    gpu::ShaderLibrary shaders(**ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(**ctx, shaders);
    REQUIRE(renderer.init().has_value());

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(project).has_value());
    engine.setViewport(kW, kH);
    FixedStepClock clock(30.0);

    auto* enabled = engine.params().findAs<bool>("env/dayNight/enabled");
    auto* paused = engine.params().findAs<bool>("env/dayNight/paused");
    auto* phase = engine.params().findAs<float>("env/dayNight/dayPhase");
    REQUIRE(enabled != nullptr);
    REQUIRE(paused != nullptr);
    REQUIRE(phase != nullptr);
    enabled->setBase(true);
    paused->setBase(true); // or the phase advances with render time and the arms are not the arms

    std::array<double, kPhases.size()> skyRB{};
    std::array<double, kPhases.size()> waterRB{};
    std::array<double, kPhases.size()> gap{};

    fmt::print("\n== sky against water, across the day (sRGB bytes, 640x360) ==\n");
    fmt::print("{:10} {:>24} {:>8} | {:>24} {:>8} | {:>8}\n", "phase", "sky rgb", "R-B", "water rgb", "R-B",
               "gap");
    for (std::size_t i = 0; i < kPhases.size(); ++i) {
        phase->setBase(kPhases[i].value);
        engine.params().resetFinals();
        // Several frames: the environment cube is rebuilt on a hash change and the day/night map
        // swap is deferred, so the first frame after a phase jump can still be carrying the last
        // one's bake. A reading taken there would be a measurement of the deferral.
        FrameTime time{};
        for (int f = 0; f < 8; ++f) {
            time = engine.tick(clock);
            engine.update(time);
        }
        auto img = renderer.renderToImage(engine.scene(), time, kW, kH);
        REQUIRE(img.has_value());

        const Rgb sky = skyBand(*img);
        const Rgb water = waterBand(*img);
        skyRB[i] = sky.rMinusB();
        waterRB[i] = water.rMinusB();
        gap[i] = skyRB[i] - waterRB[i];
        fmt::print("{:10} ({:6.1f},{:6.1f},{:6.1f}) {:+8.1f} | ({:6.1f},{:6.1f},{:6.1f}) {:+8.1f} | {:+8.1f}\n",
                   kPhases[i].name, sky.r, sky.g, sky.b, skyRB[i], water.r, water.g, water.b, waterRB[i],
                   gap[i]);

        // Neither-ran guards, and they are the reason this is a harness rather than a printout. Two
        // black bands agree perfectly, and so do two bands of the same thing: if the sky band ever
        // starts reading water, or the water band sky, every number above becomes meaningless
        // while still looking like a measurement.
        INFO("phase " << kPhases[i].name);
        CHECK(sky.r + sky.g + sky.b > 30.0);
        CHECK(water.r + water.g + water.b > 30.0);
    }

    // The sky really is on the day cycle. Without this, a frame in which the procedural background
    // had silently stopped being drawn would print four identical rows and a gap of zero, and would
    // read as the defect being fixed.
    const double skySwing = *std::max_element(skyRB.begin(), skyRB.end()) -
                            *std::min_element(skyRB.begin(), skyRB.end());
    INFO("sky R-B swings " << skySwing << " across the cycle");
    CHECK(skySwing > 30.0);

    fmt::print("\nThe reading, 2026-09-20: dawn {:+.1f}, noon {:+.1f}, sunset {:+.1f}, twilight {:+.1f}.\n"
               "Noon is the control (ADR-400). Nothing above is asserted: this harness exists so a\n"
               "fix has a before-table, and a test that asserted the gap would fail the day it closed.\n",
               gap[0], gap[1], gap[2], gap[3]);
    CHECK(ctx.value()->errorCount() == 0);
}
