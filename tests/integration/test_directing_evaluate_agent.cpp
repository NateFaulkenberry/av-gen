// The Director's evaluation loop through the AI control plane (ADR-931): propose -> evaluate ->
// Modify -> compare, with the approval gate met once per proposal and never per evaluation.
//
// The ScriptedProvider plays the model and a stub evaluator plays the Critic (the suite never depends
// on a real model or a running Critic); every tool, the orchestrator's deferred-answer path, the
// main-thread settle that keeps each report, the Modify brief and the approval are real. The stub
// judges the candidate it is handed the way the brief's owner would: an abduction flown at the height
// the valley's first one was flown at "is framed like the last one"; a lower, differently-approached
// one is not.

#include "ai/control_plane.hpp"
#include "ai/director_tools.hpp"
#include "ai/scripted_provider.hpp"
#include "app/ai_edit_sink.hpp"
#include "app/edit_system.hpp"
#include "app/engine.hpp"
#include "app/job_system.hpp"
#include "directing/evaluation.hpp"
#include "directing/plan.hpp"
#include "stage/setpiece.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <thread>

using namespace avgen;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

ai::ToolCall call(std::string id, std::string name, json args) {
    return ai::ToolCall{std::move(id), std::move(name), std::move(args)};
}

json riverPlan(float hoverHeight, float bearing) {
    return json{{"schemaVersion", 1}, {"id", "ufo"}, {"title", "UFO activity"}, {"tier", "baked"},
                {"setPieces", json::array({{{"key", "river"}, {"template", "abduction"}, {"craft", "saucer"},
                                            {"at", {{"seconds", 60}}}, {"place", {{"point", {62, 22}}}},
                                            {"set", {{"animals", 2}, {"approachSeconds", 6}, {"hoverHeight", hoverHeight},
                                                     {"approachBearing", bearing}}}}})}};
}

// The stand-in Critic: an answer at once, kept by `settle` exactly as the real hook keeps one.
class StubEvaluation final : public ai::DeferredResult {
public:
    explicit StubEvaluation(directing::EvaluationReport report) : report_(std::move(report)) {}
    [[nodiscard]] bool done() const override { return true; }
    [[nodiscard]] std::string phase() const override { return "judged"; }
    [[nodiscard]] Result<json> take() override {
        json out = report_.toJson();
        out["summary"] = report_.headline;
        return out;
    }
    void cancel() override {}
    void settle(app::Engine& engine, const json& value) override {
        if (auto r = directing::EvaluationReport::fromJson(value)) {
            engine.directingEvaluations().push_back(std::move(*r));
        }
    }

private:
    directing::EvaluationReport report_;
};

directing::EvaluationReport judge(const ai::EvaluationRequest& request) {
    directing::EvaluationReport r;
    r.planId = request.planId;
    r.revision = request.revision;
    r.candidate = request.candidate;
    r.label = request.label;
    r.from = request.from;
    r.until = request.until;
    r.mode = request.mode;
    r.evaluator = "stub";
    const float hover = request.plan["setPieces"][0]["set"].value("hoverHeight", 23.0f);
    const bool repeats = std::abs(hover - 23.0f) < 1.0f;
    r.dimensions = {{"composition", repeats ? 0.62 : 0.79}, {"motion", 0.8}};
    directing::EvaluationFinding stale;
    stale.id = "F002";
    stale.key = "stale_hold:s04";
    stale.kind = "issue";
    stale.severity = "medium";
    stale.title = "nothing new after 4 s";
    stale.start = 64.0;
    stale.end = 66.0;
    stale.items = {"river"};
    if (repeats) {
        directing::EvaluationFinding same;
        same.id = "F001";
        same.key = "repeated_composition:s03";
        same.kind = "issue";
        same.severity = "high";
        same.title = "the abduction is framed like the valley's last one";
        same.start = 60.0;
        same.end = 63.0;
        same.items = {"river"};
        r.findings.push_back(same);
    }
    r.findings.push_back(stale);
    r.headline = fmt::format("{} issue(s)", r.findings.size());
    return r;
}

struct Session {
    app::Engine engine{app::EngineMode::Offline};
    app::JobSystem jobs{2};
    ai::ControlPlane plane{engine, &jobs};
    app::EditSystem edits;
    std::unique_ptr<app::EditHistoryTransactionSink> sink;
    std::shared_ptr<ai::ScriptedProvider> provider;
    std::vector<ai::EvaluationRequest> requests;

    Session() {
        plane.setCredentialStore(std::make_unique<ai::MemoryCredentialStore>());
        sink = std::make_unique<app::EditHistoryTransactionSink>(engine, edits, plane.transactionSink());
        plane.setTransactionSink(sink.get());
        plane.setEvaluationHook([this](app::Engine&, const ai::EvaluationRequest& request)
                                    -> Result<std::shared_ptr<ai::DeferredResult>> {
            requests.push_back(request);
            return std::make_shared<StubEvaluation>(judge(request));
        });
    }
    ~Session() { plane.shutdown(); }

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

    bool runUntil(const std::shared_ptr<ai::AgentTask>& task, ai::TaskState state, double seconds = 60.0) {
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
};

} // namespace

TEST_CASE("the Director evaluates a candidate, proposes it, is asked to vary it, and compares the revision",
          "[directing][agent][evaluate][adr931]") {
    Session s;
    REQUIRE(s.engine.loadProject(fs::path(AVGEN_SOURCE_DIR) / "tests/data/setpieces/setpiece-lab.json").has_value());
    const json first = riverPlan(23.0f, 180.0f);
    const json varied = riverPlan(40.0f, 90.0f);
    s.provider = std::make_shared<ai::ScriptedProvider>(std::vector<ai::ScriptedTurn>{
        // The request: evaluate the candidate in scratch before putting it in front of anybody.
        ai::ScriptedTurn{"Trying an abduction by the river, then asking the evaluator about it.",
                         {call("e1", "director.evaluate",
                               json{{"plan", first}, {"from", 55}, {"until", 70}, {"label", "river, first try"}})},
                         ai::StopReason::EndTurn, {}},
        ai::ScriptedTurn{"It works; proposing it.", {call("p1", "director.propose_plan", json{{"plan", first}})},
                         ai::StopReason::EndTurn, {}},
        ai::ScriptedTurn{"Proposed; approve to apply.", {}, ai::StopReason::EndTurn, {}},
        // Modify: vary it, evaluate the revision, compare, and propose only the winner.
        ai::ScriptedTurn{"Lower and from the east this time; evaluating.",
                         {call("e2", "director.evaluate",
                               json{{"plan", varied}, {"from", 55}, {"until", 70}, {"label", "river, varied"}})},
                         ai::StopReason::EndTurn, {}},
        ai::ScriptedTurn{"Comparing the two.", {call("c1", "director.compare", json{{"planId", "ufo"}})},
                         ai::StopReason::EndTurn, {}},
        ai::ScriptedTurn{"The repetition is resolved; proposing the varied one.",
                         {call("p2", "director.propose_plan", json{{"plan", varied}})}, ai::StopReason::EndTurn, {}},
        ai::ScriptedTurn{"Revised; approve to apply.", {}, ai::StopReason::EndTurn, {}}});
    s.plane.setProvider(s.provider);

    auto task = s.plane.submit("Add a UFO abduction of two cows by the river at 1:00");
    REQUIRE(s.runUntil(task, ai::TaskState::AwaitingApproval));
    // The evaluation ran on the candidate, in scratch: the project holds no plan yet, and the report
    // was kept, keyed to the plan and the revision the candidate would become.
    REQUIRE(s.requests.size() == 1);
    CHECK_FALSE(s.requests[0].installed);
    CHECK(s.requests[0].planId == "ufo");
    CHECK(s.requests[0].revision == 1);
    CHECK(s.engine.directingPlans().empty());
    REQUIRE(s.engine.directingEvaluations().size() == 1);
    CHECK(s.engine.directingEvaluations()[0].label == "river, first try");
    const json e1 = s.resultOf("e1");
    INFO(e1.dump().substr(0, 600));
    CHECK(e1["result"]["findings"][0]["key"] == "repeated_composition:s03");
    CHECK(e1["result"]["findings"][0]["items"] == json::array({"river"}));
    // Evaluating did not stop at the gate: one proposal is waiting, the one after the evaluation.
    REQUIRE(task->proposal());
    CHECK(task->proposal()->planId == "ufo");

    // The person: "vary it".
    auto second = s.plane.modifyCurrentTask("the lift reads the same as the valley's last one; vary it");
    REQUIRE(second != nullptr);
    REQUIRE(s.runUntil(second, ai::TaskState::AwaitingApproval));
    REQUIRE(s.requests.size() == 2);
    CHECK(s.requests[1].candidate != s.requests[0].candidate); // two candidates of one revision differ
    REQUIRE(s.engine.directingEvaluations().size() == 2);
    // The comparison the model read: the repetition resolved, the stale hold persisting, composition up.
    const json c1 = s.resultOf("c1")["result"];
    INFO(c1.dump());
    CHECK(c1["resolved"] == json::array({"repeated_composition:s03"}));
    CHECK(c1["persisting"] == json::array({"stale_hold:s04"}));
    CHECK(c1["new"].empty());
    bool improved = false;
    for (const json& d : c1["dimensions"]) {
        improved = improved || (d["name"] == "composition" && d["verdict"] == "improved");
    }
    CHECK(improved);
    // Still nothing applied; the waiting proposal is the varied one.
    CHECK(s.engine.directingPlans().empty());
    REQUIRE(second->proposal());
    CHECK(second->proposal()->plan["setPieces"][0]["set"]["hoverHeight"] == 40.0);

    // Approved: the winner lands as one undo, and the evaluations stay with its plan.
    REQUIRE(s.plane.approveCurrentTask());
    CHECK(s.edits.history().undoSize() == 1);
    REQUIRE(s.engine.directingPlans().size() == 1);
    const bool river = std::any_of(s.engine.composition()->staging().scenarios.begin(),
                                   s.engine.composition()->staging().scenarios.end(),
                                   [](const stage::ScenarioDesc& sc) { return sc.name == "setpiece/river"; });
    CHECK(river);
    CHECK(s.engine.directingEvaluations().size() == 2);
    // And the same comparison is there to ask for by label afterwards.
    ai::ToolRegistry registry;
    ai::registerDirectorTools(registry);
    ai::ToolContext ctx(s.engine);
    const ai::ToolResult again = registry.invoke(
        "director.compare", json{{"planId", "ufo"}, {"a", "river, first try"}, {"b", "river, varied"}}, ctx);
    REQUIRE(again.success);
    CHECK(again.value["resolved"] == json::array({"repeated_composition:s03"}));
}
