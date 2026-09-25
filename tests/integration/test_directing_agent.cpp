// The Director through the AI control plane: semantic tools, the approval gate, and the commit
// (ADR-757, director-system-progress.md Slice 1.5; spec §19, §34, §45-§47).
//
// Everything but the model's judgement is real: the ScriptedProvider stands in for the model (spec
// §38: the suite never depends on a real LLM), and every tool runs against a real engine.

#include "ai/control_plane.hpp"
#include "ai/director_tools.hpp"
#include "ai/scripted_provider.hpp"
#include "app/ai_edit_sink.hpp"
#include "app/directing_record.hpp"
#include "app/edit_system.hpp"
#include "app/engine.hpp"
#include "app/job_system.hpp"
#include "directing/plan.hpp"
#include "entity/entity.hpp"
#include "support/project_round_trip.hpp"
#include "world/hero.hpp"

#include "support/project_assets.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <thread>

using namespace avgen;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

ai::ToolCall call(std::string id, std::string name, json args) {
    return ai::ToolCall{std::move(id), std::move(name), std::move(args)};
}

json benchmarkPlan() {
    return testsupport::readJson(fs::path(AVGEN_SOURCE_DIR) / "tests/data/directing/rook_umbra.plan.json");
}

json smallPlan() {
    return json::parse(R"({
      "schemaVersion": 1, "id": "small", "title": "Small", "tier": "baked",
      "subjects": [{"alias": "stone", "text": "the stone hero"}],
      "shots": [{"key": "est", "name": "establish", "start": "0:10", "durationSeconds": 4,
                 "subject": "stone", "locked": true, "camera": [{"move": "push_in"}]}],
      "markers": [{"key": "drop", "name": "drop", "at": "0:12.5"}]
    })");
}

// A control plane over a real engine, with the editor's history sink installed as the app does.
struct Session {
    app::Engine engine{app::EngineMode::Offline};
    app::JobSystem jobs{2};
    ai::ControlPlane plane{engine, &jobs};
    app::EditSystem edits;
    std::unique_ptr<app::EditHistoryTransactionSink> sink;

    Session() {
        plane.setCredentialStore(std::make_unique<ai::MemoryCredentialStore>());
        sink = std::make_unique<app::EditHistoryTransactionSink>(engine, edits, plane.transactionSink());
        plane.setTransactionSink(sink.get());
    }
    ~Session() { plane.shutdown(); }

    void smallScene() {
        engine.newComposition();
        scene::CompositionNode node;
        node.name = "stone";
        node.kind = scene::NodeKind::Group;
        node.transform.position = glm::vec3(10.0f, 0.0f, 3.0f);
        REQUIRE(engine.addNode(std::move(node)).has_value());
        world::HeroPoint stone;
        stone.name = "stone";
        stone.position = glm::vec3(10.0f, 0.0f, 3.0f);
        stone.radius = 1.5f;
        stone.height = 3.0f;
        REQUIRE(engine.composition()->setHeroes({stone}).has_value());
    }

    std::shared_ptr<ai::ScriptedProvider> provider;
    void script(std::vector<ai::ScriptedTurn> turns) {
        provider = std::make_shared<ai::ScriptedProvider>(std::move(turns));
        plane.setProvider(provider);
    }
    // What the model was sent back for a tool call, by call id.
    json resultOf(std::string_view id) const {
        for (const ai::CompletionRequest& r : provider->received()) {
            for (const ai::Message& m : r.messages) {
                for (const ai::ToolCallResult& t : m.toolResults) {
                    if (t.id == id) {
                        return t.content;
                    }
                }
            }
        }
        return json{};
    }

    // Services the main-thread queue until the task reaches `state` or finishes.
    bool runUntil(const std::shared_ptr<ai::AgentTask>& task, ai::TaskState state, double seconds = 30.0) {
        REQUIRE(task != nullptr);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
        while (task->state() != state && !task->finished()) {
            plane.pump();
            if (std::chrono::steady_clock::now() > deadline) {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        plane.pump();
        if (task->state() != state) {
            std::string trail;
            for (const ai::Activity& a : task->activities()) {
                trail += std::string(ai::activityKindName(a.kind)) + ":" + a.title + " [" + a.detail.substr(0, 300) + "] | ";
            }
            UNSCOPED_INFO("state " << ai::taskStateName(task->state()) << "; error: " << task->outcome().error
                                   << "; activities: " << trail);
        }
        return task->state() == state;
    }

    std::shared_ptr<ai::AgentTask> propose(json plan, std::string prompt = "propose it") {
        script({ai::ScriptedTurn{"Proposing.", {call("p1", "director.propose_plan", json{{"plan", std::move(plan)}})},
                                 ai::StopReason::EndTurn, {}},
                ai::ScriptedTurn{"Here is the plan; approve it to apply.", {}, ai::StopReason::EndTurn, {}}});
        auto task = plane.submit(std::move(prompt));
        REQUIRE(runUntil(task, ai::TaskState::AwaitingApproval));
        return task;
    }
};

} // namespace

TEST_CASE("the Rook/Umbra request: inspected, proposed, waited on, approved, one undo",
          "[directing][agent][integration][benchmark]") {
    Session s;
    REQUIRE(s.engine.loadProject(fs::path(AVGEN_SOURCE_DIR) / "examples/world/glowmere-valley-2-multicam.json"));
    const json sequenceBefore = s.engine.sequence().toJson();
    const json camerasBefore = s.engine.composition()->cameraDirection().toJson();

    s.script({
        ai::ScriptedTurn{"Finding who and when.",
                         {call("i1", "director.inspect_subject", json{{"name", "Rook"}}),
                          call("i2", "director.inspect_subject", json{{"name", "Umbra"}}),
                          call("i3", "director.inspect_subject", json{{"name", "the Umbra hero mushroom"}}),
                          call("i4", "director.resolve_time", json{{"text", "1:30"}}),
                          call("i5", "director.inspect_capabilities", json{{"subject", "rook"}})},
                         ai::StopReason::EndTurn, {}},
        ai::ScriptedTurn{"Proposing the shot.", {call("p1", "director.propose_plan", json{{"plan", benchmarkPlan()}})},
                         ai::StopReason::EndTurn, {}},
        ai::ScriptedTurn{"Rook has no backflip, and his jump cannot clear Umbra. I proposed the shot and the "
                         "low-angle chase; approve it to apply.",
                         {}, ai::StopReason::EndTurn, {}},
    });
    auto task = s.plane.submit("At 1:30, create a 5-second shot where Rook backflips over the Umbra hero mushroom");
    REQUIRE(s.runUntil(task, ai::TaskState::AwaitingApproval));

    // 1-4, as the model saw them: Rook resolved with his capabilities, Umbra ambiguous (never
    // silently chosen), the Umbra hero resolved, 1:30 placed by the engine.
    INFO(s.resultOf("i2").dump(2));
    CHECK(s.resultOf("i1")["result"]["status"] == "resolved");
    CHECK(s.resultOf("i1")["result"]["subject"]["id"] == "rook");
    CHECK(s.resultOf("i1")["result"].contains("capabilities"));
    CHECK(s.resultOf("i2")["result"]["status"] == "ambiguous");
    CHECK(s.resultOf("i2")["result"]["candidates"].size() >= 2);
    CHECK(s.resultOf("i3")["result"]["subject"]["id"] == "umbra-cap");
    CHECK(s.resultOf("i3")["result"]["effects"][0]["id"] == "umbra-cap-hero-pulse");
    CHECK(s.resultOf("i4")["result"]["seconds"] == 90.0);
    CHECK(s.resultOf("p1")["result"]["proposed"] == true);

    // 13: waiting, and nothing has changed.
    const auto proposal = task->proposal();
    REQUIRE(proposal);
    CHECK(proposal->planId == "rook-umbra");
    CHECK(proposal->diff.find("CAPABILITY_UNAVAILABLE: rook does not have a backflip capability") != std::string::npos);
    CHECK(proposal->diff.find("+ Shot \"rook-umbra\" 01:30.000-01:35.000") != std::string::npos);
    CHECK(s.engine.sequence().toJson() == sequenceBefore);
    CHECK(s.engine.composition()->cameraDirection().toJson() == camerasBefore);
    CHECK(s.engine.directingPlans().empty());
    CHECK(s.edits.history().undoSize() == 0);
    // A new request is refused while this one waits: approve or reject first.
    CHECK(s.plane.submit("something else") == nullptr);

    // 14: approved -- installed in one transaction, verified, one undo labelled with the request.
    REQUIRE(s.plane.approveCurrentTask());
    CHECK(task->state() == ai::TaskState::Completed);
    const ai::TaskOutcome outcome = task->outcome();
    INFO(outcome.error);
    CHECK(outcome.success);
    REQUIRE(s.engine.directingPlans().size() == 1);
    CHECK(s.engine.sequence().shotNamed("rook-umbra") != nullptr);
    REQUIRE(s.edits.history().undoSize() == 1);
    CHECK(s.edits.history().undoLabel() == task->prompt());
    CHECK(outcome.editState == s.edits.history().stateId());

    // 15: one undo takes it all back.
    REQUIRE(s.edits.execute(app::EditAction::Undo, s.engine));
    CHECK(s.engine.sequence().toJson() == sequenceBefore);
    CHECK(s.engine.composition()->cameraDirection().toJson() == camerasBefore);
    CHECK(s.engine.directingPlans().empty());
}

TEST_CASE("a rejected plan changes nothing, and the next request can proceed", "[directing][agent][integration]") {
    Session s;
    s.smallScene();
    const json before = s.engine.sequence().toJson();
    auto task = s.propose(smallPlan());
    REQUIRE(s.plane.rejectCurrentTask());
    CHECK(task->state() == ai::TaskState::Rejected);
    CHECK(task->outcome().rejected);
    CHECK(s.engine.sequence().toJson() == before);
    CHECK(s.engine.directingPlans().empty());
    CHECK(s.edits.history().undoSize() == 0);
    CHECK_FALSE(s.plane.approveCurrentTask()); // nothing waits any more

    s.script({ai::ScriptedTurn{"Nothing to do.", {}, ai::StopReason::EndTurn, {}}});
    auto next = s.plane.submit("hello");
    CHECK(next != nullptr);
    if (next) {
        CHECK(s.runUntil(next, ai::TaskState::Completed));
    }
}

TEST_CASE("cancelling a task that awaits approval declines it", "[directing][agent][integration]") {
    Session s;
    s.smallScene();
    auto task = s.propose(smallPlan());
    s.plane.cancelCurrentTask();
    CHECK(task->state() == ai::TaskState::Rejected);
    CHECK(s.engine.sequence().shots.empty());
}

TEST_CASE("approving a plan the project has moved under is refused, and changes nothing",
          "[directing][agent][integration]") {
    Session s;
    s.smallScene();
    auto task = s.propose(smallPlan());
    // The person adds a shot where the proposal's shot would go, after seeing the proposal.
    seq::Sequence edited = s.engine.sequence();
    seq::Shot theirs;
    theirs.name = "theirs";
    theirs.startSeconds = 11.0;
    theirs.durationSeconds = 1.0;
    edited.shots.push_back(theirs);
    REQUIRE(s.engine.setSequence(edited));
    const json afterTheirEdit = s.engine.sequence().toJson();

    REQUIRE(s.plane.approveCurrentTask());
    CHECK(task->state() == ai::TaskState::Failed);
    CHECK(task->outcome().error.find("changed after this plan was proposed") != std::string::npos);
    CHECK(s.engine.sequence().toJson() == afterTheirEdit);
    CHECK(s.engine.directingPlans().empty());
    CHECK(s.edits.history().undoSize() == 0);
}

TEST_CASE("a plan with nothing buildable is not proposed, and the task simply finishes",
          "[directing][agent][integration]") {
    Session s;
    s.smallScene();
    json impossible = smallPlan();
    impossible["subjects"][0]["text"] = "Nobody";
    impossible.erase("markers");
    s.script({ai::ScriptedTurn{"Proposing.", {call("p1", "director.propose_plan", json{{"plan", impossible}})},
                               ai::StopReason::EndTurn, {}},
              ai::ScriptedTurn{"Nothing could be built.", {}, ai::StopReason::EndTurn, {}}});
    auto task = s.plane.submit("do the impossible");
    REQUIRE(s.runUntil(task, ai::TaskState::Completed));
    CHECK_FALSE(task->proposal());
    CHECK(s.engine.sequence().shots.empty());
}

TEST_CASE("the Director tools declare themselves honestly", "[directing][agent][ai]") {
    ai::ToolRegistry registry;
    ai::registerDirectorTools(registry);
    std::size_t count = 0;
    for (const ai::Tool* tool : registry.all()) {
        INFO(tool->definition.name);
        CHECK(tool->definition.domain() == "director");
        // No Director tool changes the project itself: the only way content lands is an approval.
        CHECK_FALSE(tool->definition.annotations.mutatesProject);
        ++count;
    }
    CHECK(count == 9); // ADR-765 added director.record_plan, ADR-767 director.watch_events
    const ai::Tool* watch = registry.find("director.watch_events");
    REQUIRE(watch != nullptr);
    CHECK_FALSE(watch->definition.annotations.requiresApproval); // it only looks
    const ai::Tool* propose = registry.find("director.propose_plan");
    REQUIRE(propose != nullptr);
    CHECK(propose->definition.annotations.requiresApproval);
    const ai::Tool* record = registry.find("director.record_plan");
    REQUIRE(record != nullptr);
    CHECK(record->definition.annotations.requiresApproval); // it proposes; the person approves
    CHECK_FALSE(record->definition.annotations.deterministic); // what the characters did is theirs
    CHECK(registry.find("director.apply_plan") == nullptr); // applying is the person's act

    app::Engine engine(app::EngineMode::Offline);
    ai::ToolContext ctx(engine);
    const ai::ToolResult schema = registry.invoke("director.plan_schema", json::object(), ctx);
    REQUIRE(schema.success);
    CHECK(schema.value["fields"]["shots"][0]["camera"][0]["move"] == json(directing::cameraMoveNames()));
}

TEST_CASE("the assistant asks for a recording; the host records, and the person approves the recorded plan",
          "[directing][agent][record]") {
    testsupport::skipUnlessGlowmereBenchmarkAssetsPresent();
    // ADR-765: `director.record_plan` goes through the host's hook and the approval gate. The
    // ScriptedProvider stands in for the model; every tool and the recorder are real.
    Session s;
    REQUIRE(s.engine.loadProject(fs::path(AVGEN_SOURCE_DIR) / "examples/world/glowmere-valley-2-multicam.json"));
    const json goal = testsupport::readJson(fs::path(AVGEN_SOURCE_DIR) /
                                            "tests/data/directing/goal_proposal.ai-script.json")["turns"][0]["toolCalls"][0]
                          ["arguments"]["plan"];
    const json sequenceBefore = s.engine.sequence().toJson();

    SECTION("with no recorder installed, the tool says so and nothing is proposed") {
        s.script({ai::ScriptedTurn{"Recording.", {call("r1", "director.record_plan", json{{"plan", goal}})},
                                   ai::StopReason::EndTurn, {}},
                  ai::ScriptedTurn{"It could not be recorded.", {}, ai::StopReason::EndTurn, {}}});
        auto task = s.plane.submit("record Rook's walk");
        REQUIRE(s.runUntil(task, ai::TaskState::Completed));
        CHECK_FALSE(task->proposal().has_value());
        CHECK(s.resultOf("r1")["success"] == false);
        CHECK(s.resultOf("r1")["error"]["code"] == "UNAVAILABLE");
    }
    SECTION("with the host's recorder: recorded, proposed, approved as one undo") {
        s.plane.setRecordingHook(app::makeRecordingHook());
        s.script({ai::ScriptedTurn{"Recording Rook's walk so the flash can be timed.",
                                   {call("r1", "director.record_plan", json{{"plan", goal}})}, ai::StopReason::EndTurn, {}},
                  ai::ScriptedTurn{"Recorded; approve to apply.", {}, ai::StopReason::EndTurn, {}}});
        auto task = s.plane.submit("Record Rook's walk to the Lantern and flash when he arrives");
        REQUIRE(s.runUntil(task, ai::TaskState::AwaitingApproval, 180.0));
        INFO(s.resultOf("r1").dump(2));
        CHECK(s.resultOf("r1")["result"]["recorded"] == true);
        CHECK(s.resultOf("r1")["result"]["tier"] == "baked");
        // Progress was reported while it ran (the phases), and nothing changed in the project.
        const auto activities = task->activities();
        CHECK(std::count_if(activities.begin(), activities.end(), [](const ai::Activity& a) {
                  return a.kind == ai::ActivityKind::ToolProgress && a.title == "director.record_plan";
              }) >= 2);
        CHECK(s.engine.sequence().toJson() == sequenceBefore);
        CHECK(s.edits.history().undoSize() == 0);
        const auto proposal = task->proposal();
        REQUIRE(proposal);
        CHECK(proposal->plan["tier"] == "baked");
        CHECK(proposal->plan["performances"][0].contains("recording"));
        CHECK(proposal->diff.find("as recorded") != std::string::npos);
        CHECK(proposal->diff.find("+ Cue scene/brightness") != std::string::npos); // timed by the recording

        REQUIRE(s.plane.approveCurrentTask());
        CHECK(task->outcome().success);
        REQUIRE(s.edits.history().undoSize() == 1);
        CHECK(s.edits.history().undoLabel() == task->prompt());
        REQUIRE(s.engine.directingPlans().size() == 1);
        CHECK(s.engine.directingPlans()[0].tier == directing::Tier::Baked);
        REQUIRE(s.edits.execute(app::EditAction::Undo, s.engine));
        CHECK(s.engine.sequence().toJson() == sequenceBefore);
    }
}

TEST_CASE("event-driven: the assistant watches the film, proposes on what happens in it, and the person approves",
          "[directing][agent][events]") {
    testsupport::skipUnlessGlowmereBenchmarkAssetsPresent();
    // ADR-767. The model never copies a time: it watches, then writes {"event": ...}; the tool
    // attaches the observation it got, and the plan's times are placed from that.
    Session s;
    REQUIRE(s.engine.loadProject(fs::path(AVGEN_SOURCE_DIR) / "examples/world/glowmere-valley-2-multicam.json"));
    s.plane.setWatchHook(app::makeWatchHook());
    const json plan = json::parse(R"({"schemaVersion": 1, "id": "beams", "title": "Mark the second beam", "tier": "baked",
        "subjects": [{"alias": "visitor", "text": "the visitor", "hint": "hero"}],
        "markers": [{"key": "beam2", "name": "second beam", "at": {"event": "abduction/beam", "subject": "visitor", "occurrence": 2}}]})");
    s.script({ai::ScriptedTurn{"Watching the first half-minute for what the saucer does.",
                               {call("w1", "director.watch_events", json{{"untilSeconds", 30}})}, ai::StopReason::EndTurn, {}},
              ai::ScriptedTurn{"It beams twice; marking the second.", {call("p1", "director.propose_plan", json{{"plan", plan}})},
                               ai::StopReason::EndTurn, {}},
              ai::ScriptedTurn{"Marked; approve to apply.", {}, ai::StopReason::EndTurn, {}}});
    auto task = s.plane.submit("Put a marker on the second time the saucer beams someone up");
    REQUIRE(s.runUntil(task, ai::TaskState::AwaitingApproval, 180.0));
    const json watched = s.resultOf("w1")["result"];
    INFO(watched.dump().substr(0, 400));
    REQUIRE(watched.contains("observation"));
    double second = -1.0;
    int beams = 0;
    for (const json& e : watched["observation"]["events"]) {
        if (e["name"] == "abduction/beam" && e.value("subject", "") == "visitor" && ++beams == 2) {
            second = e["seconds"].get<double>();
        }
    }
    REQUIRE(second > 0.0);
    const auto proposal = task->proposal();
    REQUIRE(proposal);
    CHECK(proposal->plan.contains("observation")); // attached for the model, and kept as the times' source
    CHECK(proposal->diff.find("second beam") != std::string::npos);

    REQUIRE(s.plane.approveCurrentTask());
    CHECK(task->outcome().success);
    const auto& markers = s.engine.sequence().markers;
    const auto m = std::find_if(markers.begin(), markers.end(), [](const seq::Marker& x) { return x.name == "second beam"; });
    REQUIRE(m != markers.end());
    CHECK(m->timeSeconds == second);
    REQUIRE(s.engine.directingPlans().size() == 1);
    CHECK(s.engine.directingPlans()[0].observation.has_value());
}
