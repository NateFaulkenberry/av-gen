// Slice 3: jumps, landings and slow motion, compiled onto the Motion lead's M2-M5 (ADR-761).
//
// Played results are checked by PLAYING from t = 0, against numbers the engine itself computed (the
// arc, its peak time, the retime map), never against constants of this file's own.

#include "app/directing_apply.hpp"
#include "app/directing_context.hpp"
#include "app/engine.hpp"
#include "directing/compiler.hpp"
#include "directing/performance.hpp"
#include "directing/plan.hpp"
#include "entity/entity.hpp"
#include "seq/jump.hpp"
#include "seq/retime.hpp"
#include "support/project_assets.hpp"
#include "ui/edit_history.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>

using namespace avgen;
using namespace avgen::directing;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

std::unique_ptr<app::Engine> benchmark() {
    auto engine = std::make_unique<app::Engine>(app::EngineMode::Offline);
    REQUIRE(engine->loadProject(fs::path(AVGEN_SOURCE_DIR) / "examples/world/glowmere-valley-2-multicam.json"));
    return engine;
}

Plan planFrom(const json& doc) {
    PlanParse parsed = parsePlan(doc);
    for (const Issue& i : parsed.issues) {
        INFO(i.toJson().dump());
    }
    REQUIRE(parsed.plan);
    return *parsed.plan;
}

json hopPlan() {
    return json::parse(R"({
      "schemaVersion": 1, "id": "rook-hops", "title": "Rook hops", "tier": "baked",
      "subjects": [{"alias": "rook", "text": "Rook"}, {"alias": "umbra", "text": "the Umbra hero", "hint": "hero"}],
      "shots": [{"key": "shot", "name": "rook-hops", "start": "1:30", "durationSeconds": 8, "subject": "rook",
                 "locked": true, "camera": [{"move": "low_angle"}, {"move": "chase", "distanceMetres": 3}]}],
      "performances": [{"key": "hop", "subject": "rook", "mode": "scripted", "beats": [
        {"action": "run_past", "target": "umbra"},
        {"action": "run", "seconds": 0.5},
        {"action": "jump", "emits": "rook.jump_peak"},
        {"action": "land", "emits": "rook.landed"},
        {"action": "run", "seconds": 1.0}]}]
    })");
}

const seq::Actor* actorOf(const Compilation& c, std::string_view id) {
    for (const seq::Actor& a : c.staged.sequence.actors) {
        if (a.id == id) {
            return &a;
        }
    }
    return nullptr;
}

double markerTime(const Compilation& c, std::string_view name) {
    for (const seq::Marker& m : c.staged.sequence.markers) {
        if (m.name == name) {
            return m.timeSeconds;
        }
    }
    FAIL("no marker " << name);
    return 0.0;
}

void playTo(app::Engine& engine, std::uint64_t& frame, double seconds) {
    const auto last = static_cast<std::uint64_t>(std::llround(seconds * 60.0));
    for (; frame <= last; ++frame) {
        engine.update(FrameTime{static_cast<double>(frame) / 60.0, frame == 0 ? 0.0 : 1.0 / 60.0, frame});
    }
}

} // namespace

TEST_CASE("Rook's card: his jump is data, and his jump clip is measured", "[directing][airborne][capabilities]") {
    testsupport::skipUnlessGlowmereBenchmarkAssetsPresent();
    auto engine = benchmark();
    const SceneFacts facts = app::sceneFactsFor(*engine);
    const CharacterCard* rook = facts.capabilities.character("rook");
    REQUIRE(rook != nullptr);
    // The owner's ruling: Rook keeps the engine default; nobody chose a bigger jump for him.
    CHECK(rook->jump.source == "default");
    CHECK(rook->jump.apex == Catch::Approx(1.1f));
    CHECK(rook->jump.maxApex == Catch::Approx(1.1f));
    const ActivityCapability* jump = rook->activity("jump");
    REQUIRE(jump != nullptr);
    REQUIRE(jump->semantics.has_value());
    CHECK(jump->semantics->event("takeoff") != nullptr);
    CHECK(jump->semantics->event("peak") != nullptr);
    CHECK(jump->semantics->event("touchdown") != nullptr);
    CHECK(jump->loops); // measured: it loops, which is why a compiled jump says `once`
    CHECK(facts.groundAt); // the terrain a jump is placed and checked on
    const json card = rook->toJson();
    CHECK(card["jump"]["maxApex"].get<float>() == Catch::Approx(1.1f));
}

TEST_CASE("the owner's acceptance case: Rook cannot leap Umbra, and says what would work",
          "[directing][airborne][benchmark]") {
    auto engine = benchmark();
    json doc = hopPlan();
    doc["performances"][0]["beats"] = json::parse(R"([
        {"action": "run_to", "target": "umbra"},
        {"action": "jump", "target": "umbra", "emits": "rook.jump_peak"}])");
    const Compilation c = compilePlan(planFrom(doc), app::sceneFactsFor(*engine));
    INFO(c.diffText());
    CHECK(c.validation.isBlocked("hop"));
    const Issue* spatial = nullptr;
    for (const Issue& i : c.validation.issues) {
        CHECK(i.code != IssueCode::Unsupported); // jumps compile now; this one is refused on geometry
        if (i.code == IssueCode::SpatialInfeasible && spatial == nullptr) {
            spatial = &i;
        }
    }
    REQUIRE(spatial != nullptr);
    CHECK(spatial->details["required"].get<double>() == Catch::Approx(5.75).margin(1e-4));
    CHECK(spatial->details["apex"].get<double>() == Catch::Approx(1.1).margin(1e-4));
    CHECK(std::find(spatial->suggestions.begin(), spatial->suggestions.end(), "use a character with a larger jump") !=
          spatial->suggestions.end());
    CHECK(std::any_of(spatial->suggestions.begin(), spatial->suggestions.end(),
                      [](const std::string& s) { return s.find("different path") != std::string::npos; }));
}

TEST_CASE("a hop compiles to the engine's arc, its clip once, and its peak as a marker; played, the body flies it",
          "[directing][airborne][performance][benchmark]") {
    testsupport::skipUnlessGlowmereBenchmarkAssetsPresent();
    auto engine = benchmark();
    const SceneFacts facts = app::sceneFactsFor(*engine);
    const Compilation c = compilePlan(planFrom(hopPlan()), facts);
    INFO(c.diffText());
    REQUIRE_FALSE(c.validation.hasErrors());
    const seq::Actor* actor = actorOf(c, "rook");
    REQUIRE(actor != nullptr);
    REQUIRE(actor->airborne.size() == 1);
    const auto [takeoff, touchdown] = actor->airborne[0];
    const double peak = markerTime(c, "rook.jump_peak");
    CHECK(peak > takeoff);
    CHECK(peak < touchdown);
    CHECK(markerTime(c, "rook.landed") == Catch::Approx(touchdown).margin(1e-9));
    // The jump clip, once, fitted: its measured takeoff lands on the arc's take-off.
    const auto cue = std::find_if(actor->clips.begin(), actor->clips.end(),
                                  [](const seq::ClipCue& q) { return q.playback == seq::ClipPlayback::Once && q.clip == "Jumping"; });
    REQUIRE(cue != actor->clips.end());
    const scene::ClipSemantics& jumping = *facts.capabilities.character("rook")->activity("jump")->semantics;
    CHECK(cue->timeSeconds + (jumping.event("takeoff")->seconds / cue->speed) == Catch::Approx(takeoff).margin(1e-6));
    // The peak is the arc's: 1.1 m over the take-off ground.
    const glm::vec3 launch = actor->positionAt(takeoff);
    CHECK(actor->positionAt(peak).y - launch.y == Catch::Approx(1.1f).margin(0.01f));
    CHECK(std::abs(launch.y - facts.groundAt(launch.x, launch.z)) < 1e-3f);

    ui::EditHistory history;
    REQUIRE(app::applyCompilation(*engine, history, c));
    const entity::Entity* rook = engine->composition()->entityWorld().find("rook");
    REQUIRE(rook != nullptr);
    std::uint64_t frame = 0;
    playTo(*engine, frame, peak);
    const glm::vec3 body = rook->state().position();
    const glm::vec3 want = actor->positionAt(static_cast<double>(frame - 1) / 60.0);
    INFO("body " << body.y << " want " << want.y);
    CHECK(std::abs(body.y - want.y) < 0.02f); // mid-air, on the arc -- not flattened onto the terrain
    CHECK(body.y - facts.groundAt(body.x, body.z) > 0.8f);
    playTo(*engine, frame, touchdown + 0.25);
    const glm::vec3 landed = rook->state().position();
    CHECK(std::abs(landed.y - facts.groundAt(landed.x, landed.z)) < 0.02f); // back on the ground
    REQUIRE(history.undo(*engine).ok());
}

TEST_CASE("slow motion stretches the performance around the jump, and its peak marker moves with it",
          "[directing][airborne][retime]") {
    testsupport::skipUnlessGlowmereBenchmarkAssetsPresent();
    auto engine = benchmark();
    const SceneFacts facts = app::sceneFactsFor(*engine);
    const Compilation plain = compilePlan(planFrom(hopPlan()), facts);
    REQUIRE_FALSE(plain.validation.hasErrors());
    REQUIRE(actorOf(plain, "rook") != nullptr);
    REQUIRE(actorOf(plain, "rook")->airborne.size() == 1);
    const auto [takeoff, touchdown] = actorOf(plain, "rook")->airborne[0];
    const double peak = markerTime(plain, "rook.jump_peak");

    json doc = hopPlan();
    const double a = takeoff - 0.1;
    const double b = touchdown + 0.1;
    doc["retimes"] = json::array({json{{"key", "slow"}, {"performance", "hop"}, {"from", json{{"seconds", a}}},
                                       {"until", json{{"seconds", b}}}, {"factor", 0.35}}});
    const Compilation slow = compilePlan(planFrom(doc), facts);
    INFO(slow.diffText());
    REQUIRE_FALSE(slow.validation.hasErrors());
    const seq::Actor* actor = actorOf(slow, "rook");
    REQUIRE(actor != nullptr);
    REQUIRE(actor->airborne.size() == 1);
    CHECK(markerTime(slow, "rook.jump_peak") == Catch::Approx(seq::retimeMap(peak, a, b, 0.35f)).margin(1e-6));
    CHECK(actor->airborne[0].first == Catch::Approx(seq::retimeMap(takeoff, a, b, 0.35f)).margin(1e-6));
    CHECK(actor->airborne[0].second - actor->airborne[0].first ==
          Catch::Approx((touchdown - takeoff) / 0.35).margin(1e-6));
    // The body is at the arc's top at the stretched peak.
    const glm::vec3 launch = actor->positionAt(actor->airborne[0].first);
    CHECK(actor->positionAt(markerTime(slow, "rook.jump_peak")).y - launch.y == Catch::Approx(1.1f).margin(0.01f));

    // A window that misses the performance, and an overlap, are refused.
    doc["retimes"].push_back(json{{"key", "slow2"}, {"performance", "hop"}, {"from", json{{"seconds", b - 0.05}}},
                                  {"until", json{{"seconds", b + 0.5}}}, {"factor", 0.5}});
    doc["retimes"].push_back(json{{"key", "never"}, {"performance", "hop"}, {"from", json{{"seconds", 10.0}}},
                                  {"until", json{{"seconds", 11.0}}}, {"factor", 0.5}});
    const Compilation bad = compilePlan(planFrom(doc), facts);
    CHECK(bad.validation.isBlocked("slow2"));
    CHECK(bad.validation.isBlocked("never"));
    CHECK_FALSE(bad.validation.isBlocked("slow"));
}

TEST_CASE("over an obstacle the arc is the lowest that clears it, from the engine's own minimum",
          "[directing][airborne]") {
    // Synthetic facts: flat ground, a 1 m post, a character that can leap 3 m.
    SceneFacts facts;
    CharacterCard card;
    card.subject = "hopper";
    card.node = "hopper";
    card.runSpeed = 6.0f;
    card.walkSpeed = 1.5f;
    card.rigLoaded = true;
    for (const char* name : {"run", "walk", "jump", "land"}) {
        ActivityCapability cap;
        cap.activity = name;
        cap.available = true;
        card.activities.push_back(cap);
    }
    card.jump = JumpEnvelope{1.1f, 3.0f, 18.0f, 6.0f, 0.3f, "jump"};
    facts.capabilities = CapabilityRegistry::withCharacters({card});
    facts.characters.push_back(CharacterMark{"hopper", "hopper", glm::vec3(0.0f)});
    facts.places.push_back(Place{SubjectKind::Node, "post", glm::vec3(0.0f, 0.0f, 10.0f), 0.3f, 1.0f, 0.0f});
    facts.groundAt = [](float, float) { return 0.0f; };

    Plan plan;
    plan.id = "p";
    plan.subjects = {Subject{"hopper", "hopper", SubjectKind::Entity, SubjectKind::Entity, "hopper"},
                     Subject{"post", "post", SubjectKind::Node, SubjectKind::Node, "post"}};
    PlanPerformance perf;
    perf.key = "leap";
    perf.subject = "hopper";
    perf.beats.resize(2);
    perf.beats[0].action = "walk_to";
    perf.beats[0].target = "post";
    perf.beats[0].at = TimeRef{};
    perf.beats[0].at->seconds = 1.0;
    perf.beats[1].action = "jump";
    perf.beats[1].target = "post";
    plan.performances.push_back(perf);
    PlanTimes times;
    times.seconds.emplace_back("/performances/0/beats/0/at", 1.0);
    const CompiledPerformance cp = compilePerformance(plan, 0, facts, times);
    REQUIRE(cp.jumps.size() == 1);
    const JumpOutcome& j = cp.jumps[0];
    CHECK(j.feasible());
    CHECK(j.minimumApex > 1.25f); // over a 1 m post with 0.25 m clearance: higher at the post's edges
    CHECK(j.apex == Catch::Approx(j.minimumApex));
    // Sampled, the arc passes over the post's whole footprint with the clearance.
    for (float s = 0.0f; s <= j.arc->duration; s += 0.001f) {
        const glm::vec3 q = j.arc->at(s);
        if (glm::length(glm::vec2(q.x, q.z - 10.0f)) <= 0.3f) {
            CHECK(q.y >= 1.25f - 1e-3f);
        }
    }
}
