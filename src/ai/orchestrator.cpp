#include "ai/orchestrator.hpp"

#include "ai/director_tools.hpp"
#include "directing/compiler.hpp"

#include "api/session.hpp"
#include "app/engine.hpp"
#include "core/log.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>

namespace avgen::ai {
namespace {

std::string nextTaskId() {
    static std::atomic<std::uint64_t> counter{1};
    return fmt::format("task-{}", counter.fetch_add(1, std::memory_order_relaxed));
}

} // namespace

const char* taskStateName(TaskState state) {
    switch (state) {
    case TaskState::Idle: return "Idle";
    case TaskState::Preparing: return "Preparing";
    case TaskState::WaitingForModel: return "Thinking";
    case TaskState::ExecutingTools: return "Working";
    case TaskState::Validating: return "Validating";
    case TaskState::RollingBack: return "Rolling back";
    case TaskState::AwaitingApproval: return "Awaiting approval";
    case TaskState::Committing: return "Committing";
    case TaskState::Rejected: return "Rejected";
    case TaskState::Completed: return "Done";
    case TaskState::Failed: return "Failed";
    case TaskState::Cancelled: return "Cancelled";
    }
    return "Idle";
}

bool taskStateIsTerminal(TaskState state) {
    return state == TaskState::Completed || state == TaskState::Failed ||
           state == TaskState::Cancelled || state == TaskState::Rejected;
}

const char* activityKindName(ActivityKind kind) {
    switch (kind) {
    case ActivityKind::TaskStarted: return "task-started";
    case ActivityKind::StateChanged: return "state";
    case ActivityKind::Plan: return "plan";
    case ActivityKind::ModelText: return "text";
    case ActivityKind::ToolStarted: return "tool-started";
    case ActivityKind::ToolProgress: return "tool-progress";
    case ActivityKind::ToolCompleted: return "tool-completed";
    case ActivityKind::ToolFailed: return "tool-failed";
    case ActivityKind::TransactionOpened: return "transaction-opened";
    case ActivityKind::TransactionCommitted: return "transaction-committed";
    case ActivityKind::TransactionRolledBack: return "transaction-rolled-back";
    case ActivityKind::Note: return "note";
    case ActivityKind::TaskFinished: return "task-finished";
    }
    return "note";
}

// ---- AgentTask -----------------------------------------------------------------------------------

AgentTask::AgentTask(std::string prompt) : id_(nextTaskId()), prompt_(std::move(prompt)) {}

std::vector<Activity> AgentTask::activities() const {
    const std::lock_guard lock(mutex_);
    return activities_;
}

std::size_t AgentTask::activityCount() const {
    const std::lock_guard lock(mutex_);
    return activities_.size();
}

TaskOutcome AgentTask::outcome() const {
    const std::lock_guard lock(mutex_);
    return outcome_;
}

double AgentTask::elapsedSeconds() const {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - started_).count();
}

void AgentTask::setState(TaskState state) {
    // Release, not relaxed: this path publishes *terminal* states too -- `run` sets `Failed` here
    // when a task cannot start -- and a terminal state stored relaxed synchronises with nothing, so
    // a reader that sees it has no guarantee of seeing why. See `AgentTask::state`.
    state_.store(state, std::memory_order_release);
    Activity activity;
    activity.kind = ActivityKind::StateChanged;
    activity.title = taskStateName(state);
    append(std::move(activity));
}

void AgentTask::append(Activity activity) {
    activity.timestamp = elapsedSeconds();
    const std::lock_guard lock(mutex_);
    activities_.push_back(std::move(activity));
}

std::optional<ToolContext::Proposal> AgentTask::proposal() const {
    const std::lock_guard lock(mutex_);
    return proposal_;
}

nlohmann::json AgentTask::observation() const {
    const std::lock_guard lock(mutex_);
    return observation_;
}

void AgentTask::setObservation(nlohmann::json observation) {
    const std::lock_guard lock(mutex_);
    observation_ = std::move(observation);
}

void AgentTask::setProposal(ToolContext::Proposal proposal) {
    const std::lock_guard lock(mutex_);
    proposal_ = std::move(proposal);
}

void AgentTask::holdOutcome(TaskOutcome outcome) {
    const std::lock_guard lock(mutex_);
    outcome_ = std::move(outcome);
}

void AgentTask::finish(TaskOutcome outcome) {
    {
        const std::lock_guard lock(mutex_);
        outcome_ = std::move(outcome);
    }
    // The state is set last. A UI that sees a terminal state is entitled to assume the outcome is
    // already there, and the other order would make that assumption wrong on exactly the frames a
    // race is hardest to reproduce.
    state_.store(outcome_.rejected    ? TaskState::Rejected
                 : outcome_.cancelled ? TaskState::Cancelled
                 : outcome_.success   ? TaskState::Completed
                                      : TaskState::Failed,
                 std::memory_order_release);
}

// ---- Orchestrator ----------------------------------------------------------------------------------

Orchestrator::Orchestrator(api::Session& session) : session_(&session) {}

ToolResult Orchestrator::invoke(const ToolCall& call, AgentTask& task, ChangeLog& changes,
                                const std::string& transaction) {
    // The task owns its transaction (ADR-2153). A model that tried to open, close or abandon it would
    // take the task's rollback away from it, so those tools are refused here rather than passed on.
    if (call.name.rfind("transaction.", 0) == 0) {
        return ToolResult::failure(ToolErrorCode::FailedPrecondition,
                                   fmt::format("'{}' is not available to the assistant: its task already runs in "
                                               "one transaction",
                                               call.name),
                                   "make the changes; the task commits or rolls them back as one");
    }
    api::CallOptions options;
    options.cancel = task.cancel();
    if (!transaction.empty()) {
        options.transaction = transaction;
    }
    // A task has always been able to mix an undoable edit with a save or an import. It still can; the
    // transaction is marked partial and says what its rollback cannot reach.
    options.irreversible = api::Irreversible::Accept;
    options.onProgress = [&task, &call](float fraction, const std::string& note) {
        Activity a;
        a.kind = ActivityKind::ToolProgress;
        a.title = call.name;
        a.detail = note;
        a.progress = fraction;
        task.append(std::move(a));
    };
    api::CallArtifacts artifacts;
    artifacts.observation = task.observation();
    const api::CallResult called = session_->call(call.name, call.arguments, options, &artifacts);
    if (!called.success && called.error && called.error->code == ToolErrorCode::Cancelled) {
        return ToolResult::failure(ToolErrorCode::Cancelled,
                                   fmt::format("'{}' did not run: the task was cancelled or the application did not "
                                               "service it in time",
                                               call.name));
    }
    // The change log is merged out here rather than inside the tool, so a tool body never has to know
    // it is running inside a transaction.
    for (const ChangeRecord& record : called.changes) {
        changes.note(record.target, record.detail, record.clamped);
    }
    if (artifacts.proposal) {
        task.setProposal(*artifacts.proposal);
    }
    const std::shared_ptr<RecordingHandle>& deferred = artifacts.deferredProposal;
    const std::shared_ptr<DeferredResult>& answer = artifacts.deferredResult;
    if (answer) {
        // ADR-767: the tool started work that answers (a watch). Waited for here, off the main thread.
        std::string last;
        while (!answer->done()) {
            if (task.cancel().cancelled()) {
                answer->cancel();
            }
            if (const std::string phase = answer->phase(); phase != last) {
                last = phase;
                Activity a;
                a.kind = ActivityKind::ToolProgress;
                a.title = call.name;
                a.detail = phase;
                task.append(std::move(a));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{50});
        }
        auto value = answer->take();
        if (!value) {
            return ToolResult::failure(ToolErrorCode::Internal, value.error().message);
        }
        // ADR-931: what the answer means for the project (an evaluation is kept), on the main thread.
        (void)session_->onHost([&](app::Engine& engine) { answer->settle(engine, *value); });
        if (value->contains("observation")) {
            task.setObservation((*value)["observation"]);
        }
        if (value->contains("summary") && (*value)["summary"].is_string()) {
            return ToolResult::ok(*value, (*value)["summary"].get<std::string>());
        }
        const std::size_t count = value->contains("observation") ? (*value)["observation"]["events"].size() : 0;
        return ToolResult::ok(*value, fmt::format("{} event(s) observed", count));
    }
    if (deferred) {
        // ADR-765: the tool started a recording. Waited for HERE, on the task's worker thread, so
        // the main thread keeps pumping frames; its phases are the tool's progress.
        std::string last;
        while (!deferred->done()) {
            if (task.cancel().cancelled()) {
                deferred->cancel();
            }
            if (const std::string phase = deferred->phase(); phase != last) {
                last = phase;
                Activity a;
                a.kind = ActivityKind::ToolProgress;
                a.title = call.name;
                a.detail = phase;
                task.append(std::move(a));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{50});
        }
        auto plan = deferred->take();
        if (!plan) {
            return ToolResult::failure(ToolErrorCode::Internal, "recording failed: " + plan.error().message);
        }
        std::optional<Result<ToolContext::Proposal>> proposed;
        (void)session_->onHost([&](app::Engine& engine) { proposed = proposalFor(engine, *plan); });
        if (!proposed || !*proposed) {
            return ToolResult::failure(ToolErrorCode::Internal,
                                       proposed ? "the recording cannot be proposed: " + proposed->error().message
                                                : std::string("the recording could not be proposed"));
        }
        task.setProposal(**proposed);
        nlohmann::json out{{"recorded", true}, {"planId", (*proposed)->planId}, {"tier", plan->toJson()["tier"]},
                           {"diff", (*proposed)->diff}};
        return ToolResult::ok(out, fmt::format("recorded '{}'; proposed for approval", (*proposed)->planId));
    }
    return called.toToolResult();
}

void Orchestrator::run(AgentTask& task) {
    const auto begun = std::chrono::steady_clock::now();
    TaskOutcome outcome;

    const auto fail = [&](std::string message, bool cancelled = false) {
        outcome.success = false;
        outcome.cancelled = cancelled;
        outcome.error = std::move(message);
        Activity a;
        a.kind = ActivityKind::TaskFinished;
        a.title = cancelled ? "Cancelled" : "Failed";
        a.detail = outcome.error;
        a.success = false;
        task.append(std::move(a));
        task.finish(outcome);
    };

    if (!provider_) {
        // §46: this is the "not configured" path, and it must be a clean, informative stop.
        task.setState(TaskState::Failed);
        fail("AI assistant is not configured. Open Settings -> AI and add a provider.");
        return;
    }

    task.setState(TaskState::Preparing);
    {
        Activity a;
        a.kind = ActivityKind::TaskStarted;
        a.title = "Task started";
        a.detail = task.prompt();
        task.append(std::move(a));
    }

    // Context, gathered on the main thread because it reads engine state (§38).
    nlohmann::json ambient = nlohmann::json::object();
    const bool gathered = session_->onHost([&](app::Engine& engine) { ambient = ambientContext(engine); }, task.cancel());
    if (!gathered) {
        fail("cancelled before the task started", true);
        return;
    }

    const ToolRegistry& tools = session_->tools();
    CompletionRequest request;
    request.model = model_;
    request.system = systemPrompt(tools);
    request.temperature = temperature_;
    request.cancel = task.cancel();
    request.tools.reserve(tools.size());
    for (const Tool* tool : tools.all()) {
        request.tools.push_back(&tool->definition);
    }
    request.messages.push_back(Message::user(composeUserTurn(
        task.briefing().empty() ? task.prompt() : task.prompt() + "\n\n" + task.briefing(), ambient)));

    // The transaction opens lazily, on the first tool that says it mutates the project: the session's
    // `transaction.begin`, captured on the host thread like every call. An inspection-only task costs
    // nothing, and a snapshot taken for one would be noise in the rollback list.
    std::string transaction;
    ChangeLog changes;
    const auto openTransaction = [&] {
        if (!transaction.empty()) {
            return;
        }
        api::CallOptions options;
        options.cancel = task.cancel();
        const api::CallResult begun = session_->call("transaction.begin", nlohmann::json{{"label", task.prompt()}}, options);
        if (!begun.success) {
            // Cancelled or the host stopped before it opened (no tool will run either), or the session has
            // no way to roll back -- the task then runs without one, as it always has.
            if (begun.error && begun.error->code != ToolErrorCode::Cancelled) {
                log::warn("ai: {} runs without a transaction: {}", task.id(), begun.summary);
            }
            return;
        }
        transaction = begun.result.at("id").get<std::string>();
        if (const auto point = begun.result.find("rollbackPoint"); point != begun.result.end() && point->is_string()) {
            outcome.snapshotId = point->get<std::string>();
        }
        Activity a;
        a.kind = ActivityKind::TransactionOpened;
        a.title = "Rollback point taken";
        a.detail = begun.result.value("backedBy", std::string{});
        task.append(std::move(a));
    };

    // Commit or roll back on the host thread, through the session. A FRESH cancel token, deliberately: a
    // cancelled task is exactly the one whose rollback must still run, and the task's own token would make
    // the queue refuse it. If the host has stopped servicing calls, the application is tearing the engine
    // down; the session abandons the transaction when it closes rather than touch the engine from here.
    const auto closeTransaction = [&](bool commit) -> nlohmann::json {
        if (transaction.empty()) {
            return nullptr;
        }
        const api::CallResult closed =
            session_->call(commit ? "transaction.commit" : "transaction.rollback", nlohmann::json{{"id", transaction}});
        if (!closed.success) {
            log::warn("ai: {} could not {} its transaction: {}", task.id(), commit ? "commit" : "roll back",
                      closed.summary);
            transaction.clear();
            return nullptr;
        }
        transaction.clear();
        return closed.result;
    };

    std::string finalText;
    bool sawPlan = false;

    for (int iteration = 0; iteration < limits_.maxIterations; ++iteration) {
        outcome.iterations = iteration + 1;
        if (task.cancel().cancelled()) {
            break;
        }
        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                                             begun)
                                   .count();
        if (elapsed > limits_.maxSeconds) {
            Activity a;
            a.kind = ActivityKind::Note;
            a.title = "Time budget reached";
            a.detail = fmt::format("stopped after {:.0f}s", elapsed);
            a.success = false;
            task.append(std::move(a));
            break;
        }

        task.setState(TaskState::WaitingForModel);
        auto response = provider_->complete(request);
        if (!response) {
            task.setState(TaskState::RollingBack);
            if (!transaction.empty()) {
                (void)closeTransaction(false);
                outcome.rolledBack = true;
                Activity a;
                a.kind = ActivityKind::TransactionRolledBack;
                a.title = "Rolled back";
                a.detail = "the provider failed part-way through, so nothing was left half-applied";
                a.success = false;
                task.append(std::move(a));
            }
            fail(response.error().message, task.cancel().cancelled());
            return;
        }
        outcome.usage += response->usage;

        if (!response->text.empty()) {
            Activity a;
            // The model's first text before any tool call is its plan, and §9 asks for the plan to
            // be shown as such rather than as more chat.
            a.kind = (!sawPlan && !response->toolCalls.empty()) ? ActivityKind::Plan
                                                                : ActivityKind::ModelText;
            a.title = a.kind == ActivityKind::Plan ? "Plan" : "Note";
            a.detail = response->text;
            task.append(std::move(a));
            sawPlan = true;
            finalText = response->text;
        }

        if (response->toolCalls.empty()) {
            if (response->stopReason == StopReason::MaxTokens) {
                Activity a;
                a.kind = ActivityKind::Note;
                a.title = "Response truncated";
                a.detail = "the model reached its output limit";
                a.success = false;
                task.append(std::move(a));
            }
            break; // the model is done
        }

        request.messages.push_back(Message::assistant(response->text, response->toolCalls));

        task.setState(TaskState::ExecutingTools);
        std::vector<ToolCallResult> results;
        results.reserve(response->toolCalls.size());
        for (const ToolCall& call : response->toolCalls) {
            if (task.cancel().cancelled()) {
                break;
            }
            if (outcome.toolCalls >= limits_.maxToolCalls) {
                Activity a;
                a.kind = ActivityKind::Note;
                a.title = "Tool budget reached";
                a.detail = fmt::format("stopped after {} calls", outcome.toolCalls);
                a.success = false;
                task.append(std::move(a));
                break;
            }
            const Tool* definition = tools.find(call.name);
            if (definition != nullptr && definition->definition.annotations.mutatesProject) {
                openTransaction();
            }

            {
                Activity a;
                a.kind = ActivityKind::ToolStarted;
                a.title = call.name;
                a.detail = definition != nullptr ? definition->definition.title : "unknown tool";
                a.iteration = iteration;
                task.append(std::move(a));
            }
            const auto callBegan = std::chrono::steady_clock::now();
            ToolResult result = invoke(call, task, changes, transaction);
            const double durationMs =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                          callBegan)
                    .count();
            ++outcome.toolCalls;
            task.countToolCall();

            Activity a;
            a.kind = result.success ? ActivityKind::ToolCompleted : ActivityKind::ToolFailed;
            a.title = call.name;
            a.detail = result.summary;
            a.durationMs = durationMs;
            a.success = result.success;
            a.iteration = iteration;
            task.append(std::move(a));

            ToolCallResult back;
            back.id = call.id;
            back.name = call.name;
            back.content = result.toJson();
            back.isError = !result.success;
            results.push_back(std::move(back));
        }

        if (results.empty()) {
            break; // cancelled or budget-stopped before anything ran
        }
        request.messages.push_back(Message::results(std::move(results)));
    }

    if (task.cancel().cancelled()) {
        task.setState(TaskState::RollingBack);
        if (!transaction.empty()) {
            (void)closeTransaction(false);
            outcome.rolledBack = true;
            Activity a;
            a.kind = ActivityKind::TransactionRolledBack;
            a.title = "Rolled back";
            a.detail = "cancelled, so every change made by this task was undone together";
            task.append(std::move(a));
        }
        outcome.summary = finalText;
        fail("cancelled", true);
        return;
    }

    // §18 and §33: read the resulting state back rather than trusting the tool reports. The commit
    // measures the diff between the document at begin and the project now, which is also how "73
    // underlying modifications" becomes a sentence a person can read.
    task.setState(TaskState::Validating);
    if (!transaction.empty()) {
        const nlohmann::json committed = closeTransaction(true);
        if (committed.is_object()) {
            outcome.changedTargets = committed.value("changedTargets", std::vector<std::string>{});
            outcome.editState = committed.value("editState", std::uint64_t{0});
        }
        Activity a;
        a.kind = ActivityKind::TransactionCommitted;
        a.title = "Committed";
        a.detail = fmt::format("{} parameter value(s) changed across {} tool call(s)",
                               outcome.changedTargets.size(), outcome.toolCalls);
        task.append(std::move(a));
    }

    outcome.success = true;
    outcome.summary = finalText.empty()
                          ? fmt::format("Finished after {} tool call(s).", outcome.toolCalls)
                          : finalText;
    {
        Activity a;
        a.kind = ActivityKind::TaskFinished;
        a.title = "Done";
        a.detail = outcome.summary;
        task.append(std::move(a));
    }
    log::info("ai: {} finished: {} tool call(s), {} iteration(s), {} parameter(s) changed, "
              "{} in / {} out tokens",
              task.id(), outcome.toolCalls, outcome.iterations, outcome.changedTargets.size(),
              outcome.usage.inputTokens, outcome.usage.outputTokens);
    if (const auto proposal = task.proposal()) {
        // A plan is proposed: the task stops here, having changed nothing the plan describes, and
        // waits for the person. The outcome so far is kept; the state is published LAST (release),
        // so a reader that sees AwaitingApproval also sees the proposal and the outcome.
        task.holdOutcome(outcome);
        Activity a;
        a.kind = ActivityKind::Plan;
        a.title = "Proposed: " + proposal->planId;
        a.detail = proposal->diff;
        task.append(std::move(a));
        task.setState(TaskState::AwaitingApproval);
        return;
    }
    task.finish(outcome);
}

bool Orchestrator::approve(AgentTask& task) {
    if (task.state() != TaskState::AwaitingApproval) {
        return false;
    }
    const std::optional<ToolContext::Proposal> proposal = task.proposal();
    TaskOutcome outcome = task.outcome();
    task.setState(TaskState::Committing);
    // One transaction: the history sink makes the install one undo, labelled with the request; the
    // snapshot underneath makes a failed commit leave nothing behind. The session's own, opened and
    // closed here on the host thread (the calls run inline).
    std::string transaction;
    if (const api::CallResult begun = session_->call("transaction.begin", nlohmann::json{{"label", task.prompt()}});
        begun.success) {
        transaction = begun.result.at("id").get<std::string>();
    }
    std::optional<Result<CommitReport>> committed;
    if (!session_->onHost([&](app::Engine& engine) {
            committed = proposal ? commitProposal(engine, *proposal)
                                 : Result<CommitReport>(std::unexpected(Error{"there is no proposal to approve"}));
        })) {
        committed = Result<CommitReport>(std::unexpected(Error{"the application did not service the approval"}));
    }
    Activity a;
    a.kind = *committed ? ActivityKind::TransactionCommitted : ActivityKind::TransactionRolledBack;
    if (!*committed) {
        if (!transaction.empty()) {
            (void)session_->call("transaction.rollback", nlohmann::json{{"id", transaction}});
            outcome.rolledBack = true;
        }
        outcome.success = false;
        outcome.error = committed->error().message;
        a.title = "Not applied";
        a.detail = outcome.error;
        a.success = false;
        task.append(std::move(a));
        task.finish(std::move(outcome));
        return true;
    }
    if (!transaction.empty()) {
        const api::CallResult closed = session_->call("transaction.commit", nlohmann::json{{"id", transaction}});
        if (closed.success) {
            outcome.editState = closed.result.value("editState", std::uint64_t{0});
        }
    }
    outcome.success = true;
    outcome.error.clear();
    a.title = "Applied";
    a.detail = fmt::format("plan '{}' revision {}: {} piece(s) of content, verified", (*committed)->planId,
                           (*committed)->revision, (*committed)->produced);
    task.append(std::move(a));
    task.finish(std::move(outcome));
    return true;
}

bool Orchestrator::reject(AgentTask& task, std::string reason) {
    if (task.state() != TaskState::AwaitingApproval) {
        return false;
    }
    TaskOutcome outcome = task.outcome();
    outcome.success = false;
    outcome.rejected = true;
    outcome.error = std::move(reason);
    Activity a;
    a.kind = ActivityKind::TaskFinished;
    a.title = "Rejected";
    a.detail = "nothing was changed";
    task.append(std::move(a));
    task.finish(std::move(outcome));
    return true;
}

} // namespace avgen::ai
