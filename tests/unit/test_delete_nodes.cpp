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
// Hidden by default ([.]) only until it is understood; a crash in the suite is worse than a crash
// in the editor for everyone else's ability to work.

#include "app/engine.hpp"
#include "scene/composition.hpp"
#include "ui/edit_history.hpp"
#include "ui/world_edit.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <vector>

using namespace avgen;

namespace {
std::filesystem::path glowmereProject() {
    return std::filesystem::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-valley-2.json";
}
} // namespace

TEST_CASE("deleting animals while the scene runs does not crash", "[.delete-crash][editor]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    if (!std::filesystem::exists(glowmereProject())) {
        SKIP("Glowmere Valley 2 is not present");
    }
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
