#pragma once

// `avgen::api::Session`: the one object every client goes through to operate the engine (ADR-2153,
// Engine API stage S1; the design is 113-engine-architecture/03-phase0-api-contracts/02-session-api.md).
//
// It holds:
//   - the tool registry, immutable after `create`: the engine tools, the Director tools and the session
//     tools (`session.info`, `transaction.*`), so every client sees the same catalogue;
//   - the session's permissions (`SessionPermissions`), fixed at `create` and only ever narrowed, and the
//     `PathPolicy` every path-taking tool resolves through;
//   - its own `SnapshotStore` (two sessions never share rollback points);
//   - the transaction sink (the snapshot sink by default; Studio installs the edit-history sink, so a
//     transaction is one Cmd+Z) and at most one explicit transaction;
//   - a reference to its `Host`'s `MainThreadQueue`, the only way into engine state.
//
// `call` is the one entry point, from any thread. It looks the tool up, validates the arguments and
// checks permissions on the CALLER's thread (the registry is immutable, so that is safe), then runs the
// body on the host thread through the queue -- inline when the caller is the host thread. Its result is
// a `CallResult`, whose `toWire()` is the envelope every external client will receive.
//
// Transactions (Part 1 section 8.3):
//   - **Implicit**: a `mutatesProject` call with no transaction named runs in one of its own; a failure
//     rolls it back.
//   - **Explicit**: `transaction.begin {label}` returns an id; calls name it in `CallOptions::transaction`;
//     `transaction.commit` / `transaction.rollback` close it. One per session. While one is open, a
//     mutating call that does not name it is refused (FailedPrecondition): a change made beside the
//     transaction would be rolled back with it, by a snapshot taken before it.
//   - A non-undoable mutating call inside an explicit transaction is refused unless the caller passes
//     `Irreversible::Accept`, which marks the transaction partial; commit and rollback then say what they
//     could not restore.
//   - Files a call writes go into a compensation log (api/compensation.hpp).
//
// Live policy (owner decision 3): a `HostPolicy::Live` session whose host reports a show running refuses
// a client other than Studio's own assistant with `Busy` for structural and job calls, project calls and
// seeks. Reads stay available.
//
// Jobs (ADR-2159, S6): the session owns the job RECORDS (`jobs()`, a thread-safe table) and a scratch directory for
// the project snapshots renders load; the host owns the GPU work (`Host::submitJob`). `job.wait` blocks the CALLER
// on the table -- `call` serves it on the caller's thread, never through the host queue -- unless the caller is the
// host thread itself, which then drives the jobs while it waits.
//
// Revisions (ADR-2154): a successful `mutatesProject` call bumps `Engine::revisions()` once, after its
// implicit transaction commits; an explicit transaction bumps once, at commit; a rollback (explicit, or a
// failed call's implicit one) restores the counters recorded at its begin; a failed call bumps nothing. A
// call whose own work already moved the counters (a save, an open, a command pushed onto Studio's edit
// history by its sink) is not bumped again.
//
// The time policy (ADR-2154) is the host's: `Host::offeredTimePolicies` says which parked policies its
// loop honours, and `transport.set_policy` refuses the others.

#include "ai/main_thread_queue.hpp"
#include "ai/tool_api.hpp"
#include "ai/tool_context.hpp"
#include "ai/transaction.hpp"
#include "api/compensation.hpp"
#include "api/jobs.hpp"
#include "api/path_policy.hpp"
#include "core/error.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::app {
class Engine;
enum class TimePolicy : std::uint8_t; // app/engine.hpp (ADR-2154)
struct ProjectRevisions;
} // namespace avgen::app

namespace avgen::api {

enum class ClientKind : std::uint8_t { Studio, Cli, JsonRpc, Mcp, Test };
[[nodiscard]] const char* clientKindName(ClientKind kind);

struct ClientInfo {
    ClientKind kind = ClientKind::Test;
    std::string name;    // "avgen-studio", "poc-client"
    std::string version;
    [[nodiscard]] nlohmann::json toJson() const;
};

enum class HostPolicy : std::uint8_t {
    Authoring, // structural edits, seeks and jobs allowed
    Live,      // while the host reports a show running, a non-Studio client is refused (Busy) for those
};
[[nodiscard]] const char* hostPolicyName(HostPolicy policy);

enum class Irreversible : std::uint8_t { Refuse, Accept };

struct CallOptions {
    std::optional<std::string> transaction; // join this explicit transaction
    Irreversible irreversible = Irreversible::Refuse;
    ai::CancelToken cancel;
    std::chrono::milliseconds timeout{30'000}; // how long to wait for the host to service the call
    std::function<void(float, const std::string&)> onProgress;
};

struct Warning {
    std::string code;
    std::string message;
    std::string pointer;
};

struct CallResult {
    bool success = true;
    nlohmann::json result = nlohmann::json::object();
    std::string summary;
    std::vector<ai::ChangeRecord> changes;
    std::vector<Warning> warnings;
    std::optional<ai::ToolError> error;
    std::optional<std::string> transaction; // the one it ran in, implicit or explicit
    std::optional<std::string> job;         // set by the render tools and job.* (ADR-2159)

    // The envelope (Part 1 section 5.3): success, result, summary, changes, warnings, transaction, job,
    // and on failure error {code, message, recovery, details?}. Every key is present on every call, so a
    // client never has to ask whether one is there.
    [[nodiscard]] nlohmann::json toWire() const;
    // The compact form a model sees (`ToolResult::toJson`): success, then result or error. In-process only.
    [[nodiscard]] ai::ToolResult toToolResult() const;
    [[nodiscard]] static CallResult refused(ai::ToolError error);
};

// What an in-process client (the Orchestrator) also needs and never goes on the wire: the observation a
// Director tool reads, and the proposal and deferred handles it leaves behind.
struct CallArtifacts {
    nlohmann::json observation;                                 // in
    std::optional<ai::ToolContext::Proposal> proposal;          // out
    std::shared_ptr<ai::RecordingHandle> deferredProposal;      // out
    std::shared_ptr<ai::DeferredResult> deferredResult;         // out
};

class JobTable;

// A render handed to a host: the record it reports into, and what to render.
struct JobSubmission {
    std::shared_ptr<JobTable> table;
    RenderRequest request;
};

// What owns the Engine and its thread. Studio's is a `LocalHost` over `Application::engine_`; the EngineHost
// (`avgen serve`, `avgen call`) adds lazy GPU and render jobs (ADR-2159).
class Host {
public:
    virtual ~Host() = default;
    [[nodiscard]] virtual app::Engine& engine() = 0; // host thread only
    [[nodiscard]] virtual ai::MainThreadQueue& queue() = 0;
    // Puts the host's hooks into a call's context: the performance source, the recorder, the watcher, the
    // evaluator, the profiler. Host thread. A hook the host does not have is simply not installed, and
    // the tool that needs it says so.
    virtual void installHooks(ai::ToolContext& ctx) = 0;
    // The live policy's question (host thread): is a show running right now?
    [[nodiscard]] virtual bool showRunning() { return false; }
    // When it will stop, if the host knows (seconds); for `details.retryAfterMs`.
    [[nodiscard]] virtual std::optional<double> showRemainingSeconds() { return std::nullopt; }
    // Whether this host can run GPU work (ADR-2155). When it cannot, a tool whose permissions include "gpu"
    // answers `Unavailable` ("this host cannot render ..."), in its capability answer and when called.
    // ADR-2159: an EngineHost with a GPU half says true (Studio's LocalHost stays false until stage S10).
    [[nodiscard]] virtual bool canRender() { return false; }
    // ADR-2159. Host thread. Queues a GPU job whose record is already in `submission.table`; the host reports
    // into it from then on. A refusal (Busy: the queue is full; Unavailable: no GPU) leaves the record alone.
    [[nodiscard]] virtual std::optional<ai::ToolError> submitJob(JobSubmission submission);
    // Host thread: one slice of job work. True while jobs remain. A caller that waits for a job ON the host
    // thread (`avgen call`'s `job.wait`) drives them through this.
    virtual bool stepJobs() { return false; }
    // Host thread: stop every job reporting into `table` (its session is closing).
    virtual void cancelJobs(const JobTable& /*table*/) {}
    // ADR-2154: the time policies this host's loop honours, which `transport.set_policy` may choose
    // between. Never empty: a host offers at least the one it runs (the default: the engine's setting).
    [[nodiscard]] virtual std::vector<app::TimePolicy> offeredTimePolicies();
};

// A host over an engine someone else owns, with its own queue and the hooks an application installs.
// Studio's (Application sets the hooks and the show predicate), a headless run's, and the tests'.
class LocalHost final : public Host {
public:
    explicit LocalHost(app::Engine& engine) : engine_(&engine) {}

    [[nodiscard]] app::Engine& engine() override { return *engine_; }
    [[nodiscard]] ai::MainThreadQueue& queue() override { return queue_; }
    void installHooks(ai::ToolContext& ctx) override;
    [[nodiscard]] bool showRunning() override { return showRunning_ ? showRunning_() : false; }
    [[nodiscard]] std::optional<double> showRemainingSeconds() override {
        return showRemaining_ ? showRemaining_() : std::nullopt;
    }
    // Empty (the default): only the engine's current setting -- a headless run's Offline engine is
    // ticked by its fixed-step loop and honours nothing else. Studio offers StoppedAmbient and
    // StoppedFrozen; a test may offer Stepped.
    [[nodiscard]] std::vector<app::TimePolicy> offeredTimePolicies() override;
    void setOfferedTimePolicies(std::vector<app::TimePolicy> offered) { offered_ = std::move(offered); }

    void setPerformanceSource(ai::PerformanceSource source) { performance_ = std::move(source); }
    void setRecordingHook(ai::RecordingHook hook) { recording_ = std::move(hook); }
    void setWatchHook(ai::WatchHook hook) { watch_ = std::move(hook); }
    void setEvaluationHook(ai::EvaluationHook hook) { evaluation_ = std::move(hook); }
    void setProfileHook(ai::ProfileHook hook) { profile_ = std::move(hook); }
    void setShowPredicate(std::function<bool()> running, std::function<std::optional<double>()> remaining = {}) {
        showRunning_ = std::move(running);
        showRemaining_ = std::move(remaining);
    }

private:
    app::Engine* engine_;
    ai::MainThreadQueue queue_;
    ai::PerformanceSource performance_;
    ai::RecordingHook recording_;
    ai::WatchHook watch_;
    ai::EvaluationHook evaluation_;
    ai::ProfileHook profile_;
    std::function<bool()> showRunning_;
    std::function<std::optional<double>()> showRemaining_;
    std::vector<app::TimePolicy> offered_;
};

struct SessionOptions {
    ClientInfo client;
    SessionPermissions permissions;
    HostPolicy policy = HostPolicy::Authoring;
    std::size_t snapshotCapacity = ai::SnapshotStore::kDefaultCapacity;
    // Tools added to this session's registry beside the standard ones (a test's probe). They are part of
    // the immutable registry from `create` on, like every other tool.
    std::vector<ai::Tool> extraTools;
};

enum class SessionState : std::uint8_t { Ready, Closing, Closed };

// One tool's three answers (spec section 5, "Capability discovery").
struct ToolAvailability {
    bool supported = false; // in this build's catalogue
    bool available = false; // usable in this host now: its hooks installed, a composition when it needs one
    bool permitted = false; // this session's permissions allow it
    std::string unavailableReason;
    std::string deniedReason;
    [[nodiscard]] nlohmann::json toJson(std::string_view name) const;
};

class Session {
public:
    // Builds the per-session state. Does not open a project or touch the GPU. Fails when the permissions
    // name a relative root (a root must not depend on the working directory either).
    [[nodiscard]] static Result<std::unique_ptr<Session>> create(SessionOptions options, Host& host);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // ---- catalogue (any thread; immutable after create) ----
    [[nodiscard]] const ai::ToolRegistry& tools() const { return registry_; }

    // ---- the one entry point (any thread) ----
    [[nodiscard]] CallResult call(std::string_view tool, const nlohmann::json& args = nlohmann::json::object(),
                                  const CallOptions& options = {},
                                  CallArtifacts* artifacts = nullptr);
    // Runs `fn` against the engine on the host thread (inline when already there). For an in-process
    // client's own engine reads between calls (the Orchestrator's ambient context). False when the host did
    // not service it.
    [[nodiscard]] bool onHost(const std::function<void(app::Engine&)>& fn, const ai::CancelToken& cancel = {},
                              std::chrono::milliseconds timeout = std::chrono::milliseconds{30'000});

    // ---- state (any thread) ----
    [[nodiscard]] SessionState state() const { return state_.load(std::memory_order_acquire); }
    [[nodiscard]] std::uint64_t id() const { return id_; }
    [[nodiscard]] const ClientInfo& client() const { return client_; }
    [[nodiscard]] HostPolicy policy() const { return policy_; }
    [[nodiscard]] SessionPermissions permissions() const;
    // Takes permissions away. Refused (and nothing changes) when `narrower` is not within the current ones.
    [[nodiscard]] Result<void> narrow(SessionPermissions narrower);

    // ---- jobs (ADR-2159; any thread) ----
    [[nodiscard]] JobTable& jobs() { return *jobs_; }
    [[nodiscard]] const std::shared_ptr<JobTable>& jobTable() const { return jobs_; }
    // Where this session's render snapshots go: a folder of its own under the system temp directory, made on
    // first use and removed when the session is destroyed. Never inside a project or a root.
    [[nodiscard]] Result<std::filesystem::path> scratchDir();

    // ---- host-thread state ----
    [[nodiscard]] ai::SnapshotStore& snapshots() { return snapshots_; }
    // Null restores the default snapshot sink. Refused while an explicit transaction is open.
    [[nodiscard]] Result<void> setTransactionSink(ai::TransactionSink* sink);
    [[nodiscard]] ai::TransactionSink& transactionSink() const { return *sink_; }
    [[nodiscard]] Host& host() { return *host_; }

    // Host thread, once per frame: runs queued calls under the budget.
    std::size_t pump(double budgetMs = ai::MainThreadQueue::kDefaultBudgetMs);
    // Refuses new calls, rolls back an open explicit transaction (or abandons it when the host's queue has
    // stopped: the engine is going away), and closes. Host thread. Never saves the project.
    void close();

    // ---- what the session tools call (host thread, inside a call) ----
    [[nodiscard]] ai::ToolResult beginTransaction(const std::string& label);
    [[nodiscard]] ai::ToolResult commitTransaction(const std::string& id);
    [[nodiscard]] ai::ToolResult rollbackTransaction(const std::string& id);
    [[nodiscard]] ai::ToolResult transactionStatus(const std::string& id) const;
    [[nodiscard]] nlohmann::json info(const nlohmann::json& toolNames);
    // ADR-2154: {policy, effective, offered[], renderTime}, and the host's choice of a parked policy.
    [[nodiscard]] nlohmann::json timePolicyJson();
    [[nodiscard]] ai::ToolResult setTimePolicy(const std::string& name);
    [[nodiscard]] ToolAvailability availability(std::string_view tool);
    // "s3/txn-2": a transaction's process-wide name, which the file claims are recorded under.
    [[nodiscard]] std::string transactionKey(const std::string& id) const;

private:
    Session(SessionOptions options, Host& host);

    struct OpenTransaction {
        std::string id;
        std::string label;
        std::unique_ptr<ai::Transaction> txn;
        nlohmann::json before; // the parameter half, for changedTargets
        std::string rollbackPoint;
        bool partial = false;
        int calls = 0;
        std::vector<std::string> irreversible; // tools accepted that a rollback cannot reach
        CompensationLog files;
        // ADR-2154: the counters at begin (a rollback restores them), and whether a call inside made an
        // authored change its own work did not already account for (commit bumps once for it).
        std::uint64_t revisionsProject = 0;
        std::uint64_t revisionsScene = 0;
        std::uint64_t revisionsSavedAt = 0;
        bool pendingBump = false;
        bool pendingScene = false;
    };

    // Each runs on the caller's thread and refuses before the queue is involved.
    [[nodiscard]] std::optional<ai::ToolError> permissionDenial(const ai::Tool& tool) const;
    // Host thread.
    [[nodiscard]] std::optional<ai::ToolError> livePolicyRefusal(const ai::Tool& tool);
    [[nodiscard]] std::string unavailability(const ai::Tool& tool, ai::ToolContext& ctx);
    void prepareContext(ai::ToolContext& ctx, const PathPolicy& paths);
    [[nodiscard]] CallResult runOnHost(const ai::Tool& tool, const nlohmann::json& args, const CallOptions& options,
                                       CallArtifacts* artifacts);
    [[nodiscard]] nlohmann::json transactionJson(const OpenTransaction& t) const;
    [[nodiscard]] bool referencedOutside(const std::filesystem::path& file);
    [[nodiscard]] PathPolicy pathsNow() const;
    // `job.wait` on a thread that is not the host's: blocks on the job table.
    [[nodiscard]] CallResult waitOnCaller(const nlohmann::json& args, const CallOptions& options);

    std::uint64_t id_;
    ClientInfo client_;
    HostPolicy policy_;
    mutable std::mutex permissionsMutex_;
    SessionPermissions permissions_;
    ai::ToolRegistry registry_;
    ai::SnapshotStore snapshots_;
    std::unique_ptr<ai::SnapshotTransactionSink> defaultSink_;
    // Implicit per-call transactions' rollback points: never listed, never more than one in use.
    ai::SnapshotStore implicitSnapshots_{2};
    std::unique_ptr<ai::SnapshotTransactionSink> implicitSink_;
    ai::TransactionSink* sink_ = nullptr;
    std::optional<OpenTransaction> explicit_;
    std::uint64_t nextTransaction_ = 1;
    Host* host_;
    std::atomic<SessionState> state_{SessionState::Ready};
    std::shared_ptr<JobTable> jobs_ = std::make_shared<JobTable>();
    std::mutex scratchMutex_;
    std::filesystem::path scratch_;
};

// The standard registry: the engine tools, the Director tools and the session tools, registered in place
// (some tools keep a reference to the registry they are in, so it must not move afterwards).
void registerAllTools(ai::ToolRegistry& registry);
// The session tools alone (api/session_tools.cpp).
void registerSessionTools(ai::ToolRegistry& registry);
// `render.capture`, `render.start` and `job.*` (api/render_tools.cpp, ADR-2159).
void registerRenderTools(ai::ToolRegistry& registry);
// `job.wait`'s answer for a record, shared by the tool body (host thread) and `Session::waitOnCaller`.
[[nodiscard]] CallResult jobWaitResult(const JobRecord& record, bool timedOut);
// ADR-2157: `document.get`, `document.validate`, `document.patch` and `project.validate`
// (api/document_tools.cpp).
void registerDocumentTools(ai::ToolRegistry& registry);
//
// {project, scene, savedAt, dirty}: how session.info, project.diff_saved and the transaction results
// report `Engine::revisions()` (ADR-2154).
[[nodiscard]] nlohmann::json revisionsJson(const app::ProjectRevisions& revisions);

} // namespace avgen::api
