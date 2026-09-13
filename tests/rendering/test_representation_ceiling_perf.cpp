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
    // Every culling procedural's authored minScreenRadius, captured once. An arm **assigns** from
    // this rather than raising the live value, because `Composition::update` does not rewrite
    // `lod.minScreenRadius` each frame and a `std::max` therefore never comes back down.
    //
    // The first version did raise it, and the leak is on the record: after the 200 px arm, runs 1
    // and 2 reported 99,820 triangles for **every** arm including the baseline -- eight
    // measurements of the 200 px arm wearing four different labels. This is the failure the
    // measurement protocol names: a probe that changes state must re-establish it before each
    // measurement.
    std::vector<float> authoredMinRadius;
    authoredMinRadius.reserve(composition->scene().procedurals.size());
    for (const scene::ProceduralGeometry& p : composition->scene().procedurals) {
        authoredMinRadius.push_back(p.lod.minScreenRadius);
    }

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
            {
                auto& procs = composition->scene().procedurals;
                for (std::size_t pi = 0; pi < procs.size(); ++pi) {
                    scene::ProceduralGeometry& p = procs[pi];
                    // Only objects whose author already turned culling on. The first version of
                    // this arm set `lod.cull = true` on every procedural, which switched culling on
                    // for hand-placed hero geometry that never had it -- and deleted the elder
                    // mushroom's cap, 330 px wide in the baseline capture, from the 40 px arm. That
                    // is not "the C6 band removed"; it is a different frame, and its 20.9% was
                    // measuring the hero going missing as much as the ecology.
                    //
                    // Restricting the arm to `lod.cull` also makes it the right population: the
                    // ecology scatter is what C5 and C6 exist for, and it is exactly the set the
                    // world builder enables culling on.
                    if (!p.lod.cull) {
                        continue;
                    }
                    const float authored =
                        pi < authoredMinRadius.size() ? authoredMinRadius[pi] : p.lod.minScreenRadius;
                    p.lod.minScreenRadius = std::max(authored, arm.minScreenRadius);
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
                "vs base", "triangles");
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
                    static_cast<unsigned long long>(results[a].back().triangles));
    }
    std::printf("\n  the A/B floor is %.0f%% GPU (§3.3). A delta smaller than that is not a result.\n",
                rendering::kGpuNoiseFloorPercent);
    std::printf("  frames written to %s -- §50 requires looking at them, not only at the timer.\n\n",
                outDir.string().c_str());
    std::fflush(stdout);

    // ---- what the instrument must prove about itself --------------------------------------------
    //
    // A null delta is only evidence if the arm really removed the geometry, so this asserts that it
    // did. It says nothing about how much time was saved, because that is the measurement.
    //
    // It asserts on **triangles**, not on `visibleInstances`. An earlier version asserted on the
    // instance count and failed with the baseline at 1 against an arm at 71 -- backwards. That was
    // first blamed on the counter being read back late (ADR-077); it was not. It was the state leak
    // below, and once the leak was fixed the instance counts came out in the right order. The
    // attribution is recorded because a wrong one that is never corrected is how a healthy counter
    // acquires a reputation. Triangles remain the guard regardless: they are exact, CPU-side, and
    // not a GPU readback at all.
    // The baseline must submit the same geometry every time it is measured. This is the assertion
    // the state leak above would have failed loudly instead of silently: three identical labels
    // reporting three different triangle counts is a probe contaminating itself, and no delta
    // measured beside it means anything.
    REQUIRE(results[0].back().triangles > 0);
    for (std::size_t run = 1; run < results[0].size(); ++run) {
        INFO("the baseline arm must be re-established before every run");
        CHECK(results[0][run].triangles == results[0][0].triangles);
    }
    for (std::size_t a = 1; a < kArms.size(); ++a) {
        INFO("arm " << kArms[a].name << " must submit strictly fewer triangles than the baseline");
        CHECK(results[a].back().triangles < results[0].back().triangles);
        // Each band is a subset of the next, so the counts must be monotone. If they are not, the
        // arms are not nested and their deltas are not comparable with each other.
        CHECK(results[a].back().triangles <= results[a - 1].back().triangles);
    }
}
