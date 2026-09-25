// A proposed plan's preview stills (spec §55, ADR-764): rendered from a scratch copy with the
// proposal installed, one per proposed shot at its middle, and the person's project untouched.

#include "app/directing_context.hpp"
#include "app/director_stills.hpp"
#include "app/engine.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "directing/compiler.hpp"
#include "directing/plan.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/composition.hpp"
#include "support/project_round_trip.hpp"

#include "support/project_assets.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>
#include <algorithm>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

double meanAbsDifference(const gpu::Image8& a, const gpu::Image8& b) {
    REQUIRE(a.rgba.size() == b.rgba.size());
    double sum = 0.0;
    for (std::size_t i = 0; i < a.rgba.size(); i += 4) {
        for (int c = 0; c < 3; ++c) {
            sum += std::abs(static_cast<int>(a.rgba[i + c]) - static_cast<int>(b.rgba[i + c]));
        }
    }
    return sum / static_cast<double>((a.rgba.size() / 4) * 3);
}

} // namespace

TEST_CASE("a proposal's stills come from a scratch copy with it installed, and leave the project alone",
          "[directing][stills]") {
    testsupport::skipUnlessGlowmereBenchmarkAssetsPresent();
    log::init(log::Level::Warn);
    auto ctx = gpu::Context::create(gpu::ContextDesc{});
    if (!ctx) {
        SKIP("no GPU adapter available");
    }
    gpu::ShaderLibrary shaders(**ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});

    app::Engine live(app::EngineMode::Offline);
    REQUIRE(live.loadProject(fs::path(AVGEN_SOURCE_DIR) / "examples/world/glowmere-valley-2-multicam.json"));
    live.update(FrameTime{0.0, 0.0, 0});
    const nlohmann::json golden =
        testsupport::readJson(fs::path(AVGEN_SOURCE_DIR) / "tests/data/directing/golden/rook_run_past_umbra.json");
    directing::PlanParse parsed = directing::parsePlan(golden.at("plan"));
    REQUIRE(parsed.plan);
    const directing::Compilation c = directing::compilePlan(*parsed.plan, app::sceneFactsFor(live));
    REQUIRE(app::proposedShots(c).size() == 1);

    const std::string sequenceBefore = live.sequence().toJson().dump();
    const std::string camerasBefore = live.composition()->cameraDirection().toJson().dump();
    const bool dirtyBefore = live.projectDirty();
    const fs::path project = live.projectPath();

    constexpr std::uint32_t kW = 256;
    constexpr std::uint32_t kH = 144;
    auto report = app::renderShotStills(**ctx, shaders, live, c, kW, kH, fs::temp_directory_path());
    REQUIRE(report.has_value());
    REQUIRE(report->stills.size() == 1);
    const app::ShotStill& still = report->stills[0];
    CHECK(still.item == "run-shot");
    CHECK(still.shot == "rook-runs-past");
    CHECK(still.seconds == 92.5); // 1:30 + 5 s / 2
    CHECK(still.image.width == kW);
    CHECK(still.image.height == kH);

    // The person's project: not a byte of it changed, and it was not saved.
    CHECK(live.sequence().toJson().dump() == sequenceBefore);
    CHECK(live.composition()->cameraDirection().toJson().dump() == camerasBefore);
    CHECK(live.directingPlans().empty());
    CHECK(live.projectDirty() == dirtyBefore);
    CHECK(live.projectPath() == project);

    // It is the proposal's shot: the same instant of the project WITHOUT the proposal (whatever
    // camera the project had there) looks different -- the chase camera on Rook is not there.
    live.setViewport(kW, kH);
    live.seekSeconds(still.seconds);
    const FrameTime t{still.seconds, 1.0 / 60.0, 1};
    live.update(t);
    rendering::SceneRenderer renderer(**ctx, shaders);
    REQUIRE(renderer.init().has_value());
    auto without = renderer.renderToImage(live.composition()->scene(), t, kW, kH);
    REQUIRE(without.has_value());
    const double difference = meanAbsDifference(still.image, *without);
    INFO("mean absolute difference from the project without the proposal: " << difference);
    CHECK(difference > 8.0);
    INFO("load " << report->loadMs << " ms, render " << report->renderMs << " ms");
    SUCCEED();
}

TEST_CASE("the stills session keeps the editor's frames short, reuses its scratch copy, and rebuilds it on a new key",
          "[directing][stills]") {
    testsupport::skipUnlessGlowmereBenchmarkAssetsPresent();
    log::init(log::Level::Warn);
    auto ctx = gpu::Context::create(gpu::ContextDesc{});
    if (!ctx) {
        SKIP("no GPU adapter available");
    }
    gpu::ShaderLibrary shaders(**ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    app::Engine live(app::EngineMode::Offline);
    REQUIRE(live.loadProject(fs::path(AVGEN_SOURCE_DIR) / "examples/world/glowmere-valley-2-multicam.json"));
    live.update(FrameTime{0.0, 0.0, 0});
    const nlohmann::json golden =
        testsupport::readJson(fs::path(AVGEN_SOURCE_DIR) / "tests/data/directing/golden/rook_run_past_umbra.json");
    directing::PlanParse parsed = directing::parsePlan(golden.at("plan"));
    REQUIRE(parsed.plan);
    const directing::Compilation c = directing::compilePlan(*parsed.plan, app::sceneFactsFor(live));

    app::StillsSession session(**ctx, shaders, fs::temp_directory_path());
    // Runs a request to the end the way the editor does: one `step` per frame. Returns the longest
    // single frame's worth of main-thread work, which is what the person feels.
    const auto run = [&](const std::string& key, std::vector<app::ShotStill>& out) {
        const auto t0 = std::chrono::steady_clock::now();
        REQUIRE(session.request(live, key, "task", c, 256, 144));
        double longest = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        CHECK_FALSE(session.request(live, key, "task", c, 256, 144)); // one at a time
        for (int frame = 0; frame < 100000; ++frame) {
            const auto f0 = std::chrono::steady_clock::now();
            const app::StillsSession::Progress p = session.step();
            longest = std::max(longest, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - f0).count());
            for (auto& s : session.takeNew()) {
                out.push_back(std::move(s));
            }
            REQUIRE_FALSE(p.failed);
            if (!p.busy) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        return longest;
    };

    std::vector<app::ShotStill> first;
    const double firstLongest = run("project@1", first);
    REQUIRE(first.size() == 1);
    CHECK(session.loads() == 1);
    const double firstWall = session.wallMs();
    INFO("first: wall " << firstWall << " ms, main thread " << session.mainThreadMs() << " ms, longest frame "
                        << firstLongest << " ms");

    std::vector<app::ShotStill> again;
    const double againLongest = run("project@1", again); // same key: the session is reused
    REQUIRE(again.size() == 1);
    CHECK(session.loads() == 1);
    const double againWall = session.wallMs();
    INFO("again: wall " << againWall << " ms, main thread " << session.mainThreadMs() << " ms, longest frame "
                        << againLongest << " ms");
    CHECK(againWall < firstWall);

    std::vector<app::ShotStill> edited;
    (void)run("project@2", edited); // an edit moved the key: rebuilt
    CHECK(session.loads() == 2);
    REQUIRE(edited.size() == 1);

    // The same shot the synchronous path renders (the session is the same work, spread out). Not
    // bit-identical: a reused session's renderer and particles carry history from its last frame
    // (2.6 of 255 measured) -- which is an order of magnitude below the 31 that separates the
    // proposal's shot from the project without it, the difference that matters here.
    auto sync = app::renderShotStills(**ctx, shaders, live, c, 256, 144, fs::temp_directory_path());
    REQUIRE(sync.has_value());
    CHECK(meanAbsDifference(first[0].image, sync->stills[0].image) < 6.0);
    CHECK(meanAbsDifference(again[0].image, sync->stills[0].image) < 6.0);
    // Nothing the editor does per frame waits on the simulation: the scratch copy is written on the
    // main thread, and the first frame rendered uploads the scratch scene's textures; after that each
    // frame is one small render.
    CHECK(againLongest < 250.0);
    std::printf("stills session: first %.0f ms wall (longest editor frame %.0f ms), reused %.0f ms wall "
                "(longest editor frame %.0f ms)\n", firstWall, firstLongest, againWall, againLongest);
}
