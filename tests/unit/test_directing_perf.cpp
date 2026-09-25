// The Director's costs, measured on the golden plans (spec §37; director-system-progress.md Slice 5).
//
// Hidden behind `[.perf]`: a number in milliseconds is a fact about this machine on this day, so
// nothing here runs in the suite. Run it on purpose:
//
//   ./build/release/tests/avgen_tests "[.perf][directing]"
//
// What it measures, per golden plan, against the benchmark scene (Glowmere Valley 2 multicam):
//   facts     `app::sceneFactsFor`          -- what the Director reads before it plans
//   parse     `parsePlan`
//   compile   `compilePlan`                  -- validation and compilation together
//   apply     `app::applyCompilation`        -- install, fingerprint, record one undo
//   install   `Engine::setSequence`          -- `seq::install` alone, re-installing what apply put in
//   frame     the first `Engine::update` after apply, where any rebuild the apply caused is paid
//   undo      `EditHistory::undo`
// and once for the scene:
//   steady    an `Engine::update` with nothing changed
//   rebuild   an `Engine::update` after a forced `Composition::rebuild` (the same composition data
//             set again), which is what an apply would cost if it ever caused one
//
// The one thing asserted is a property, not a time: **applying a plan never re-flattens the
// composition and never asks the renderer to re-upload textures.** A rebuild on this scene costs
// hundreds of milliseconds and, before the digest guard, 1.1 s of `SceneRenderer::uploadTextures`
// (docs/investigations/ui-responsiveness.md). `meshVersion` is bumped by every rebuild and
// `textureVersion` by every rebuild whose textures changed, so both staying put across apply and
// the next frame is the evidence. The renderer's own upload cost is measured on the GPU binary
// (tests/rendering/test_directing_perf_gpu.cpp).
//
// Method, per the standing rule: minima over repeats, never means.
//
// And `EntityWorld::seek`, by `Engine::seekSeconds`, in its own case below (after ADR-800).

#include "app/directing_apply.hpp"
#include "app/directing_context.hpp"
#include "app/engine.hpp"
#include "core/phase2_probe.hpp"
#include "directing/compiler.hpp"
#include "directing/plan.hpp"
#include "scene/composition.hpp"
#include "support/project_round_trip.hpp"
#include "ui/edit_history.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

using namespace avgen;
using namespace avgen::directing;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

constexpr int kRepeats = 3;

template <typename F>
double millis(F&& f) {
    const auto start = std::chrono::steady_clock::now();
    f();
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

struct Minimum {
    double value = std::numeric_limits<double>::infinity();
    void add(double ms) { value = std::min(value, ms); }
};

} // namespace

TEST_CASE("the Director's costs on the golden plans", "[.perf][directing]") {
    const fs::path dir = fs::path(AVGEN_SOURCE_DIR) / "tests/data/directing/golden";
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() == ".json") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    REQUIRE_FALSE(files.empty());

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(fs::path(AVGEN_SOURCE_DIR) / "examples/world/glowmere-valley-2-multicam.json"));
    std::uint64_t frame = 0;
    const auto step = [&] {
        engine.update(FrameTime{static_cast<double>(frame) / 60.0, frame == 0 ? 0.0 : 1.0 / 60.0, frame});
        ++frame;
    };
    for (int i = 0; i < 3; ++i) {
        step(); // warm: the first frames build what a loaded project has not built yet
    }
    scene::Composition* comp = engine.composition();
    REQUIRE(comp != nullptr);

    Minimum steady;
    Minimum rebuild;
    for (int r = 0; r < kRepeats; ++r) {
        steady.add(millis(step));
        comp->setComposition(comp->composition());
        const std::uint64_t meshBefore = comp->scene().meshVersion;
        rebuild.add(millis(step));
        REQUIRE(comp->scene().meshVersion != meshBefore); // the forced rebuild did happen
    }
    fmt::print("\nDirector costs on Glowmere Valley 2 multicam, ms, minimum of {} repeats\n", kRepeats);
    fmt::print("  steady frame {:.2f}; frame with a forced Composition::rebuild {:.1f}\n\n", steady.value,
               rebuild.value);
    fmt::print("  {:<24} {:>7} {:>6} {:>8} {:>7} {:>8} {:>7} {:>6}  {}\n", "golden", "facts", "parse", "compile",
               "apply", "install", "frame", "undo", "rebuilt / textures");

    for (const fs::path& file : files) {
        const std::string name = file.stem().string();
        INFO("golden: " << name);
        const json golden = testsupport::readJson(file);
        Minimum facts, parse, compile, apply, install, next, undo;
        bool rebuilt = false;
        bool retextured = false;
        bool applied = false;
        for (int r = 0; r < kRepeats; ++r) {
            SceneFacts sceneFacts;
            facts.add(millis([&] { sceneFacts = app::sceneFactsFor(engine); }));
            PlanParse parsed;
            parse.add(millis([&] { parsed = parsePlan(golden.at("plan")); }));
            REQUIRE(parsed.plan);
            Compilation c;
            compile.add(millis([&] { c = compilePlan(*parsed.plan, sceneFacts); }));
            if (!c.changesAnything()) {
                continue;
            }
            ui::EditHistory history;
            const std::uint64_t meshBefore = comp->scene().meshVersion;
            const std::uint64_t textureBefore = comp->scene().textureVersion;
            bool ok = false;
            apply.add(millis([&] { ok = app::applyCompilation(engine, history, c).has_value(); }));
            REQUIRE(ok);
            applied = true;
            next.add(millis(step));
            rebuilt = rebuilt || comp->scene().meshVersion != meshBefore;
            retextured = retextured || comp->scene().textureVersion != textureBefore;
            const seq::Sequence installed = engine.sequence();
            install.add(millis([&] { ok = engine.setSequence(installed).has_value(); }));
            REQUIRE(ok);
            undo.add(millis([&] { ok = history.undo(engine).ok(); }));
            REQUIRE(ok);
            step();
            REQUIRE(engine.directingPlans().empty());
        }
        const auto cell = [&](const Minimum& m, const char* format) {
            return applied ? fmt::format(fmt::runtime(format), m.value) : std::string("-");
        };
        fmt::print("  {:<24} {:>7.2f} {:>6.2f} {:>8.2f} {:>7} {:>8} {:>7} {:>6}  {}\n", name, facts.value,
                   parse.value, compile.value, cell(apply, "{:.2f}"), cell(install, "{:.2f}"), cell(next, "{:.2f}"),
                   cell(undo, "{:.2f}"),
                   applied ? fmt::format("{} / {}", rebuilt ? "REBUILT" : "no", retextured ? "RE-UPLOAD" : "no")
                           : std::string("nothing to apply"));
        CHECK_FALSE(rebuilt);
        CHECK_FALSE(retextured);
    }
    fmt::print("\n");
}

TEST_CASE("what a preview seek costs on the benchmark, with and without a Director plan applied",
          "[.perf][directing][seek]") {
    // Spec §37's last item, measured now that ADR-800 is on main. As the coordinator asked: the
    // entity distance cull lifted (every body simulated, as an offline render has it) and no audio
    // (audio-reactive divergence, cause 2, is still open). One fresh engine per target, so the first
    // seek is cold -- nothing checkpointed yet -- and the second, to the same instant, is what a
    // repeated preview costs.
    const auto load = [](app::Engine& engine) {
        REQUIRE(engine.loadProject(fs::path(AVGEN_SOURCE_DIR) / "examples/world/glowmere-valley-2-multicam.json"));
        scene::DetailLimits limits = engine.detailLimits();
        limits.entityDistanceCull = false;
        engine.setDetailLimits(limits);
        REQUIRE(engine.setAudioClips({}).has_value());
        engine.update(FrameTime{0.0, 0.0, 0});
    };
    const json hop = testsupport::readJson(fs::path(AVGEN_SOURCE_DIR) / "tests/data/directing/golden/rook_hop.json");
    fmt::print("\nSeek on Glowmere Valley 2 multicam, cull lifted, no audio, ms (entity re-simulation in brackets)\n");
    fmt::print("  {:<8} {:>9} {:>20} {:>20}\n", "plan", "target s", "cold seek", "repeat seek");
    for (const bool withPlan : {false, true}) {
        for (const double target : {30.0, 60.0, 90.0}) {
            app::Engine engine(app::EngineMode::Offline);
            load(engine);
            if (withPlan) {
                PlanParse parsed = parsePlan(hop.at("plan"));
                REQUIRE(parsed.plan);
                REQUIRE(app::installCompilation(engine, compilePlan(*parsed.plan, app::sceneFactsFor(engine))));
            }
            const auto seek = [&](double t) {
                probe2::frame().clear();
                const double ms = millis([&] { engine.seekSeconds(t); });
                return std::pair{ms, probe2::frame().entitySeekMs};
            };
            const auto cold = seek(target);
            (void)seek(1.0);
            const auto repeat = seek(target);
            fmt::print("  {:<8} {:>9.0f} {:>11.1f} ({:>6.1f}) {:>11.1f} ({:>6.1f})\n", withPlan ? "rook_hop" : "none",
                       target, cold.first, cold.second, repeat.first, repeat.second);
        }
    }
    fmt::print("\n");
}
