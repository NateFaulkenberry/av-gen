// The renderer's side of the Director's costs (spec §37; the CPU side is
// tests/unit/test_directing_perf.cpp).
//
// Hidden behind `[.perf]`; run it on purpose, under the GPU lock:
//
//   tools/gpu-lock.sh ./build/release/tests/avgen_render_tests "[.perf][directing]"
//
// `SceneRenderer::uploadTextures` is the cost the feasibility report named (1,125 ms of starring a
// hero, before the digest guard). It is paid only when `textureVersion` moves. So this measures two
// things:
//   - What a full re-upload costs on the benchmark scene today. The version is bumped by hand, and
//     the next frame pays it.
//   - What the frame after applying each golden plan uploads. It should upload nothing.
//
// The probe (`core/phase2_probe.hpp`) is the investigation's own instrument: it counts textures
// re-created and the time spent inside `uploadTextures`. Minima over repeats.
//
// This binary does not link the edit history, so a plan is installed with `installCompilation` (what
// `applyCompilation` wraps in its capture) and taken out by re-installing the starting content.

#include "app/directing_context.hpp"
#include "app/engine.hpp"
#include "core/log.hpp"
#include "core/phase2_probe.hpp"
#include "directing/compiler.hpp"
#include "directing/plan.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/composition.hpp"
#include "support/project_round_trip.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <limits>
#include <vector>

using namespace avgen;
using namespace avgen::directing;
namespace fs = std::filesystem;

TEST_CASE("the renderer's cost of the Director's golden plans", "[.perf][directing]") {
    log::init(log::Level::Warn);
    auto ctx = gpu::Context::create(gpu::ContextDesc{});
    if (!ctx) {
        SKIP("no GPU adapter available");
    }
    gpu::ShaderLibrary shaders(**ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(**ctx, shaders);
    REQUIRE(renderer.init().has_value());

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(fs::path(AVGEN_SOURCE_DIR) / "examples/world/glowmere-valley-2-multicam.json"));
    constexpr std::uint32_t kWidth = 640;
    constexpr std::uint32_t kHeight = 360;
    engine.setViewport(kWidth, kHeight);
    std::uint64_t frame = 0;
    // One frame: update, render, and what the render uploaded.
    struct Uploaded {
        std::uint64_t textures = 0;
        double ms = 0.0;
        double frameMs = 0.0;
    };
    const auto render = [&] {
        const FrameTime t{static_cast<double>(frame) / 60.0, frame == 0 ? 0.0 : 1.0 / 60.0, frame};
        ++frame;
        engine.update(t);
        probe2::frame().clear();
        const auto start = std::chrono::steady_clock::now();
        REQUIRE(renderer.renderToImage(engine.composition()->scene(), t, kWidth, kHeight).has_value());
        return Uploaded{probe2::frame().texturesUploaded, probe2::frame().textureUploadMs,
                        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count()};
    };
    for (int i = 0; i < 3; ++i) {
        (void)render(); // the first frame uploads everything; the warm frames after it upload nothing
    }
    scene::Composition* comp = engine.composition();
    REQUIRE(comp != nullptr);

    double fullMs = std::numeric_limits<double>::infinity();
    double fullFrameMs = std::numeric_limits<double>::infinity();
    std::uint64_t fullCount = 0;
    double steadyFrameMs = std::numeric_limits<double>::infinity();
    for (int r = 0; r < 3; ++r) {
        steadyFrameMs = std::min(steadyFrameMs, render().frameMs);
        ++comp->scene().textureVersion; // what a rebuild whose textures changed does
        const Uploaded u = render();
        fullMs = std::min(fullMs, u.ms);
        fullFrameMs = std::min(fullFrameMs, u.frameMs);
        fullCount = u.textures;
    }
    CHECK(fullCount > 0); // the bump did reach the renderer
    fmt::print("\nRenderer costs on Glowmere Valley 2 multicam at {}x{}, ms, minimum of 3\n", kWidth, kHeight);
    fmt::print("  steady frame {:.2f}\n", steadyFrameMs);
    fmt::print("  full texture re-upload: {} textures, uploadTextures {:.1f}, whole frame {:.1f}\n\n", fullCount,
               fullMs, fullFrameMs);

    const Staging start = app::sceneFactsFor(engine).staged;
    const fs::path dir = fs::path(AVGEN_SOURCE_DIR) / "tests/data/directing/golden";
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() == ".json") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    for (const fs::path& file : files) {
        INFO("golden: " << file.stem().string());
        PlanParse parsed = parsePlan(testsupport::readJson(file).at("plan"));
        REQUIRE(parsed.plan);
        const Compilation c = compilePlan(*parsed.plan, app::sceneFactsFor(engine));
        if (!c.changesAnything()) {
            fmt::print("  {:<24} nothing to apply\n", file.stem().string());
            continue;
        }
        REQUIRE(app::installCompilation(engine, c));
        const Uploaded after = render();
        fmt::print("  {:<24} frame after apply {:>6.2f} ms; textures uploaded {} ({:.2f} ms)\n", file.stem().string(),
                   after.frameMs, after.textures, after.ms);
        CHECK(after.textures == 0);
        REQUIRE(engine.setCameraDirection(start.cameras));
        REQUIRE(engine.setSequence(start.sequence));
        REQUIRE(engine.setEffects(start.effects));
        engine.directingPlans().clear();
        (void)render();
    }
    fmt::print("\n");
}
