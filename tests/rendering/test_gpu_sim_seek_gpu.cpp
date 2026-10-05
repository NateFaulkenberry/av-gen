// A seeked frame against the played frame, for the GPU state that integrates over time
// (docs/development/gpu-sim-seek-investigation.md).
//
// The claim under test came from the GPU world spike (Phase 3, system C): production's stateful GPU
// systems "reset and warm up or catch up <= 240 steps, so past 4 s a scrubbed frame is not the played
// frame". This file drives the real product path -- `app::Engine` plus `rendering::SceneRenderer` at
// the Offline tier, the pair `RenderJob` uses -- in three ways and compares the pictures:
//
//   played   one engine, one renderer, every frame from 0 to T at 30 fps (a `--range 0:T` render)
//   fresh    a new engine and renderer that seek straight to T (a `--range T:T` render)
//   scrubbed one engine and renderer that played a second, then seek T forwards and backwards with
//            `resetTemporalHistory()` as the transport does (the editor's scrub)
//
// The Offline tier is not decoration. It has no temporal shortcut, so a fresh renderer and one that
// has played 1,800 frames draw the same picture of the same state; the T = 2 s arm below is the
// proof of that (the positive control). Any difference at a later T is therefore the state, not
// the harness.
//
// Frames are written to $AVGEN_SEEK_FRAMES when it is set, so a person can look at them.

#include "app/engine.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "assets/image.hpp"
#include "rendering/render_quality.hpp"
#include "rendering/scene_renderer.hpp"
#include "rendering/field_uniforms.hpp"
#include "rendering/simulation.hpp"
#include "scene/scene.hpp"
#include "spatial/field.hpp"
#include "spatial/grid_field.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <utility>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

constexpr double kFps = 30.0;
constexpr std::uint32_t kW = 320;
constexpr std::uint32_t kH = 180;

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

struct FrameDiff {
    double meanAbs = 0.0;        // mean absolute difference over RGB, in 8-bit levels
    int maxAbs = 0;              // largest channel difference
    double fractionOver2 = 0.0;  // pixels whose largest channel difference exceeds 2 levels
    double meanLumaA = 0.0;
    double meanLumaB = 0.0;

    [[nodiscard]] std::string describe() const {
        return fmt::format("mean |d| {:.3f} levels, max {}, {:.3f}% of pixels > 2 levels, mean luma {:.2f} vs {:.2f}",
                           meanAbs, maxAbs, 100.0 * fractionOver2, meanLumaA, meanLumaB);
    }
    // The tolerance this file states for "the same frame": at most 0.1% of pixels differ by more than
    // two levels and the mean difference is under a quarter of a level. Loose enough for a stray
    // LSB, three orders of magnitude tighter than anything the defect produces.
    [[nodiscard]] bool same() const { return fractionOver2 <= 0.001 && meanAbs <= 0.25; }
};

FrameDiff diff(const gpu::Image8& a, const gpu::Image8& b) {
    REQUIRE(a.width == b.width);
    REQUIRE(a.height == b.height);
    FrameDiff d;
    const std::size_t pixels = static_cast<std::size_t>(a.width) * a.height;
    double sum = 0.0;
    std::size_t over = 0;
    for (std::size_t p = 0; p < pixels; ++p) {
        int worst = 0;
        for (int c = 0; c < 3; ++c) {
            const int delta = std::abs(static_cast<int>(a.rgba[p * 4 + c]) - static_cast<int>(b.rgba[p * 4 + c]));
            sum += delta;
            worst = std::max(worst, delta);
        }
        d.maxAbs = std::max(d.maxAbs, worst);
        over += worst > 2 ? 1 : 0;
        const auto luma = [&](const gpu::Image8& img) {
            return 0.2126 * img.rgba[p * 4] + 0.7152 * img.rgba[p * 4 + 1] + 0.0722 * img.rgba[p * 4 + 2];
        };
        d.meanLumaA += luma(a);
        d.meanLumaB += luma(b);
    }
    d.meanAbs = sum / static_cast<double>(pixels * 3);
    d.fractionOver2 = static_cast<double>(over) / static_cast<double>(pixels);
    d.meanLumaA /= static_cast<double>(pixels);
    d.meanLumaB /= static_cast<double>(pixels);
    return d;
}

void save(const gpu::Image8& image, const std::string& name) {
    const char* dir = std::getenv("AVGEN_SEEK_FRAMES");
    if (dir == nullptr || *dir == '\0') {
        return;
    }
    fs::create_directories(dir);
    (void)assets::writePng(fs::path(dir) / (name + ".png"), image.width, image.height, image.rgba);
}

// The engine/renderer pair one product path owns.
struct Rig {
    Rig(gpu::Context& ctx, gpu::ShaderLibrary& shaders, const fs::path& sceneFile)
        : engine(app::EngineMode::Offline), renderer(ctx, shaders), clock(kFps) {
        REQUIRE(engine.loadComposition(sceneFile).has_value());
        REQUIRE(renderer.init().has_value());
        renderer.setQuality(rendering::QualityTier::Offline); // what RenderJob renders at
    }

    // RenderJob's positioning: a fresh fixed-step clock whose next tick is `seconds`, frame 0.
    void restartAt(double seconds) {
        clock = FixedStepClock(kFps);
        clock.restartAt(seconds);
        engine.seekSeconds(seconds);
    }

    // The editor's scrub: the transport seeks, the renderer drops its temporal history. The clock
    // is restarted rather than `seek`ed because `FixedStepClock::seek` makes the NEXT tick land one
    // frame after `seconds`; the first version of this file compared T + 1/30 with T and read the
    // two sub-steps as a defect. Same engine, same renderer: only the landing instant is RenderJob's.
    void scrubTo(double seconds) {
        clock.restartAt(seconds);
        engine.seekSeconds(seconds);
        renderer.resetTemporalHistory();
    }

    FrameTime advance() {
        const FrameTime t = engine.tick(clock);
        engine.setViewport(kW, kH);
        engine.update(t);
        return t;
    }

    void drawOnly() {
        const FrameTime t = advance();
        REQUIRE(renderer.renderFrame(engine.scene(), t, kW, kH).has_value());
    }

    gpu::Image8 capture() {
        const FrameTime t = advance();
        auto image = renderer.renderToImage(engine.scene(), t, kW, kH);
        REQUIRE(image.has_value());
        return std::move(*image);
    }

    app::Engine engine;
    rendering::SceneRenderer renderer;
    FixedStepClock clock;
};

// Plays 0 -> max(times) once and captures the frame at each requested second.
std::map<double, gpu::Image8> playThrough(gpu::Context& ctx, gpu::ShaderLibrary& shaders, const fs::path& scene,
                                          const std::vector<double>& times) {
    Rig rig(ctx, shaders, scene);
    rig.restartAt(0.0);
    std::map<double, gpu::Image8> out;
    const double last = *std::max_element(times.begin(), times.end());
    const auto frames = static_cast<long>(std::lround(last * kFps));
    for (long f = 0; f <= frames; ++f) {
        const double t = static_cast<double>(f) / kFps;
        const bool wanted = std::any_of(times.begin(), times.end(),
                                        [&](double w) { return std::lround(w * kFps) == f; });
        if (wanted) {
            out.emplace(t, rig.capture());
        } else {
            rig.drawOnly();
        }
    }
    return out;
}

gpu::Image8 freshSeek(gpu::Context& ctx, gpu::ShaderLibrary& shaders, const fs::path& scene, double seconds,
                      std::uint32_t particleWarmUp = 0) {
    Rig rig(ctx, shaders, scene);
    rig.renderer.setParticleWarmUpFrames(particleWarmUp); // RenderJob's `--particle-warmup`
    rig.restartAt(seconds);
    return rig.capture();
}

const gpu::Image8& at(const std::map<double, gpu::Image8>& frames, double seconds) {
    for (const auto& [t, image] : frames) {
        if (std::abs(t - seconds) < 1e-6) {
            return image;
        }
    }
    FAIL("no played frame at " << seconds);
    return frames.begin()->second;
}

} // namespace

// ---- simulated grids (ADR-032, ADR-581) ---------------------------------------------------------
//
// examples/labs/grid-catchup-lab.scene.json is the one authored grid scene: smoke injected at the
// base and carried up by a spiral, rendered through the volume pass. How far the smoke has
// travelled is a readout of how many sub-steps the grid has taken. Its fields do not vary in time,
// so the grid's state at t is a pure function of the step count, and a seek CAN be exact.
TEST_CASE("A simulated grid seeked to T draws the frame played to T", "[gpu][simulation][seek]") {
    const fs::path scene = fs::path(AVGEN_SOURCE_DIR) / "examples" / "labs" / "grid-catchup-lab.scene.json";
    REQUIRE(fs::is_regular_file(scene));
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});

    const std::vector<double> times = {2.0, 10.0, 30.0, 60.0};
    const auto played = playThrough(*ctx, shaders, scene, times);
    for (const auto& [t, image] : played) {
        save(image, fmt::format("grid-played-{:03.0f}s", t));
    }

    // Sensitivity: the grid really does change between the seconds compared, so "same" below cannot
    // be a scene that stopped moving.
    {
        const FrameDiff d = diff(at(played, 2.0), at(played, 10.0));
        INFO("played 2 s vs played 10 s: " << d.describe());
        CHECK_FALSE(d.same());
    }

    // A `--range T:T` render: a fresh engine and renderer that go straight to T.
    for (const double t : times) {
        const gpu::Image8 seeked = freshSeek(*ctx, shaders, scene, t);
        save(seeked, fmt::format("grid-fresh-seek-{:03.0f}s", t));
        const FrameDiff d = diff(at(played, t), seeked);
        INFO("fresh seek to " << t << " s vs played: " << d.describe());
        CHECK(d.same());
    }

    // The editor's scrub: one engine and renderer, played for a second, then seeked forwards through
    // the times and finally backwards to the middle one.
    {
        Rig rig(*ctx, shaders, scene);
        rig.restartAt(0.0);
        for (int f = 0; f < 30; ++f) {
            rig.drawOnly();
        }
        std::vector<double> order = times;
        order.push_back(times[1]); // backwards, from the last
        std::string label = "fwd";
        for (std::size_t i = 0; i < order.size(); ++i) {
            const double t = order[i];
            if (i + 1 == order.size()) {
                label = "back";
            }
            rig.scrubTo(t);
            const gpu::Image8 scrubbed = rig.capture();
            save(scrubbed, fmt::format("grid-scrub-{}-{:03.0f}s", label, t));
            const FrameDiff d = diff(at(played, t), scrubbed);
            INFO("scrub (" << label << ") to " << t << " s vs played: " << d.describe());
            CHECK(d.same());
        }
    }
    CHECK(ctx->errorCount() == 0);
}

// ---- GPU particles (ADR-015/040), measured, and hidden on purpose ----------------------------
//
// ADR-360 RELAXED scrub == play for particles on the owner's decision (2026-09-19, "I think we can
// ease the scrub must exactly replay particle animations"): a seek empties the pools, an opt-in
// warm-up of at most 240 frames refills them, and ADR-395 records that even a full warm-up lands
// particles in different slots and so with different velocities. This case states the strict
// contract anyway, so the size of the relaxation is a number rather than an adjective, and it FAILS
// today by design. It is hidden (`[.]`) so the suite keeps the owner's contract; run it with
// `avgen_render_tests "[.investigate][particles]"`.
//
// Two scenes: the particle VFX lab (lifetimes 0.5-1.2 s, well inside a 4 s warm-up) and the Tree
// of Life island (motes and leaves that live 13-56 s, which no 240-frame warm-up can refill).
TEST_CASE("A particle field seeked to T draws the frame played to T", "[.investigate][gpu][particles][seek]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    struct Case {
        const char* label;
        fs::path file;
    };
    const std::vector<Case> cases = {
        {"vfx-lab", fs::path(AVGEN_SOURCE_DIR) / "examples" / "labs" / "particle-vfx-lab.scene.json"},
        {"tree-of-life", fs::path(AVGEN_SOURCE_DIR) / "examples" / "treeisland" / "tree-of-life-floating-island.scene.json"},
    };
    const std::vector<double> times = {2.0, 10.0, 30.0};
    for (const Case& c : cases) {
        if (!fs::is_regular_file(c.file)) {
            WARN("skipping " << c.label << ": " << c.file << " is not present");
            continue;
        }
        const auto played = playThrough(*ctx, shaders, c.file, times);
        for (const double t : times) {
            save(at(played, t), fmt::format("particles-{}-played-{:03.0f}s", c.label, t));
            for (const std::uint32_t warm : {0u, 240u}) {
                const gpu::Image8 seeked = freshSeek(*ctx, shaders, c.file, t, warm);
                save(seeked, fmt::format("particles-{}-seek-warm{}-{:03.0f}s", c.label, warm, t));
                const FrameDiff d = diff(at(played, t), seeked);
                INFO(c.label << ": fresh seek to " << t << " s, warm-up " << warm << " vs played: " << d.describe());
                CHECK(d.same());
            }
        }
    }
}

// Hidden cost probe: `avgen_render_tests "[.perf][seek]"`, Release, under the lock. What a seek to T
// costs a fresh grid now that it replays the whole backlog (ADR-1114): wall time of the one frame
// that lands on T, for the lab's 48^3 scalar smoke and for a 128^3 scalar grid with Jacobi diffusion
// (the largest single grid the shared table holds).
TEST_CASE("What a seek costs a simulated grid", "[.perf][seek][simulation]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});

    const auto makeScene = [](int resolution, bool diffuse) {
        scene::Scene s;
        spatial::FieldSpec source;
        source.name = "source";
        source.kind = spatial::FieldKind::Box;
        source.position = {1.5f, 4.5f, 4.5f};
        source.size = glm::vec3(1.0f);
        source.softness = 0.1f;
        s.fields.fields.push_back(source);
        spatial::FieldSpec wind;
        wind.name = "wind";
        wind.kind = spatial::FieldKind::Direction;
        wind.axis = {1.0f, 0.0f, 0.0f};
        wind.strength = 1.0f;
        s.fields.fields.push_back(wind);
        spatial::GridField g;
        g.name = "smoke";
        g.resolution = glm::ivec3(resolution);
        g.boundsMin = glm::vec3(0.0f);
        g.boundsMax = glm::vec3(16.0f);
        g.simRate = 60.0f;
        g.maxSubSteps = 4;
        g.injectField = "source";
        g.injectRate = 2.5f;
        g.velocityField = "wind";
        g.dissipation = 0.12f;
        if (diffuse) {
            g.diffusion = 0.5f;
            g.diffuseIterations = 4;
        }
        s.fields.grids.push_back(g);
        return s;
    };

    for (const auto& [resolution, diffuse] : std::vector<std::pair<int, bool>>{{48, false}, {128, true}}) {
        const scene::Scene s = makeScene(resolution, diffuse);
        for (const double t : {4.0, 10.0, 60.0, 300.0}) {
            rendering::FieldUniforms fields(*ctx);
            rendering::Simulation sim(*ctx, shaders);
            REQUIRE(sim.init(fields.buffer(), fields.gridBuffer()).has_value());
            fields.update(s.fields, t);
            // One untimed frame at t = 0 so pipeline creation and the initial upload are not billed.
            {
                FrameTime z{};
                wgpu::CommandEncoder e = ctx->device().CreateCommandEncoder();
                sim.update(e, s, z);
                wgpu::CommandBuffer c = e.Finish();
                ctx->queue().Submit(1, &c);
                ctx->waitForQueue();
            }
            sim.markDiscontinuity();
            FrameTime time{};
            time.renderTime = t;
            time.deltaTime = 0.0;
            const auto start = std::chrono::steady_clock::now();
            wgpu::CommandEncoder e = ctx->device().CreateCommandEncoder();
            sim.update(e, s, time);
            wgpu::CommandBuffer c = e.Finish();
            ctx->queue().Submit(1, &c);
            ctx->waitForQueue();
            const double ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            const std::uint64_t floats = s.fields.grids[0].floatCount();
            WARN(fmt::format("grid {}^3{}: seek to {:.0f} s replays {} sub-steps in {:.1f} ms ({:.3f} ms/step); "
                             "state {:.2f} MB",
                             resolution, diffuse ? " +diffusion" : "", t, sim.stats().catchUpSteps, ms,
                             ms / std::max<double>(1.0, static_cast<double>(sim.stats().catchUpSteps)),
                             static_cast<double>(floats) * 4.0 / 1048576.0));
        }
    }
}
