// The Director Plan: schema, versioning, serialization, and its life in a project (ADR-755,
// director-system-progress.md Slice 1.1).
//
// A plan is saved in the project as the provenance of the content it produced (the owner's ruling,
// 2026-09-24), so it has to survive exactly what content survives: a save after the engine has run,
// a reload, a second save; and an undo of whatever wrote it.

#include "app/edit_capture.hpp"
#include "app/engine.hpp"
#include "directing/issue.hpp"
#include "directing/plan.hpp"
#include "seq/sequence.hpp"
#include "support/project_round_trip.hpp"
#include "ui/edit_history.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

using namespace avgen;
using namespace avgen::directing;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

json benchmarkDocument() {
    return testsupport::readJson(fs::path(AVGEN_SOURCE_DIR) / "tests/data/directing/rook_umbra.plan.json");
}

Plan benchmarkPlan() {
    PlanParse parsed = parsePlan(benchmarkDocument());
    for (const Issue& i : parsed.issues) {
        INFO(i.toJson().dump());
    }
    REQUIRE(parsed.plan.has_value());
    return *parsed.plan;
}

bool hasCode(const std::vector<Issue>& issues, IssueCode code) {
    return std::any_of(issues.begin(), issues.end(), [&](const Issue& i) { return i.code == code; });
}

const Issue* first(const std::vector<Issue>& issues, IssueCode code) {
    const auto it = std::find_if(issues.begin(), issues.end(), [&](const Issue& i) { return i.code == code; });
    return it == issues.end() ? nullptr : &*it;
}

} // namespace

TEST_CASE("the Rook/Umbra benchmark plan parses cleanly and keeps the request as written",
          "[directing][plan]") {
    const PlanParse parsed = parsePlan(benchmarkDocument());
    for (const Issue& i : parsed.issues) {
        INFO(i.toJson().dump());
    }
    CHECK(parsed.issues.empty());
    REQUIRE(parsed.plan);
    const Plan& plan = *parsed.plan;
    CHECK(plan.id == "rook-umbra");
    CHECK(plan.tier == Tier::Baked);
    REQUIRE(plan.shots.size() == 1);
    // "1:30" was written as text and is kept as the request: seconds for the resolver, text for the
    // person reading the diff.
    CHECK(plan.shots[0].start.kind == TimeRef::Kind::Seconds);
    CHECK(plan.shots[0].start.seconds == 90.0);
    CHECK(plan.shots[0].start.text == "1:30");
    REQUIRE(plan.shots[0].camera.size() == 4);
    CHECK(plan.shots[0].camera[2].move == CameraMove::RiseOver);
    REQUIRE(plan.performances.size() == 1);
    CHECK(plan.performances[0].beats[2].action == "backflip"); // the request, not what is possible
    REQUIRE(plan.cues.size() == 2);
    REQUIRE(plan.cues[1].effect);
    CHECK(plan.cues[1].effect->owner == "umbra");
    CHECK(plan.cues[1].effect->type == "groundPulse");
    CHECK(plan.subject("umbra")->hint == SubjectKind::Hero);
}

TEST_CASE("a plan round-trips through JSON to an equal plan and identical bytes", "[directing][plan]") {
    Plan plan = benchmarkPlan();
    // Every optional filled somewhere, so the round trip exercises it.
    plan.subjects[0].kind = SubjectKind::Entity;
    plan.subjects[0].id = "rook";
    std::vector<Issue> parseIssues;
    const std::optional<TimeRef> bar = parseTime("bar 45 beat 2", parseIssues);
    REQUIRE(bar);
    plan.markers.push_back(PlanMarker{"drop", "drop", *bar});
    TimeRef chorus;
    chorus.kind = TimeRef::Kind::Section;
    chorus.section = "chorus";
    chorus.occurrence = -1;
    chorus.anchor = TimeRef::Anchor::End;
    chorus.offsetSeconds = -0.5;
    plan.markers.push_back(PlanMarker{"tail", "tail", chorus});
    plan.cues[0].until = TimeRef::at(95.0);
    plan.shots[0].rig = "Rook Chase";
    plan.shots[0].camera[3].side = "left";
    plan.shots[0].camera[1].degrees = 45.0f;
    plan.performances[0].beats[0].at = TimeRef::at(90.0);
    plan.produced.push_back(ContentRef{"rook-umbra", ContentDomain::SequenceShot, "rook-umbra", "sha1:abc"});
    plan.produced.push_back(ContentRef{"rook-umbra", ContentDomain::CameraShot, "rookchase@90", ""});

    const json once = plan.toJson();
    const PlanParse back = parsePlan(once);
    for (const Issue& i : back.issues) {
        INFO(i.toJson().dump());
    }
    REQUIRE(back.plan);
    CHECK(*back.plan == plan);
    CHECK(back.plan->toJson().dump() == once.dump()); // canonical: same plan, same bytes
    CHECK(back.issues.empty());
}

TEST_CASE("a plan from a newer schema is refused whole, and an old build says why", "[directing][plan]") {
    json doc = benchmarkDocument();
    doc["schemaVersion"] = kPlanSchemaVersion + 1;
    doc["somethingNew"] = true; // a newer field: not reported, because nothing past the version is read
    const PlanParse parsed = parsePlan(doc);
    CHECK_FALSE(parsed.plan);
    REQUIRE(parsed.issues.size() == 1);
    CHECK(parsed.issues[0].code == IssueCode::SchemaVersionUnsupported);
    CHECK_FALSE(parsed.issues[0].recoverable);

    doc.erase("schemaVersion");
    const PlanParse missing = parsePlan(doc);
    CHECK_FALSE(missing.plan);
    CHECK(hasCode(missing.issues, IssueCode::SchemaInvalid));
}

TEST_CASE("the schema reports each mistake at its location, with suggestions", "[directing][plan]") {
    SECTION("an unknown field is a warning and the plan still reads") {
        json doc = benchmarkDocument();
        doc["shots"][0]["durationSecond"] = 5.0; // a model's typo
        const PlanParse parsed = parsePlan(doc);
        REQUIRE(parsed.plan);
        const Issue* issue = first(parsed.issues, IssueCode::SchemaUnknownField);
        REQUIRE(issue != nullptr);
        CHECK(issue->severity == Severity::Warning);
        CHECK(issue->location == "/shots/0/durationSecond");
        CHECK(std::find(issue->suggestions.begin(), issue->suggestions.end(), "durationSeconds") !=
              issue->suggestions.end());
    }
    SECTION("an unknown camera move names the vocabulary") {
        json doc = benchmarkDocument();
        doc["shots"][0]["camera"][0]["move"] = "push-in";
        const PlanParse parsed = parsePlan(doc);
        CHECK_FALSE(parsed.plan);
        const Issue* issue = first(parsed.issues, IssueCode::SchemaInvalid);
        REQUIRE(issue != nullptr);
        CHECK(issue->location == "/shots/0/camera/0/move");
        CHECK(std::find(issue->suggestions.begin(), issue->suggestions.end(), "push_in") != issue->suggestions.end());
    }
    SECTION("duplicate keys") {
        json doc = benchmarkDocument();
        doc["cues"][1]["key"] = "frame-drag";
        const PlanParse parsed = parsePlan(doc);
        CHECK_FALSE(parsed.plan);
        CHECK(first(parsed.issues, IssueCode::DuplicateKey)->location == "/cues/1/key");
    }
    SECTION("an alias the subjects table does not declare") {
        json doc = benchmarkDocument();
        doc["performances"][0]["subject"] = "rok";
        const PlanParse parsed = parsePlan(doc);
        CHECK_FALSE(parsed.plan);
        const Issue* issue = first(parsed.issues, IssueCode::UndeclaredSubject);
        REQUIRE(issue != nullptr);
        CHECK(issue->location == "/performances/0/subject");
        CHECK(issue->suggestions == std::vector<std::string>{"rook"});
    }
    SECTION("a cue needs exactly one target and exactly one start") {
        json doc = benchmarkDocument();
        doc["cues"][0]["effect"] = {{"owner", "world"}, {"type", "aurora"}};
        doc["cues"][1]["at"] = "1:32";
        const PlanParse parsed = parsePlan(doc);
        CHECK_FALSE(parsed.plan);
        std::size_t invalid = 0;
        for (const Issue& i : parsed.issues) {
            invalid += i.code == IssueCode::SchemaInvalid ? 1 : 0;
        }
        CHECK(invalid == 2);
    }
    SECTION("a retime must name a performance, and durations and factors are positive") {
        json doc = benchmarkDocument();
        doc["retimes"][0]["performance"] = "nobody";
        doc["retimes"][0]["factor"] = 0.0;
        doc["shots"][0]["durationSeconds"] = -1.0;
        const PlanParse parsed = parsePlan(doc);
        CHECK_FALSE(parsed.plan);
        CHECK(parsed.issues.size() >= 3);
    }
    SECTION("a malformed time is refused where it is") {
        json doc = benchmarkDocument();
        doc["shots"][0]["start"] = "1:75";
        const PlanParse parsed = parsePlan(doc);
        CHECK_FALSE(parsed.plan);
        const Issue* issue = first(parsed.issues, IssueCode::MalformedTime);
        REQUIRE(issue != nullptr);
        CHECK(issue->location == "/shots/0/start");
    }
}

TEST_CASE("every issue code and severity has a stable name that reads back", "[directing][plan]") {
    for (int i = 0; i <= static_cast<int>(kLastIssueCode); ++i) {
        const auto code = static_cast<IssueCode>(i);
        INFO(i);
        const auto back = issueCodeFromName(issueCodeName(code));
        REQUIRE(back);
        CHECK(*back == code);
    }
    Issue issue;
    issue.severity = Severity::Warning;
    issue.code = IssueCode::CapabilityUnavailable;
    issue.subject = "rook";
    issue.message = "Rook does not have a backflip capability.";
    issue.details = {{"available", {"jump", "fall", "land"}}};
    issue.suggestions = {"jump over the target"};
    CHECK(Issue::fromJson(issue.toJson()) == issue);
}

TEST_CASE("plan ids are readable, unique and path-safe", "[directing][plan]") {
    CHECK(mintPlanId("Rook / Umbra!", {}) == "rook-umbra");
    CHECK(mintPlanId("Rook / Umbra!", {"rook-umbra"}) == "rook-umbra-2");
    CHECK(mintPlanId("Rook / Umbra!", {"rook-umbra", "rook-umbra-2"}) == "rook-umbra-3");
    CHECK(mintPlanId("", {}) == "plan");
    CHECK(mintPlanId("a/b", {}).find('/') == std::string::npos);
}

TEST_CASE("a project keeps its plans through a save after a frame, a reload and a second save",
          "[directing][plan][persistence]") {
    testsupport::ScratchDir dir{"directing_plan_project"};
    app::Engine engine(app::EngineMode::Offline);
    engine.newComposition();

    SECTION("an untouched project writes no key") {
        auto trip = testsupport::saveAndReload(engine, dir / "none.json");
        REQUIRE(trip);
        CHECK_FALSE(trip->saved.contains("directingPlans"));
    }
    SECTION("a plan comes back equal, in order, and the second save is the first") {
        Plan a = benchmarkPlan();
        a.produced.push_back(ContentRef{"rook-umbra", ContentDomain::SequenceShot, "rook-umbra", "fp-1"});
        Plan b = benchmarkPlan();
        b.id = "second";
        b.revision = 3;
        engine.directingPlans() = {a, b};
        auto trip = testsupport::saveAndReload(engine, dir / "plans.json");
        INFO((trip ? std::string() : trip.error().message));
        REQUIRE(trip);
        CHECK(trip->warnings.empty());
        REQUIRE(trip->reloaded->directingPlans().size() == 2);
        CHECK(trip->reloaded->directingPlans()[0] == a);
        CHECK(trip->reloaded->directingPlans()[1] == b);
        CHECK(trip->saved["directingPlans"] == trip->resaved["directingPlans"]);
        CHECK(testsupport::missingTopLevelKeys(trip->saved, trip->resaved).empty());
    }
    SECTION("a plan this build cannot read is kept verbatim, with a warning, not dropped by the save") {
        json future = benchmarkPlan().toJson();
        future["schemaVersion"] = kPlanSchemaVersion + 1;
        future["fromTheFuture"] = {1, 2, 3};
        engine.directingPlans() = {benchmarkPlan()};
        REQUIRE(engine.saveProject(dir / "future.json"));
        json doc = testsupport::readJson(dir / "future.json");
        doc["directingPlans"].push_back(future);
        {
            std::ofstream out(dir / "future.json");
            out << doc.dump(2);
        }
        app::Engine reloaded(app::EngineMode::Offline);
        REQUIRE(reloaded.loadProject(dir / "future.json"));
        CHECK(reloaded.directingPlans().size() == 1);
        REQUIRE(reloaded.unreadableDirectingPlans().size() == 1);
        CHECK(std::any_of(reloaded.projectWarnings().begin(), reloaded.projectWarnings().end(),
                          [](const std::string& w) { return w.find("directingPlans[1]") != std::string::npos; }));
        REQUIRE(reloaded.saveProject(dir / "future-again.json"));
        const json again = testsupport::readJson(dir / "future-again.json");
        REQUIRE(again["directingPlans"].size() == 2);
        CHECK(again["directingPlans"][1] == future);
    }
}

TEST_CASE("the plans are part of the one undo: adding a plan with its content undoes both",
          "[directing][plan][undo]") {
    app::Engine engine(app::EngineMode::Offline);
    engine.newComposition();
    ui::EditHistory history;

    app::EditCapture capture;
    capture.begin(engine);
    Plan plan = benchmarkPlan();
    plan.produced.push_back(ContentRef{"rook-umbra", ContentDomain::SequenceShot, "rook-umbra", ""});
    engine.directingPlans().push_back(plan);
    seq::Sequence piece;
    seq::Shot shot;
    shot.name = "rook-umbra";
    shot.startSeconds = 90.0;
    shot.durationSeconds = 5.0;
    piece.shots.push_back(shot);
    REQUIRE(engine.setSequence(piece));
    ui::EditCommand command = capture.finish(engine, "Director: rook-umbra");
    REQUIRE(command.plans != nullptr);
    REQUIRE(command.timeline != nullptr);
    history.push(std::move(command));

    REQUIRE(history.undo(engine).ok());
    CHECK(engine.directingPlans().empty());
    CHECK(engine.sequence().shots.empty());
    REQUIRE(history.redo(engine).ok());
    REQUIRE(engine.directingPlans().size() == 1);
    CHECK(engine.directingPlans()[0] == plan);
    CHECK(engine.sequence().shots.size() == 1);
}
