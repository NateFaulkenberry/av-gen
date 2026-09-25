// Scheduled direction is replayed, not merely restored (ADR-824).
//
// A sequence event that gives an entity an order at a known second -- a section action, a Director's
// timed `direct` -- used to be delivered by the engine after the frame, and a seek re-delivered only
// the latest standing intent per target (ADR-093). With ADR-700's checkpoints the replay can do
// better, and a Director issuing orders over time needs it to: the composition now applies those
// orders itself, at their seconds, on a play and in the replay alike. So a scrub lands where the play
// did -- including after a `release` hands the body back, and after a runtime `goal` (Phase D §35)
// has sent a character on an errand of its own choosing.
//
// Play == scrub here is on the composition harness, never through `Engine`, whose seek has its own
// open differences (the lead's 2026-09-24 measurements); `Engine` is used only to install.

#include "app/engine.hpp"
#include "core/time.hpp"
#include "entity/entity.hpp"
#include "scene/composition.hpp"
#include "seq/events.hpp"
#include "seq/section_actions.hpp"
#include "seq/section_performance.hpp"
#include "seq/sequence.hpp"
#include "signals/signal_bus.hpp"
#include "support/project_round_trip.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <set>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

seq::SequenceEvent order(std::string id, double at, std::string who, std::string verb, std::string argument) {
    seq::SequenceEvent e;
    e.id = std::move(id);
    e.when.kind = seq::TriggerKind::Time;
    e.when.timeSeconds = at;
    e.what.kind = seq::EventActionKind::EntityAction;
    e.what.target = std::move(who);
    e.what.value = std::move(verb);
    e.what.argument = std::move(argument);
    return e;
}

// A wanderer and a post, and a sequence that sends the wanderer to the post at 2 s and lets it go at
// 5 s. The move is long enough to still be under way at 3.5 s.
struct Harness {
    app::Engine engine{app::EngineMode::Offline};
    signals::SignalBus bus;
    long long frame = 0;

    explicit Harness(bool withOrders = true) {
        engine.newComposition();
        for (const char* name : {"walker", "post"}) {
            scene::CompositionNode node;
            node.name = name;
            node.kind = scene::NodeKind::Group;
            if (std::string(name) == "post") {
                node.transform.position = {18.0f, 0.0f, 6.0f};
            }
            REQUIRE(engine.addNode(std::move(node)).has_value());
        }
        entity::EntityDesc walker;
        walker.name = "walker";
        walker.seed = 99u;
        entity::BehaviorDesc wander;
        wander.kind = "wander";
        wander.settings = {{"speed", 1.4}, {"minRange", 4.0}, {"maxRange", 9.0}, {"pauseMin", 0.2}, {"pauseMax", 0.5}};
        walker.behaviors.push_back(wander);
        entity::EntityDesc post;
        post.name = "post";
        REQUIRE(engine.composition()->setEntities({walker, post}).has_value());
        if (withOrders) {
            seq::Sequence piece;
            piece.events.push_back(order("go", 2.0, "walker", "move", "post"));
            piece.events.push_back(order("free", 5.0, "walker", "release", ""));
            piece.durationSeconds = 20.0;
            REQUIRE(engine.setSequence(piece).has_value());
        }
    }
    scene::Composition& comp() { return *engine.composition(); }
    void tick() {
        FrameTime time;
        time.renderTime = static_cast<double>(frame) / 60.0;
        time.deltaTime = frame == 0 ? 0.0 : 1.0 / 60.0;
        time.frameIndex = static_cast<std::uint64_t>(frame);
        engine.params().resetFinals();
        comp().updateBehaviour(time, bus);
        comp().update(time);
        ++frame;
    }
    void playTo(double seconds) {
        const auto last = static_cast<long long>(std::llround(seconds * 60.0));
        while (frame <= last) {
            tick();
        }
    }
    void seekTo(double seconds) {
        comp().seekWithDirector(seconds, engine.params(),
                                entity::SeekBudget{.maxSeconds = 90.0,
                                                   .maxBodySteps = entity::SeekBudget::kEditorBodySteps,
                                                   .mode = entity::SeekMode::Checkpointed},
                                1.0 / 60.0);
        frame = static_cast<long long>(std::llround(seconds * 60.0)) + 1;
    }
    [[nodiscard]] const entity::Entity& walker() { return *comp().entityWorld().find("walker"); }
};

} // namespace

TEST_CASE("a scheduled order is given at its second and a release hands the body back", "[motion][direction]") {
    Harness h;
    REQUIRE(h.comp().directives().size() == 2);
    h.playTo(1.9);
    CHECK(h.walker().actions().authority() != entity::Authority::Director);
    h.playTo(3.5);
    CHECK(h.walker().actions().authority() == entity::Authority::Director);
    const float before = glm::length(h.walker().state().position() - glm::vec3(18.0f, 0.0f, 6.0f));
    h.playTo(4.5);
    CHECK(glm::length(h.walker().state().position() - glm::vec3(18.0f, 0.0f, 6.0f)) < before); // closing in
    h.playTo(5.5);
    CHECK_FALSE((h.walker().actions().running() && h.walker().actions().authority() == entity::Authority::Director));
}

TEST_CASE("a scrub lands where the play did, mid-order and after the release", "[motion][direction][seek][determinism]") {
    for (const double target : {3.5, 8.0}) {
        CAPTURE(target);
        Harness played;
        played.playTo(target);
        played.tick();
        Harness scrubbed;
        scrubbed.tick();
        scrubbed.seekTo(target);
        scrubbed.tick();
        CHECK(glm::length(played.walker().state().position() - scrubbed.walker().state().position()) < 1e-5f);
    }
    SECTION("control: without the orders the same scrub is somewhere else") {
        Harness played;
        played.playTo(8.0);
        played.tick();
        Harness plain(false);
        plain.tick();
        plain.seekTo(8.0);
        plain.tick();
        CHECK(glm::length(played.walker().state().position() - plain.walker().state().position()) > 0.5f);
    }
}

TEST_CASE("through the engine an order is given once, not once by the replay path and again by the host",
          "[motion][direction]") {
    Harness h;
    int cancelledEarly = 0;
    h.comp().entityWorld().setActionListener([&](const entity::ActionEvent& e) {
        if (e.result == entity::ActionResult::Cancelled && e.time < 4.9) {
            ++cancelledEarly;
        }
    });
    testsupport::stepFrames(h.engine, 60 * 6);
    // A second `direct` of the same order would cancel the first on the spot.
    CHECK(cancelledEarly == 0);
}

TEST_CASE("the section vocabulary has release and goal, and goal needs something to go after", "[motion][direction]") {
    const auto release = seq::actionFromEvent("walker", "release", "");
    REQUIRE(release.has_value());
    CHECK(release->release);
    const auto goal = seq::actionFromEvent("warden", "goal", "mushroom-2.inspect");
    REQUIRE(goal.has_value());
    CHECK(goal->goal);
    CHECK(goal->goalSubject == "mushroom-2");
    CHECK(goal->goalAffordance == "inspect");
    CHECK_FALSE(seq::actionFromEvent("warden", "goal", "").has_value());
    CHECK(seq::isSectionVerb("move"));
    CHECK_FALSE(seq::isSectionVerb("teleport"));
}

TEST_CASE("a runtime goal sends a character on its own errand, and a scrub lands on it too",
          "[motion][direction][goal][phaseD]") {
    const fs::path demo = fs::path(AVGEN_SOURCE_DIR) / "examples" / "labs" / "character" / "autonomy-demo.scene.json";
    if (!fs::exists(fs::path(AVGEN_SOURCE_DIR) / "assets" / "aliens" / "alien-scout.glb")) {
        SKIP("alien assets are not present");
    }
    // The warden's decider with an EMPTY goal slot: nothing authored, so only a director fills it.
    const auto build = [&](app::Engine& engine, bool withGoal) {
        REQUIRE(engine.loadComposition(demo).has_value());
        std::vector<entity::EntityDesc> descs = engine.composition()->entities();
        bool slotted = false;
        for (entity::EntityDesc& d : descs) {
            if (d.name != "warden") {
                continue;
            }
            for (entity::BehaviorDesc& b : d.behaviors) {
                if (b.kind == "decide") {
                    b.settings["considerers"].push_back({{"kind", "goal"}, {"name", "errand"}, {"weight", 6.0}});
                    slotted = true;
                }
            }
        }
        REQUIRE(slotted);
        REQUIRE(engine.composition()->setEntities(descs).has_value());
        seq::Sequence piece;
        if (withGoal) {
            piece.events.push_back(order("errand", 20.0, "warden", "goal", "mushroom-2.inspect"));
        }
        piece.durationSeconds = 120.0;
        REQUIRE(engine.setSequence(piece).has_value());
    };
    struct Run {
        app::Engine engine{app::EngineMode::Offline};
        signals::SignalBus bus;
        long long frame = 0;
        void tick() {
            FrameTime time;
            time.renderTime = static_cast<double>(frame) / 60.0;
            time.deltaTime = frame == 0 ? 0.0 : 1.0 / 60.0;
            time.frameIndex = static_cast<std::uint64_t>(frame);
            engine.params().resetFinals();
            engine.composition()->updateBehaviour(time, bus);
            engine.composition()->update(time);
            ++frame;
        }
    };
    Run played;
    build(played.engine, true);
    const entity::Entity* warden = played.engine.composition()->entityWorld().find("warden");
    REQUIRE(warden != nullptr);
    double firstUse = -1.0;
    while (played.frame <= 60 * 60) {
        played.tick();
        const entity::ActionDesc* a = warden->actions().current(entity::Authority::Routine);
        if (firstUse < 0.0 && a != nullptr && a->kind == entity::ActionKind::Interact && a->target.name == "mushroom-2") {
            firstUse = static_cast<double>(played.frame) / 60.0;
        }
    }
    INFO("the warden first used mushroom-2 at " << firstUse << " s");
    CHECK(firstUse >= 20.0);
    CHECK(warden->directorGoal().active);

    Run scrubbed;
    build(scrubbed.engine, true);
    scrubbed.tick();
    scrubbed.engine.composition()->seekWithDirector(
        60.0, scrubbed.engine.params(),
        entity::SeekBudget{.maxSeconds = 90.0, .maxBodySteps = entity::SeekBudget::kEditorBodySteps,
                           .mode = entity::SeekMode::Checkpointed},
        1.0 / 60.0);
    scrubbed.frame = 60 * 60 + 1;
    scrubbed.tick();
    played.tick();
    const entity::Entity* w2 = scrubbed.engine.composition()->entityWorld().find("warden");
    CHECK(glm::length(warden->state().position() - w2->state().position()) < 1e-5f);

    SECTION("control: with the slot empty the warden never goes on that errand") {
        Run plain;
        build(plain.engine, false);
        const entity::Entity* w = plain.engine.composition()->entityWorld().find("warden");
        bool used = false;
        while (plain.frame <= 60 * 60) {
            plain.tick();
            const entity::ActionDesc* a = w->actions().current(entity::Authority::Routine);
            used = used || (a != nullptr && a->kind == entity::ActionKind::Interact && a->target.name == "mushroom-2" &&
                            w->actions().current(entity::Authority::Routine)->name == "errand");
        }
        CHECK_FALSE(used);
    }
}
