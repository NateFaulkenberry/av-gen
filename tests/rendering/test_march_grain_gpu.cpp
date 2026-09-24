// ADR-710: the march's GRAIN on the tornado hero, the labs and the fog arms, at matched luminance.
//
// A hidden probe (`[.probe]`): it prints a table for the ADR and asserts nothing about the numbers,
// because a test pinned to a measurement is a tripwire on every unrelated change (ADR-577).
//
// **What "grain" means here, and why this estimator.** Grain is the march's sampling error: where
// the samples along a ray land decides what a pixel shows, and the per-pixel start jitter moves
// them every frame. So two frames 1/240 s apart -- the smallest step that changes the jitter's frame
// nonce -- differ by (sampling error) + (the storm's motion in 4 ms). The same pair with the jitter
// OFF differs by the motion alone. Both are reported; the first minus the second is the grain.
//
// **Matched luminance** (the measurement rule this project learned the hard way: a density change
// fakes a noise change). The noise is divided by the medium's OWN mean contribution in the same
// pixels -- the frame with the medium minus the frame without it -- and that contribution is
// printed beside it, so an arm that got darker cannot pass for one that got cleaner.

#include "app/engine.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/render_quality.hpp"
#include "rendering/scene_renderer.hpp"
#include "world/effects/effect_registry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

constexpr std::uint32_t kWidth = 640;
constexpr std::uint32_t kHeight = 360;

std::unique_ptr<gpu::Context> makeContext() {
    static bool logInit = false;
    if (!logInit) {
        log::init(log::Level::Error);
        logInit = true;
    }
    auto ctx = gpu::Context::create(gpu::ContextDesc{});
    if (!ctx) {
        SKIP("no GPU adapter available: " << ctx.error().message);
    }
    return std::move(*ctx);
}

struct Case {
    const char* name;
    const char* path;  // relative to the source root
    bool project;      // a project (.json) or a composition (.scene.json)
    double second;
    int shadowSteps;   // -1 = as authored
    float shadowStrength;
    int steps = -1;    // -1 = as authored
};

std::vector<float> luminance(const gpu::ImageF& img) {
    std::vector<float> l(static_cast<std::size_t>(img.width) * img.height);
    for (std::size_t i = 0; i < l.size(); ++i) {
        const float* p = img.rgba.data() + i * 4;
        l[i] = 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
    }
    return l;
}

void setParam(app::Engine& engine, const std::string& path, float v) {
    auto* p = engine.params().find(path);
    INFO("parameter " << path);
    REQUIRE(p != nullptr);
    p->setBaseComponent(0, v);
}

void setMedia(app::Engine& engine, bool on) {
    for (const auto& e : engine.effects()) {
        if (e.kind == world::EffectKind::Tornado || e.kind == world::EffectKind::VolumetricFog ||
            e.kind == world::EffectKind::Vortex) {
            setParam(engine, world::effectParameterPrefix(e.id) + "enabled", on ? 1.0f : 0.0f);
        }
    }
}

gpu::ImageF frameAt(app::Engine& engine, rendering::SceneRenderer& renderer, double seconds) {
    engine.seekSeconds(seconds);
    FixedStepClock clock(30.0);
    clock.restartAt(seconds);
    FrameTime time = engine.tick(clock);
    engine.setViewport(kWidth, kHeight);
    engine.update(time);
    renderer.resetTemporalHistory();
    auto image = renderer.renderToImageFloat(engine.scene(), time, kWidth, kHeight);
    REQUIRE(image.has_value());
    return std::move(*image);
}

void measure(const Case& c) {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    renderer.setQuality(rendering::QualityTier::Offline);
    REQUIRE(renderer.resize(kWidth, kHeight).has_value());
    app::Engine engine(app::EngineMode::Offline);
    const fs::path path = fs::path(AVGEN_SOURCE_DIR) / c.path;
    REQUIRE((c.project ? engine.loadProject(path) : engine.loadComposition(path)).has_value());
    if (c.steps > 0) {
        setParam(engine, "scene/volumeSteps", static_cast<float>(c.steps));
    }
    if (c.shadowSteps >= 0) {
        setParam(engine, "scene/volumeShadowSteps", static_cast<float>(c.shadowSteps));
        setParam(engine, "scene/volumeShadowStrength", c.shadowStrength);
    }
    const double dt = 1.0 / 240.0;

    setMedia(engine, false);
    const std::vector<float> without = luminance(frameAt(engine, renderer, c.second));
    setMedia(engine, true);
    const std::vector<float> a = luminance(frameAt(engine, renderer, c.second));
    const int steps = static_cast<int>(renderer.stats().volume.steps);
    const std::vector<float> b = luminance(frameAt(engine, renderer, c.second + dt));
    setParam(engine, "scene/volumeJitter", 0.0f);
    const std::vector<float> a0 = luminance(frameAt(engine, renderer, c.second));
    const std::vector<float> b0 = luminance(frameAt(engine, renderer, c.second + dt));

    // The medium's pixels: where it contributes at least 5% of its own 99th-percentile contribution.
    std::vector<float> contribution(a.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        contribution[i] = std::abs(a[i] - without[i]);
    }
    std::vector<float> sorted = contribution;
    std::sort(sorted.begin(), sorted.end());
    const float p99 = sorted[static_cast<std::size_t>(0.99 * static_cast<double>(sorted.size() - 1))];
    const float floor = 0.05f * p99;
    double n = 0.0;
    double sumC = 0.0;
    double sumL = 0.0;
    double sumD = 0.0;
    double sumD0 = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (contribution[i] < floor || p99 <= 0.0f) {
            continue;
        }
        n += 1.0;
        const double da = static_cast<double>(a[i]) - static_cast<double>(b[i]);
        const double d0 = static_cast<double>(a0[i]) - static_cast<double>(b0[i]);
        sumC += static_cast<double>(contribution[i]);
        sumL += static_cast<double>(a[i]);
        sumD += da * da;
        sumD0 += d0 * d0;
    }
    REQUIRE(n > 0.0);
    const double meanC = sumC / n;
    const double noise = std::sqrt(sumD / n / 2.0);
    const double motion = std::sqrt(sumD0 / n / 2.0);
    const double grain = std::sqrt(std::max(noise * noise - motion * motion, 0.0));
    std::fprintf(stderr,
                 "GRAIN %-22s steps %3d  px %6.0f  medium mean %.4f  frame mean %.4f  noise %.5f  motion %.5f  "
                 "grain/medium %.4f\n",
                 c.name, steps, n, meanC, sumL / n, noise, motion, grain / std::max(meanC, 1e-9));
}

} // namespace

TEST_CASE("PROBE the march's grain at matched luminance", "[.probe][volume][grain]") {
    const char* only = std::getenv("AVGEN_GRAIN_CASE");
    const std::vector<Case> cases = {
        {"hero", "examples/treeisland/tree-of-life-floating-island.json", true, 6.0, 0, 0.0f},
        {"hero-shadow4-0.4", "examples/treeisland/tree-of-life-floating-island.json", true, 6.0, 4, 0.4f},
        {"hero-128", "examples/treeisland/tree-of-life-floating-island.json", true, 6.0, 0, 0.0f, 128},
        {"hero-256", "examples/treeisland/tree-of-life-floating-island.json", true, 6.0, 0, 0.0f, 256},
        {"hero-256-shadow4-0.4", "examples/treeisland/tree-of-life-floating-island.json", true, 6.0, 4, 0.4f, 256},
        {"showcase", "examples/labs/tornado-showcase.scene.json", false, 6.0, -1, 0.0f},
        {"modes-d", "examples/labs/tornado-modes-d-full.scene.json", false, 6.0, -1, 0.0f},
        {"fog-bank-32", "examples/treeisland/_march-fog-bank-32.json", true, 6.0, -1, 0.0f},
        {"fog-sphere-32", "examples/treeisland/_march-fog-sphere-32.json", true, 6.0, -1, 0.0f},
        {"fog-box-32", "examples/treeisland/_march-fog-box-32.json", true, 6.0, -1, 0.0f},
    };
    for (const Case& c : cases) {
        if (only != nullptr && std::string(only) != c.name) {
            continue;
        }
        if (!fs::exists(fs::path(AVGEN_SOURCE_DIR) / c.path)) {
            std::fprintf(stderr, "GRAIN %-22s skipped: %s is not generated\n", c.name, c.path);
            continue;
        }
        measure(c);
    }
}
