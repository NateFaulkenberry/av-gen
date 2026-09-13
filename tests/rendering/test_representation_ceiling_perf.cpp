// **The ceiling of Phase C's remaining work, measured on the real frame.**
//
//   tools/gpu-lock.sh ./build/release/tests/avgen_render_tests "[.perf][representation]" --success
//
// ---- the question, and why it needed a new instrument ------------------------------------------
//
// C5 (impostors, 2-8 px of projected radius) and C6 (HLOD proxies, 8-40 px) are worth building if
// the geometry in those bands costs enough to pay for a proxy system. Two prior measurements bracket
// the answer and neither settles it:
//
//   * ADR-126 estimates Glowmere's whole-frame fragment-cost excess at 1.52x, from the CPU, against
//     §4.5's synthetic curve. It is a shape, not milliseconds, and it says so.
//   * ADR-131 then measured that the cheap half of that curve **cannot be resolved** -- spreads to
//     120%, every interval overlapping. So a chunk of the 1.52x rests on a region the synthetic
//     instrument cannot see, and re-running the synthetic sweep with more arms was tried and failed.
//
// The way out is to stop asking the synthetic plane and ask **the frame we actually care about**,
// with a real intervention, in milliseconds. What this does is measure the *ceiling*: not what a
// proxy would save, but what deleting the geometry entirely saves. No representation system can
// beat deletion -- a proxy still draws something -- so if deletion is inside the noise floor then
// no implementation of C5 or C6 can be worth its own code.
//
// ---- the arms ----------------------------------------------------------------------------------
//
// The intervention is `LodSettings::minScreenRadius`, which is shipped, general, policy-exposed and
// exactly the right units: cull.wgsl culls an instance whose `radius / distance * projScale` is
// below it, and that is the same projected radius `RepresentationPolicy` bands on. Raising it to 8
// deletes precisely C5's band and below; to 40, C5's and C6's together.
//
//   baseline    the scene as authored (Glowmere's layers set ~1.5 px)
//   >= 8 px     everything C5 would ever have drawn, gone
//   >= 40 px    everything C5 and C6 would ever have drawn, gone
//   >= 200 px   a scale check, not a proposal
//
// The 200 px arm is load-bearing and is the reason this test can report a null result honestly.
// §4.5's first run was a clean, convincing null produced by rendering nothing; the inverse failure
// -- a convincing null produced by an arm that changed nothing -- is guarded here by asserting that
// each arm actually removed instances, and by an arm large enough that it *must* show a delta. If
// the 200 px arm is also flat, the instrument is broken and no conclusion may be drawn from it.
//
// ---- conditions ---------------------------------------------------------------------------------
//
// 1280x800 (the baseline's resolution, §3.1), one fixed timeline second so every arm renders
// geometrically the same frame, arms interleaved within one process session, three runs each
// (§3.1's A/B rule). A difference below 2% GPU is not a result.

#include "app/engine.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/frame_timeline.hpp"
#include "assets/image.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/render_stats.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/composition.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

constexpr std::uint32_t kWidth = 1280;
constexpr std::uint32_t kHeight = 800;
constexpr int kWarmup = 12;    // §3.1's warm-up
constexpr int kMeasured = 108; // §3.1's steady window
constexpr double kFixedSecond = 4.0;
constexpr int kRuns = 3;

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

double median(std::vector<double> v) {
    if (v.empty()) return -1.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

struct Arm {
    const char* name;
    float minScreenRadius; // 0 = leave the scene's own value alone
};

struct Sample {
    double frameMs = -1.0;
    double sceneMs = -1.0;
    std::uint64_t triangles = 0;
    std::uint64_t visibleInstances = 0;
    std::uint64_t culledInstances = 0;
    std::uint32_t drawCalls = 0;
};

} // namespace

TEST_CASE("The ceiling of an impostor and HLOD proxy system on Glowmere",
          "[.perf][representation]") {
    const fs::path sceneFile =
        fs::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-stylized.scene.json";
    if (!fs::is_regular_file(sceneFile)) {
        SKIP("the Glowmere scene is not present");
    }
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadComposition(sceneFile).has_value());
    scene::Composition* composition = engine.composition();
    REQUIRE(composition != nullptr);

    constexpr std::array<Arm, 4> kArms{{
        {"baseline (as authored)", 0.0f},
        {"cull below 8 px radius (C5's whole band)", 8.0f},
        {"cull below 40 px radius (C5 + C6)", 40.0f},
        {"cull below 200 px radius (scale check)", 200.0f},
    }};

    const fs::path outDir = fs::path(AVGEN_SOURCE_DIR) / "build" / "representation-ceiling";
    std::error_code ec;
    fs::create_directories(outDir, ec);

    // One arm, one run. Returns the medians over the steady window.
    const auto measure = [&](const Arm& arm, bool capture) {
        std::vector<double> frames;
        std::vector<double> scenes;
        Sample last;
        for (int i = 0; i < kWarmup + kMeasured; ++i) {
            FixedStepClock clock(60.0);
            clock.restartAt(kFixedSecond);
            const FrameTime time = engine.tick(clock);
            engine.setViewport(kWidth, kHeight);
            engine.update(time);
            // After update, because update is what rebuilds the scene each frame. This is a
            // per-frame uniform (LodSettings::structuralHash covers lodCount only), so raising it
            // costs no rebuild and changes nothing but which instances the cull pass keeps.
            if (arm.minScreenRadius > 0.0f) {
                for (scene::ProceduralGeometry& p : composition->scene().procedurals) {
                    p.lod.cull = true;
                    p.lod.minScreenRadius = std::max(p.lod.minScreenRadius, arm.minScreenRadius);
                }
            }
            auto image = renderer.renderToImage(engine.scene(), time, kWidth, kHeight);
            REQUIRE(image.has_value());
            const rendering::RenderStats& stats = renderer.stats();
            if (i == kWarmup + kMeasured - 1) {
                last.triangles = stats.triangles;
                last.visibleInstances = stats.visibleInstances;
                last.culledInstances = stats.culledInstances;
                last.drawCalls = stats.drawCalls;
                if (capture) {
                    std::string file(arm.name);
                    std::replace_if(file.begin(), file.end(),
                                    [](char c) { return !std::isalnum(static_cast<unsigned char>(c)); },
                                    '-');
                    (void)assets::writePng(outDir / (file + ".png"), image->width, image->height,
                                           image->rgba);
                }
            }
            if (i < kWarmup) continue;
            if (stats.gpuFrameMs >= 0.0) frames.push_back(stats.gpuFrameMs);
            for (const gpu::TimelineInterval& p :
                 rendering::sumByLabel(renderer.timeline().passes())) {
                if (p.label == "scene") {
                    scenes.push_back(p.ms);
                    break;
                }
            }
        }
        last.frameMs = median(std::move(frames));
        last.sceneMs = median(std::move(scenes));
        return last;
    };

    std::array<std::vector<Sample>, kArms.size()> results;
    std::printf("\n===== representation ceiling, Glowmere, %ux%u, fixed t=%.1fs =====\n", kWidth,
                kHeight, kFixedSecond);
    std::fflush(stdout);
    for (int run = 0; run < kRuns; ++run) {
        for (std::size_t a = 0; a < kArms.size(); ++a) {
            const Sample s = measure(kArms[a], run == 0);
            results[a].push_back(s);
            std::printf("  run %d  %-44s frame %7.3f ms  scene %7.3f ms  tris %8llu  inst %7llu\n",
                        run, kArms[a].name, s.frameMs, s.sceneMs,
                        static_cast<unsigned long long>(s.triangles),
                        static_cast<unsigned long long>(s.visibleInstances));
            std::fflush(stdout);
        }
    }

    std::printf("\n  %-44s %10s %10s %10s %10s %12s\n", "arm", "frame ms", "vs base", "scene ms",
                "vs base", "instances");
    std::array<double, kArms.size()> frameMedian{};
    std::array<double, kArms.size()> sceneMedian{};
    for (std::size_t a = 0; a < kArms.size(); ++a) {
        std::vector<double> f;
        std::vector<double> s;
        for (const Sample& x : results[a]) {
            f.push_back(x.frameMs);
            s.push_back(x.sceneMs);
        }
        frameMedian[a] = median(std::move(f));
        sceneMedian[a] = median(std::move(s));
        const double df = 100.0 * (frameMedian[a] / std::max(frameMedian[0], 1e-9) - 1.0);
        const double ds = 100.0 * (sceneMedian[a] / std::max(sceneMedian[0], 1e-9) - 1.0);
        std::printf("  %-44s %10.3f %9.1f%% %10.3f %9.1f%% %12llu\n", kArms[a].name, frameMedian[a],
                    df, sceneMedian[a], ds,
                    static_cast<unsigned long long>(results[a].back().visibleInstances));
    }
    std::printf("\n  the A/B floor is %.0f%% GPU (§3.3). A delta smaller than that is not a result.\n",
                rendering::kGpuNoiseFloorPercent);
    std::printf("  frames written to %s -- §50 requires looking at them, not only at the timer.\n\n",
                outDir.string().c_str());
    std::fflush(stdout);

    // ---- what the instrument must prove about itself --------------------------------------------
    // A null delta is only evidence if the arm really removed the geometry. These assert that it
    // did; they say nothing about how much time it saved, because that is the measurement.
    REQUIRE(results[0].back().visibleInstances > 0);
    for (std::size_t a = 1; a < kArms.size(); ++a) {
        INFO("arm " << kArms[a].name << " must draw strictly fewer instances than the baseline");
        CHECK(results[a].back().visibleInstances < results[0].back().visibleInstances);
    }
    // Each band is a proper subset of the next, so the instance counts must be monotone. If they
    // are not, the arms are not measuring nested bands and the deltas are not comparable.
    for (std::size_t a = 1; a < kArms.size(); ++a) {
        CHECK(results[a].back().visibleInstances <= results[a - 1].back().visibleInstances);
    }
}
