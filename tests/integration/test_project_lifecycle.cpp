// Opening a project, closing it, and opening another one (ADR-440).
//
// The owner asked for this as a lifecycle test rather than a smoke test, and the half that catches
// real bugs is the second one: **A's state must be gone**, not merely B's state present. This
// codebase has had state survive a scene change before -- `Engine::resetCameraState` exists
// precisely because some of it must be explicitly cleared, and ADR-330 was written after eighty
// nodes came back from a project that had deleted one.
//
// ADR-182: a probe that cannot fail proves nothing, so A and B are chosen to be as unlike each
// other as two real projects in this repository get:
//
//                                      A: glowmere-valley-2-multicam   B: hero
//     project file                            675 KB                     1.3 KB
//     serialised parameters                    5,502                         25
//     heroes                                      16                          0
//     camera aim-follow spans                     37                          0
//     shot spans                                  42                          0
//     world effects / atmospherics               yes                         no
//     audio                                      yes                         no
//
// Every assertion below is a fact that is true of exactly one of them, so a leak in either
// direction is a failure rather than a coincidence.

#include "app/engine.hpp"
#include "scene/composition.hpp"
#include "params/parameter.hpp"
#include "params/parameter_set.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

fs::path examples() { return fs::path(AVGEN_SOURCE_DIR) / "examples"; }
fs::path projectA() { return examples() / "world" / "glowmere-valley-2-multicam.json"; }
fs::path projectB() { return examples() / "hero" / "hero.json"; }

// A parameter that exists in exactly one of the two, checked by name. `fx/aurora/...` is one of
// A's effects (ADR-702: keyed by the effect's id); `sources/turn/rate` is B's procedural source. Neither project has any
// reason to register the other's.
constexpr const char* kOnlyInA = "fx/aurora/baseHeight";
constexpr const char* kOnlyInB = "sources/turn/rate";

bool hasParameter(app::Engine& engine, const char* path) {
    return engine.params().find(path) != nullptr;
}

} // namespace

TEST_CASE("open a project, close it, open another: A's state does not survive into B",
          "[project][lifecycle][regression]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    if (!fs::exists(projectA()) || !fs::exists(projectB())) {
        SKIP("the example projects are not present");
    }
    app::Engine engine(app::EngineMode::Offline);

    // ---- open A ---------------------------------------------------------------------------------
    auto openedA = engine.loadProject(projectA());
    INFO((openedA ? std::string() : openedA.error().message));
    REQUIRE(openedA.has_value());
    REQUIRE(engine.composition() != nullptr);

    const std::size_t nodesA = engine.composition()->nodes().size();
    const std::size_t heroesA = engine.composition()->heroes().size();
    const std::size_t aimA = engine.composition()->aimFollow().size();
    const std::size_t paramsA = engine.params().size();
    INFO("A: " << nodesA << " nodes, " << heroesA << " heroes, " << aimA << " aim-follow spans, "
               << paramsA << " parameters");

    // Specific facts about A, not "it loaded". Each is zero or absent in B.
    CHECK(engine.projectPath() == projectA());
    CHECK(nodesA > 50);
    CHECK(heroesA > 0);
    CHECK(aimA > 0);
    CHECK(hasParameter(engine, kOnlyInA));
    CHECK_FALSE(hasParameter(engine, kOnlyInB));

    // ---- close it -------------------------------------------------------------------------------
    // File > New is what "close the project" is in this application: there is no other close.
    engine.newProject();
    CHECK(engine.projectPath().empty());
    CHECK(engine.composition() == nullptr); // the orb scene is not a composition
    CHECK_FALSE(hasParameter(engine, kOnlyInA));
    CHECK(engine.timeline().empty());

    // ---- open B ---------------------------------------------------------------------------------
    auto openedB = engine.loadProject(projectB());
    INFO((openedB ? std::string() : openedB.error().message));
    REQUIRE(openedB.has_value());
    REQUIRE(engine.composition() != nullptr);

    const std::size_t nodesB = engine.composition()->nodes().size();
    INFO("B: " << nodesB << " nodes, " << engine.params().size() << " parameters");

    // B is live...
    CHECK(engine.projectPath() == projectB());
    CHECK(hasParameter(engine, kOnlyInB));
    CHECK(nodesB > 0);

    // ...and this is the half that catches the bugs: A is gone.
    CHECK_FALSE(hasParameter(engine, kOnlyInA));
    CHECK(engine.composition()->heroes().empty());
    CHECK(engine.composition()->aimFollow().empty());
    CHECK(nodesB < nodesA);
    CHECK(engine.params().size() < paramsA);
#endif
}

TEST_CASE("opening B directly over A, with no close in between, leaves nothing of A",
          "[project][lifecycle][regression]") {
    // The same assertion without the `newProject()` in the middle, because that is what File > Open
    // actually does -- and a cleanup that only happens in `newProject` would pass the test above
    // and lose to this one.
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    if (!fs::exists(projectA()) || !fs::exists(projectB())) {
        SKIP("the example projects are not present");
    }
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(projectA()).has_value());
    REQUIRE(engine.composition() != nullptr);
    const std::size_t nodesA = engine.composition()->nodes().size();
    REQUIRE(hasParameter(engine, kOnlyInA));
    REQUIRE(engine.composition()->aimFollow().size() > 0);

    REQUIRE(engine.loadProject(projectB()).has_value());
    REQUIRE(engine.composition() != nullptr);
    CHECK(hasParameter(engine, kOnlyInB));
    CHECK_FALSE(hasParameter(engine, kOnlyInA));
    CHECK(engine.composition()->heroes().empty());
    CHECK(engine.composition()->aimFollow().empty());
    CHECK(engine.composition()->nodes().size() < nodesA);
#endif
}

TEST_CASE("a project is clean when it is opened and dirty once something changes",
          "[project][lifecycle][unsaved]") {
    // The measurement ADR-440 is built on, as an assertion: a load-then-serialise comparison against
    // the file reports every project dirty, and a comparison against the *serialisation* taken at
    // load reports none of them. If a future change makes a load dirty its own project, the prompt
    // starts firing on every close and this fails first.
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    if (!fs::exists(projectA()) || !fs::exists(projectB())) {
        SKIP("the example projects are not present");
    }
    app::Engine engine(app::EngineMode::Offline);

    for (const fs::path& project : {projectB(), projectA()}) {
        REQUIRE(engine.loadProject(project).has_value());
        INFO("just opened " << project.filename().string());
        // Asked as the application asks it at a close: "something touched me since you last asked".
        CHECK_FALSE(engine.projectDirty(true));

        // The control (ADR-182): change one number and the same call must say yes. Without this the
        // assertion above would pass on a `projectDirty` that returned false unconditionally.
        params::IParameter* p = engine.params().find("camera/fov");
        REQUIRE(p != nullptr);
        const float fov = p->baseComponent(0);
        p->setBaseComponent(0, fov + 3.0f);
        REQUIRE(p->baseComponent(0) != fov); // the edit actually took (ADR-387)
        CHECK(engine.projectDirty(true));

        // And it is monotone: putting the value back does not un-dirty the project, because a save
        // is the only thing that makes memory and disk agree again.
        p->setBaseComponent(0, fov);
        CHECK(engine.projectDirty(true));
    }

    // A save clears it, and the state survives a round trip through the disk (ADR-225/350).
    const fs::path out = testsupport::processTempDir() / "lifecycle-roundtrip.json";
    REQUIRE(engine.saveProject(out).has_value());
    CHECK_FALSE(engine.projectDirty(true));
    CHECK_FALSE(engine.projectDirtyCached());

    app::Engine reopened(app::EngineMode::Offline);
    REQUIRE(reopened.loadProject(out).has_value());
    CHECK_FALSE(reopened.projectDirty(true));

    // A new project is clean: there is nothing in it yet to lose.
    reopened.newProject();
    CHECK_FALSE(reopened.projectDirty(true));

    std::error_code ec;
    fs::remove(out, ec);
#endif
}

TEST_CASE("the engine's own writeback does not dirty an untouched project",
          "[project][lifecycle][unsaved]") {
    // The measured drift, asserted. Playing 600 frames of Glowmere Valley 2 Multi-Camera with no
    // input moves 18 hero position components and two `particles/visitor-beam` parameter bases --
    // ADR-386's per-frame writeback and the entity simulation. A prompt that fired on those would
    // fire on every close of a project nobody had touched, and that is the failure the brief calls
    // worse than having no prompt at all.
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    if (!fs::exists(projectA())) {
        SKIP("Glowmere Valley 2 Multi-Camera is not present");
    }
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(projectA()).has_value());
    engine.setViewport(1920, 1080);

    double t = 0.0;
    const double dt = 1.0 / 30.0;
    const auto play = [&](int frames) {
        for (int i = 0; i < frames; ++i) {
            FrameTime ft{};
            ft.renderTime = t;
            ft.deltaTime = dt;
            t += dt;
            engine.update(ft);
            // What the application does on an idle frame: sample, reporting that nothing touched it.
            engine.sampleProjectDirty(false);
        }
    };

    play(600);
    INFO("after 20 s of playback with no input");
    CHECK_FALSE(engine.projectDirtyCached());
    CHECK_FALSE(engine.projectDirty(false));

    // The control (ADR-182), and the one that matters most: an edit made *during* playback is not
    // absorbed by the same mechanism. Without this, "the drift is ignored" and "everything is
    // ignored" would be the same passing test -- and the second one silently loses work.
    params::IParameter* p = engine.params().find("camera/fov");
    REQUIRE(p != nullptr);
    const float fov = p->baseComponent(0);
    p->setBaseComponent(0, fov + 5.0f);
    REQUIRE(p->baseComponent(0) != fov); // the edit actually took (ADR-387)
    CHECK(engine.projectDirty(true));
    // ...and it stays dirty across further playback, rather than being absorbed by the next quiet
    // sample. This is the monotonicity that keeps the absorption from becoming a work-loser.
    play(120);
    CHECK(engine.projectDirtyCached());
#endif
}

TEST_CASE("a close during playback, with no periodic sample, still answers correctly",
          "[project][lifecycle][unsaved][adr952]") {
    // ADR-952: the periodic sample does not run while the transport plays, so a close in the middle
    // of a film measures once, on demand, over the whole playback. Both directions must hold from
    // that one measurement: the engine's own writes during a long untouched playback are absorbed
    // (no spurious prompt), and an edit made mid-playback is reported (no lost work).
    //
    // Measured while writing ADR-952: 600 frames of this project, and 300 frames of Glowmere Valley
    // 3 from 0 s, 85.8 s and 194.7 s, now drift by **zero** paths -- the 20 paths ADR-440 measured
    // on 2026-09-20 no longer move. So the engine's writeback is stood in for by the thing it is: a
    // parameter base written with nothing touching the application. The same write, reported as a
    // touch, is the edit section's control (ADR-182).
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    if (!fs::exists(projectA())) {
        SKIP("Glowmere Valley 2 Multi-Camera is not present");
    }
    double t = 0.0;
    const double dt = 1.0 / 30.0;
    // Playback as ADR-952 has it: frames advance, and nothing samples.
    const auto play = [&](app::Engine& engine, int frames) {
        for (int i = 0; i < frames; ++i) {
            FrameTime ft{};
            ft.renderTime = t;
            ft.deltaTime = dt;
            t += dt;
            engine.update(ft);
        }
    };

    SECTION("an untouched playback is absorbed in one on-demand sample") {
        app::Engine engine(app::EngineMode::Offline);
        REQUIRE(engine.loadProject(projectA()).has_value());
        engine.setViewport(1920, 1080);
        const nlohmann::json opened = engine.projectDocument(engine.projectPath());
        play(engine, 300);
        // The engine writing its own state mid-playback, as ADR-386's writeback does.
        params::IParameter* p = engine.params().find("camera/fov");
        REQUIRE(p != nullptr);
        p->setBaseComponent(0, p->baseComponent(0) + 5.0f);
        play(engine, 300);
        // The document did move, so "absorbed" below is not "there was nothing to absorb".
        REQUIRE(engine.projectDocument(engine.projectPath()) != opened);
        // The close, as `Application::requestClose` asks it after an untouched playback.
        CHECK_FALSE(engine.projectDirty(/*touchedSinceLastSample=*/false));
    }

    SECTION("an edit made mid-playback is reported by the close") {
        app::Engine engine(app::EngineMode::Offline);
        REQUIRE(engine.loadProject(projectA()).has_value());
        engine.setViewport(1920, 1080);
        play(engine, 300);
        params::IParameter* p = engine.params().find("camera/fov");
        REQUIRE(p != nullptr);
        const float fov = p->baseComponent(0);
        p->setBaseComponent(0, fov + 5.0f);
        REQUIRE(p->baseComponent(0) != fov); // the edit actually took (ADR-387)
        play(engine, 300);
        // Nothing sampled in 20 s, and the cached answer is the stale one the title would show...
        CHECK_FALSE(engine.projectDirtyCached());
        // ...which is why a close never reads it: measured on demand, with the touch the schedule
        // kept, the edit is there.
        CHECK(engine.projectDirty(/*touchedSinceLastSample=*/true));
        // And the stop sample that follows keeps it (monotone).
        play(engine, 60);
        engine.sampleProjectDirty(false);
        CHECK(engine.projectDirtyCached());
    }
#endif
}
