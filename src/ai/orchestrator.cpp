#include "ai/orchestrator.hpp"

#include "ai/director_tools.hpp"
#include "directing/compiler.hpp"

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

Orchestrator::Orchestrator(app::Engine& engine, const ToolRegistry& tools, MainThreadQueue& queue)
    : engine_(&engine), tools_(&tools), queue_(&queue) {}

ToolResult Orchestrator::invokeOnMainThread(const ToolCall& call, AgentTask& task,
                                            ChangeLog& changes) {
    ToolResult result;
    std::shared_ptr<RecordingHandle> deferred;
    std::shared_ptr<DeferredResult> answer;
    const bool ran = queue_->run(
        [&] {
            ToolContext ctx(*engine_);
            ctx.setCancelToken(task.cancel());
            if (!projectsRoot_.empty()) {
                ctx.setProjectsRoot(projectsRoot_);
            }
            if (!contentRoots_.empty()) {
                ctx.setContentRoots(contentRoots_);
            }
            if (performance_) {
                ctx.setPerformanceSource(performance_);
            }
            if (recordingHook_) {
                ctx.setRecordingHook(recordingHook_);
            }
            if (watchHook_) {
                ctx.setWatchHook(watchHook_);
            }
            ctx.setObservation(task.observation());
            ctx.onProgress = [&task, &call](float fraction, const std::string& note) {
                Activity a;
                a.kind = ActivityKind::ToolProgress;
                a.title = call.name;
                a.detail = note;
                a.progress = fraction;
                task.append(std::move(a));
            };
            result = tools_->invoke(call.name, call.arguments, ctx);
            // The change log is merged out here rather than inside the tool, so a tool body never
            // has to know it is running inside a transaction.
            for (const ChangeRecord& record : ctx.changes().records()) {
                changes.note(record.target, record.detail, record.clamped);
            }
            if (ctx.proposal()) {
                task.setProposal(*ctx.proposal());
            }
            deferred = ctx.deferredProposal();
            answer = ctx.deferredResult();
        },
        task.cancel());
    if (!ran) {
        return ToolResult::failure(ToolErrorCode::Cancelled,
                                   fmt::format("'{}' did not run: the task was cancelled or the "
                                               "application did not service it in time",
                                               call.name));
    }
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
        if (value->contains("observation")) {
            task.setObservation((*value)["observation"]);
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
        (void)queue_->run([&] { proposed = proposalFor(*engine_, *plan); }, CancelToken{});
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
    return result;
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
    const bool gathered = queue_->run([&] { ambient = ambientContext(*engine_); }, task.cancel());
    if (!gathered) {
        fail("cancelled before the task started", true);
        return;
    }

    CompletionRequest request;
    request.model = model_;
    request.system = systemPrompt(*tools_);
    request.temperature = temperature_;
    request.cancel = task.cancel();
    request.tools.reserve(tools_->size());
    for (const Tool* tool : tools_->all()) {
        request.tools.push_back(&tool->definition);
    }
    request.messages.push_back(Message::user(composeUserTurn(
        task.briefing().empty() ? task.prompt() : task.prompt() + "\n\n" + task.briefing(), ambient)));

    // The transaction opens lazily, on the first tool that says it mutates the project. An
    // inspection-only task should cost nothing, and a snapshot taken for one would be noise in the
    // rollback list.
    std::unique_ptr<Transaction> transaction;
    ChangeLog changes;
    nlohmann::json before;
    const auto openTransaction = [&] {
        if (transaction || sink_ == nullptr || !sink_->available()) {
            return;
        }
        // Captured before the sink opens, so the diff is measured against the same instant the
        // rollback point is -- and both on the thread that pumps. The sink reads the whole engine
        // (a snapshot; since ADR-752 every domain `EditCapture` measures), and a sink opened from
        // this worker would read it while the frame loop writes it.
        (void)queue_->run(
            [&] {
                before = SnapshotStore::captureDocument(*engine_);
                transaction = std::make_unique<Transaction>(*sink_, task.prompt());
            },
            task.cancel());
        if (!transaction) {
            return; // cancelled or the queue shut down before it opened; no tool will run either
        }
        Activity a;
        a.kind = ActivityKind::TransactionOpened;
        a.title = "Rollback point taken";
        a.detail = std::string(sink_->kind());
        task.append(std::move(a));
    };

    // Commit or roll back on the pumping thread, for the reason the open is there. A FRESH cancel
    // token, deliberately: a cancelled task is exactly the one whose rollback must still run, and
    // the task's own token would make the queue refuse it. If the queue is already shut down the
    // application is tearing the engine down; touching it from here would be the race this exists
    // to prevent, so the transaction is abandoned instead and says so.
    const auto closeOnMainThread = [&](bool commit) {
        if (!transaction || !transaction->open()) {
            return;
        }
        const bool ran = queue_->run(
            [&] {
                if (commit) {
                    transaction->commit();
                    outcome.editState = sink_->committedEditState();
                } else {
                    transaction->rollback();
                }
            },
            CancelToken{});
        if (!ran) {
            log::warn("ai: {} could not {} its transaction: the application stopped servicing it",
                      task.id(), commit ? "commit" : "roll back");
            transaction->abandon();
        }
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
            if (transaction) {
                closeOnMainThread(false);
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
            const Tool* definition = tools_->find(call.name);
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
            ToolResult result = invokeOnMainThread(call, task, changes);
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
        if (transaction) {
            closeOnMainThread(false);
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

    // §18 and §33: read the resulting state back rather than trusting the tool reports. This is
    // the diff between the snapshot taken at the start and the project now, which is also how "73
    // underlying modifications" becomes a sentence a person can read.
    task.setState(TaskState::Validating);
    if (transaction) {
        (void)queue_->run(
            [&] {
                const nlohmann::json after = SnapshotStore::captureDocument(*engine_);
                outcome.changedTargets = SnapshotStore::changedParameters(before, after);
            },
            task.cancel());
        if (auto* snapshotSink = dynamic_cast<SnapshotTransactionSink*>(sink_)) {
            outcome.snapshotId = snapshotSink->openSnapshotId();
        }
        closeOnMainThread(true);
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
    // snapshot underneath makes a failed commit leave nothing behind.
    std::unique_ptr<Transaction> transaction;
    if (sink_ != nullptr && sink_->available()) {
        transaction = std::make_unique<Transaction>(*sink_, task.prompt());
    }
    auto committed = proposal ? commitProposal(*engine_, *proposal)
                              : Result<CommitReport>(std::unexpected(Error{"there is no proposal to approve"}));
    Activity a;
    a.kind = committed ? ActivityKind::TransactionCommitted : ActivityKind::TransactionRolledBack;
    if (!committed) {
        if (transaction) {
            transaction->rollback();
            outcome.rolledBack = true;
        }
        outcome.success = false;
        outcome.error = committed.error().message;
        a.title = "Not applied";
        a.detail = outcome.error;
        a.success = false;
        task.append(std::move(a));
        task.finish(std::move(outcome));
        return true;
    }
    if (transaction) {
        transaction->commit();
        outcome.editState = sink_->committedEditState();
    }
    outcome.success = true;
    outcome.error.clear();
    a.title = "Applied";
    a.detail = fmt::format("plan '{}' revision {}: {} piece(s) of content, verified", committed->planId,
                           committed->revision, committed->produced);
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
