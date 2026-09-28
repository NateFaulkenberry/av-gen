// Deleting objects out of a live scene, which the owner reports crashes the editor.
//
// The report: "I am able to crash the app by deleting objects out of the glowmere scene ... after I
// delete a few of them the app will crash", with the guess that a running abduction scenario still
// references an entity whose node is gone.
//
// `ui::deleteNodes` is two calls -- `Composition::detachNode` per doomed node, then
// `Engine::rebind()` -- and neither removes the matching `entity::EntityDesc`, so `installEntities`
// recreates an entity for a node that no longer exists. That is the shape this reproduces: run the
// scene long enough for the staging to bind animals, then delete them out from under it and keep
// stepping.
//
// The defect, once found: `detachNode` destroys the node's transform parameters, and `EntityWorld`
// caches RAW POINTERS to them. Nothing re-bound -- `Engine::rebind()` covers the modulator and the
// timeline, not the entity world -- so the next `updateBehaviour` wrote through a freed pointer.
//
// **This test only fails under ASan, and that is the point.** In a release build the freed block
// still holds plausible floats and the run completes; the corruption surfaces later and somewhere
// else entirely (both of the owner's crash reports landed in AppKit timer code). Run it with
// `cmake --build --preset asan --target avgen_tests && ./build/asan/tests/avgen_tests "[delete]"`.

#include "app/directing_plan_file.hpp"
#include "app/engine.hpp"
#include "scene/composition.hpp"
#include "stage/staging.hpp"
#include "ui/edit_history.hpp"
#include "support/project_assets.hpp"
#include "ui/world_edit.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

using namespace avgen;

namespace {
std::filesystem::path glowmereProject() {
    return std::filesystem::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-valley-2.json";
}
} // namespace

TEST_CASE("deleting animals while the scene runs does not crash", "[editor][delete][regression]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    if (!std::filesystem::exists(glowmereProject())) {
        SKIP("Glowmere Valley 2 is not present");
    }
    // The animals ARE the farm GLBs: without them there is no animal node to delete.
    testsupport::skipUnlessFarmAssetsPresent();
    app::Engine engine(app::EngineMode::Offline);
    auto loaded = engine.loadProject(glowmereProject());
    INFO((loaded ? std::string() : loaded.error().message));
    REQUIRE(loaded.has_value());
    scene::Composition* comp = engine.composition();
    REQUIRE(comp != nullptr);

    const double dt = 1.0 / 30.0;
    double t = 0.0;
    const auto step = [&](int n) {
        for (int i = 0; i < n; ++i) {
            FrameTime time{};
            time.renderTime = t;
            time.deltaTime = dt;
            time.frameIndex = static_cast<std::uint64_t>(t / dt);
            engine.update(time);
            t += dt;
        }
    };

    // Long enough for the abduction to have claimed something.
    step(900);

    // EVERY farm animal, not just the chickens the report names. The scenario binds one animal at a
    // time and the report's chickens are 4 of ~18 -- so a run that deleted only those would usually
    // delete nothing the scenario was holding, pass, and prove nothing (ADR-182). Deleting all of
    // them guarantees the bound one is in the list whatever it is.
    static constexpr const char* kSpecies[] = {"chicken", "rooster", "chick", "cow",
                                               "bull",    "horse",   "pig",   "goat", "sheep"};
    std::vector<std::string> doomed;
    for (const auto& node : comp->nodes()) {
        const std::string& n = node->name;
        for (const char* species : kSpecies) {
            if (n.find(species) != std::string::npos) {
                doomed.push_back(n);
                break;
            }
        }
    }
    INFO("deleting " << doomed.size() << " animal node(s)");
    REQUIRE_FALSE(doomed.empty());

    // Through the editor's own path, not `detachNode` directly: `ui::deleteNodes` also expands to
    // descendants, builds an `EditCommand` that OWNS the removed nodes, and the history keeps that
    // command alive. A raw detach frees the node; this keeps it, which is a different lifetime.
    ui::EditHistory history;
    for (const std::string& name : doomed) {
        INFO("deleting '" << name << "'");
        const std::string one[] = {name};
        ui::EditCommand command = ui::deleteNodes(engine, one);
        CHECK_FALSE(command.removed.empty());
        history.push(std::move(command));
        step(60);           // the frames the editor would draw after the click
        CHECK(comp->findNode(name) == nullptr);
    }
#endif
}

// The same regression with no gitignored asset, so it runs on every CI runner and under the nightly
// ASan build. The case above needs the purchased farm GLBs (skipped on hosted runners since 4a138886);
// the defect it guards was never about meshes: `EntityWorld` cached raw pointers to a node's
// transform parameters, and deleting the node freed them. The set-piece lab
// (tests/data/setpieces/) has a craft, a beam and eight primitive cows, and a set piece that abducts
// one of them. Deleting every cow while the abduction HOLDS one is the owner's report in miniature.
TEST_CASE("deleting a held animal from a running set piece does not crash, without any asset",
          "[editor][delete][regression][setpiece]") {
    const std::filesystem::path project =
        std::filesystem::path(AVGEN_SOURCE_DIR) / "tests" / "data" / "setpieces" / "setpiece-lab.json";
    app::Engine engine(app::EngineMode::Offline);
    auto loaded = engine.loadProject(project);
    INFO((loaded ? std::string() : loaded.error().message));
    REQUIRE(loaded.has_value());
    const auto plan = nlohmann::json::parse(R"({
      "schemaVersion": 1, "id": "ufo", "title": "UFO activity", "tier": "baked",
      "setPieces": [
        {"key": "west", "template": "abduction", "craft": "saucer", "at": {"seconds": 8},
         "place": {"point": [-120, -60]},
         "set": {"animals": 1, "approachSeconds": 3, "approachBearing": 270}, "framingMetres": 30}
      ]})");
    auto applied = app::applyPlanDocument(engine, plan);
    INFO((applied ? applied->toJson().dump() : applied.error().message));
    REQUIRE(applied.has_value());
    scene::Composition* comp = engine.composition();
    REQUIRE(comp != nullptr);
    comp->scene().detailLimits.entityDistanceCull = false;

    const double dt = 1.0 / 30.0;
    double t = 0.0;
    const auto step = [&] {
        FrameTime time{};
        time.renderTime = t;
        time.deltaTime = dt;
        time.frameIndex = static_cast<std::uint64_t>(t / dt);
        engine.update(time);
        t += dt;
    };
    // Which cow a running abduction holds as its target, or "".
    const auto held = [&]() -> std::string {
        for (const auto& state : comp->director().sequenceStates()) {
            for (const auto& cue : state.cues) {
                // A set piece numbers its roles: target1, target2, ...
                if (state.running && cue.role.rfind("target", 0) == 0 && cue.entity.rfind("cow-", 0) == 0) {
                    return cue.entity;
                }
            }
        }
        return {};
    };

    // Run until the set piece has bound a cow. A run that deleted animals nobody held would pass and
    // prove nothing (ADR-182), so reaching this state is REQUIRED, not hoped for.
    std::string target;
    while (target.empty() && t < 60.0) {
        step();
        target = held();
    }
    INFO("the set piece held '" << target << "' at " << t << " s");
    REQUIRE_FALSE(target.empty());

    std::vector<std::string> doomed;
    for (const auto& node : comp->nodes()) {
        if (node->name.rfind("cow-", 0) == 0) {
            doomed.push_back(node->name);
        }
    }
    REQUIRE(doomed.size() == 8);
    // The held cow first: the scenario's binding is the reference the owner's crash went through.
    std::stable_partition(doomed.begin(), doomed.end(), [&](const std::string& n) { return n == target; });

    ui::EditHistory history;
    for (const std::string& name : doomed) {
        INFO("deleting '" << name << "'");
        const std::string one[] = {name};
        ui::EditCommand command = ui::deleteNodes(engine, one);
        CHECK_FALSE(command.removed.empty());
        history.push(std::move(command));
        for (int i = 0; i < 60; ++i) {
            step();
        }
        CHECK(comp->findNode(name) == nullptr);
    }
    CHECK(held().empty()); // nothing left to hold, and nothing still pointing at what was
}
