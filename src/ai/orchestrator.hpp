#pragma once

// The agent loop (ADR-094, spec §3/§9/§10/§26/§27/§28, addendum §15).
//
// ## The loop, made explicit
//
//   GatherContext -> ModelTurn -> tool calls? -> (yes: ExecuteTools -> ToolResults -> ModelTurn
//                                                 no:  Validate -> Commit -> FinalResponse)
//
// Addendum §15 asks for the state machine to be explicit rather than buried in a large function,
// and `TaskState` is that: every transition is published as an `Activity`, so the panel shows
// structured progress rather than a spinner, and shows it *without parsing model text*. §9 is
// specific that the UI must not scrape prose to find out what is happening, and this is why.
//
// ## What the user is shown, and what they are not
//
// Shown: the state, the plan the model wrote as its first text, every tool call with its
// human-readable description, duration and outcome, and a final summary of what changed. Not
// shown: private model reasoning. Addendum §14 is explicit -- expose structured execution state,
// not raw chain-of-thought -- and building the UI on the latter would make it depend on a thing
// providers are entitled to stop returning.
//
// ## Transactions and the loop
//
// One transaction per task, opened before the first tool that mutates anything and closed once.
// Not one per tool call: the user asked for one thing, and undoing "make the scene cinematic"
// halfway is not an outcome anybody wants. It is opened lazily so a purely inspective task ("what
// is in this scene?") takes no snapshot at all.
//
// Rollback happens on: a provider error after mutations, cancellation, budget exhaustion with work
// in flight, and an unhandled failure. It does **not** happen because a single tool returned an
// error -- a structured tool error is information the agent is expected to recover from, and
// rolling back the whole task because one call had a typo in a parameter path would make the agent
// unable to correct itself (§25, §42's failure case).
//
// ## Budgets
//
// §55: the agent must never silently make unlimited modifications. Iterations, tool calls and wall
// time are all capped; hitting a cap ends the task with a report of what was done, not silently.

#include "ai/context.hpp"
#include "ai/main_thread_queue.hpp"
#include "ai/provider.hpp"
#include "ai/tool_api.hpp"
#include "ai/tool_context.hpp"
#include "ai/transaction.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace avgen::app {
class Engine;
} // namespace avgen::app

namespace avgen::ai {

// addendum §15's states, and the Director's approval gate (spec §19, ADR-757):
//
//   Preparing -> WaitingForModel <-> ExecutingTools -> Validating -> Completed
//                                                    \-> AwaitingApproval -> Committing -> Completed
//                                                                        \-> Rejected
//
// A task whose model proposed a plan (`director.propose_plan`) does not finish: it waits, having
// changed nothing, until the person approves -- the commit then runs on the main thread in one
// transaction and is verified -- or rejects, which changes nothing at all.
enum class TaskState : std::uint8_t {
    Idle,
    Preparing,        // gathering context
    WaitingForModel,
    ExecutingTools,
    Validating,       // reading back what changed
    RollingBack,
    AwaitingApproval, // a plan is proposed; nothing has changed; the person decides
    Committing,       // approved: installing and verifying, on the main thread
    Completed,
    Failed,
    Cancelled,
    Rejected,         // the person said no; the project is as it was
};
[[nodiscard]] const char* taskStateName(TaskState state);
[[nodiscard]] bool taskStateIsTerminal(TaskState state);

enum class ActivityKind : std::uint8_t {
    TaskStarted,
    StateChanged,
    Plan,          // the model's own text before its first tool call
    ModelText,
    ToolStarted,
    ToolProgress,
    ToolCompleted,
    ToolFailed,
    TransactionOpened,
    TransactionCommitted,
    TransactionRolledBack,
    Note,
    TaskFinished,
};
[[nodiscard]] const char* activityKindName(ActivityKind kind);

// §10: every tool invocation produces a structured record. Not a log line -- a value the UI reads.
struct Activity {
    ActivityKind kind = ActivityKind::Note;
    double timestamp = 0.0; // seconds since the task started
    std::string title;      // "parameter.set", "Planning"
    std::string detail;     // human-readable; a tool's own summary, or an error message
    double durationMs = 0.0;
    bool success = true;
    int iteration = 0;
    float progress = -1.0f; // < 0 when the step cannot measure itself (app::JobStatus's rule)
};

struct TaskLimits {
    int maxIterations = 12;   // model turns
    int maxToolCalls = 64;
    double maxSeconds = 300.0;
};

struct TaskOutcome {
    bool success = false;
    bool cancelled = false;
    bool rolledBack = false;
    std::string summary;   // the model's closing words, or why it stopped
    std::string error;
    Usage usage;
    int toolCalls = 0;
    int iterations = 0;
    std::string snapshotId; // the rollback point, empty when nothing was mutated
    bool rejected = false;  // the proposal was declined
    // The editor-history state this task's commit produced, 0 when it pushed none (ADR-752). The
    // host undoes the task through its history with it; the AI layer never interprets it.
    std::uint64_t editState = 0;
    std::vector<std::string> changedTargets;
};

// The shared object the worker writes and the UI reads. Everything mutable is behind the mutex or
// atomic, because the two are genuinely on different threads and pretending otherwise is how a UI
// ends up reading a half-written string.
class AgentTask {
public:
    explicit AgentTask(std::string prompt);

    [[nodiscard]] const std::string& id() const { return id_; }
    [[nodiscard]] const std::string& prompt() const { return prompt_; }
    // **Acquire**, to pair with the release stores in `finish` and `setState`. A terminal state is
    // a promise that the outcome, the activities and everything the provider recorded are already
    // written -- `finish`'s own comment says a UI "is entitled to assume the outcome is already
    // there" -- and a relaxed load keeps no such promise: it may observe the state and still not
    // observe the writes that came before it. ThreadSanitizer reported eight races on exactly that
    // (2026-09-23), all of them the main thread reading a finished task's data. `finish` stored with
    // release all along; the reading side was the half that was missing.
    [[nodiscard]] TaskState state() const { return state_.load(std::memory_order_acquire); }
    [[nodiscard]] bool finished() const { return taskStateIsTerminal(state()); }
    [[nodiscard]] const CancelToken& cancel() const { return cancel_; }
    void requestCancel() const { cancel_.cancel(); }

    // A copy, deliberately: the UI iterates it for a whole frame while the worker keeps appending.
    [[nodiscard]] std::vector<Activity> activities() const;
    [[nodiscard]] std::size_t activityCount() const;
    // The final report. Empty until the task reaches a terminal state -- read `toolCallsSoFar()`
    // and `activities()` while it is running.
    [[nodiscard]] TaskOutcome outcome() const;
    [[nodiscard]] double elapsedSeconds() const;
    // Live, so the panel can say "4 steps" while the task is still working rather than only
    // afterwards. Atomic because the worker writes it and the UI reads it every frame.
    [[nodiscard]] int toolCallsSoFar() const { return toolCalls_.load(std::memory_order_relaxed); }
    void countToolCall() { toolCalls_.fetch_add(1, std::memory_order_relaxed); }

    // The plan this task proposed, waiting for approval; empty when it proposed none.
    [[nodiscard]] std::optional<ToolContext::Proposal> proposal() const;

    // Worker side.
    void setState(TaskState state);
    void append(Activity activity);
    void finish(TaskOutcome outcome);
    void setProposal(ToolContext::Proposal proposal);
    // The model phase's outcome, kept while the task waits for approval: `finish` would publish a
    // terminal state, which a waiting task is not.
    void holdOutcome(TaskOutcome outcome);

private:
    std::string id_;
    std::string prompt_;
    std::atomic<TaskState> state_{TaskState::Idle};
    std::atomic<int> toolCalls_{0};
    CancelToken cancel_;
    mutable std::mutex mutex_;
    std::vector<Activity> activities_;
    TaskOutcome outcome_;
    std::optional<ToolContext::Proposal> proposal_;
    std::chrono::steady_clock::time_point started_ = std::chrono::steady_clock::now();
};

class Orchestrator {
public:
    Orchestrator(app::Engine& engine, const ToolRegistry& tools, MainThreadQueue& queue);

    void setProvider(std::shared_ptr<Provider> provider) { provider_ = std::move(provider); }
    [[nodiscard]] const std::shared_ptr<Provider>& provider() const { return provider_; }
    void setTransactionSink(TransactionSink* sink) { sink_ = sink; }
    void setPerformanceSource(PerformanceSource source) { performance_ = std::move(source); }
    // Where `project.create` / `open` / `save_as` resolve a name. Empty means those tools refuse,
    // which is the right default: a session that never says where projects live should not have
    // one guessed for it.
    void setProjectsRoot(std::filesystem::path root) { projectsRoot_ = std::move(root); }
    void setContentRoots(std::vector<std::filesystem::path> roots) { contentRoots_ = std::move(roots); }
    void setLimits(TaskLimits limits) { limits_ = limits; }
    [[nodiscard]] const TaskLimits& limits() const { return limits_; }
    void setModel(std::string model) { model_ = std::move(model); }
    void setTemperature(double temperature) { temperature_ = temperature; }

    // Runs a whole task on the calling thread, to completion or to `AwaitingApproval`. **Worker
    // threads only** -- it blocks on the network and on the main-thread queue. `task` is written
    // throughout so a UI can watch.
    void run(AgentTask& task);

    // The person's decision on a task in `AwaitingApproval`. **Main thread only**: approving
    // installs the plan's content, which is the engine's to change. Approving re-compiles the plan
    // against the project as it is NOW and refuses if the result differs from what was shown (the
    // project changed underneath the proposal); otherwise it installs it inside one transaction --
    // one undo, through the history sink -- and verifies the installed content against the plan's
    // fingerprints before committing. Anything short of that rolls back. Rejecting changes nothing.
    // False when the task is not awaiting approval.
    bool approve(AgentTask& task);
    bool reject(AgentTask& task, std::string reason = "rejected");

private:
    [[nodiscard]] ToolResult invokeOnMainThread(const ToolCall& call, AgentTask& task,
                                                ChangeLog& changes);

    app::Engine* engine_;
    const ToolRegistry* tools_;
    MainThreadQueue* queue_;
    std::shared_ptr<Provider> provider_;
    TransactionSink* sink_ = nullptr;
    PerformanceSource performance_;
    std::filesystem::path projectsRoot_;
    std::vector<std::filesystem::path> contentRoots_;
    TaskLimits limits_;
    std::string model_;
    double temperature_ = 0.4;
};

} // namespace avgen::ai
