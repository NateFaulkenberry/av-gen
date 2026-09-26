// A plan's set pieces (ADR-929): parsed, validated against the scene, compiled into its staging.
//
// The scene is `tests/data/setpieces/setpiece-lab`: flat ground, a craft with a beam as the staging
// actor "saucer", and three small herds (one, two and three animals) with two strays. No licensed
// asset, so it runs anywhere. Where a check needs something the lab does not have -- a canopy, a song
// -- the facts the validator reads are given it directly: `SceneFacts` is a plain value, which is the
// point of it (ADR-750).
//
// Every refusal has a partner that differs in the one thing refused and passes.

#include "app/directing_apply.hpp"
#include "app/directing_context.hpp"
#include "app/engine.hpp"
#include "directing/compiler.hpp"
#include "directing/plan.hpp"
#include "directing/setpieces.hpp"
#include "directing/validator.hpp"
#include "stage/setpiece.hpp"
#include "support/project_round_trip.hpp"
#include "ui/edit_history.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <filesystem>
#include <string>

using namespace avgen;
using namespace avgen::directing;
using Catch::Approx;
using Catch::Matchers::ContainsSubstring;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

fs::path labProject() { return fs::path(AVGEN_SOURCE_DIR) / "tests/data/setpieces/setpiece-lab.json"; }

std::unique_ptr<app::Engine> lab() {
    auto engine = std::make_unique<app::Engine>(app::EngineMode::Offline);
    auto loaded = engine->loadProject(labProject());
    INFO((loaded ? std::string() : loaded.error().message));
    REQUIRE(loaded.has_value());
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

json piece(const std::string& key, double seconds, float x, float z, int animals = 1) {
    return json{{"key", key},
                {"template", "abduction"},
                {"craft", "saucer"},
                {"at", {{"seconds", seconds}}},
                {"place", {{"point", {x, z}}}},
                {"set", {{"animals", animals}, {"approachSeconds", 6}}}};
}

json planWith(json pieces, std::string id = "ufo") {
    return json{{"schemaVersion", 1}, {"id", id}, {"title", "UFO activity"}, {"tier", "baked"}, {"setPieces", std::move(pieces)}};
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

std::string all(const Validation& v) {
    std::string out;
    for (const Issue& i : v.issues) {
        out += i.toJson().dump() + "\n";
    }
    return out;
}

} // namespace

TEST_CASE("a plan's set pieces parse, round-trip canonically, and a plan without one keeps its bytes",
          "[directing][setpiece][plan]") {
    const json doc = json::parse(R"({
      "schemaVersion": 1, "id": "ufo", "title": "UFO activity", "tier": "baked",
      "subjects": [{"alias": "herd", "text": "cow-b1"}, {"alias": "horse", "text": "cow-a1"}],
      "setPieces": [
        {"key": "west", "template": "abduction", "craft": "saucer", "at": "12s", "moment": "lift",
         "place": {"point": [-120, -60]}, "animals": ["horse"], "set": {"hoverHeight": 30, "approachBearing": 270},
         "beamColor": [1.0, 0.4, 0.2], "framingMetres": 40},
        {"key": "field", "template": "survey", "craft": "saucer", "at": {"seconds": 60},
         "place": {"near": "herd", "offset": [5, -5]}},
        {"key": "south", "template": "abduction", "craft": "saucer", "at": "bar 20",
         "place": {"region": {"center": [150, 180], "radius": 30}}, "tag": "cow"}
      ]})");
    const Plan plan = planFrom(doc);
    REQUIRE(plan.setPieces.size() == 3);
    CHECK(plan.setPieces[0].moment == "lift");
    CHECK(plan.setPieces[0].animals == std::vector<std::string>{"horse"});
    CHECK(plan.setPieces[0].set.size() == 2);
    CHECK((*plan.setPieces[0].beamColor)[0] == Approx(1.0f));
    CHECK(plan.setPieces[1].where.near == "herd");
    CHECK(plan.setPieces[1].where.offset.first == Approx(5.0f));
    CHECK(plan.setPieces[2].where.region);
    CHECK(plan.setPieces[2].where.radius == Approx(30.0f));
    CHECK(plan.setPieces[2].at.kind == TimeRef::Kind::Bar);
    // Canonical: out and back is the same plan and the same bytes.
    const json out = plan.toJson();
    const Plan again = planFrom(out);
    CHECK(again == plan);
    CHECK(again.toJson() == out);
    // A plan with no set piece writes no key: every plan saved before this reads and writes as it did.
    Plan bare = plan;
    bare.setPieces.clear();
    CHECK_FALSE(bare.toJson().contains("setPieces"));
}

TEST_CASE("a malformed set piece is refused where it is malformed", "[directing][setpiece][plan]") {
    const auto issuesOf = [](json pieceJson) {
        return parsePlan(planWith(json::array({std::move(pieceJson)}))).issues;
    };
    const auto has = [](const std::vector<Issue>& issues, std::string_view location) {
        return std::any_of(issues.begin(), issues.end(), [&](const Issue& i) {
            return i.severity == Severity::Error && i.location.find(location) != std::string::npos;
        });
    };
    json both = piece("k", 30, 0, 0);
    both["place"]["near"] = "somebody"; // a point AND a subject
    CHECK(has(issuesOf(both), "/setPieces/0/place"));
    json slash = piece("a/b", 30, 0, 0);
    CHECK(has(issuesOf(slash), "/setPieces/0/key"));
    json noCraft = piece("k", 30, 0, 0);
    noCraft.erase("craft");
    CHECK(has(issuesOf(noCraft), "/setPieces/0/craft"));
    json badColour = piece("k", 30, 0, 0);
    badColour["beamColor"] = {1, 2};
    CHECK(has(issuesOf(badColour), "/setPieces/0/beamColor"));
    // The control: the same piece, well formed, parses with no error.
    const PlanParse fine = parsePlan(planWith(json::array({piece("k", 30, 0, 0)})));
    CHECK(fine.plan.has_value());
    CHECK_FALSE(hasErrors(fine.issues));
}

TEST_CASE("the validator refuses an unknown template, an unknown craft, and a craft without a beam",
          "[directing][setpiece][validator]") {
    auto engine = lab();
    SceneFacts facts = app::sceneFactsFor(*engine);

    json wrongTemplate = piece("k", 30, 62, 22);
    wrongTemplate["template"] = "abducton";
    Plan p = planFrom(planWith(json::array({wrongTemplate})));
    Validation v = validatePlan(p, facts);
    auto found = issuesWith(v, IssueCode::UnknownSubject, "k");
    REQUIRE(found.size() == 1);
    CHECK(found[0]->location == "/setPieces/0/template");
    CHECK(std::find(found[0]->suggestions.begin(), found[0]->suggestions.end(), "abduction") != found[0]->suggestions.end());
    CHECK(v.isBlocked("k"));

    json wrongCraft = piece("k", 30, 62, 22);
    wrongCraft["craft"] = "sauce";
    p = planFrom(planWith(json::array({wrongCraft})));
    v = validatePlan(p, facts);
    found = issuesWith(v, IssueCode::UnknownSubject, "k");
    REQUIRE(found.size() == 1);
    CHECK(std::find(found[0]->suggestions.begin(), found[0]->suggestions.end(), "saucer") != found[0]->suggestions.end());

    // A craft whose actor has no beam part can fly by, and cannot abduct.
    SceneFacts beamless = facts;
    beamless.staged.staging.actors[0].parts.clear();
    p = planFrom(planWith(json::array({piece("k", 30, 62, 22)})));
    v = validatePlan(p, beamless);
    CHECK(issuesWith(v, IssueCode::CapabilityUnavailable, "k").size() == 1);
    json flyby = piece("k", 30, 62, 22);
    flyby["template"] = "flyby";
    flyby.erase("set");
    p = planFrom(planWith(json::array({flyby})));
    v = validatePlan(p, beamless);
    INFO(all(v));
    CHECK_FALSE(v.isBlocked("k"));

    // The control for all three: the real template and craft, with its beam.
    p = planFrom(planWith(json::array({piece("k", 30, 62, 22, 2)})));
    v = validatePlan(p, facts);
    INFO(all(v));
    CHECK_FALSE(v.hasErrors());
}

TEST_CASE("a set piece needs clear air, asked of the canopy", "[directing][setpiece][validator]") {
    auto engine = lab();
    SceneFacts facts = app::sceneFactsFor(*engine);
    REQUIRE(facts.canopyAt); // the lab has navigation; it has no trees, so one is planted in the facts
    // A 12 m tree 10 m round (62, 22), where the two-cow herd grazes.
    facts.canopyAt = [](float x, float z) { return glm::length(glm::vec2(x - 62.0f, z - 22.0f)) < 10.0f ? 12.0f : 0.0f; };
    Plan p = planFrom(planWith(json::array({piece("under", 30, 62, 22, 2)})));
    Validation v = validatePlan(p, facts);
    auto found = issuesWith(v, IssueCode::SpatialInfeasible, "under");
    REQUIRE_FALSE(found.empty());
    CHECK_THAT(found[0]->message, ContainsSubstring("canopy"));
    CHECK(v.isBlocked("under"));
    // The control: 30 m away, in the open.
    p = planFrom(planWith(json::array({piece("open", 30, 92, 22, 2)})));
    v = validatePlan(p, facts);
    const auto open = issuesWith(v, IssueCode::SpatialInfeasible, "open");
    CHECK(std::none_of(open.begin(), open.end(), [](const Issue* i) { return i->severity == Severity::Error; }));
    CHECK_FALSE(v.isBlocked("open"));
    // A region is refused only when none of it is clear: wholly under the tree, then half of it.
    json region = piece("region", 30, 62, 22, 1);
    region["place"] = {{"region", {{"center", {62, 22}}, {"radius", 8}}}};
    p = planFrom(planWith(json::array({region})));
    CHECK(validatePlan(p, facts).isBlocked("region"));
    region["place"] = {{"region", {{"center", {62, 22}}, {"radius", 30}}}};
    p = planFrom(planWith(json::array({region})));
    CHECK_FALSE(validatePlan(p, facts).isBlocked("region"));
}

TEST_CASE("a set piece must fall inside the song", "[directing][setpiece][validator][time]") {
    auto engine = lab();
    SceneFacts facts = app::sceneFactsFor(*engine);
    facts.music.durationSeconds = 100.0;
    // Ends after the piece: placed at 95 s it would still be leaving at ~110 s.
    Plan p = planFrom(planWith(json::array({piece("late", 95, 62, 22, 2)})));
    Validation v = validatePlan(p, facts);
    REQUIRE(issuesWith(v, IssueCode::TimeOutOfRange, "late").size() == 1);
    CHECK_THAT(issuesWith(v, IssueCode::TimeOutOfRange, "late")[0]->message, ContainsSubstring("after the piece"));
    // Needs the craft before the film starts: a 6 s approach cannot put the beam at 5 s.
    p = planFrom(planWith(json::array({piece("early", 5, 62, 22, 2)})));
    v = validatePlan(p, facts);
    REQUIRE(issuesWith(v, IssueCode::TimeOutOfRange, "early").size() == 1);
    CHECK_THAT(issuesWith(v, IssueCode::TimeOutOfRange, "early")[0]->message, ContainsSubstring("before the film starts"));
    // The control.
    p = planFrom(planWith(json::array({piece("fits", 40, 62, 22, 2)})));
    CHECK(issuesWith(validatePlan(p, facts), IssueCode::TimeOutOfRange).empty());
}

TEST_CASE("one craft cannot be in two places, nor make travel it cannot make", "[directing][setpiece][validator]") {
    auto engine = lab();
    const SceneFacts facts = app::sceneFactsFor(*engine);
    // Overlap: the second needs the craft while the first is still leaving.
    Plan p = planFrom(planWith(json::array({piece("one", 30, -120, -60), piece("two", 38, 62, 22, 2)})));
    Validation v = validatePlan(p, facts);
    auto found = issuesWith(v, IssueCode::TimingConflict, "two");
    REQUIRE(found.size() == 1);
    CHECK_THAT(found[0]->message, ContainsSubstring("one craft in two places"));
    CHECK(v.isBlocked("two"));
    CHECK_FALSE(v.isBlocked("one")); // the earlier one is what the later one collides with
    // Travel: far enough apart in time not to overlap, not far enough for the distance.
    // The first leaves toward the south-west at ~44.6 s; the second needs the craft appearing 220 m
    // north-east of the field by ~47.1 s: 583 m in 2.5 s.
    json far = piece("far", 55, 62, 22, 2);
    far["set"]["approachBearing"] = 45;
    json first = piece("first", 30, -120, -60);
    first["set"]["departBearing"] = 200;
    p = planFrom(planWith(json::array({first, far})));
    v = validatePlan(p, facts);
    found = issuesWith(v, IssueCode::TimingConflict, "far");
    REQUIRE(found.size() == 1);
    CHECK_THAT(found[0]->message, ContainsSubstring("cannot make"));
    // The control: the same two, with a craft allowed to fly fast enough.
    far["set"]["cruiseSpeed"] = 400;
    p = planFrom(planWith(json::array({first, far})));
    v = validatePlan(p, facts);
    INFO(all(v));
    CHECK(issuesWith(v, IssueCode::TimingConflict).empty());
    // And a well spaced film of three passes outright.
    p = planFrom(planWith(json::array({piece("a", 30, -120, -60), piece("b", 80, 62, 22, 2), piece("c", 130, 150, 180, 3)})));
    v = validatePlan(p, facts);
    INFO(all(v));
    CHECK_FALSE(v.hasErrors());
}

TEST_CASE("two set pieces at one place, or framed from one distance, are flagged as repetition",
          "[directing][setpiece][validator][repetition]") {
    auto engine = lab();
    const SceneFacts facts = app::sceneFactsFor(*engine);
    json a = piece("a", 30, 62, 22, 2);
    json b = piece("b", 80, 70, 30, 1); // 11 m from a
    Plan p = planFrom(planWith(json::array({a, b})));
    Validation v = validatePlan(p, facts);
    auto found = issuesWith(v, IssueCode::Repetition);
    REQUIRE(found.size() == 1);
    CHECK(found[0]->item == "b"); // the later one repeats
    CHECK(found[0]->severity == Severity::Warning);
    CHECK_FALSE(v.isBlocked("b")); // a warning: the person decides
    // Framing: both close, 60 m and 64 m.
    a["framingMetres"] = 60;
    json c = piece("c", 80, -120, -60, 1);
    c["framingMetres"] = 64;
    p = planFrom(planWith(json::array({a, c})));
    v = validatePlan(p, facts);
    found = issuesWith(v, IssueCode::Repetition);
    REQUIRE(found.size() == 1);
    CHECK_THAT(found[0]->message, ContainsSubstring("same distance"));
    // The controls: far apart and framed differently -- one close, one far.
    c["framingMetres"] = 220;
    p = planFrom(planWith(json::array({a, c})));
    CHECK(issuesWith(validatePlan(p, facts), IssueCode::Repetition).empty());
}

TEST_CASE("a set piece can be placed on the music", "[directing][setpiece][time]") {
    auto engine = lab();
    SceneFacts facts = app::sceneFactsFor(*engine);
    // 120 bpm from 0.5 s: bar 17 starts on beat 65, at 0.5 + 64 * 0.5 = 32.5 s.
    for (int i = 0; i < 400; ++i) {
        facts.music.beatTimes.push_back(0.5 + (0.5 * i));
    }
    facts.music.beatsPerBar = 4;
    json onTheBar = piece("bar", 0, 62, 22, 2);
    onTheBar["at"] = "bar 17";
    Plan p = planFrom(planWith(json::array({onTheBar})));
    const Compilation c = compilePlan(p, facts);
    INFO(all(c.validation));
    REQUIRE_FALSE(c.validation.isBlocked("bar"));
    // The beam -- the placed moment -- is on the bar, and its marker says so.
    const auto marker = std::find_if(c.staged.sequence.markers.begin(), c.staged.sequence.markers.end(),
                                     [](const seq::Marker& m) { return m.name == "setpiece/bar/beam"; });
    REQUIRE(marker != c.staged.sequence.markers.end());
    CHECK(marker->timeSeconds == Approx(32.5).margin(1e-9));
    const auto scenario = std::find_if(c.staged.staging.scenarios.begin(), c.staged.staging.scenarios.end(),
                                       [](const stage::ScenarioDesc& s) { return s.name == "setpiece/bar"; });
    REQUIRE(scenario != c.staged.staging.scenarios.end());
    const auto clock = std::find_if(scenario->params.begin(), scenario->params.end(),
                                    [](const stage::ScenarioParam& q) { return q.name == "beamAt"; });
    REQUIRE(clock != scenario->params.end());
    CHECK(clock->value == Approx(32.5f));
}

TEST_CASE("compiling a set piece writes its scenario and a marker per moment; a revision replaces them",
          "[directing][setpiece][compile]") {
    auto engine = lab();
    Plan p = planFrom(planWith(json::array({piece("field", 40, 62, 22, 2)})));
    Compilation c = compilePlan(p, app::sceneFactsFor(*engine));
    INFO(c.diffText());
    REQUIRE_FALSE(c.validation.hasErrors());
    const auto count = [](const Staging& s, std::string_view name) {
        return std::count_if(s.staging.scenarios.begin(), s.staging.scenarios.end(),
                             [&](const stage::ScenarioDesc& sc) { return sc.name == name; });
    };
    CHECK(count(c.staged, "setpiece/field") == 1);
    int markers = 0;
    for (const seq::Marker& m : c.staged.sequence.markers) {
        markers += m.name.rfind("setpiece/field/", 0) == 0 ? 1 : 0;
    }
    CHECK(markers == 4); // approach, beam, lift, depart
    const bool recorded = std::any_of(c.plan.produced.begin(), c.plan.produced.end(), [](const ContentRef& r) {
        return r.domain == ContentDomain::StagingScenario && r.id == "setpiece/field" && !r.fingerprint.empty();
    });
    CHECK(recorded);
    CHECK(c.diffText().find("Set piece \"field\": abduction of 2 animals by saucer") != std::string::npos);

    // Installed, then revised: the same key, a new hover height -- replaced, not duplicated.
    REQUIRE(app::installCompilation(*engine, c).has_value());
    CHECK(app::verifyInstalled(*engine, "ufo").empty());
    json revisedDoc = planWith(json::array({piece("field", 40, 62, 22, 2)}));
    revisedDoc["setPieces"][0]["set"]["hoverHeight"] = 31;
    Compilation r = compilePlan(planFrom(revisedDoc), app::sceneFactsFor(*engine));
    INFO(r.diffText());
    CHECK(r.plan.revision == 2);
    CHECK(count(r.staged, "setpiece/field") == 1);
    CHECK(r.diffText().find("~ Set piece \"field\"") != std::string::npos);
    REQUIRE(app::installCompilation(*engine, r).has_value());
    const params::IParameter* hover = engine->params().find("staging/setpiece/field/hoverHeight");
    REQUIRE(hover != nullptr);
    CHECK(hover->baseComponent(0) == Approx(31.0f));
}

TEST_CASE("a set piece tuned by hand is not overwritten by a revision", "[directing][setpiece][compile][provenance]") {
    auto engine = lab();
    Plan p = planFrom(planWith(json::array({piece("field", 40, 62, 22, 2)})));
    REQUIRE(app::installCompilation(*engine, compilePlan(p, app::sceneFactsFor(*engine))).has_value());
    // The person drags the hover height in the editor.
    params::IParameter* hover = engine->params().find("staging/setpiece/field/hoverHeight");
    REQUIRE(hover != nullptr);
    hover->setBaseComponent(0, 40.0f);
    json revisedDoc = planWith(json::array({piece("field", 40, 62, 22, 2)}));
    revisedDoc["setPieces"][0]["set"]["hoverSeconds"] = 3;
    Compilation r = compilePlan(planFrom(revisedDoc), app::sceneFactsFor(*engine));
    CHECK(issuesWith(r.validation, IssueCode::HandEdited, "field").size() == 1);
    CHECK(r.validation.isBlocked("field"));
    REQUIRE(app::installCompilation(*engine, r).has_value());
    hover = engine->params().find("staging/setpiece/field/hoverHeight"); // looked up again after an install
    REQUIRE(hover != nullptr);
    CHECK(hover->baseComponent(0) == Approx(40.0f)); // still the person's
    // The control: untouched, the same revision replaces it.
    auto fresh = lab();
    REQUIRE(app::installCompilation(*fresh, compilePlan(p, app::sceneFactsFor(*fresh))).has_value());
    Compilation clean = compilePlan(planFrom(revisedDoc), app::sceneFactsFor(*fresh));
    CHECK(issuesWith(clean.validation, IssueCode::HandEdited).empty());
    CHECK_FALSE(clean.validation.isBlocked("field"));
}

TEST_CASE("a cue can start on a set piece's moment, and not on one that cannot happen", "[directing][setpiece][compile]") {
    auto engine = lab();
    json doc = planWith(json::array({piece("field", 40, 62, 22, 2)}));
    doc["cues"] = json::array({json{{"key", "flash"}, {"parameter", "particles/visitor-beam/emissive"},
                                    {"on", "setpiece/field/lift"}, {"value", 3.0}, {"holdSeconds", 1.0}}});
    Compilation c = compilePlan(planFrom(doc), app::sceneFactsFor(*engine));
    INFO(c.diffText());
    REQUIRE_FALSE(c.validation.isBlocked("flash"));
    const auto event = std::find_if(c.staged.sequence.events.begin(), c.staged.sequence.events.end(),
                                    [](const seq::SequenceEvent& e) { return e.id == "ufo.flash"; });
    REQUIRE(event != c.staged.sequence.events.end());
    const auto marker = std::find_if(c.staged.sequence.markers.begin(), c.staged.sequence.markers.end(),
                                     [](const seq::Marker& m) { return m.name == "setpiece/field/lift"; });
    REQUIRE(marker != c.staged.sequence.markers.end());
    CHECK(event->when.timeSeconds == Approx(marker->timeSeconds));
    // A moment the template does not have is an unknown event, with the real ones suggested.
    doc["cues"][0]["on"] = "setpiece/field/sweep";
    Validation v = validatePlan(*std::make_unique<Plan>(planFrom(doc)), app::sceneFactsFor(*engine));
    REQUIRE(issuesWith(v, IssueCode::UnknownEvent, "flash").size() == 1);
    // A set piece that cannot happen blocks the cue that waits on it.
    doc["cues"][0]["on"] = "setpiece/field/lift";
    doc["setPieces"][0]["craft"] = "nobody";
    Plan blocked = planFrom(doc);
    v = validatePlan(blocked, app::sceneFactsFor(*engine));
    CHECK(v.isBlocked("field"));
    CHECK(issuesWith(v, IssueCode::Blocked, "flash").size() == 1);
}

TEST_CASE("an authored scenario that drives the craft from the start refuses a set piece on it",
          "[directing][setpiece][validator]") {
    auto engine = lab();
    SceneFacts facts = app::sceneFactsFor(*engine);
    stage::ScenarioDesc authored;
    authored.name = "patrol";
    authored.actor = "saucer";
    authored.autoStart = true;
    authored.beats.push_back(stage::BeatDesc{});
    facts.staged.staging.scenarios.push_back(authored);
    Plan p = planFrom(planWith(json::array({piece("field", 40, 62, 22, 2)})));
    Validation v = validatePlan(p, facts);
    REQUIRE(issuesWith(v, IssueCode::TimingConflict, "field").size() == 1);
    CHECK(v.isBlocked("field"));
    // Started only on an event, it is a warning: it may never run during the set piece.
    facts.staged.staging.scenarios.back().autoStart = false;
    v = validatePlan(p, facts);
    REQUIRE(issuesWith(v, IssueCode::TimingConflict, "field").size() == 1);
    CHECK_FALSE(v.isBlocked("field"));
}

TEST_CASE("a set piece's animals are estimated from where the scene puts them", "[directing][setpiece][validator]") {
    auto engine = lab();
    const SceneFacts facts = app::sceneFactsFor(*engine);
    // Three animals asked of the two-cow herd: a warning, with the count.
    Plan p = planFrom(planWith(json::array({piece("greedy", 40, 62, 22, 3)})));
    Validation v = validatePlan(p, facts);
    auto found = issuesWith(v, IssueCode::SpatialInfeasible, "greedy");
    REQUIRE(found.size() == 1);
    CHECK(found[0]->severity == Severity::Warning);
    CHECK_THAT(found[0]->message, ContainsSubstring("2 animal(s)"));
    // The control: two.
    p = planFrom(planWith(json::array({piece("fair", 40, 62, 22, 2)})));
    CHECK(issuesWith(validatePlan(p, facts), IssueCode::SpatialInfeasible).empty());
}
