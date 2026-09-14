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
#include <vector>

#include <glm/trigonometric.hpp>

using namespace avgen;
namespace fs = std::filesystem;

namespace {
constexpr std::uint32_t kWidth = 1280;
constexpr std::uint32_t kHeight = 800; // pinned to match the original's baseline exactly
constexpr int kWarmup = 12;
constexpr int kMeasured = 30;
constexpr int kRuns = 3;

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

    const auto measure = [&](const View& v) {
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
            engine.composition()->scene().camera.farPlane = 1400.0f;
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
    std::array<std::vector<Sample>, views.size()> results;
    for (int run = 0; run < kRuns; ++run) {
        for (std::size_t i = 0; i < views.size(); ++i) {
            const Sample s = measure(views[i]);
            results[i].push_back(s);
            std::printf("  run %d  %-14s frame %7.3f ms  scene %7.3f ms  tris %8llu  vis %6llu  draws %4u\n",
                        run, views[i].name, s.frameMs, s.sceneMs,
                        static_cast<unsigned long long>(s.triangles),
                        static_cast<unsigned long long>(s.visible), s.draws);
            std::fflush(stdout);
        }
    }
    std::printf("\n  %-14s %10s %10s %10s\n", "view", "frame ms", "scene ms", "triangles");
    for (std::size_t i = 0; i < views.size(); ++i) {
        std::vector<double> f, sc;
        for (const Sample& s : results[i]) {
            f.push_back(s.frameMs);
            sc.push_back(s.sceneMs);
        }
        std::printf("  %-14s %10.3f %10.3f %10llu\n", views[i].name, median(std::move(f)),
                    median(std::move(sc)),
                    static_cast<unsigned long long>(results[i].back().triangles));
    }
    std::printf("\n  the ceiling is 16 ms and the target 13-14; the original Glowmere is 15.93 ms median\n");
    std::fflush(stdout);
}
