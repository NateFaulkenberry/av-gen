// Slice 4: goal-mode performances (ADR-763 on the Motion lead's ADR-828). A goal is compiled to a
// CharacterGoal sequence event; it is live, so it compiles only in a live-tier plan, and a cue on its
// events waits for a recording. Played checks run from zero with the cull lifted and no audio, and
// a scrub is compared with the play (ADR-800's method).

#include "app/directing_apply.hpp"
#include "app/directing_context.hpp"
#include "app/directing_record.hpp"
#include "app/engine.hpp"
#include "directing/compiler.hpp"
#include "directing/plan.hpp"
#include "entity/entity.hpp"
#include "scene/composition.hpp"
#include "ui/edit_history.hpp"

#include "support/project_assets.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>

using namespace avgen;
using namespace avgen::directing;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

void load(app::Engine& engine) {
    REQUIRE(engine.loadProject(fs::path(AVGEN_SOURCE_DIR) / "examples/world/glowmere-valley-2-multicam.json"));
    scene::DetailLimits limits = engine.detailLimits();
    limits.entityDistanceCull = false;
    engine.setDetailLimits(limits);
    REQUIRE(engine.setAudioClips({}).has_value());
}

json goalPlan() {
    return json::parse(R"({
      "schemaVersion": 1, "id": "rook-wanders", "title": "Rook wanders to the Lantern", "tier": "goal",
      "subjects": [{"alias": "rook", "text": "Rook"}, {"alias": "lantern", "text": "the Lantern hero", "hint": "hero"}],
      "performances": [{"key": "wander", "subject": "rook", "mode": "goal", "beats": [
        {"action": "go_to", "target": "lantern", "at": {"seconds": 5}, "emits": "rook.reaches_lantern"}]}]
    })");
}

Plan planFrom(const json& doc) {
    PlanParse parsed = parsePlan(doc);
    for (const Issue& i : parsed.issues) {
        INFO(i.toJson().dump());
    }
    REQUIRE(parsed.plan);
    return *parsed.plan;
}

bool has(const Validation& v, IssueCode code, std::string_view item, Severity severity = Severity::Error) {
    return std::any_of(v.issues.begin(), v.issues.end(),
                       [&](const Issue& i) { return i.code == code && i.item == item && i.severity == severity; });
}

void frame(app::Engine& engine, std::uint64_t f) {
    engine.update(FrameTime{static_cast<double>(f) / 60.0, f == 0 ? 0.0 : 1.0 / 60.0, f});
}

} // namespace

TEST_CASE("a goal compiles to a CharacterGoal event, only in a live plan", "[directing][goal]") {
    testsupport::skipUnlessGlowmereBenchmarkAssetsPresent();
    app::Engine engine(app::EngineMode::Offline);
    load(engine);
    const SceneFacts facts = app::sceneFactsFor(engine);
    REQUIRE(facts.capabilities.character("rook") != nullptr);
    CHECK(facts.capabilities.character("rook")->goalSlot); // ADR-828 gave the aliens an empty slot

    const Compilation c = compilePlan(planFrom(goalPlan()), facts);
    INFO(c.diffText());
    CHECK_FALSE(c.validation.isBlocked("wander"));
    const auto goal = std::find_if(c.staged.sequence.events.begin(), c.staged.sequence.events.end(),
                                   [](const seq::SequenceEvent& e) { return e.what.kind == seq::EventActionKind::CharacterGoal; });
    REQUIRE(goal != c.staged.sequence.events.end());
    CHECK(goal->when.kind == seq::TriggerKind::Time);
    CHECK(goal->when.timeSeconds == 5.0);
    CHECK(goal->what.target == "rook");
    CHECK(goal->what.value == "lantern-cap");
    CHECK(goal->what.goal.intent == "investigate");
    // Its event, heard where the sequence can hear it -- live, and named.
    const auto heard = std::find_if(c.staged.sequence.events.begin(), c.staged.sequence.events.end(),
                                    [](const seq::SequenceEvent& e) { return e.when.kind == seq::TriggerKind::ActionComplete; });
    REQUIRE(heard != c.staged.sequence.events.end());
    CHECK(heard->when.name == "goal.arrived");
    CHECK(heard->when.subject == "rook");
    CHECK(heard->what.target == "rook.reaches_lantern");
    CHECK(c.diffText().find("live: how and when rook gets there is the character's") != std::string::npos);

    SECTION("a baked plan refuses it: live output is never labelled reproducible (§1.3)") {
        json doc = goalPlan();
        doc["tier"] = "baked";
        CHECK(has(compilePlan(planFrom(doc), facts).validation, IssueCode::NonDeterministic, "wander"));
    }
    SECTION("what a goal cannot be") {
        json doc = goalPlan();
        doc["performances"][0]["beats"].push_back(json{{"action", "go_to"}, {"target", "lantern"}}); // no time
        doc["performances"][0]["beats"].push_back(json{{"action", "dance"}, {"target", "lantern"}, {"at", "0:30"}});
        doc["performances"][0]["beats"].push_back(
            json{{"action", "go_to"}, {"target", "lantern"}, {"at", "0:40"}, {"moment", "peak"}});
        const Validation v = compilePlan(planFrom(doc), facts).validation;
        CHECK(has(v, IssueCode::SchemaInvalid, "wander"));    // the untimed goal, the bad moment
        CHECK(has(v, IssueCode::Unsupported, "wander"));      // "dance" is not a goal
    }
    SECTION("a cue on a live event waits for a recording, and so does slow motion") {
        json doc = goalPlan();
        doc["cues"] = json::array({json{{"key", "flash"}, {"parameter", "scene/brightness"},
                                        {"on", "rook.reaches_lantern"}, {"value", 1.3}, {"holdSeconds", 0.3}}});
        doc["retimes"] = json::array({json{{"key", "slow"}, {"performance", "wander"}, {"from", "0:06"},
                                           {"until", "0:08"}, {"factor", 0.5}}});
        const Validation v = compilePlan(planFrom(doc), facts).validation;
        CHECK(has(v, IssueCode::NonDeterministic, "flash", Severity::Warning));
        CHECK(has(v, IssueCode::Unsupported, "flash"));
        CHECK(has(v, IssueCode::Unsupported, "slow"));
        CHECK_FALSE(v.isBlocked("wander"));
    }
    SECTION("a place with no walking route is a warning: the goal may never be reached") {
        json doc = goalPlan();
        doc["subjects"][1] = json{{"alias", "lantern"}, {"text", "the Umbra hero mushroom"}, {"hint", "hero"}};
        const Validation v = compilePlan(planFrom(doc), facts).validation;
        CHECK(has(v, IssueCode::SpatialInfeasible, "wander", Severity::Warning)); // umbra-cap: unreachable
        CHECK_FALSE(v.isBlocked("wander"));
        CHECK_FALSE(has(compilePlan(planFrom(goalPlan()), facts).validation, IssueCode::SpatialInfeasible, "wander",
                        Severity::Warning)); // the Lantern: reachable
    }
    SECTION("a character with no goal slot is told so") {
        json doc = goalPlan();
        doc["subjects"][0] = json{{"alias", "rook"}, {"text", "cow-1"}};
        const Compilation cow = compilePlan(planFrom(doc), facts);
        INFO(cow.diffText());
        if (const CharacterCard* card = facts.capabilities.character(cow.plan.subject("rook")->id); card != nullptr &&
                                                                                                  !card->goalSlot) {
            CHECK(has(cow.validation, IssueCode::CapabilityUnavailable, "wander"));
        } else {
            WARN("no slotless character resolved from 'cow-1' on this scene; the check did not run");
        }
    }
}

TEST_CASE("a goal, played: Rook heads for the Lantern, says when he gets there, and a scrub lands where the play did",
          "[directing][goal][benchmark]") {
    testsupport::skipUnlessGlowmereBenchmarkAssetsPresent();
    app::Engine played(app::EngineMode::Offline);
    load(played);
    const Compilation c = compilePlan(planFrom(goalPlan()), app::sceneFactsFor(played));
    REQUIRE_FALSE(c.validation.hasErrors());
    ui::EditHistory history;
    REQUIRE(app::applyCompilation(played, history, c));
    const entity::Entity* rook = played.composition()->entityWorld().find("rook");
    REQUIRE(rook != nullptr);
    const SceneFacts playedFacts = app::sceneFactsFor(played); // held: `place` points into it
    const Place* lantern = playedFacts.place("lantern-cap");
    REQUIRE(lantern != nullptr);
    const auto distance = [&](glm::vec3 p) {
        return glm::length(glm::vec2(p.x - lantern->position.x, p.z - lantern->position.z));
    };

    // The same world without the goal, for comparison.
    app::Engine idle(app::EngineMode::Offline);
    load(idle);
    const entity::Entity* idleRook = idle.composition()->entityWorld().find("rook");

    double arrived = -1.0;
    float arrivedDistance = 1e9f; // the goal's Rook, at the moment he says he arrived
    float idleThen = 0.0f;        // Rook without the goal, at that same moment
    constexpr std::uint64_t kEnd = 60 * 60;
    float closest = 1e9f;
    for (std::uint64_t f = 0; f <= kEnd; ++f) {
        frame(played, f);
        frame(idle, f);
        closest = std::min(closest, distance(rook->state().position()));
        for (const seq::FiredEvent& fired : played.firedEvents()) {
            const auto& events = played.sequence().events;
            if (fired.eventIndex < events.size() && events[fired.eventIndex].id == "rook-wanders.wander.rook.reaches_lantern" &&
                arrived < 0.0) {
                arrived = fired.timeSeconds;
                arrivedDistance = distance(rook->state().position());
                idleThen = distance(idleRook->state().position());
            }
        }
    }
    const float atEnd = distance(rook->state().position());
    const float idleAtEnd = distance(idleRook->state().position());
    INFO("rook ends " << atEnd << " m from the Lantern (closest " << closest << "); without the goal " << idleAtEnd
                      << " m; arrival event at " << arrived << " s, " << arrivedDistance << " m away (without the goal, "
                      << idleThen << " m)");
    CHECK(arrivedDistance < lantern->radius + 4.0f); // he is there when he says so (a 2 m stand-off)
    CHECK(idleThen > arrivedDistance + 10.0f);        // ...and on his own he was nowhere near by then
    CHECK(arrived > 5.0);                     // the character said so, after the goal was given
    // A scrub into the goal's window lands where the play did (ADR-800, ADR-824).
    const double probe = arrived > 0.0 ? std::max(6.0, arrived - 2.0) : 30.0;
    app::Engine scrubbed(app::EngineMode::Offline);
    load(scrubbed);
    ui::EditHistory h2;
    REQUIRE(app::applyCompilation(scrubbed, h2, compilePlan(planFrom(goalPlan()), app::sceneFactsFor(scrubbed))));
    app::Engine replay(app::EngineMode::Offline);
    load(replay);
    ui::EditHistory h3;
    REQUIRE(app::applyCompilation(replay, h3, compilePlan(planFrom(goalPlan()), app::sceneFactsFor(replay))));
    const auto target = static_cast<std::uint64_t>(std::llround(probe * 60.0));
    for (std::uint64_t f = 0; f <= target + 1; ++f) {
        frame(replay, f);
    }
    scrubbed.seekSeconds(static_cast<double>(target) / 60.0);
    frame(scrubbed, target + 1);
    const glm::vec3 a = replay.composition()->entityWorld().find("rook")->visualPosition();
    const glm::vec3 b = scrubbed.composition()->entityWorld().find("rook")->visualPosition();
    INFO("scrub to " << probe << " s: " << glm::length(a - b) << " m from the play");
    CHECK(glm::length(a - b) == 0.0f);
}

TEST_CASE("a goal is baked by recording it: a scripted actor, its event at the recorded time, replayed exactly",
          "[directing][goal][record][benchmark]") {
    testsupport::skipUnlessGlowmereBenchmarkAssetsPresent();
    // ADR-763's only route from live to baked. The recording is played back on its own scratch copy
    // and must put the body on every key, and a scrub into it must land where that play did.
    app::Engine live(app::EngineMode::Offline);
    load(live);
    json doc = goalPlan();
    doc["cues"] = json::array({json{{"key", "flash"}, {"parameter", "scene/brightness"}, {"on", "rook.reaches_lantern"},
                                    {"value", 1.3}, {"rampSeconds", 0.05}, {"holdSeconds", 0.3}}});
    const Compilation c = compilePlan(planFrom(doc), app::sceneFactsFor(live));
    INFO(c.diffText());
    CHECK(c.validation.isBlocked("flash")); // live: waits for the recording
    CHECK_FALSE(c.validation.isBlocked("wander"));
    const std::string before = live.sequence().toJson().dump();

    app::RecordOptions options;
    options.maxSeconds = 20.0;
    auto report = app::recordLivePerformances(live, c, options);
    REQUIRE(report.has_value());
    for (const std::string& n : report->notes) {
        UNSCOPED_INFO(n);
    }
    INFO("recorded in " << report->recordMs << " ms; check " << report->checkMs << " ms; played back "
                        << report->replayWorstMetres << " m, scrubbed " << report->scrubWorstMetres << " m");
    CHECK(live.sequence().toJson().dump() == before); // the person's project: untouched

    const Plan& recorded = report->plan;
    CHECK(recorded.tier == Tier::Baked); // every live performance is recorded
    const PlanPerformance& wander = recorded.performances[0];
    REQUIRE(wander.recording.has_value());
    REQUIRE(wander.recording->events.size() == 1);
    CHECK(wander.recording->events[0].first == "rook.reaches_lantern");
    const double arrival = wander.recording->events[0].second;
    CHECK(arrival > 5.0);
    CHECK(report->replayWorstMetres < 0.01);  // the body is on the recording
    CHECK(report->scrubWorstMetres == 0.0);   // and a scrub lands where the play did

    // The plan survives its own document, and compiles baked: the actor, a marker at the recorded
    // arrival, and the cue on it -- no longer blocked.
    const Plan reread = planFrom(recorded.toJson());
    CHECK(reread.performances[0].recording == wander.recording);
    Plan fresh = reread;
    fresh.produced.clear();
    const Compilation baked = compilePlan(fresh, app::sceneFactsFor(live));
    INFO(baked.diffText());
    CHECK_FALSE(baked.validation.hasErrors());
    const auto actor = std::find_if(baked.staged.sequence.actors.begin(), baked.staged.sequence.actors.end(),
                                    [](const seq::Actor& a) { return a.id == "rook"; });
    REQUIRE(actor != baked.staged.sequence.actors.end());
    const auto marker = std::find_if(baked.staged.sequence.markers.begin(), baked.staged.sequence.markers.end(),
                                     [](const seq::Marker& m) { return m.name == "rook.reaches_lantern"; });
    REQUIRE(marker != baked.staged.sequence.markers.end());
    CHECK(marker->timeSeconds == arrival);
    const auto flash = std::find_if(baked.staged.sequence.events.begin(), baked.staged.sequence.events.end(),
                                    [](const seq::SequenceEvent& e) { return e.id == "rook-wanders.flash"; });
    REQUIRE(flash != baked.staged.sequence.events.end());
    CHECK(flash->when.kind == seq::TriggerKind::Time);
    CHECK(flash->when.timeSeconds == arrival);
    CHECK(std::none_of(baked.staged.sequence.events.begin(), baked.staged.sequence.events.end(),
                       [](const seq::SequenceEvent& e) { return e.what.kind == seq::EventActionKind::CharacterGoal; }));
}
