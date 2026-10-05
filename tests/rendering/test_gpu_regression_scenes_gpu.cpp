// The permanent GPU architectural regression scenes (examples/gpu-regression/: Echo Field, Endless
// Meadow, Mycelium), on the production systems, through the product path (app::Engine + SceneRenderer).
//
// Default run ([gpu][gpu-regression]): each scene loads, hears audio, renders a non-empty frame with
// no GPU error, and its systems do what the scene says (a million-cell generator window, a window that
// does not grow with distance, a million-agent grid); Mycelium's seek lands on the played state byte
// for byte -- grid AND frame -- through the engine, which is where the seek rule is kept.
//
// Benchmark ([.perf][gpu-regression], hidden; run alone, under tools/gpu-lock.sh, on a quiet machine):
// 1920x1080 at the Realtime tier, 30 warm-up frames then 120 measured; prints CPU encode, GPU frame,
// population, bytes and seek costs, and CHECKs them against the expected ranges written in
// docs/research/gpu-world-productionization.md ("Regression benchmarks"). A range is wide on purpose:
// it is there to catch a step change (a system that stops being O(window), a readback in the loop, a
// checkpoint that stops restoring), not to rank a quiet afternoon against a busy one.
//
// Audio is a synthetic groove (tests/support/groove.hpp) written to a temporary WAV, so these tests
// need none of the gitignored assets.
#include "app/engine.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/render_quality.hpp"
#include "rendering/scene_renderer.hpp"
#include "rendering/simulation.hpp"
#include "scene/generator.hpp"
#include "support/groove.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

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

fs::path scenePath(const char* name) {
    return fs::path(AVGEN_SOURCE_DIR) / "examples" / "gpu-regression" / name;
}

// A 64 s groove (120 bpm, kicks on every beat), written once per process.
const fs::path& grooveWav() {
    static const fs::path path = [] {
        testsupport::GrooveSpec spec;
        spec.bars = 32;
        const testsupport::Groove groove = testsupport::makeGroove(spec);
        const fs::path p = fs::temp_directory_path() / "avgen-gpu-regression-groove.wav";
        REQUIRE(groove.file.writeWav(p).has_value());
        return p;
    }();
    return path;
}

struct Rig {
    Rig(gpu::Context& ctx, gpu::ShaderLibrary& shaders, const fs::path& scene, std::uint32_t w, std::uint32_t h,
        rendering::QualityTier tier, double fps)
        : engine(app::EngineMode::Offline), renderer(ctx, shaders), clock(fps), width(w), height(h) {
        REQUIRE(engine.loadComposition(scene).has_value());
        REQUIRE(engine.loadAudio(grooveWav()).has_value());
        // The composition's four default routes move the whole world with the music: scale, an
        // integrated SPIN (a function of the frame history, not of t), brightness and an impulse. The
        // regression projects zero them (examples/gpu-regression/*.json); a bare composition gets them,
        // so they go here too. What is left to compare is what the GPU systems decide.
        engine.modulator().clearRoutes();
        REQUIRE(renderer.init().has_value());
        renderer.setQuality(tier);
    }
    void restartAt(double seconds) {
        clock.restartAt(seconds);
        engine.seekSeconds(seconds);
        renderer.resetTemporalHistory();
    }
    FrameTime advance() {
        const FrameTime t = engine.tick(clock);
        engine.setViewport(width, height);
        engine.update(t);
        return t;
    }
    void draw() {
        const FrameTime t = advance();
        REQUIRE(renderer.renderFrame(engine.scene(), t, width, height).has_value());
    }
    gpu::Image8 capture() {
        const FrameTime t = advance();
        auto image = renderer.renderToImage(engine.scene(), t, width, height);
        REQUIRE(image.has_value());
        return std::move(*image);
    }
    app::Engine engine;
    rendering::SceneRenderer renderer;
    FixedStepClock clock;
    std::uint32_t width;
    std::uint32_t height;
};

double meanLuma(const gpu::Image8& image) {
    double sum = 0.0;
    for (std::size_t i = 0; i + 3 < image.rgba.size(); i += 4) {
        sum += 0.2126 * image.rgba[i] + 0.7152 * image.rgba[i + 1] + 0.0722 * image.rgba[i + 2];
    }
    return sum / static_cast<double>(image.rgba.size() / 4);
}

const scene::ProceduralGeometry* procedural(const scene::Scene& s, const std::string& name) {
    for (const auto& p : s.procedurals) {
        if (p.name == name) {
            return &p;
        }
    }
    return nullptr;
}

constexpr std::uint32_t kW = 320;
constexpr std::uint32_t kH = 180;

} // namespace

TEST_CASE("Echo Field renders a million-cell generated disc that hears the song", "[gpu][gpu-regression]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    Rig rig(*ctx, shaders, scenePath("echo-field.scene.json"), kW, kH, rendering::QualityTier::Realtime, 30.0);
    rig.restartAt(12.0);
    rig.draw();
    const gpu::Image8 frame = rig.capture();
    const auto& stats = rig.renderer.stats();
    INFO("luma " << meanLuma(frame) << ", generator cells " << stats.procedural.generatorCells);
    CHECK(meanLuma(frame) > 2.0);
    CHECK(stats.procedural.generatorObjects == 1);
    // The window: +/-115 m about the camera (0, 22, 72), clamped to the disc's square: 1000 x 696 cells.
    CHECK(stats.procedural.generatorCells == 696000);
    CHECK(stats.fieldAudio.bound);
    const scene::ProceduralGeometry* reeds = procedural(rig.engine.scene(), "reeds");
    REQUIRE(reeds != nullptr);
    CHECK(reeds->instances.empty()); // no CPU records exist
    auto counts = rig.renderer.procedurals().readCullCounts("reeds");
    REQUIRE(counts.has_value());
    CHECK(counts->visible > 150000);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("Endless Meadow's windows do not grow with distance", "[gpu][gpu-regression]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    Rig rig(*ctx, shaders, scenePath("endless-meadow.scene.json"), kW, kH, rendering::QualityTier::Realtime, 30.0);
    rig.restartAt(20.0);
    std::uint64_t cells = 0;
    for (const float far : {0.0f, 2000.0f, 50000.0f}) {
        const FrameTime t = rig.advance();
        scene::Scene s = rig.engine.scene();
        s.camera.position = {far, 6.0f, -far * 0.3f};
        s.camera.target = s.camera.position + glm::vec3(0.0f, -0.15f, -1.0f);
        auto image = rig.renderer.renderToImage(s, t, kW, kH);
        REQUIRE(image.has_value());
        const auto& stats = rig.renderer.stats();
        if (cells == 0) {
            cells = stats.procedural.generatorCells;
        }
        INFO("at " << far << " m: cells " << stats.procedural.generatorCells << ", luma " << meanLuma(*image));
        CHECK(stats.procedural.generatorObjects == 4);
        CHECK(stats.procedural.generatorCells == cells);
        CHECK(meanLuma(*image) > 1.0);
        auto grass = rig.renderer.procedurals().readCullCounts("grass");
        REQUIRE(grass.has_value());
        CHECK(grass->visible > 1000);
    }
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("Mycelium seeks to the played state, grid and frame, through the engine", "[gpu][gpu-regression][seek]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    const double t = 6.0;
    // Played from 0 to 6 s at 30 fps (two 60 Hz steps a frame).
    Rig played(*ctx, shaders, scenePath("mycelium.scene.json"), kW, kH, rendering::QualityTier::Offline, 30.0);
    played.restartAt(0.0);
    for (int f = 0; f < static_cast<int>(t * 30.0); ++f) {
        played.draw();
    }
    const gpu::Image8 playedFrame = played.capture();
    auto playedGrid = played.renderer.simulation().readGrid(played.engine.scene().fields, 0);
    REQUIRE(playedGrid.has_value());
    CHECK(played.renderer.stats().simulation.agents == 1000000);
    CHECK(played.renderer.stats().simulation.checkpoints == 1); // at 5 s
    // A fresh engine and renderer, straight to 6 s: replays 360 steps from 0.
    Rig fresh(*ctx, shaders, scenePath("mycelium.scene.json"), kW, kH, rendering::QualityTier::Offline, 30.0);
    fresh.restartAt(t);
    const gpu::Image8 freshFrame = fresh.capture();
    auto freshGrid = fresh.renderer.simulation().readGrid(fresh.engine.scene().fields, 0);
    REQUIRE(freshGrid.has_value());
    double trail = 0.0;
    for (std::size_t i = 3; i < playedGrid->size(); i += 4) {
        trail += static_cast<double>((*playedGrid)[i]);
    }
    INFO("trail sum " << trail << ", played luma " << meanLuma(playedFrame));
    CHECK(trail > 100.0);
    CHECK(std::memcmp(freshGrid->data(), playedGrid->data(), playedGrid->size() * sizeof(float)) == 0);
    {
        std::size_t over = 0;
        int worst = 0;
        double sum = 0.0;
        for (std::size_t i = 0; i < freshFrame.rgba.size(); ++i) {
            const int d = std::abs(int(freshFrame.rgba[i]) - int(playedFrame.rgba[i]));
            worst = std::max(worst, d);
            over += d > 2 ? 1u : 0u;
            sum += d;
        }
        // The simulation is byte-identical (above). The frame is held to the project's "same frame" rule
        // (ADR-1114's seek test: <= 0.1% of channels more than 2 levels off, mean <= 0.25): measured,
        // 36 of 230,400 channels differ by ONE level between a continuous play and a fresh seek -- renderer
        // frame history, not GPU-system state (the scrub below, restored from a checkpoint, matches a
        // fresh seek byte for byte). Unexplained; recorded in the productionization doc.
        INFO("fresh vs played frame: worst " << worst << ", >2 levels " << over << " of " << freshFrame.rgba.size()
                                             << ", mean " << sum / static_cast<double>(freshFrame.rgba.size()));
        CHECK(static_cast<double>(over) <= 0.001 * static_cast<double>(freshFrame.rgba.size()));
        CHECK(sum / static_cast<double>(freshFrame.rgba.size()) <= 0.25);
    }
    // The played one scrubs back to 5.5 s: restores the 5 s checkpoint (30 steps), and that frame equals
    // a fresh seek's.
    played.restartAt(5.5);
    const gpu::Image8 scrubbed = played.capture();
    CHECK(played.renderer.stats().simulation.restores == 1);
    Rig second(*ctx, shaders, scenePath("mycelium.scene.json"), kW, kH, rendering::QualityTier::Offline, 30.0);
    second.restartAt(5.5);
    CHECK(gpu::hashImage(second.capture()) == gpu::hashImage(scrubbed));
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("The GPU regression scenes stay inside their expected ranges", "[.perf][gpu-regression]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    struct Range {
        const char* scene;
        double cpuMaxMs;      // CPU encode + update per frame (median)
        double gpuMinMs, gpuMaxMs;
        double memMaxMb;      // generator + simulation bytes held
        double popMin;        // generator cells or agents
        double seekMaxMs;     // seek to 60 s and draw one frame
    };
    const Range ranges[] = {
        {"echo-field.scene.json", 4.0, 0.5, 40.0, 140.0, 1.0e6, 2000.0},
        {"endless-meadow.scene.json", 4.0, 0.5, 40.0, 140.0, 5.0e4, 2000.0},
        {"mycelium.scene.json", 6.0, 0.5, 40.0, 900.0, 1.0e6, 15000.0},
    };
    for (const Range& r : ranges) {
        Rig rig(*ctx, shaders, scenePath(r.scene), 1920, 1080, rendering::QualityTier::Realtime, 60.0);
        rig.restartAt(20.0);
        for (int f = 0; f < 30; ++f) {
            rig.draw();
        }
        std::vector<double> cpu;
        std::vector<double> gpuMs;
        for (int f = 0; f < 120; ++f) {
            const auto start = std::chrono::steady_clock::now();
            const FrameTime t = rig.advance();
            REQUIRE(rig.renderer.renderFrame(rig.engine.scene(), t, 1920, 1080).has_value());
            cpu.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
            if (rig.renderer.stats().gpuFrameMs > 0.0) {
                gpuMs.push_back(rig.renderer.stats().gpuFrameMs);
            }
        }
        std::sort(cpu.begin(), cpu.end());
        std::sort(gpuMs.begin(), gpuMs.end());
        const double cpuMs = cpu[cpu.size() / 2];
        const double gpuMedian = gpuMs.empty() ? -1.0 : gpuMs[gpuMs.size() / 2];
        const auto& s = rig.renderer.stats();
        const double memMb = static_cast<double>(s.procedural.generatorBytes + s.simulation.stateBytes +
                                                 s.simulation.checkpointBytes + s.fieldAudio.ringBytes) /
                             (1024.0 * 1024.0);
        const double population =
            std::max(static_cast<double>(s.procedural.generatorCells), static_cast<double>(s.simulation.agents));
        // Seek: to 60 s, one frame drawn and waited for.
        const auto seekStart = std::chrono::steady_clock::now();
        rig.restartAt(60.0);
        rig.capture();
        const double seekMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - seekStart).count();
        const auto restores = s.simulation.restores;
        // And back to 58 s, which a stateful scene restores from its 55 s checkpoint.
        const auto backStart = std::chrono::steady_clock::now();
        rig.restartAt(58.0);
        rig.capture();
        const double backMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - backStart).count();
        WARN(fmt::format("{}: CPU {:.2f} ms, GPU {:.2f} ms ({} samples), generator {:.0f} cells / {:.1f} MB, sim "
                         "{} agents / state {:.1f} MB / {} checkpoints {:.1f} MB, audio ring {:.2f} MB; held {:.1f} MB; "
                         "seek 20->60 s {:.0f} ms (restores {}), back 60->58 s {:.0f} ms (restores {})",
                         r.scene, cpuMs, gpuMedian, gpuMs.size(), static_cast<double>(s.procedural.generatorCells),
                         s.procedural.generatorBytes / 1048576.0, s.simulation.agents,
                         s.simulation.stateBytes / 1048576.0, s.simulation.checkpoints,
                         s.simulation.checkpointBytes / 1048576.0, s.fieldAudio.ringBytes / 1048576.0, memMb, seekMs,
                         restores, backMs, rig.renderer.stats().simulation.restores));
        CHECK(cpuMs <= r.cpuMaxMs);
        if (gpuMedian > 0.0) {
            CHECK(gpuMedian >= r.gpuMinMs);
            CHECK(gpuMedian <= r.gpuMaxMs);
        }
        CHECK(memMb <= r.memMaxMb);
        CHECK(population >= r.popMin);
        CHECK(seekMs <= r.seekMaxMs);
        CHECK(backMs <= r.seekMaxMs);
    }
    CHECK(ctx->errorCount() == 0);
}


