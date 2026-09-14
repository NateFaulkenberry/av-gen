// Glowmere Valley 2's frame cost, against the budget Phase 3 has to hold (16 ms ceiling, 13-14 ms
// target) and against the original Glowmere's own 15.93 ms median.
//
// The methodology is ADR-150's and the rules are the ones this repo bought with wrong conclusions:
// one GPU one run, the resolution pinned, warm-up discarded, medians over a steady window, three
// interleaved runs in one process session, and -- ADR-170 -- the device actually quiet, which the
// caller checks with `pgrep avgen` on both sides of the run because the lock does not cover a window
// a human opened.
//
// Two viewpoints, because this scene's cost is not one number: the opening shot is the authored
// composition and the valley axis is the widest thing the auto-director will ever ask for.

#include "app/engine.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/frame_timeline.hpp"
#include "params/parameter.hpp"
#include "params/parameter_set.hpp"
#include "rendering/render_stats.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/composition.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <cstdlib>
#include <string>
#include <vector>

#include <glm/trigonometric.hpp>

using namespace avgen;
namespace fs = std::filesystem;

namespace {
constexpr std::uint32_t kWidth = 1280;
constexpr std::uint32_t kHeight = 800; // pinned to match the original's baseline exactly
constexpr int kWarmup = 12;
constexpr int kMeasured = 30;
constexpr int kRuns = 4; // even, so the counterbalancing is exact rather than nearly

std::unique_ptr<gpu::Context> makeContext() {
    static bool logInit = false;
    if (!logInit) {
        log::init(log::Level::Warn);
        logInit = true;
    }
    auto ctx = gpu::Context::create(gpu::ContextDesc{});
    if (!ctx) {
        SKIP("no GPU adapter available");
    }
    return std::move(*ctx);
}

double median(std::vector<double> v) {
    if (v.empty()) return -1.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}
} // namespace

TEST_CASE("Glowmere Valley 2 frame cost", "[.perf][glowmere2]") {
    const fs::path sceneFile =
        fs::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-valley-2.scene.json";
    if (!fs::is_regular_file(sceneFile)) {
        SKIP("the Glowmere Valley 2 scene is not present");
    }
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadComposition(sceneFile).has_value());
    REQUIRE(engine.composition() != nullptr);

    struct View {
        const char* name;
        glm::vec3 eye;
        glm::vec3 target;
        float fov;
    };
    const std::array<View, 2> views{{
        {"opening", {-118.0f, 32.1f, -96.0f}, {-20.0f, 4.0f, 40.0f}, 40.0f},
        {"valley-axis", {-30.0f, 30.0f, -232.0f}, {10.0f, -4.0f, 200.0f}, 38.0f},
    }};

    struct Sample {
        double frameMs = -1.0;
        double sceneMs = -1.0;
        std::uint64_t triangles = 0;
        std::uint64_t visible = 0;
        std::uint32_t draws = 0;
    };

    // The arms are interleaved **inside one process**, which is the whole of ADR-150's protocol and
    // not a nicety. Measured on this machine on 2026-09-14: three identical runs of the identical
    // scene, triangle count byte-identical at 252,996, gave 10.945 / 11.272 / 13.697 ms -- a 25%
    // spread between invocations. A two-process A/B below about 3 ms is therefore not evidence, and
    // the first version of this arm ran the two halves as separate invocations and reported that
    // hiding the heroes made the frame *slower*.
    const auto measure = [&](const View& v, bool hideHeroes) {
        std::vector<double> frames;
        std::vector<double> scenes;
        Sample last;
        for (int i = 0; i < kWarmup + kMeasured; ++i) {
            FixedStepClock clock(60.0);
            clock.restartAt(6.0);
            const FrameTime time = engine.tick(clock);
            engine.setViewport(kWidth, kHeight);
            // Before update, through the parameters: update is what culls, and a camera written
            // afterwards measures a frame culled for the authored viewpoint rather than this one.
            const auto setVec = [&](const char* path, glm::vec3 val) {
                params::IParameter* p = engine.params().find(path);
                REQUIRE(p != nullptr);
                for (std::size_t k = 0; k < 3; ++k) {
                    p->setBaseComponent(k, val[static_cast<glm::length_t>(k)]);
                }
            };
            setVec("camera/position", v.eye);
            setVec("camera/target", v.target);
            if (params::IParameter* p = engine.params().find("camera/fov")) {
                p->setBaseComponent(0, v.fov);
            }
            engine.update(time);
            scene::Scene& sc = engine.composition()->scene();
            sc.camera.farPlane = 1400.0f;
            // The A/B that answers where the heroes' cost goes. Hiding them after `update` is safe
            // *for this question* -- it changes what is drawn, not what was culled -- and the thing
            // being measured is exactly the pixels they cover.
            if (hideHeroes) {
                for (scene::ProceduralGeometry& p : sc.procedurals) {
                    if (p.name.find("elder-2") != std::string::npos || p.name.find("lantern") != std::string::npos ||
                        p.name.find("spire") != std::string::npos || p.name.find("bloom") != std::string::npos ||
                        p.name.find("veil") != std::string::npos || p.name.find("umbra") != std::string::npos) {
                        p.visible = false;
                    }
                }
            }
            auto image = renderer.renderToImage(engine.composition()->scene(), time, kWidth, kHeight);
            REQUIRE(image.has_value());
            const rendering::RenderStats& stats = renderer.stats();
            if (i == kWarmup + kMeasured - 1) {
                last.triangles = stats.triangles;
                last.visible = stats.visibleInstances;
                last.draws = stats.drawCalls;
            }
            if (i < kWarmup) continue;
            if (stats.gpuFrameMs >= 0.0) frames.push_back(stats.gpuFrameMs);
            for (const gpu::TimelineInterval& p : rendering::sumByLabel(renderer.timeline().passes())) {
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

    std::printf("\n===== Glowmere Valley 2 frame cost, %ux%u, fixed t=6.0s =====\n", kWidth, kHeight);
    std::array<std::vector<Sample>, views.size() * 2> results;
    for (int run = 0; run < kRuns; ++run) {
        for (std::size_t i = 0; i < views.size(); ++i) {
            for (int slot = 0; slot < 2; ++slot) {
                // **Counterbalanced.** Co-locating the arms in one process is not enough: if the
                // machine drifts during a run, an arm that always goes second always pays for it.
                // The first version had "no heroes" second every time and reported that hiding six
                // mushrooms made the frame 2.3 ms *slower* -- the same impossible result as the
                // two-invocation version, from a bias rather than from noise. Alternating by run
                // parity makes each arm go first half the time.
                const int arm = (run % 2 == 0) ? slot : 1 - slot;
                const Sample s = measure(views[i], arm == 1);
                results[i * 2 + static_cast<std::size_t>(arm)].push_back(s);
                std::printf("  run %d  %-14s %-12s frame %7.3f ms  scene %7.3f ms  tris %8llu  draws %4u\n",
                            run, views[i].name, arm == 1 ? "no heroes" : "with heroes", s.frameMs,
                            s.sceneMs, static_cast<unsigned long long>(s.triangles), s.draws);
                std::fflush(stdout);
            }
        }
    }
    std::printf("\n  %-14s %-12s %10s %10s %10s\n", "view", "arm", "frame ms", "scene ms", "triangles");
    for (std::size_t i = 0; i < views.size(); ++i) {
        double withHeroes = 0.0;
        for (int arm = 0; arm < 2; ++arm) {
            const std::vector<Sample>& rows = results[i * 2 + static_cast<std::size_t>(arm)];
            std::vector<double> f, sc;
            for (const Sample& s : rows) {
                f.push_back(s.frameMs);
                sc.push_back(s.sceneMs);
            }
            const double m = median(std::move(f));
            if (arm == 0) withHeroes = m;
            std::printf("  %-14s %-12s %10.3f %10.3f %10llu\n", views[i].name,
                        arm == 1 ? "no heroes" : "with heroes", m, median(std::move(sc)),
                        static_cast<unsigned long long>(rows.back().triangles));
            if (arm == 1) {
                std::printf("  %-14s %-12s %10.3f  (the six heroes)\n", "", "delta", withHeroes - m);
            }
        }
    }
    std::printf("\n  the ceiling is 16 ms and the target 13-14; the original Glowmere is 15.93 ms median\n");
    std::fflush(stdout);
}
