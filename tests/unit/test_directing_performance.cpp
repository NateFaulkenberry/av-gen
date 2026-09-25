// Scripted performances compiled to `seq::Actor` (ADR-759, director-system-progress.md Slice 2;
// spec §23-§26, §30, §52).
//
// Everything is checked by PLAYING the applied result from t = 0 (never by a seek: seek == play is an
// open engine defect this code neither depends on nor works around), against numbers the compiler
// itself computed: the mark, the path, the event times.

#include "app/directing_apply.hpp"
#include "app/directing_context.hpp"
#include "app/engine.hpp"
#include "directing/compiler.hpp"
#include "directing/performance.hpp"
#include "directing/plan.hpp"
#include "entity/entity.hpp"
#include "support/project_round_trip.hpp"
#include "ui/edit_history.hpp"
#include "world/terrain_query.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <numbers>

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

json runPastPlan() {
    return json::parse(R"({
      "schemaVersion": 1, "id": "rook-passes", "title": "Rook runs past Umbra", "tier": "baked",
      "subjects": [{"alias": "rook", "text": "Rook"}, {"alias": "umbra", "text": "the Umbra hero", "hint": "hero"}],
      "shots": [{"key": "shot", "name": "rook-passes", "start": "1:30", "durationSeconds": 8, "subject": "rook",
                 "locked": true, "camera": [{"move": "low_angle"}, {"move": "chase", "distanceMetres": 3}]}],
      "performances": [{"key": "run", "subject": "rook", "mode": "scripted", "beats": [
        {"action": "run_past", "target": "umbra", "emits": "rook.passes_umbra"},
        {"action": "hold", "seconds": 1.0},
        {"action": "look_at", "target": "umbra", "seconds": 1.5, "emits": "rook.looks"}]}],
      "cues": [{"key": "flash", "parameter": "scene/brightness", "on": "rook.passes_umbra", "value": 1.3,
                "rampSeconds": 0.1, "holdSeconds": 0.3}]
    })");
}

void playTo(app::Engine& engine, double seconds) {
    const auto frames = static_cast<std::uint64_t>(std::llround(seconds * 60.0));
    for (std::uint64_t i = 0; i <= frames; ++i) {
        engine.update(FrameTime{static_cast<double>(i) / 60.0, i == 0 ? 0.0 : 1.0 / 60.0, i});
    }
}

} // namespace

TEST_CASE("Rook runs past Umbra: the performance, its events and its cue, played from zero",
          "[directing][performance][benchmark]") {
    auto engine = benchmark();
    const SceneFacts facts = app::sceneFactsFor(*engine);
    const Compilation c = compilePlan(planFrom(runPastPlan()), facts);
    INFO(c.diffText());
    REQUIRE_FALSE(c.validation.hasErrors());
    const seq::Actor* actor = nullptr;
    for (const seq::Actor& a : c.staged.sequence.actors) {
        if (a.id == "rook") {
            actor = &a;
        }
    }
    REQUIRE(actor != nullptr);
    CHECK(actor->clips.empty()); // the gait plays the path; no clip cue is needed (ADR-759)
    CHECK(actor->keys.front().timeSeconds == 90.0); // starts at the shot's cut

    // The event is the compiler's closest approach -- checked against the actor itself, sampled.
    const auto marker = std::find_if(c.staged.sequence.markers.begin(), c.staged.sequence.markers.end(),
                                     [](const seq::Marker& m) { return m.name == "rook.passes_umbra"; });
    REQUIRE(marker != c.staged.sequence.markers.end());
    const Place* umbra = facts.place("umbra-cap");
    REQUIRE(umbra != nullptr);
    double closestT = 0.0;
    float closest = std::numeric_limits<float>::infinity();
    for (double t = actor->keys.front().timeSeconds; t <= actor->keys.back().timeSeconds; t += 0.005) {
        const glm::vec3 p = actor->positionAt(t);
        const float d = glm::length(glm::vec2(p.x - umbra->position.x, p.z - umbra->position.z));
        if (d < closest) {
            closest = d;
            closestT = t;
        }
    }
    CHECK(std::abs(closestT - marker->timeSeconds) < 0.01);
    CHECK(closest == Catch::Approx(umbra->radius + 1.5f).margin(0.05f)); // passes beside it, not through it
    // The cue waits on that event: a baked Time event at the computed time.
    const auto flash = std::find_if(c.staged.sequence.events.begin(), c.staged.sequence.events.end(),
                                    [](const seq::SequenceEvent& e) { return e.id == "rook-passes.flash"; });
    REQUIRE(flash != c.staged.sequence.events.end());
    CHECK(flash->when.kind == seq::TriggerKind::Time);
    CHECK(flash->when.timeSeconds == marker->timeSeconds);

    ui::EditHistory history;
    REQUIRE(app::applyCompilation(*engine, history, c));
    REQUIRE(engine->composition()->performers().size() == 1);
    const entity::Entity* rook = engine->composition()->entityWorld().find("rook");
    REQUIRE(rook != nullptr);
    const world::TerrainQuery ground = engine->composition()->terrainQuery();

    playTo(*engine, 91.0); // one second into the run
    glm::vec3 at = rook->state().position();
    glm::vec3 want = actor->positionAt(91.0);
    CHECK(glm::length(glm::vec2(at.x - want.x, at.z - want.z)) < 1e-3f);
    CHECK(std::abs(at.y - ground.surfaceAt(glm::vec2(at.x, at.z))) < 1e-3f);
    CHECK(rook->locomotion().activity == entity::Activity::Run);

    // During the look: facing Umbra, standing still.
    const double lookMid = actor->keys.back().timeSeconds - 0.5;
    for (std::uint64_t i = 91 * 60 + 1; i <= static_cast<std::uint64_t>(std::llround(lookMid * 60.0)); ++i) {
        engine->update(FrameTime{static_cast<double>(i) / 60.0, 1.0 / 60.0, i});
    }
    at = rook->state().position();
    const float towardUmbra = std::atan2(umbra->position.x - at.x, umbra->position.z - at.z);
    const float yawError = std::remainder(rook->state().yaw - towardUmbra, 2.0f * std::numbers::pi_v<float>);
    INFO("yaw error " << yawError);
    CHECK(std::abs(yawError) < 0.02f);
    CHECK(rook->locomotion().activity != entity::Activity::Run);

    // Handed back at the end mark.
    const glm::vec3 end = actor->keys.back().position;
    const double after = actor->keys.back().timeSeconds + 10.0 / 60.0;
    for (std::uint64_t i = static_cast<std::uint64_t>(std::llround(lookMid * 60.0)) + 1;
         i <= static_cast<std::uint64_t>(std::llround(after * 60.0)); ++i) {
        engine->update(FrameTime{static_cast<double>(i) / 60.0, 1.0 / 60.0, i});
    }
    CHECK_FALSE(rook->directorMotion().active);
    at = rook->state().position();
    CHECK(glm::length(glm::vec2(at.x - end.x, at.z - end.z)) < 7.4f * 11.0f / 60.0f + 0.05f);

    // And it is one undo, like everything the Director applies.
    REQUIRE(history.undo(*engine).ok());
    CHECK(engine->composition()->performers().empty());
}

TEST_CASE("a walk_to stops short of its target, from a mark on the line from the character's anchor",
          "[directing][performance][benchmark]") {
    auto engine = benchmark();
    const SceneFacts facts = app::sceneFactsFor(*engine);
    json doc = runPastPlan();
    doc["performances"][0]["beats"] = json::parse(R"([{"action": "walk_to", "target": "umbra", "seconds": 3.0}])");
    doc.erase("cues");
    const Compilation c = compilePlan(planFrom(doc), facts);
    INFO(c.diffText());
    REQUIRE_FALSE(c.validation.hasErrors());
    const seq::Actor& actor = c.staged.sequence.actors.back();
    const Place* umbra = facts.place("umbra-cap");
    const CharacterMark* rook = facts.character("rook");
    REQUIRE(umbra != nullptr);
    REQUIRE(rook != nullptr);
    const glm::vec3 stop = actor.keys.back().position;
    CHECK(glm::length(glm::vec2(stop.x - umbra->position.x, stop.z - umbra->position.z)) ==
          Catch::Approx(umbra->radius + 1.0f).margin(1e-3f));
    // Three seconds at his walk speed, on the line from his AUTHORED anchor toward Umbra.
    const glm::vec3 mark = actor.keys.front().position;
    const CharacterCard* card = facts.capabilities.character("rook");
    CHECK(glm::length(glm::vec2(stop.x - mark.x, stop.z - mark.z)) == Catch::Approx(card->walkSpeed * 3.0f).margin(1e-3f));
    const glm::vec2 toUmbra = glm::normalize(glm::vec2(umbra->position.x - rook->anchor.x, umbra->position.z - rook->anchor.z));
    const glm::vec2 path = glm::normalize(glm::vec2(stop.x - mark.x, stop.z - mark.z));
    CHECK(glm::dot(toUmbra, path) == Catch::Approx(1.0f).margin(1e-4f));
    CHECK(actor.keys.back().timeSeconds == Catch::Approx(93.0).margin(1e-6));
}

TEST_CASE("the validator on performances: starts, targets, collisions, and what is not compiled yet",
          "[directing][performance][validate][benchmark]") {
    auto engine = benchmark();
    const SceneFacts facts = app::sceneFactsFor(*engine);
    const auto codes = [&](const json& doc, const std::string& item) {
        Plan plan = planFrom(doc);
        const Validation v = validatePlan(plan, facts);
        std::vector<std::pair<std::string, Severity>> out;
        for (const Issue& i : v.issues) {
            if (i.item == item) {
                out.emplace_back(issueCodeName(i.code), i.severity);
            }
        }
        return out;
    };
    const auto has = [](const auto& list, const char* code, Severity s) {
        return std::any_of(list.begin(), list.end(), [&](const auto& e) { return e.first == code && e.second == s; });
    };

    SECTION("a start not at a cut is a visible jump: a warning, and it still compiles") {
        json doc = runPastPlan();
        doc.erase("shots");
        doc["performances"][0]["beats"][0]["at"] = "1:31";
        const auto found = codes(doc, "run");
        CHECK(has(found, "TIMING_CONFLICT", Severity::Warning));
        CHECK_FALSE(has(found, "TIMING_CONFLICT", Severity::Error));
    }
    SECTION("no start at all") {
        json doc = runPastPlan();
        doc.erase("shots");
        CHECK(has(codes(doc, "run"), "SCHEMA_INVALID", Severity::Error));
    }
    SECTION("toward another character: not a plan-time fact") {
        json doc = runPastPlan();
        doc["subjects"].push_back({{"alias", "tide"}, {"text", "Tide"}});
        doc["performances"][0]["beats"][0]["target"] = "tide";
        CHECK(has(codes(doc, "run"), "UNSUPPORTED", Severity::Error));
    }
    SECTION("two performances of one character in one plan") {
        json doc = runPastPlan();
        json second = doc["performances"][0];
        second["key"] = "again";
        doc["performances"].push_back(second);
        CHECK(has(codes(doc, "again"), "TIMING_CONFLICT", Severity::Error));
    }
    SECTION("an airborne action the character HAS compiles (ADR-761), except a fall; one it lacks is unavailable") {
        json doc = runPastPlan();
        doc["performances"][0]["beats"].push_back({{"action", "jump"}});
        doc["performances"][0]["beats"].push_back({{"action", "fall"}});
        doc["performances"][0]["beats"].push_back({{"action", "backflip"}});
        const auto found = codes(doc, "run");
        CHECK(has(found, "UNSUPPORTED", Severity::Error)); // the fall: no drop to fall from
        CHECK(has(found, "CAPABILITY_UNAVAILABLE", Severity::Error));
        json hop = runPastPlan();
        hop["performances"][0]["beats"].push_back({{"action", "jump"}});
        CHECK_FALSE(has(codes(hop, "run"), "UNSUPPORTED", Severity::Error));
    }
}

TEST_CASE("a chase that rises over its character and passes it moves behind, over, then ahead, played",
          "[directing][performance][camera][benchmark]") {
    // ADR-760. The camera is measured in Rook's frame while the shot plays -- behind and low, then
    // above, then in front looking back -- which is the claim the plan makes, not a key's value.
    auto engine = benchmark();
    json doc = runPastPlan();
    doc["shots"][0]["camera"].push_back(json{{"move", "rise_over"}, {"at", "1:33"}});
    doc["shots"][0]["camera"].push_back(json{{"move", "pass"}, {"at", "1:35"}});
    doc.erase("cues");
    const Compilation c = compilePlan(planFrom(doc), app::sceneFactsFor(*engine));
    INFO(c.diffText());
    REQUIRE_FALSE(c.validation.hasErrors());
    REQUIRE(c.validation.issues.empty());
    ui::EditHistory history;
    REQUIRE(app::applyCompilation(*engine, history, c));
    const entity::Entity* rook = engine->composition()->entityWorld().find("rook");
    REQUIRE(rook != nullptr);

    struct Relative {
        float along; // + in front of Rook
        float up;
    };
    std::uint64_t frame = 0;
    const auto sampleAt = [&](double seconds) {
        const auto last = static_cast<std::uint64_t>(std::llround(seconds * 60.0));
        for (; frame <= last; ++frame) {
            engine->update(FrameTime{static_cast<double>(frame) / 60.0, frame == 0 ? 0.0 : 1.0 / 60.0, frame});
        }
        REQUIRE(engine->composition()->activeCamera().name == "rook-passes");
        const glm::vec3 body = rook->state().position();
        const glm::vec3 forward(std::sin(rook->state().yaw), 0.0f, std::cos(rook->state().yaw));
        const glm::vec3 d = engine->scene().camera.position - body;
        INFO("t " << seconds << " along " << glm::dot(d, forward) << " up " << d.y);
        return Relative{glm::dot(d, forward), d.y};
    };
    const Relative chase = sampleAt(92.0);
    const Relative over = sampleAt(94.2);
    const Relative ahead = sampleAt(96.5);
    INFO("chase " << chase.along << "/" << chase.up << " over " << over.along << "/" << over.up << " ahead "
                  << ahead.along << "/" << ahead.up);
    CHECK(chase.along < -2.5f);
    CHECK(chase.up < 1.0f);
    CHECK(over.up > 3.5f);
    CHECK(std::abs(over.along) < 1.0f);
    CHECK(ahead.along > 2.5f);
    REQUIRE(history.undo(*engine).ok());
}

TEST_CASE("revising a chase's distance moves the camera, and undoing the revision moves it back",
          "[directing][performance][camera]") {
    // ADR-760: the offset is now a parameter, and `ParameterSet::add` keeps an existing parameter's
    // value -- so a rig revised in place would keep the OLD distance unless the install writes it.
    auto engine = benchmark();
    json doc = runPastPlan();
    doc.erase("cues");
    doc.erase("performances");
    ui::EditHistory history;
    REQUIRE(app::applyCompilation(*engine, history, compilePlan(planFrom(doc), app::sceneFactsFor(*engine))));
    const scene::CameraRig* rig = engine->composition()->cameraDirection().findByName("rook-passes");
    REQUIRE(rig != nullptr);
    const std::string slug = rig->slug; // `rig` points into the collection the revision replaces
    const std::string path = "cameras/" + slug + "/followOffset";
    REQUIRE(engine->params().find(path) != nullptr);
    CHECK(engine->params().find(path)->baseComponent(2) == Catch::Approx(-3.0f));

    doc["shots"][0]["camera"][1]["distanceMetres"] = 6.0;
    const Compilation revised = compilePlan(planFrom(doc), app::sceneFactsFor(*engine));
    INFO(revised.diffText());
    REQUIRE(revised.plan.revision == 2);
    REQUIRE(app::applyCompilation(*engine, history, revised));
    CHECK(engine->composition()->cameraDirection().findByName("rook-passes")->slug == slug); // in place
    CHECK(engine->params().find(path)->baseComponent(2) == Catch::Approx(-6.0f));
    REQUIRE(history.undo(*engine).ok());
    CHECK(engine->params().find(path)->baseComponent(2) == Catch::Approx(-3.0f));
}

TEST_CASE("an entry blend is live state: a baked plan refuses it, a directed plan is warned",
          "[directing][performance][determinism]") {
    // ADR-820 / ADR-758: a non-zero entrySeconds starts from wherever the simulation had the body.
    auto engine = benchmark();
    const SceneFacts facts = app::sceneFactsFor(*engine);
    json doc = runPastPlan();
    doc.erase("cues");

    const Compilation instant = compilePlan(planFrom(doc), facts);
    REQUIRE_FALSE(instant.validation.isBlocked("run"));
    const auto actorOf = [](const Compilation& c) -> const seq::Actor* {
        for (const seq::Actor& a : c.staged.sequence.actors) {
            if (a.id == "rook") {
                return &a;
            }
        }
        return nullptr;
    };
    REQUIRE(actorOf(instant) != nullptr);
    CHECK(actorOf(instant)->entrySeconds == 0.0f);

    doc["performances"][0]["entrySeconds"] = 0.5;
    const Plan blended = planFrom(doc);
    CHECK(blended.performances[0].entrySeconds == 0.5);
    CHECK(parsePlan(blended.toJson()).plan->performances[0].entrySeconds == 0.5); // round-trips
    const Compilation baked = compilePlan(blended, facts);
    INFO(baked.diffText());
    CHECK(baked.validation.isBlocked("run"));
    bool flagged = false;
    for (const Issue& i : baked.validation.issues) {
        flagged = flagged || (i.code == IssueCode::NonDeterministic && i.item == "run" &&
                              i.severity == Severity::Error && i.location == "/performances/0/entrySeconds");
    }
    CHECK(flagged);

    doc["tier"] = "directed";
    const Compilation directed = compilePlan(planFrom(doc), facts);
    bool warned = false;
    for (const Issue& i : directed.validation.issues) {
        warned = warned || (i.code == IssueCode::NonDeterministic && i.item == "run" && i.severity == Severity::Warning &&
                            i.location == "/performances/0/entrySeconds");
    }
    CHECK(warned);
    REQUIRE(actorOf(directed) != nullptr);
    CHECK(actorOf(directed)->entrySeconds == 0.5f); // compiled as asked, with the warning
}
