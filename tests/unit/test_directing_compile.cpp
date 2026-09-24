// Validate -> compile -> diff -> apply -> undo -> save -> reload, on the benchmark and on small
// scenes (ADR-756, director-system-progress.md Slices 1.3-1.4; spec §18, §20, §21, §34, §56).

#include "app/directing_context.hpp"
#include "app/engine.hpp"
#include "directing/compiler.hpp"
#include "directing/plan.hpp"
#include "directing/validator.hpp"
#include "entity/entity.hpp"
#include "scene/camera_rig.hpp"
#include "support/gltf_fixture.hpp"
#include "support/project_round_trip.hpp"
#include "ui/edit_history.hpp"
#include "world/hero.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <fmt/ranges.h>
#include <filesystem>
#include <string>

using namespace avgen;
using namespace avgen::directing;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

Plan planFrom(const json& doc) {
    PlanParse parsed = parsePlan(doc);
    for (const Issue& i : parsed.issues) {
        INFO(i.toJson().dump());
    }
    REQUIRE(parsed.plan);
    return *parsed.plan;
}

Plan benchmarkPlan() {
    return planFrom(testsupport::readJson(fs::path(AVGEN_SOURCE_DIR) / "tests/data/directing/rook_umbra.plan.json"));
}

std::unique_ptr<app::Engine> benchmark() {
    auto engine = std::make_unique<app::Engine>(app::EngineMode::Offline);
    auto loaded = engine->loadProject(fs::path(AVGEN_SOURCE_DIR) / "examples/world/glowmere-valley-2-multicam.json");
    INFO((loaded ? std::string() : loaded.error().message));
    REQUIRE(loaded.has_value());
    return engine;
}

std::vector<const Issue*> issuesWith(const Validation& v, IssueCode code, std::string_view item = {}) {
    std::vector<const Issue*> out;
    for (const Issue& i : v.issues) {
        if (i.code == code && (item.empty() || i.item == item)) {
            out.push_back(&i);
        }
    }
    return out;
}

Staging staged(app::Engine& engine) {
    return app::sceneFactsFor(engine).staged;
}

bool diffHas(const Compilation& c, char sign, std::string_view fragment) {
    return std::any_of(c.diff.begin(), c.diff.end(), [&](const DiffLine& d) {
        return d.sign == sign && d.text.find(fragment) != std::string::npos;
    });
}

// A small scene: a character "scout" (no rig), a 3 m hero "stone", a plain node "tree", a parameter.
struct SmallScene {
    app::Engine engine{app::EngineMode::Offline};
    SmallScene() {
        engine.newComposition();
        scene::Composition* comp = engine.composition();
        for (const char* name : {"stone", "tree", "scout"}) {
            scene::CompositionNode node;
            node.name = name;
            node.kind = scene::NodeKind::Group;
            node.transform.position = glm::vec3(name[0] == 's' ? 10.0f : -5.0f, 0.0f, 3.0f);
            REQUIRE(engine.addNode(std::move(node)).has_value());
        }
        world::HeroPoint stone;
        stone.name = "stone";
        stone.position = glm::vec3(10.0f, 0.0f, 3.0f);
        stone.radius = 1.5f;
        stone.height = 3.0f;
        REQUIRE(comp->setHeroes({stone}).has_value());
        entity::EntityDesc scout;
        scout.name = "scout";
        scout.clips = {{"run", "Running"}, {"jump", "Jumping"}};
        REQUIRE(comp->setEntities({scout}).has_value());
        engine.params().add(params::ParamDesc<float>{.path = "test/flash", .defaultValue = 0.0f, .hardMin = 0.0f, .hardMax = 10.0f});
        engine.params().add(params::ParamDesc<float>{.path = "test/keyed", .defaultValue = 0.0f, .hardMin = 0.0f, .hardMax = 10.0f});
    }
};

json smallPlan() {
    return json::parse(R"({
      "schemaVersion": 1, "id": "small", "title": "Small", "tier": "baked",
      "subjects": [{"alias": "stone", "text": "the stone hero"}, {"alias": "scout", "text": "Scout"}],
      "shots": [{"key": "est", "name": "establish", "start": "0:10", "durationSeconds": 4,
                 "subject": "stone", "locked": true, "camera": [{"move": "push_in"}]}],
      "markers": [{"key": "drop", "name": "drop", "at": "0:12.5"}],
      "cues": [{"key": "flash", "parameter": "test/flash", "at": "0:12.5", "value": 2.0, "rampSeconds": 0.1, "holdSeconds": 0.4}]
    })");
}

} // namespace

TEST_CASE("the Rook/Umbra benchmark: refused honestly, the feasible part built, nothing substituted",
          "[directing][compile][benchmark]") {
    auto engine = benchmark();
    const SceneFacts facts = app::sceneFactsFor(*engine);
    const json sequenceBefore = engine->sequence().toJson();
    const json camerasBefore = engine->composition()->cameraDirection().toJson();

    const Compilation c = compilePlan(benchmarkPlan(), facts);
    const Validation& v = c.validation;
    INFO(c.diffText());

    // 1-3: resolved
    CHECK(c.plan.subject("rook")->id == "rook");
    CHECK(c.plan.subject("umbra")->id == "umbra-cap");
    CHECK(v.times.at("/shots/0/start") == 90.0);

    // 4-7: capabilities discovered, backflip missing and reported -- not hallucinated
    const auto backflip = issuesWith(v, IssueCode::CapabilityUnavailable, "rook-run");
    REQUIRE(backflip.size() == 1);
    CHECK(backflip[0]->message.find("rook does not have a backflip capability") != std::string::npos);
    CHECK(backflip[0]->message.find("Available airborne actions: fall, jump, land") != std::string::npos);
    CHECK(backflip[0]->details["requested"] == "backflip");
    CHECK(std::find(backflip[0]->suggestions.begin(), backflip[0]->suggestions.end(), "jump over the target instead") !=
          backflip[0]->suggestions.end());
    // ...and the spatial check on the jump over a 5.5 m cap with a 1.1 m apex.
    const auto spatial = issuesWith(v, IssueCode::SpatialInfeasible, "rook-run");
    REQUIRE_FALSE(spatial.empty());
    CHECK(spatial[0]->details["obstacle"] == "umbra-cap");
    CHECK_THAT(spatial[0]->details["required"].get<double>(), Catch::Matchers::WithinAbs(5.75, 1e-4));
    CHECK_THAT(spatial[0]->details["apex"].get<double>(), Catch::Matchers::WithinAbs(1.1, 1e-4));
    CHECK(v.isBlocked("rook-run"));

    // What depends on the impossible cannot happen either, and says why.
    CHECK(v.isBlocked("frame-drag"));
    CHECK(v.isBlocked("umbra-pulse"));
    CHECK(v.isBlocked("flip-slowmo"));
    CHECK_FALSE(issuesWith(v, IssueCode::Blocked, "umbra-pulse").empty());
    // "The Umbra hero effect" resolves (the owner's ruling: umbra-cap's Ground Pulse); it is blocked
    // only by the peak it waits for.
    CHECK(issuesWith(v, IssueCode::UnknownSubject, "umbra-pulse").empty());
    CHECK(issuesWith(v, IssueCode::Unsupported, "umbra-pulse").empty());

    // 8-9: the feasible part: the shot, with a low-angle chase that follows Rook's node
    CHECK_FALSE(v.isBlocked("rook-umbra"));
    REQUIRE(c.staged.sequence.shotNamed("rook-umbra") != nullptr);
    const scene::CameraRig* rig = c.staged.cameras.findByName("rook-umbra");
    REQUIRE(rig != nullptr);
    CHECK(rig->followNode == "rook");
    CHECK(rig->followOffset.y == 0.4f);  // low angle
    CHECK(rig->followOffset.z == -3.0f); // the plan's 3 m
    const auto cut = std::find_if(c.staged.cameras.shots.begin(), c.staged.cameras.shots.end(),
                                  [&](const scene::CameraShot& s) { return s.camera == rig->id; });
    REQUIRE(cut != c.staged.cameras.shots.end());
    CHECK(cut->startSeconds == 90.0);
    CHECK(cut->endSeconds == 95.0);
    CHECK(cut->locked); // UFO Watch claims abductions; this moment is the plan's
    // rise_over and pass are named as not compiled, not dropped silently
    CHECK(issuesWith(v, IssueCode::Unsupported, "rook-umbra").size() == 2);

    // 12: the diff reads as intent
    CHECK(diffHas(c, '+', "Shot \"rook-umbra\" 01:30.000-01:35.000"));
    CHECK(diffHas(c, '+', "low-angle chase on rook"));
    CHECK(diffHas(c, '!', "CAPABILITY_UNAVAILABLE"));
    CHECK(diffHas(c, '!', "SPATIAL_INFEASIBLE"));
    CHECK(c.changesAnything());

    // Dry run: the project is exactly as it was.
    CHECK(engine->sequence().toJson() == sequenceBefore);
    CHECK(engine->composition()->cameraDirection().toJson() == camerasBefore);
    CHECK(engine->directingPlans().empty());

    // 17: deterministic -- same plan, same facts, same content, same bytes.
    const Compilation again = compilePlan(benchmarkPlan(), facts);
    CHECK(again.staged.sequence.toJson() == c.staged.sequence.toJson());
    CHECK(again.staged.cameras.toJson() == c.staged.cameras.toJson());
    CHECK(again.plan.toJson() == c.plan.toJson());
    CHECK(again.diff == c.diff);
}

TEST_CASE("the benchmark applies as one undo, and survives a save after a frame and a reload",
          "[directing][compile][benchmark][persistence]") {
    auto engine = benchmark();
    ui::EditHistory history;
    const json sequenceBefore = engine->sequence().toJson();
    const json camerasBefore = engine->composition()->cameraDirection().toJson();

    const Compilation c = compilePlan(benchmarkPlan(), app::sceneFactsFor(*engine));
    REQUIRE(app::applyCompilation(*engine, history, c));
    REQUIRE(history.undoSize() == 1);
    CHECK(history.undoLabel() == "Director: Rook flips over Umbra");
    REQUIRE(engine->directingPlans().size() == 1);
    const Plan& stored = engine->directingPlans()[0];
    CHECK(stored.revision == 1);
    CHECK(stored.produced.size() == 3); // the seq shot, the rig, the camera-track shot
    for (const ContentRef& ref : stored.produced) {
        INFO(contentDomainName(ref.domain) << " " << ref.id);
        const auto now = contentOf(ref, staged(*engine));
        REQUIRE(now);
        CHECK(fingerprint(*now) == ref.fingerprint); // installed exactly as compiled
    }
    const json sequenceAfter = engine->sequence().toJson();
    const json camerasAfter = engine->composition()->cameraDirection().toJson();

    // 15: undo, all of it, and redo
    REQUIRE(history.undo(*engine).ok());
    CHECK(engine->sequence().toJson() == sequenceBefore);
    CHECK(engine->composition()->cameraDirection().toJson() == camerasBefore);
    CHECK(engine->directingPlans().empty());
    REQUIRE(history.redo(*engine).ok());
    CHECK(engine->sequence().toJson() == sequenceAfter);
    CHECK(engine->composition()->cameraDirection().toJson() == camerasAfter);

    // 16: save after a frame, reload, compare -- the plan, its content, and its fingerprints
    testsupport::ScratchDir dir{"directing_benchmark_apply"};
    auto trip = testsupport::saveAndReload(*engine, dir / "film.json");
    INFO((trip ? std::string() : trip.error().message));
    REQUIRE(trip);
    REQUIRE(trip->reloaded->directingPlans().size() == 1);
    const Plan& back = trip->reloaded->directingPlans()[0];
    CHECK(back == stored);
    for (const ContentRef& ref : back.produced) {
        INFO(contentDomainName(ref.domain) << " " << ref.id);
        const auto now = contentOf(ref, staged(*trip->reloaded));
        REQUIRE(now);
        CHECK(fingerprint(*now) == ref.fingerprint);
    }

    // And revising the reloaded project finds its own content intact: revision 2, nothing hand-edited,
    // the same content rebuilt in place (a revision replaces, it does not stack).
    const Compilation revised = compilePlan(benchmarkPlan(), app::sceneFactsFor(*trip->reloaded));
    CHECK(revised.plan.revision == 2);
    CHECK(issuesWith(revised.validation, IssueCode::HandEdited).empty());
    CHECK(revised.staged.sequence.toJson() == trip->reloaded->sequence().toJson());
    CHECK(diffHas(revised, '~', "Shot \"rook-umbra\""));
}

TEST_CASE("a revision never overwrites what the person edited by hand", "[directing][compile]") {
    SmallScene s;
    ui::EditHistory history;
    REQUIRE(app::applyCompilation(s.engine, history, compilePlan(planFrom(smallPlan()), app::sceneFactsFor(s.engine))));
    REQUIRE(s.engine.sequence().shotNamed("establish") != nullptr);

    // The person trims the shot.
    seq::Sequence edited = s.engine.sequence();
    edited.shots.front().durationSeconds = 2.0;
    REQUIRE(s.engine.setSequence(edited));

    json revisedDoc = smallPlan();
    revisedDoc["shots"][0]["durationSeconds"] = 6.0;
    revisedDoc["markers"][0]["at"] = "0:13";
    const Compilation c = compilePlan(planFrom(revisedDoc), app::sceneFactsFor(s.engine));
    INFO(c.diffText());
    CHECK(c.plan.revision == 2);
    CHECK_FALSE(issuesWith(c.validation, IssueCode::HandEdited, "est").empty());
    CHECK(c.validation.isBlocked("est"));
    REQUIRE(c.staged.sequence.shotNamed("establish") != nullptr);
    CHECK(c.staged.sequence.shotNamed("establish")->durationSeconds == 2.0); // the person's, untouched
    // ...and the item is kept WHOLE: its camera cut stays with the shot the person kept.
    CHECK(std::any_of(c.staged.cameras.shots.begin(), c.staged.cameras.shots.end(),
                      [](const scene::CameraShot& cut) { return cut.label == "establish"; }));
    CHECK(issuesWith(c.validation, IssueCode::TimingConflict, "est").empty()); // not re-placed, so no collision
    // The marker was not touched by hand, so it moves: replaced, not duplicated.
    CHECK(std::count_if(c.staged.sequence.markers.begin(), c.staged.sequence.markers.end(),
                        [](const seq::Marker& m) { return m.name == "drop"; }) == 1);
    CHECK(diffHas(c, '~', "Marker drop 00:13.000"));
}

TEST_CASE("framing, markers and parameter cues compile to editable native content that installs",
          "[directing][compile]") {
    SmallScene s;
    ui::EditHistory history;
    const Compilation c = compilePlan(planFrom(smallPlan()), app::sceneFactsFor(s.engine));
    INFO(c.diffText());
    CHECK_FALSE(c.validation.hasErrors());
    const seq::Shot* shot = c.staged.sequence.shotNamed("establish");
    REQUIRE(shot != nullptr);
    CHECK(shot->camera.kind == seq::CameraKind::Move); // a push-in, editable as a move
    CHECK(shot->camera.move.subject.name == "stone");
    CHECK(shot->camera.move.startDistance > shot->camera.move.endDistance);
    REQUIRE(c.staged.cameras.shots.size() == 1);
    CHECK(c.staged.cameras.shots[0].camera == scene::kMainCamera); // both shot types, written together
    REQUIRE(c.staged.sequence.events.size() == 1);
    CHECK(c.staged.sequence.events[0].what.target == "test/flash");
    CHECK(seq::triggerIsScheduled(c.staged.sequence.events[0].when.kind)); // deterministic tier
    CHECK(seq::actionIsBaked(c.staged.sequence.events[0].what.kind));
    REQUIRE(app::applyCompilation(s.engine, history, c));
    for (const auto& w : s.engine.sequenceReport().warnings) {
        INFO(w);
    }
    INFO(fmt::format("{}", fmt::join(s.engine.sequenceReport().warnings, " | ")));
    CHECK(s.engine.sequenceReport().warnings.empty());
    CHECK(std::find(s.engine.sequenceTargets().begin(), s.engine.sequenceTargets().end(), "test/flash") !=
          s.engine.sequenceTargets().end());
}

TEST_CASE("spec §18's validation examples, each at its item", "[directing][validate]") {
    SmallScene s;
    const SceneFacts facts = app::sceneFactsFor(s.engine);
    const auto validate = [&](json doc) {
        Plan plan = planFrom(doc);
        return validatePlan(plan, facts);
    };

    SECTION("unknown subject") {
        json doc = smallPlan();
        doc["subjects"][1]["text"] = "Scoutt";
        doc["performances"] = json::parse(R"([{"key": "p", "subject": "scout", "beats": [{"action": "run"}]}])");
        const Validation v = validate(doc);
        const auto unknown = issuesWith(v, IssueCode::UnknownSubject);
        REQUIRE(unknown.size() == 1);
        CHECK(unknown[0]->suggestions == std::vector<std::string>{"scout"});
        CHECK(v.isBlocked("p"));
        CHECK_FALSE(v.isBlocked("est")); // only what names it
    }
    SECTION("missing capability, with the character's rig not loaded: it can do nothing") {
        json doc = smallPlan();
        doc["performances"] = json::parse(R"([{"key": "p", "subject": "scout", "beats": [{"action": "run"}]}])");
        const Validation v = validate(doc); // held: issuesWith points into it
        const auto missing = issuesWith(v, IssueCode::CapabilityUnavailable, "p");
        REQUIRE(missing.size() == 1);
        INFO(missing[0]->toJson().dump(2));
        CHECK(missing[0]->cause.find("not on the loaded rig") != std::string::npos);
    }
    SECTION("timeline overlap: within the plan, and with a shot already there") {
        json doc = smallPlan();
        doc["shots"].push_back(json::parse(R"({"key": "b", "name": "b", "start": "0:12", "durationSeconds": 5, "subject": "stone", "locked": true})"));
        const Validation v = validate(doc);
        CHECK_FALSE(issuesWith(v, IssueCode::TimingConflict, "b").empty());
        CHECK_FALSE(v.isBlocked("est"));

        SceneFacts crowded = facts;
        seq::Shot existing;
        existing.name = "someone-elses";
        existing.startSeconds = 11.0;
        existing.durationSeconds = 1.0;
        crowded.staged.sequence.shots.push_back(existing);
        Plan plan = planFrom(smallPlan());
        const Validation w = validatePlan(plan, crowded);
        const auto clash = issuesWith(w, IssueCode::TimingConflict, "est");
        REQUIRE(clash.size() == 1);
        CHECK(clash[0]->details["existing"] == "someone-elses");
    }
    SECTION("nondeterminism: a live performance in a baked plan, and a rendered cue on its event") {
        json doc = smallPlan();
        doc["performances"] = json::parse(
            R"([{"key": "p", "subject": "scout", "mode": "goal", "beats": [{"action": "run", "emits": "scout.arrived"}]}])");
        doc["cues"].push_back(json::parse(R"({"key": "c", "parameter": "test/flash", "on": "scout.arrived", "value": 1})"));
        const Validation v = validate(doc);
        CHECK_FALSE(issuesWith(v, IssueCode::NonDeterministic, "p").empty());
        CHECK_FALSE(issuesWith(v, IssueCode::NonDeterministic, "c").empty());
    }
    SECTION("camera conflict: an event camera may take an unlocked shot") {
        SceneFacts withEvents = facts;
        scene::CameraRig ufo;
        ufo.name = "UFO Watch";
        ufo.eventScenario = "abduction";
        withEvents.staged.cameras.addCamera(ufo);
        json doc = smallPlan();
        doc["shots"][0]["locked"] = false;
        Plan plan = planFrom(doc);
        const Validation v = validatePlan(plan, withEvents);
        const auto conflict = issuesWith(v, IssueCode::CameraConflict, "est");
        REQUIRE(conflict.size() == 1);
        CHECK(conflict[0]->severity == Severity::Warning);
        CHECK_FALSE(v.isBlocked("est"));
    }
    SECTION("an impossible jump: the apex cannot clear the obstacle") {
        SceneFacts able = facts;
        CharacterCard card;
        card.subject = "scout";
        card.rigLoaded = true;
        card.activities = {{"jump", ActivityKind::Airborne, "Jumping", true, 1.0f, true}};
        card.jump.apex = 1.1f;
        able.capabilities = CapabilityRegistry::withCharacters({card});
        json doc = smallPlan();
        doc["performances"] = json::parse(R"([{"key": "p", "subject": "scout", "beats": [{"action": "jump", "target": "stone"}]}])");
        Plan plan = planFrom(doc);
        const Validation v = validatePlan(plan, able);
        const auto spatial = issuesWith(v, IssueCode::SpatialInfeasible, "p");
        REQUIRE(spatial.size() == 1);
        CHECK(spatial[0]->details["obstacleHeight"] == 3.0);
        CHECK(issuesWith(v, IssueCode::CapabilityUnavailable, "p").empty()); // it can jump, just not that high
    }
    SECTION("a baked cue on a parameter the author keyed would erase their keys: refused") {
        SceneFacts keyed = facts;
        keyed.authorTrackTargets = {"test/flash"};
        Plan plan = planFrom(smallPlan());
        const Validation v = validatePlan(plan, keyed);
        CHECK_FALSE(issuesWith(v, IssueCode::TimingConflict, "flash").empty());
        CHECK(v.isBlocked("flash"));
    }
    SECTION("an event nothing emits") {
        json doc = smallPlan();
        doc["cues"][0].erase("at");
        doc["cues"][0]["on"] = "scout.landed";
        const Validation v = validate(doc);
        CHECK_FALSE(issuesWith(v, IssueCode::UnknownEvent, "flash").empty());
    }
}
