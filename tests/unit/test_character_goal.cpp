// The Director's Slice 4 interface (ADR-828, the Director's ADR-763): a `CharacterGoal` sequence
// event, the character's semantic events, a clip readout, the goal in the checkpoint, and a seek
// that does not give an order twice.
//
// A goal is live -- the character decides the how -- but it is SCHEDULED: it is given at a known
// second, so the replay gives it at the same second, and a scrub lands where a play does. These arms
// hold that through `Engine` in the ADR-800 pattern (cull lifted, no audio: the autonomy demo has
// none), including a scrub that restores a checkpoint from inside the goal's window.

#include "app/engine.hpp"
#include "core/time.hpp"
#include "entity/entity.hpp"
#include "scene/composition.hpp"
#include "scene/detail_limits.hpp"
#include "seq/director.hpp"
#include "seq/events.hpp"
#include "seq/sequence.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <map>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

fs::path demo() { return fs::path(AVGEN_SOURCE_DIR) / "examples" / "labs" / "character" / "autonomy-demo.scene.json"; }
bool present() { return fs::exists(fs::path(AVGEN_SOURCE_DIR) / "assets" / "aliens" / "alien-scout.glb"); }

seq::SequenceEvent characterGoal(double at, double seconds) {
    seq::SequenceEvent e;
    e.id = "errand";
    e.when.kind = seq::TriggerKind::Time;
    e.when.timeSeconds = at;
    e.what.kind = seq::EventActionKind::CharacterGoal;
    e.what.target = "warden";
    e.what.value = "mushroom-2";
    e.what.argument = "inspect";
    e.what.seconds = seconds;
    e.what.goal.intent = "interact";
    e.what.goal.dwell = 2.0f;
    return e;
}

// "When the warden arrives, tell the host": a live sequence event on the goal's named arrival.
seq::SequenceEvent onArrival() {
    seq::SequenceEvent e;
    e.id = "arrived";
    e.when.kind = seq::TriggerKind::ActionComplete;
    e.when.name = "goal.arrived";
    e.when.subject = "warden";
    e.what.kind = seq::EventActionKind::Notify;
    e.what.target = "warden-arrived";
    return e;
}

// The demo, the warden's decider given an EMPTY goal slot (the plan owns the goal, not the entity),
// and a sequence carrying the goal event.
void build(app::Engine& engine, bool withGoal, double goalSeconds = 0.0,
           const std::vector<seq::SequenceEvent>& extra = {}) {
    REQUIRE(engine.loadComposition(demo()).has_value());
    engine.setDetailLimits(scene::DetailLimits::unlimited());
    std::vector<entity::EntityDesc> descs = engine.composition()->entities();
    for (entity::EntityDesc& d : descs) {
        if (d.name != "warden") {
            continue;
        }
        for (entity::BehaviorDesc& b : d.behaviors) {
            if (b.kind == "decide") {
                b.settings["considerers"].push_back({{"kind", "goal"}, {"name", "errand"}, {"weight", 6.0}});
            }
        }
    }
    REQUIRE(engine.composition()->setEntities(descs).has_value());
    seq::Sequence piece;
    piece.durationSeconds = 120.0;
    if (withGoal) {
        piece.events.push_back(characterGoal(20.0, goalSeconds));
        piece.events.push_back(onArrival());
    }
    piece.events.insert(piece.events.end(), extra.begin(), extra.end());
    REQUIRE(engine.setSequence(piece).has_value());
}

// A live sequence event that notifies when `kind`/`name` happens to the warden.
seq::SequenceEvent onWarden(const std::string& id, seq::TriggerKind kind, const std::string& name) {
    seq::SequenceEvent e;
    e.id = id;
    e.when.kind = kind;
    e.when.name = name;
    e.when.subject = "warden";
    e.what.kind = seq::EventActionKind::Notify;
    e.what.target = id;
    return e;
}

void frame(app::Engine& engine, long long f) {
    engine.update(FrameTime{static_cast<double>(f) / 60.0, f == 0 ? 0.0 : 1.0 / 60.0, static_cast<std::uint64_t>(f)});
}

std::map<std::string, glm::vec3> drawn(const app::Engine& engine) {
    std::map<std::string, glm::vec3> out;
    for (const auto& e : engine.composition()->entityWorld().entities()) {
        out[e->name()] = e->visualPosition();
    }
    return out;
}

float worst(const std::map<std::string, glm::vec3>& a, const std::map<std::string, glm::vec3>& b) {
    float w = 0.0f;
    for (const auto& [name, p] : a) {
        w = std::max(w, glm::length(p - b.at(name)));
    }
    return w;
}

} // namespace

TEST_CASE("a CharacterGoal survives the project file", "[motion][goal][seq]") {
    seq::Sequence piece;
    piece.events.push_back(characterGoal(20.0, 15.0));
    auto again = seq::Sequence::fromJson(piece.toJson());
    REQUIRE(again.has_value());
    const seq::EventAction& a = again->events.front().what;
    CHECK(a.kind == seq::EventActionKind::CharacterGoal);
    CHECK(a.value == "mushroom-2");
    CHECK(a.argument == "inspect");
    CHECK(a.seconds == 15.0);
    CHECK(a.goal.intent == "interact");
    CHECK(a.goal.dwell == 2.0f);
    CHECK(a.goal.approach < 0.0f); // unset: the considerer's own
    CHECK_FALSE(seq::actionIsBaked(seq::EventActionKind::CharacterGoal));
}

TEST_CASE("a CharacterGoal is taken up at its second, arrives by name, and lapses at its end",
          "[motion][goal][benchmark]") {
    if (!present()) {
        SKIP("alien assets are not present");
    }
    app::Engine engine(app::EngineMode::Offline);
    build(engine, true, 60.0); // stands from 20 s to 80 s
    const entity::Entity* warden = engine.composition()->entityWorld().find("warden");
    REQUIRE(warden != nullptr);
    double firstUse = -1.0;
    double arrivedAt = -1.0;
    double notified = -1.0;
    engine.composition()->entityWorld().setActionListener([&](const entity::ActionEvent& e) {
        if (e.entity == "warden" && e.event == "goal.arrived" && arrivedAt < 0.0) {
            arrivedAt = e.time;
        }
    });
    for (long long f = 0; f <= 60 * 100; ++f) {
        frame(engine, f);
        const entity::ActionDesc* a = warden->actions().current(entity::Authority::Routine);
        if (firstUse < 0.0 && a != nullptr && a->kind == entity::ActionKind::Interact && a->target.name == "mushroom-2") {
            firstUse = static_cast<double>(f) / 60.0;
        }
        for (const seq::FiredEvent& fired : engine.firedEvents()) {
            if (notified < 0.0 && engine.sequence().events[fired.eventIndex].id == "arrived") {
                notified = fired.timeSeconds;
            }
        }
    }
    INFO("first used the mushroom at " << firstUse << " s; arrived " << arrivedAt << " s; notified " << notified << " s");
    CHECK(firstUse >= 20.0);
    CHECK(arrivedAt >= 20.0);
    CHECK(notified == arrivedAt); // `ActionComplete` has a producer, at the simulation second
    // Lapsed at 80 s: the errand is no longer on offer.
    CHECK(warden->directorGoal().until == 80.0);
    const entity::ActionDesc* late = warden->actions().current(entity::Authority::Routine);
    CHECK_FALSE((late != nullptr && late->name == "errand"));
}

TEST_CASE("through the Engine, a scrub into and past a goal lands where the play did",
          "[motion][goal][seek][determinism]") {
    if (!present()) {
        SKIP("alien assets are not present");
    }
    const std::vector<long long> targets{60 * 30, 60 * 45, 60 * 90};
    app::Engine played(app::EngineMode::Offline);
    build(played, true);
    std::map<long long, std::map<std::string, glm::vec3>> at;
    std::map<long long, std::size_t> eventsAt;
    long long f = 0;
    for (long long t : targets) {
        for (; f <= t + 1; ++f) {
            frame(played, f);
        }
        at[t] = drawn(played);
        eventsAt[t] = played.composition()->entityWorld().worldEvents().size();
    }
    for (long long t : targets) {
        CAPTURE(t);
        app::Engine scrubbed(app::EngineMode::Offline);
        build(scrubbed, true);
        frame(scrubbed, 0);
        scrubbed.seekSeconds(static_cast<double>(t) / 60.0);
        frame(scrubbed, t + 1);
        CHECK(worst(at[t], drawn(scrubbed)) == 0.0f);
        // The goal's named completions are world events on the replay too, so a recording that
        // samples either sees the same record.
        CHECK(scrubbed.composition()->entityWorld().worldEvents().size() == eventsAt[t]);
    }

    SECTION("from a checkpoint: a scrub back into the goal's window after one past it") {
        app::Engine scrubbed(app::EngineMode::Offline);
        build(scrubbed, true);
        frame(scrubbed, 0);
        scrubbed.seekSeconds(90.0); // records checkpoints through the goal
        scrubbed.seekSeconds(45.0); // restores one from inside it
        CHECK(scrubbed.composition()->entityWorld().lastSeekWork().restoredFrom > 20.0);
        frame(scrubbed, 60 * 45 + 1);
        CHECK(worst(at[60 * 45], drawn(scrubbed)) == 0.0f);
        CHECK(scrubbed.composition()->entityWorld().find("warden")->directorGoal().active);
    }
    SECTION("control: without the goal the warden is elsewhere") {
        app::Engine plain(app::EngineMode::Offline);
        build(plain, false);
        for (long long g = 0; g <= 60 * 45 + 1; ++g) {
            frame(plain, g);
        }
        CHECK(worst(at[60 * 45], drawn(plain)) > 0.5f);
    }
}

TEST_CASE("the clip readout reports what a cue would reproduce", "[motion][goal][benchmark]") {
    if (!present()) {
        SKIP("alien assets are not present");
    }
    app::Engine engine(app::EngineMode::Offline);
    build(engine, false);
    for (long long f = 0; f <= 60 * 12; ++f) {
        frame(engine, f);
    }
    const double now = 12.0;
    const auto r = engine.composition()->clipReadout("warden", now);
    REQUIRE(r.found);
    CHECK_FALSE(r.state.empty());
    const scene::ClipSemanticsTable* table = engine.composition()->clipSemanticsFor("warden");
    REQUIRE(table != nullptr);
    const scene::ClipSemantics* clip = table->find(r.state);
    REQUIRE(clip != nullptr);
    CHECK(r.clipSeconds >= 0.0f);
    CHECK(r.clipSeconds <= clip->length + 1e-4f);
    CHECK(r.loops == clip->loops); // autonomous clips play as they measure (ADR-827)
    // A cue at the readout's phase origin and speed reproduces the same clip second.
    const float reproduced = static_cast<float>((now - r.startSeconds) * static_cast<double>(r.speed));
    const float wrapped = clip->loops && clip->length > 0.0f ? std::fmod(reproduced, clip->length) : std::min(reproduced, clip->length);
    CHECK(std::abs(wrapped - r.clipSeconds) < 1e-3f);
    CHECK_FALSE(engine.composition()->clipReadout("no-such-node", now).found);
}

// ADR-832 (Phase D §26): the two live trigger kinds that had no producer. An interaction completing
// posts `InteractionComplete` named "prop.verb"; a field's edge posts `VolumeEnter`/`VolumeExit`
// named after the field. Each must fire at the simulation second the world recorded it.
TEST_CASE("an interaction and a volume edge fire their live triggers at the second they happen",
          "[motion][goal][adr832][benchmark]") {
    if (!present()) {
        SKIP("alien assets are not present");
    }
    app::Engine engine(app::EngineMode::Offline);
    build(engine, true, 60.0,
          {onWarden("inspected", seq::TriggerKind::InteractionComplete, "mushroom-2.inspect"),
           onWarden("entered", seq::TriggerKind::VolumeEnter, "by-the-mushroom"),
           onWarden("left", seq::TriggerKind::VolumeExit, "by-the-mushroom")});
    const entity::Entity* mushroom = engine.composition()->entityWorld().find("mushroom-2");
    REQUIRE(mushroom != nullptr);
    entity::FieldDesc field;
    field.name = "by-the-mushroom";
    field.volume.shape = entity::VolumeShape::Sphere;
    field.volume.center = mushroom->state().position();
    field.volume.radius = 3.0f;
    field.scaleReactions = false; // an edge detector only: the demo's reactions are not its business
    REQUIRE(engine.composition()->setFields({field}).has_value());

    double inspectedAt = -1.0;
    engine.composition()->entityWorld().setActionListener([&](const entity::ActionEvent& e) {
        if (e.entity == "warden" && e.interaction == "mushroom-2.inspect" &&
            e.result == entity::ActionResult::Completed && inspectedAt < 0.0) {
            inspectedAt = e.time;
        }
    });
    std::map<std::string, double> fired;
    double enteredAt = -1.0;
    double leftAt = -1.0;
    for (long long f = 0; f <= 60 * 100; ++f) {
        frame(engine, f);
        const entity::EntityWorld& world = engine.composition()->entityWorld();
        for (const entity::TriggerEvent& t : world.triggerEvents()) {
            if (world.entities()[t.entity]->name() != "warden") {
                continue;
            }
            double& at = t.enter ? enteredAt : leftAt;
            if (at < 0.0) {
                at = t.time;
            }
        }
        for (const seq::FiredEvent& e : engine.firedEvents()) {
            fired.try_emplace(engine.sequence().events[e.eventIndex].id, e.timeSeconds);
        }
    }
    const auto when = [&](const char* id) { return fired.count(id) != 0 ? fired.at(id) : -1.0; };
    INFO("inspected " << inspectedAt << " (fired " << when("inspected") << "); entered " << enteredAt << " (fired "
                      << when("entered") << "); left " << leftAt << " (fired " << when("left") << ")");
    REQUIRE(inspectedAt >= 20.0); // the goal sends the warden to the mushroom, so all three happen
    REQUIRE(enteredAt >= 20.0);
    REQUIRE(leftAt > enteredAt);
    CHECK(when("inspected") == inspectedAt);
    CHECK(when("entered") == enteredAt);
    CHECK(when("left") == leftAt);
}
