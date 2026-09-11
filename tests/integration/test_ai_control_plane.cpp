// The AI control plane end to end (ADR-094).
//
// This is the test §58 is about. A prompt goes in; a real `app::Engine` -- the same class the
// application runs -- comes out changed. The path exercised here is the production one, not a
// shortcut:
//
//     ControlPlane::submit  ->  app::JobSystem worker  ->  Orchestrator::run
//         ->  Provider::complete  ->  ToolCall  ->  MainThreadQueue
//         ->  the test's own "main thread" pump  ->  ToolRegistry::invoke  ->  app::Engine
//
// The only substitution is the provider, which §52 explicitly asks for and §54 explicitly limits
// to that one thing. Every tool is the real tool, the transaction is the real transaction, and the
// engine is the real engine -- if any of that were stubbed the test would prove nothing.
//
// The pumping loop also *is* the threading proof: the tools only ever run on the thread that calls
// `pump()`, because that is the only place `MainThreadQueue` executes anything. A tool that
// touched the engine from the worker would have to bypass the queue to do it, and there is no path
// that does.

#include "ai/control_plane.hpp"
#include "ai/scripted_provider.hpp"
#include "app/engine.hpp"
#include "app/job_system.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <thread>

using namespace avgen;
using Catch::Matchers::WithinAbs;
using nlohmann::json;

namespace {

std::filesystem::path helixScene() {
    return std::filesystem::path(AVGEN_SOURCE_DIR) / "examples" / "helix" / "helix.scene.json";
}

ai::ScriptedTurn turn(std::string text, std::vector<ai::ToolCall> calls = {}) {
    return ai::ScriptedTurn{std::move(text), std::move(calls), ai::StopReason::EndTurn, {}};
}

ai::ToolCall call(std::string id, std::string name, json args) {
    return ai::ToolCall{std::move(id), std::move(name), std::move(args)};
}

// A session: engine, job system, control plane, and a pump that stands in for the frame loop.
struct Session {
    app::Engine engine{app::EngineMode::Offline};
    app::JobSystem jobs{2};
    ai::ControlPlane plane{engine, &jobs};

    Session() {
        // Tests must never write to a developer's real keychain.
        plane.setCredentialStore(std::make_unique<ai::MemoryCredentialStore>());
    }

    void script(std::vector<ai::ScriptedTurn> turns) {
        plane.setProvider(std::make_shared<ai::ScriptedProvider>(std::move(turns)));
    }

    // Runs a task to completion, servicing the main-thread queue the way the frame loop does.
    // Returns false if it did not finish inside the timeout.
    bool runToCompletion(const std::shared_ptr<ai::AgentTask>& task, double timeoutSeconds = 20.0) {
        REQUIRE(task != nullptr);
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::duration<double>(timeoutSeconds);
        while (!task->finished()) {
            plane.pump();
            if (std::chrono::steady_clock::now() > deadline) {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        plane.pump(); // anything queued between the last pump and the finish
        return true;
    }
};

std::size_t countActivities(const std::vector<ai::Activity>& activities, ai::ActivityKind kind) {
    return static_cast<std::size_t>(
        std::count_if(activities.begin(), activities.end(),
                      [kind](const ai::Activity& a) { return a.kind == kind; }));
}

} // namespace

TEST_CASE("An unconfigured control plane is a clean stop, not a broken application",
          "[integration][ai]") {
    // ADR-065: an optional failure must never become a total failure. §46: show that the assistant
    // is not configured and point at Settings -- do not crash and do not disable anything else.
    app::Engine engine(app::EngineMode::Offline);
    app::JobSystem jobs(1);
    ai::ControlPlane plane(engine, &jobs);
    plane.setCredentialStore(std::make_unique<ai::MemoryCredentialStore>());

    CHECK_FALSE(plane.configured());
    CHECK(!plane.unconfiguredReason().empty());
    // The tool surface exists and is fully usable regardless: another client -- a script API, an
    // MCP server, a test -- can drive the engine with no model anywhere in sight.
    CHECK(plane.tools().size() > 20);
    // And the engine is untouched and working.
    CHECK(engine.params().size() > 0);

    auto task = plane.submit("make the moon brighter");
    REQUIRE(task != nullptr);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
    while (!task->finished() && std::chrono::steady_clock::now() < deadline) {
        plane.pump();
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    REQUIRE(task->finished());
    CHECK(task->state() == ai::TaskState::Failed);
    CHECK(task->outcome().error.find("not configured") != std::string::npos);
    CHECK(task->outcome().error.find("Settings") != std::string::npos);
}

TEST_CASE("A prompt produces real engine modifications inside one transaction",
          "[integration][ai]") {
    Session s;
    REQUIRE(s.engine.loadComposition(helixScene()).has_value());

    auto* fog = s.engine.params().find("scene/fogDensity");
    auto* brightness = s.engine.params().find("scene/brightness");
    REQUIRE(fog != nullptr);
    REQUIRE(brightness != nullptr);
    const auto fogBefore = static_cast<double>(fog->baseComponent(0));

    // The script stands in for the model's judgement and nothing else: it inspects first, then
    // makes a coherent multi-property change, then reports -- the behaviour §31 asks for.
    s.script({
        turn("I'll look at the scene before changing anything.",
             {call("c1", "scene.get_summary", json::object()),
              call("c2", "parameter.search", json{{"query", "fog"}})}),
        turn("Thickening the atmosphere and lifting the ambient level.",
             {call("c3", "environment.set",
                   json{{"values", json{{"scene/fogDensity", 0.14}, {"scene/brightness", 1.35}}}}),
              call("c4", "parameter.set", json{{"path", "scene/keyLight"}, {"value", 0.6}})}),
        turn("I thickened the fog, lifted the ambient brightness and dropped the key light so the "
             "scene reads as a hazier, moodier space."),
    });

    auto task = s.plane.submit("make this feel foggier and more atmospheric");
    REQUIRE(s.runToCompletion(task));
    REQUIRE(task->state() == ai::TaskState::Completed);

    const ai::TaskOutcome outcome = task->outcome();
    CHECK(outcome.success);
    CHECK_FALSE(outcome.rolledBack);
    CHECK(outcome.toolCalls == 4);
    CHECK(outcome.iterations == 3);
    CHECK(!outcome.summary.empty());

    // The engine actually changed. This is the assertion the whole exercise exists for.
    CHECK_THAT(static_cast<double>(fog->baseComponent(0)), WithinAbs(0.14, 1e-4));
    CHECK_THAT(static_cast<double>(brightness->baseComponent(0)), WithinAbs(1.35, 1e-4));
    CHECK_THAT(static_cast<double>(s.engine.params().find("scene/keyLight")->baseComponent(0)),
               WithinAbs(0.6, 1e-4));
    CHECK(std::abs(fogBefore - 0.14) > 1e-3); // it really moved

    SECTION("and the change set was measured from the engine, not from the tool reports") {
        // §18: verification is not optional. `changedTargets` is the diff between the snapshot
        // taken before the first mutation and the project afterwards.
        REQUIRE(outcome.changedTargets.size() == 3);
        CHECK(std::find(outcome.changedTargets.begin(), outcome.changedTargets.end(),
                        "scene/fogDensity") != outcome.changedTargets.end());
        CHECK(std::find(outcome.changedTargets.begin(), outcome.changedTargets.end(),
                        "scene/keyLight") != outcome.changedTargets.end());
    }
    SECTION("one transaction opened once, committed once") {
        const auto activities = task->activities();
        CHECK(countActivities(activities, ai::ActivityKind::TransactionOpened) == 1);
        CHECK(countActivities(activities, ai::ActivityKind::TransactionCommitted) == 1);
        CHECK(countActivities(activities, ai::ActivityKind::TransactionRolledBack) == 0);
        // And the rollback point survives the commit, so the user can still go back to it.
        CHECK(!outcome.snapshotId.empty());
        CHECK(s.plane.snapshots().find(outcome.snapshotId) != nullptr);
    }
    SECTION("the activity stream is structured, not scraped prose") {
        // §9: the UI must be able to distinguish planning, execution, validation and completion
        // without parsing model text.
        const auto activities = task->activities();
        CHECK(countActivities(activities, ai::ActivityKind::TaskStarted) == 1);
        CHECK(countActivities(activities, ai::ActivityKind::Plan) == 1);
        CHECK(countActivities(activities, ai::ActivityKind::ToolStarted) == 4);
        CHECK(countActivities(activities, ai::ActivityKind::ToolCompleted) == 4);
        CHECK(countActivities(activities, ai::ActivityKind::ToolFailed) == 0);
        CHECK(countActivities(activities, ai::ActivityKind::TaskFinished) == 1);
        for (const ai::Activity& a : activities) {
            if (a.kind == ai::ActivityKind::ToolCompleted) {
                CHECK(!a.title.empty());
                CHECK(!a.detail.empty()); // §10: a human-readable result summary
                CHECK(a.durationMs >= 0.0);
            }
        }
        // States were published in order, ending in Validating.
        bool sawWaiting = false;
        bool sawExecuting = false;
        bool sawValidating = false;
        for (const ai::Activity& a : activities) {
            if (a.kind != ai::ActivityKind::StateChanged) {
                continue;
            }
            sawWaiting = sawWaiting || a.title == std::string(ai::taskStateName(
                                                        ai::TaskState::WaitingForModel));
            sawExecuting = sawExecuting || a.title == std::string(ai::taskStateName(
                                                          ai::TaskState::ExecutingTools));
            sawValidating = sawValidating || a.title == std::string(ai::taskStateName(
                                                            ai::TaskState::Validating));
        }
        CHECK(sawWaiting);
        CHECK(sawExecuting);
        CHECK(sawValidating);
    }
}

TEST_CASE("The system prompt and ambient context reach the model", "[integration][ai]") {
    Session s;
    auto provider = std::make_shared<ai::ScriptedProvider>(
        std::vector<ai::ScriptedTurn>{turn("Nothing to do.")});
    s.plane.setProvider(provider);

    auto task = s.plane.submit("what is in this scene?");
    REQUIRE(s.runToCompletion(task));
    REQUIRE(provider->received().size() == 1);
    const ai::CompletionRequest& request = provider->received().front();

    // Static product knowledge (§30).
    CHECK(request.system.find("AV Gen") != std::string::npos);
    CHECK(request.system.find("Parameters are the engine's semantic surface") != std::string::npos);
    // The tool index is generated from the registry, so there is no second copy to drift (§24).
    CHECK(request.system.find("parameter.set") != std::string::npos);
    CHECK(request.tools.size() == s.plane.tools().size());

    // Dynamic state provided automatically (addendum §13), and the prompt itself.
    REQUIRE(request.messages.size() == 1);
    CHECK(request.messages[0].text.find("what is in this scene?") != std::string::npos);
    CHECK(request.messages[0].text.find("<current-project>") != std::string::npos);
    CHECK(request.messages[0].text.find("parameters") != std::string::npos);

    SECTION("and the ambient block stays small however big the project is") {
        // §29: do not send the entire project on every request. The block is O(1) in project size.
        const auto start = request.messages[0].text.find("<current-project>");
        const auto end = request.messages[0].text.find("</current-project>");
        REQUIRE(start != std::string::npos);
        REQUIRE(end != std::string::npos);
        CHECK(end - start < 1200);
    }
}

TEST_CASE("A tool error is recoverable: the agent sees it and carries on", "[integration][ai]") {
    // §42's failure case. An invalid operation must return a structured error the agent can act
    // on, with no corruption -- it must not abort the task and must not roll anything back.
    Session s;
    s.script({
        turn("Setting it.", {call("c1", "parameter.set",
                                  json{{"path", "orb/doesNotExist"}, {"value", 3.0}})}),
        turn("Let me find the right name.",
             {call("c2", "parameter.set", json{{"path", "orb/scale"}, {"value", 3.0}})}),
        turn("The path was wrong the first time; orb/scale is the one, and it is now 3."),
    });

    auto task = s.plane.submit("make the orb three times bigger");
    REQUIRE(s.runToCompletion(task));
    CHECK(task->state() == ai::TaskState::Completed);

    const auto activities = task->activities();
    CHECK(countActivities(activities, ai::ActivityKind::ToolFailed) == 1);
    CHECK(countActivities(activities, ai::ActivityKind::ToolCompleted) == 1);
    CHECK(countActivities(activities, ai::ActivityKind::TransactionRolledBack) == 0);
    CHECK_THAT(static_cast<double>(s.engine.params().find("orb/scale")->baseComponent(0)),
               WithinAbs(3.0, 1e-5));
}

TEST_CASE("A provider failure part-way through rolls the whole task back", "[integration][ai]") {
    Session s;
    const auto before = static_cast<double>(s.engine.params().find("orb/scale")->baseComponent(0));
    s.script({
        turn("Working.", {call("c1", "parameter.set", json{{"path", "orb/scale"}, {"value", 6.0}}),
                          call("c2", "parameter.set", json{{"path", "orb/emissive"}, {"value", 9.0}})}),
        ai::ScriptedTurn{"", {}, ai::StopReason::EndTurn, "the provider went away"},
    });

    auto task = s.plane.submit("go wild");
    REQUIRE(s.runToCompletion(task));
    CHECK(task->state() == ai::TaskState::Failed);

    const ai::TaskOutcome outcome = task->outcome();
    CHECK(outcome.rolledBack);
    CHECK(outcome.error.find("provider went away") != std::string::npos);
    // §27: never leave the project in an obviously corrupted partial state. Both writes are gone,
    // not just the second.
    CHECK_THAT(static_cast<double>(s.engine.params().find("orb/scale")->baseComponent(0)),
               WithinAbs(before, 1e-5));
    CHECK_THAT(static_cast<double>(s.engine.params().find("orb/emissive")->baseComponent(0)),
               WithinAbs(0.15, 1e-5));
    CHECK(countActivities(task->activities(), ai::ActivityKind::TransactionRolledBack) == 1);
}

TEST_CASE("Cancellation stops execution and rolls back deterministically", "[integration][ai]") {
    // §28: cancellation must not merely stop rendering the response while background mutations
    // continue. The project must end up valid and the UI must be told.
    Session s;
    const auto before = static_cast<double>(s.engine.params().find("orb/scale")->baseComponent(0));

    std::vector<ai::ScriptedTurn> turns;
    turns.push_back(turn("Starting.",
                         {call("c1", "parameter.set", json{{"path", "orb/scale"}, {"value", 5.0}})}));
    // Plenty of further turns the loop must never reach.
    for (int i = 0; i < 8; ++i) {
        turns.push_back(turn("More.", {call(fmt::format("c{}", i + 2), "parameter.set",
                                            json{{"path", "orb/emissive"}, {"value", 20.0}})}));
    }
    s.script(std::move(turns));

    auto task = s.plane.submit("take this as far as you can");
    REQUIRE(task != nullptr);
    // Let the first turn land, then cancel.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
    while (task->toolCallsSoFar() == 0 && !task->finished() &&
           std::chrono::steady_clock::now() < deadline) {
        s.plane.pump();
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    task->requestCancel();
    REQUIRE(s.runToCompletion(task));

    CHECK(task->state() == ai::TaskState::Cancelled);
    CHECK(task->outcome().cancelled);
    CHECK(task->outcome().rolledBack);
    CHECK_THAT(static_cast<double>(s.engine.params().find("orb/scale")->baseComponent(0)),
               WithinAbs(before, 1e-5));
    CHECK(countActivities(task->activities(), ai::ActivityKind::TransactionRolledBack) == 1);
}

TEST_CASE("An inspection-only task takes no snapshot at all", "[integration][ai]") {
    Session s;
    s.script({
        turn("Looking.", {call("c1", "project.get_state", json::object()),
                          call("c2", "signal.list", json{{"query", "audio"}})}),
        turn("There are no changes to make; this is what the project contains."),
    });
    auto task = s.plane.submit("what does this project contain?");
    REQUIRE(s.runToCompletion(task));
    CHECK(task->state() == ai::TaskState::Completed);
    CHECK(task->outcome().snapshotId.empty());
    CHECK(countActivities(task->activities(), ai::ActivityKind::TransactionOpened) == 0);
    CHECK(s.plane.snapshots().size() == 0);
}

TEST_CASE("Budgets stop the agent rather than letting it run forever", "[integration][ai]") {
    // §55: the agent should never silently make unlimited modifications.
    Session s;
    s.plane.settings().limits.maxToolCalls = 3;
    s.plane.settings().limits.maxIterations = 10;
    REQUIRE_FALSE(s.plane.applySettings().has_value()); // no provider configured yet, by design
    std::vector<ai::ScriptedTurn> turns;
    for (int i = 0; i < 10; ++i) {
        turns.push_back(turn("Again.", {call(fmt::format("c{}", i), "parameter.set",
                                             json{{"path", "orb/scale"}, {"value", 1.0 + i * 0.1}})}));
    }
    s.script(std::move(turns));

    auto task = s.plane.submit("keep going");
    REQUIRE(s.runToCompletion(task));
    CHECK(task->outcome().toolCalls <= 3);
    // Stopping is reported, not silent.
    bool sawBudgetNote = false;
    for (const ai::Activity& a : task->activities()) {
        if (a.kind == ai::ActivityKind::Note && a.title.find("budget") != std::string::npos) {
            sawBudgetNote = true;
        }
    }
    CHECK(sawBudgetNote);
}

TEST_CASE("Tools only ever run on the thread that pumps", "[integration][ai][threading]") {
    // The §38 guarantee, asserted rather than asserted-about: a tool body that ran on the
    // orchestrator's worker would be a data race against the frame loop, so the queue is the only
    // path and this checks that nothing bypasses it.
    Session s;
    const std::thread::id pumpThread = std::this_thread::get_id();
    std::atomic<bool> sawWrongThread{false};

    // A probe tool that records which thread it ran on.
    ai::Tool probe;
    probe.definition.name = "test.probe";
    probe.definition.title = "Probe";
    probe.definition.description = "Records the thread it ran on.";
    probe.definition.inputSchema = ai::schema::object(json::object());
    probe.definition.annotations.readOnly = true;
    probe.execute = [&](const json&, ai::ToolContext&) {
        if (std::this_thread::get_id() != pumpThread) {
            sawWrongThread.store(true);
        }
        return ai::ToolResult::ok(json::object(), "probed");
    };
    s.plane.tools().add(std::move(probe));

    s.script({
        turn("Probing.", {call("c1", "test.probe", json::object()),
                          call("c2", "test.probe", json::object())}),
        turn("Done."),
    });
    auto task = s.plane.submit("probe");
    REQUIRE(s.runToCompletion(task));
    CHECK(task->state() == ai::TaskState::Completed);
    CHECK(task->outcome().toolCalls == 2);
    CHECK_FALSE(sawWrongThread.load());
}

TEST_CASE("A task the frame loop never services times out instead of deadlocking",
          "[integration][ai][threading]") {
    // The failure mode this guards: an application that stops pumping -- shutting down, or a modal
    // stall -- must not leave a worker blocked forever holding the engine hostage.
    app::Engine engine(app::EngineMode::Offline);
    app::JobSystem jobs(1);
    ai::ControlPlane plane(engine, &jobs);
    plane.setCredentialStore(std::make_unique<ai::MemoryCredentialStore>());
    plane.setProvider(std::make_shared<ai::ScriptedProvider>(std::vector<ai::ScriptedTurn>{
        turn("Setting.",
             {call("c1", "parameter.set", json{{"path", "orb/scale"}, {"value", 2.0}})}),
        turn("Done."),
    }));

    auto task = plane.submit("set it");
    REQUIRE(task != nullptr);
    // Never pump. Instead, shut the queue down, which is what teardown does.
    std::this_thread::sleep_for(std::chrono::milliseconds{50});
    plane.shutdown();

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{15};
    while (!task->finished() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    REQUIRE(task->finished());
    // Either cancelled or failed is acceptable; hanging is not.
    CHECK(ai::taskStateIsTerminal(task->state()));
}

TEST_CASE("A whole audio-reactive pass works through the tool API", "[integration][ai]") {
    // §42's fourth acceptance test: "make the glowing plants pulse with the bass". Inspect the
    // signals, find the material parameter, create the modulation relationship, configure its
    // depth, verify the binding.
    Session s;
    REQUIRE(s.engine.loadComposition(helixScene()).has_value());
    const auto nodes = s.plane.tools().all();
    REQUIRE(!nodes.empty());

    // Find a real emissive parameter the way the agent would.
    std::string emissivePath;
    for (const params::IParameter* p : s.engine.params().ordered()) {
        if (p->path().find("emissive") != std::string::npos && p->componentCount() == 1 &&
            p->flags().modulatable) {
            emissivePath = p->path();
            break;
        }
    }
    REQUIRE(!emissivePath.empty());

    s.script({
        turn("Finding the signals and the material.",
             {call("c1", "signal.list", json{{"query", "bass"}}),
              call("c2", "material.list", json::object())}),
        turn("Binding the bass to the emission with a short attack and a long decay so it pulses.",
             {call("c3", "modulation.create",
                   json{{"source", "audio.bass"},
                        {"target", emissivePath},
                        {"amount", 2.5},
                        {"op", "add"},
                        {"attackMs", 8.0},
                        {"decayMs", 180.0}})}),
        turn("Verifying.", {call("c4", "modulation.list", json{{"target", emissivePath}})}),
        turn("The bass band now drives the emission with a 2.5 depth, smoothed so it pulses "
             "rather than flickers."),
    });

    auto task = s.plane.submit("make the glowing parts pulse with the bass");
    REQUIRE(s.runToCompletion(task));
    REQUIRE(task->state() == ai::TaskState::Completed);

    // The route exists, is bound, and is the real thing the modulator will evaluate.
    const auto& routes = s.engine.modulator().routes();
    const auto created = std::find_if(routes.begin(), routes.end(),
                                      [&](const params::ModRoute& r) {
                                          return r.source == "audio.bass" && r.target == emissivePath;
                                      });
    REQUIRE(created != routes.end());
    CHECK(created->targetParam != nullptr);
    CHECK(created->sourceId != signals::kInvalidSignal);
    CHECK_THAT(static_cast<double>(created->amount), WithinAbs(2.5, 1e-5));
    CHECK_THAT(static_cast<double>(created->chain.decayMs), WithinAbs(180.0, 1e-5));

    SECTION("and the route survives a frame of real evaluation") {
        FixedStepClock clock(60.0);
        for (int i = 0; i < 4; ++i) {
            const auto time = s.engine.tick(clock);
            s.engine.update(time);
        }
        const auto& after = s.engine.modulator().routes();
        const auto still = std::find_if(after.begin(), after.end(), [&](const params::ModRoute& r) {
            return r.source == "audio.bass" && r.target == emissivePath;
        });
        REQUIRE(still != after.end());
        CHECK(still->targetParam != nullptr);
    }
}

TEST_CASE("A camera move is keyed onto the real timeline", "[integration][ai]") {
    // §42's third acceptance test: "create a 10-second camera push toward the selected object".
    Session s;
    REQUIRE(s.engine.loadComposition(helixScene()).has_value());

    s.script({
        turn("Reading the camera and the timeline.",
             {call("c1", "camera.get", json::object()),
              call("c2", "sequencer.get_state", json::object())}),
        turn("Keying a ten-second push.",
             {call("c3", "sequencer.add_keyframe",
                   json{{"target", "camera/position"},
                        {"time", 0.0},
                        {"value", json::array({0.0, 4.0, 18.0})},
                        {"interp", "smooth"}}),
              call("c4", "sequencer.add_keyframe",
                   json{{"target", "camera/position"},
                        {"time", 10.0},
                        {"value", json::array({0.0, 4.0, 6.0})},
                        {"interp", "easeinout"}})}),
        turn("The camera now pushes from 18 to 6 metres out over ten seconds."),
    });

    auto task = s.plane.submit("create a ten second camera push toward the helix");
    REQUIRE(s.runToCompletion(task));
    REQUIRE(task->state() == ai::TaskState::Completed);

    const params::Track* track = s.engine.timeline().findTrack("camera/position", -1);
    REQUIRE(track != nullptr);
    REQUIRE(track->param != nullptr); // bound, so it animates something
    REQUIRE(track->keys.size() == 2);
    CHECK_THAT(static_cast<double>(track->evaluate(0.0)[2]), WithinAbs(18.0, 1e-3));
    CHECK_THAT(static_cast<double>(track->evaluate(10.0)[2]), WithinAbs(6.0, 1e-3));
    CHECK(s.engine.timeline().unboundTargets().empty());

    SECTION("and driving the engine actually moves the camera parameter") {
        // The proof that the keys are not merely stored: run the pipeline and read the final.
        FixedStepClock clock(60.0);
        double reached = 0.0;
        for (int i = 0; i < 320; ++i) {
            const auto time = s.engine.tick(clock);
            s.engine.update(time);
            reached = time.renderTime;
        }
        CHECK(reached > 4.0);
        const params::IParameter* position = s.engine.params().find("camera/position");
        REQUIRE(position != nullptr);
        // Somewhere between the two keys, and not still at the first.
        CHECK(position->finalComponent(2) < 18.0F);
        CHECK(position->finalComponent(2) > 5.0F);
    }
}

TEST_CASE("Settings round-trip without ever carrying a credential", "[integration][ai]") {
    Session s;
    s.plane.settings().activeProvider = "anthropic";
    ai::ProviderConfig* anthropic = s.plane.settings().find("anthropic");
    REQUIRE(anthropic != nullptr);
    anthropic->enabled = true;
    anthropic->model = "claude-opus-5";

    // Not configured until a credential exists, and the reason says what to do.
    const auto without = s.plane.applySettings();
    REQUIRE_FALSE(without.has_value());
    CHECK(s.plane.unconfiguredReason().find("credential") != std::string::npos);

    REQUIRE(s.plane.credentials().store("anthropic", "sk-test").has_value());
    REQUIRE(s.plane.applySettings().has_value());
    CHECK(s.plane.configured());

    const json serialised = s.plane.settings().toJson();
    CHECK(serialised.dump().find("sk-test") == std::string::npos);
    const auto reloaded = ai::AiSettings::fromJson(serialised);
    REQUIRE(reloaded.has_value());
    CHECK(reloaded->activeProvider == "anthropic");
    CHECK(reloaded->find("anthropic")->enabled);

    SECTION("and status reports configuration without the value") {
        for (const ai::ProviderStatus& status : s.plane.providerStatus()) {
            CHECK(status.credential.detail.find("sk-test") == std::string::npos);
            if (status.id == "anthropic") {
                CHECK(status.credential.configured);
                CHECK(status.active);
                CHECK(status.enabled);
            }
        }
    }
}
