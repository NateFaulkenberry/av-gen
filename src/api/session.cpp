#include "api/session.hpp"

#include "ai/director_tools.hpp"
#include "ai/engine_tools.hpp"
#include "api/version.hpp"
#include "app/engine.hpp"
#include "core/log.hpp"
#include "scene/composition.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <set>

namespace avgen::api {
namespace {

namespace fs = std::filesystem;
using nlohmann::json;

std::atomic<std::uint64_t> gNextSession{1};

ai::ToolError err(ai::ToolErrorCode code, std::string message, std::string recovery = {}, json details = nullptr) {
    return ai::ToolError{code, std::move(message), std::move(recovery), std::move(details)};
}

// The session's own tools: they ARE the transaction machinery, so they are never wrapped in one.
bool isSessionTool(const ai::Tool& tool) {
    const std::string domain = tool.definition.domain();
    return domain == "session" || domain == "transaction";
}

// A Live host refuses these by name while a show runs, beside the structural, job and project classes: a
// seek moves the show's own clock, and replacing the track swaps what the audience is hearing.
constexpr std::array<std::string_view, 2> kLiveRefusedByName = {"sequencer.set_playhead", "asset.import_audio"};

void collectStrings(const json& j, std::vector<std::string>& out) {
    if (j.is_string()) {
        out.push_back(j.get<std::string>());
    } else if (j.is_object() || j.is_array()) {
        for (const auto& v : j) {
            collectStrings(v, out);
        }
    }
}

json readJsonFile(const fs::path& path) {
    std::ifstream in(path);
    if (!in) {
        return nullptr;
    }
    json j = json::parse(in, nullptr, false);
    return j.is_discarded() ? json(nullptr) : j;
}

} // namespace

const char* clientKindName(ClientKind kind) {
    switch (kind) {
    case ClientKind::Studio: return "studio";
    case ClientKind::Cli: return "cli";
    case ClientKind::JsonRpc: return "json-rpc";
    case ClientKind::Mcp: return "mcp";
    case ClientKind::Test: return "test";
    }
    return "test";
}

const char* hostPolicyName(HostPolicy policy) {
    return policy == HostPolicy::Live ? "live" : "authoring";
}

json ClientInfo::toJson() const {
    return json{{"kind", clientKindName(kind)}, {"name", name}, {"version", version}};
}

json ToolAvailability::toJson(std::string_view name) const {
    json j{{"name", std::string(name)}, {"supported", supported}, {"available", available}, {"permitted", permitted}};
    if (!unavailableReason.empty()) {
        j["unavailableReason"] = unavailableReason;
    }
    if (!deniedReason.empty()) {
        j["deniedReason"] = deniedReason;
    }
    return j;
}

// ---- results ---------------------------------------------------------------------------------------

json CallResult::toWire() const {
    json changesJson = json::array();
    for (const ai::ChangeRecord& c : changes) {
        changesJson.push_back(json{{"target", c.target}, {"detail", c.detail}, {"clamped", c.clamped}});
    }
    json warningsJson = json::array();
    for (const Warning& w : warnings) {
        json entry{{"code", w.code}, {"message", w.message}};
        if (!w.pointer.empty()) {
            entry["pointer"] = w.pointer;
        }
        warningsJson.push_back(std::move(entry));
    }
    json j{{"success", success},
           {"result", success ? result : json(nullptr)},
           {"summary", summary},
           {"changes", std::move(changesJson)},
           {"warnings", std::move(warningsJson)},
           {"transaction", transaction ? json(*transaction) : json(nullptr)},
           {"job", job ? json(*job) : json(nullptr)}};
    if (!success && error) {
        json e{{"code", ai::toolErrorCodeName(error->code)}, {"message", error->message}, {"recovery", error->recovery}};
        if (error->details.is_object() && !error->details.empty()) {
            e["details"] = error->details;
        }
        j["error"] = std::move(e);
    }
    return j;
}

ai::ToolResult CallResult::toToolResult() const {
    ai::ToolResult r;
    r.success = success;
    r.value = success ? result : json::object();
    r.summary = summary;
    r.error = error;
    return r;
}

CallResult CallResult::refused(ai::ToolError error) {
    CallResult r;
    r.success = false;
    r.result = json::object();
    r.summary = error.message;
    r.error = std::move(error);
    return r;
}

// ---- the local host ----------------------------------------------------------------------------------

void LocalHost::installHooks(ai::ToolContext& ctx) {
    if (performance_) {
        ctx.setPerformanceSource(performance_);
    }
    if (recording_) {
        ctx.setRecordingHook(recording_);
    }
    if (watch_) {
        ctx.setWatchHook(watch_);
    }
    if (evaluation_) {
        ctx.setEvaluationHook(evaluation_);
    }
    if (profile_) {
        ctx.setProfileHook(profile_);
    }
}

// ---- registry ----------------------------------------------------------------------------------------

void registerAllTools(ai::ToolRegistry& registry) {
    ai::registerEngineTools(registry);
    ai::registerDirectorTools(registry);
    registerSessionTools(registry);
}

// ---- life cycle --------------------------------------------------------------------------------------

Session::Session(SessionOptions options, Host& host)
    : id_(gNextSession.fetch_add(1, std::memory_order_relaxed)), client_(std::move(options.client)),
      policy_(options.policy), permissions_(std::move(options.permissions)), snapshots_(options.snapshotCapacity),
      host_(&host) {
    registerAllTools(registry_);
    for (ai::Tool& tool : options.extraTools) {
        registry_.add(std::move(tool));
    }
    defaultSink_ = std::make_unique<ai::SnapshotTransactionSink>(host.engine(), snapshots_);
    implicitSink_ = std::make_unique<ai::SnapshotTransactionSink>(host.engine(), implicitSnapshots_);
    sink_ = defaultSink_.get();
}

Result<std::unique_ptr<Session>> Session::create(SessionOptions options, Host& host) {
    for (const auto* roots : {&options.permissions.readRoots, &options.permissions.writeRoots}) {
        for (const fs::path& root : *roots) {
            if (root.empty() || root.is_relative()) {
                return fail("session root '{}' is not an absolute path: a root must not depend on the working "
                            "directory",
                            root.string());
            }
        }
    }
    std::unique_ptr<Session> session(new Session(std::move(options), host));
    log::info("api: session {} ({}) ready -- {} tool(s), policy {}", session->id_, clientKindName(session->client_.kind),
              session->registry_.size(), hostPolicyName(session->policy_));
    return session;
}

Session::~Session() {
    if (state() != SessionState::Closed) {
        close();
    }
}

void Session::close() {
    SessionState expected = SessionState::Ready;
    if (!state_.compare_exchange_strong(expected, SessionState::Closing)) {
        return;
    }
    if (explicit_) {
        if (host_->queue().running()) {
            // Host thread: roll back here, as the Orchestrator rolls back a cancelled task.
            const std::string id = explicit_->id;
            (void)rollbackTransaction(id);
        } else {
            // The queue has stopped: the engine is being torn down, and touching it is the race the queue
            // exists to prevent. The project is left as the transaction left it.
            log::warn("api: session {} closed with transaction '{}' open; the host stopped, so it is abandoned", id_,
                      explicit_->id);
            explicit_->txn->abandon();
            FileClaims::instance().rolledBack(transactionKey(explicit_->id));
            explicit_.reset();
        }
    }
    state_.store(SessionState::Closed, std::memory_order_release);
}

std::size_t Session::pump(double budgetMs) { return host_->queue().pump(budgetMs); }

SessionPermissions Session::permissions() const {
    const std::lock_guard lock(permissionsMutex_);
    return permissions_;
}

Result<void> Session::narrow(SessionPermissions narrower) {
    const std::lock_guard lock(permissionsMutex_);
    if (!narrower.within(permissions_)) {
        return fail("a session's permissions can be narrowed, never widened");
    }
    permissions_ = std::move(narrower);
    return {};
}

Result<void> Session::setTransactionSink(ai::TransactionSink* sink) {
    if (explicit_) {
        return fail("the transaction sink cannot change while transaction '{}' is open", explicit_->id);
    }
    sink_ = sink != nullptr ? sink : defaultSink_.get();
    return {};
}

PathPolicy Session::pathsNow() const { return PathPolicy(permissions(), &host_->engine()); }

std::string Session::transactionKey(const std::string& id) const { return fmt::format("s{}/{}", id_, id); }

bool Session::onHost(const std::function<void(app::Engine&)>& fn, const ai::CancelToken& cancel,
                     std::chrono::milliseconds timeout) {
    return host_->queue().run([&] { fn(host_->engine()); }, cancel, timeout);
}

// ---- refusals ----------------------------------------------------------------------------------------

std::optional<ai::ToolError> Session::permissionDenial(const ai::Tool& tool) const {
    const SessionPermissions perms = permissions();
    const ai::ToolAnnotations& a = tool.definition.annotations;
    const std::string& name = tool.definition.name;
    // "read" and "write" are not refused here: whether a path is inside a root is the PathPolicy's answer,
    // given when the tool resolves the path it was handed (a group node needs no file at all). A session
    // with no roots gets the same PermissionDenied from there, with the same recovery.
    for (const std::string& p : a.permissions) {
        if (p == "subprocess" && !perms.subprocess) {
            return err(ai::ToolErrorCode::PermissionDenied,
                       fmt::format("'{}' starts a child process, which this session may not", name),
                       "start the session with --allow-subprocess", json{{"field", "subprocess"}});
        }
        if (p == "gpu" && !perms.gpu) {
            return err(ai::ToolErrorCode::PermissionDenied, fmt::format("'{}' needs the GPU, which this session may "
                                                                        "not use",
                                                                        name),
                       "start a session with GPU access", json{{"field", "gpu"}});
        }
        if (p == "write" && perms.projectAccess != ProjectAccess::ReadWrite) {
            return err(ai::ToolErrorCode::PermissionDenied,
                       fmt::format("'{}' writes files, and this session is read-only", name),
                       "start a session with projectAccess read-write", json{{"field", "projectAccess"}});
        }
    }
    if (isSessionTool(tool)) {
        return std::nullopt;
    }
    if (perms.projectAccess != ProjectAccess::ReadWrite && (a.mutatesProject || a.mutatesSession)) {
        if (perms.projectAccess == ProjectAccess::None || a.mutatesProject || tool.definition.domain() == "project") {
            return err(ai::ToolErrorCode::PermissionDenied,
                       fmt::format("'{}' changes the project, and this session's project access is '{}'", name,
                                   projectAccessName(perms.projectAccess)),
                       "start a session with projectAccess read-write", json{{"field", "projectAccess"}});
        }
    }
    return std::nullopt;
}

std::optional<ai::ToolError> Session::livePolicyRefusal(const ai::Tool& tool) {
    if (policy_ != HostPolicy::Live || client_.kind == ClientKind::Studio) {
        return std::nullopt;
    }
    const ai::ToolAnnotations& a = tool.definition.annotations;
    if (a.readOnly && a.effectiveExecutionClass() == ai::ExecutionClass::Read) {
        return std::nullopt; // reads stay available
    }
    const ai::ExecutionClass cls = a.effectiveExecutionClass();
    const std::string& name = tool.definition.name;
    const bool byName = std::find(kLiveRefusedByName.begin(), kLiveRefusedByName.end(), name) != kLiveRefusedByName.end();
    const bool refusedClass = cls == ai::ExecutionClass::Structural || cls == ai::ExecutionClass::Job;
    const bool projectCall = tool.definition.domain() == "project" && !a.readOnly;
    if (!refusedClass && !projectCall && !byName) {
        return std::nullopt;
    }
    if (!host_->showRunning()) {
        return std::nullopt;
    }
    json details{{"field", "policy"}};
    if (const auto remaining = host_->showRemainingSeconds(); remaining && *remaining > 0.0) {
        details["retryAfterMs"] = static_cast<std::int64_t>(std::ceil(*remaining * 1000.0));
    }
    const char* why = byName ? (name == "sequencer.set_playhead" ? "a seek moves the show's own clock"
                                                                 : "it replaces what the audience is hearing")
                      : projectCall ? "a project call replaces or rewrites the show's project"
                      : cls == ai::ExecutionClass::Job
                          ? "a job competes with the live frame for the GPU and the machine"
                          : "a structural edit rebuilds the composition, which stalls the live frame";
    return err(ai::ToolErrorCode::Busy,
               fmt::format("'{}' is refused while the host is running a live show: {}", name, why),
               "read-only calls stay available; retry when the show stops", std::move(details));
}

std::string Session::unavailability(const ai::Tool& tool, ai::ToolContext& ctx) {
    for (const std::string& r : tool.definition.annotations.requirements) {
        if (r == "composition" && host_->engine().composition() == nullptr) {
            return "no composition is loaded";
        }
        if (r == "performance" && !ctx.hasPerformanceSource()) {
            return "no renderer is attached to this host";
        }
        if (r == "recording" && !ctx.recordingHook()) {
            return "this host has no recorder installed";
        }
        if (r == "watch" && !ctx.watchHook()) {
            return "this host has no watcher installed";
        }
        if (r == "evaluation" && !ctx.evaluationHook()) {
            return "this host has no evaluator installed";
        }
        if (r == "profile" && !ctx.profileHook()) {
            return "this host has no live profiler installed";
        }
    }
    return {};
}

void Session::prepareContext(ai::ToolContext& ctx, const PathPolicy& paths) {
    ctx.setPaths(paths);
    ctx.setSession(this);
    ctx.setSnapshots(&snapshots_);
    host_->installHooks(ctx);
}

// ---- the entry point -----------------------------------------------------------------------------------

CallResult Session::call(std::string_view toolName, const json& args, const CallOptions& options,
                         CallArtifacts* artifacts) {
    if (state() != SessionState::Ready) {
        return CallResult::refused(err(ai::ToolErrorCode::FailedPrecondition, "the session is closing",
                                       "start a new session"));
    }
    // The caller's thread: the registry is immutable and validation is pure, so a malformed call is
    // refused without costing the host thread anything.
    const ai::Tool* tool = registry_.find(toolName);
    if (tool == nullptr) {
        return CallResult::refused(ai::ToolRegistry::unknownTool(toolName));
    }
    if (auto e = ai::ToolRegistry::checkArguments(*tool, args)) {
        return CallResult::refused(std::move(*e));
    }
    if (auto e = permissionDenial(*tool)) {
        return CallResult::refused(std::move(*e));
    }
    CallResult out;
    const bool ran = host_->queue().run([&] { out = runOnHost(*tool, args, options, artifacts); }, options.cancel,
                                        options.timeout);
    if (!ran) {
        return CallResult::refused(err(ai::ToolErrorCode::Cancelled,
                                       fmt::format("'{}' did not run: the call was cancelled or the host did not "
                                                   "service it in time",
                                                   toolName),
                                       "retry; nothing was applied"));
    }
    return out;
}

CallResult Session::runOnHost(const ai::Tool& tool, const json& args, const CallOptions& options,
                              CallArtifacts* artifacts) {
    if (state() == SessionState::Closed) {
        return CallResult::refused(err(ai::ToolErrorCode::FailedPrecondition, "the session is closed",
                                       "start a new session"));
    }
    if (auto e = livePolicyRefusal(tool)) {
        return CallResult::refused(std::move(*e));
    }
    const ai::ToolAnnotations& a = tool.definition.annotations;
    const std::string& name = tool.definition.name;
    const bool sessionTool = isSessionTool(tool);
    const bool mutating = !a.readOnly && !sessionTool;

    OpenTransaction* joined = nullptr;
    if (options.transaction) {
        if (!explicit_ || explicit_->id != *options.transaction) {
            return CallResult::refused(err(
                ai::ToolErrorCode::FailedPrecondition, fmt::format("there is no open transaction '{}'", *options.transaction),
                "transaction.status reports the open one; transaction.begin opens one",
                json{{"field", "transaction"}}));
        }
        joined = &*explicit_;
    } else if (explicit_ && mutating) {
        return CallResult::refused(err(
            ai::ToolErrorCode::FailedPrecondition,
            fmt::format("transaction '{}' is open, and '{}' does not name it", explicit_->id, name),
            "pass the open transaction's id with the call, or commit or roll it back first: a change made beside it "
            "would be rolled back with it",
            json{{"field", "transaction"}}));
    }
    bool accepted = false;
    if (joined != nullptr && mutating && !a.undoable) {
        if (options.irreversible != Irreversible::Accept) {
            return CallResult::refused(err(
                ai::ToolErrorCode::FailedPrecondition,
                fmt::format("'{}' cannot be undone by a rollback, and transaction '{}' would report it restored", name,
                            joined->id),
                "pass irreversible: accept to run it anyway (the transaction is then partial), or run it outside the "
                "transaction",
                json{{"field", "irreversible"}}));
        }
        accepted = true;
    }

    std::unique_ptr<ai::Transaction> implicit;
    std::string implicitId;
    // An implicit transaction's rollback point is kept apart from the session's own (`project.list_snapshots`):
    // one per mutating call would push a client's checkpoints out of the ring. A host that installed its own
    // sink (Studio's edit history) gets it, so an external client's call is one undo there too.
    ai::TransactionSink& implicitSink = sink_ == defaultSink_.get() ? *implicitSink_ : *sink_;
    if (joined == nullptr && a.mutatesProject && !sessionTool && implicitSink.available()) {
        implicitId = fmt::format("txn-{}", nextTransaction_++);
        implicit = std::make_unique<ai::Transaction>(implicitSink, name);
    }
    const std::string key = joined != nullptr ? transactionKey(joined->id)
                            : implicit        ? transactionKey(implicitId)
                                              : std::string{};

    app::Engine& engine = host_->engine();
    ai::ToolContext ctx(engine);
    prepareContext(ctx, pathsNow());
    ctx.setCancelToken(options.cancel);
    ctx.onProgress = options.onProgress;
    if (artifacts != nullptr) {
        ctx.setObservation(artifacts->observation);
    }
    const ai::ToolResult r = ai::ToolRegistry::run(tool, args, ctx);

    CompensationLog callFiles(key);
    for (const ai::ToolContext::FileEvent& event : ctx.files()) {
        callFiles.record(event);
    }

    CallResult out;
    out.success = r.success;
    out.result = r.success ? r.value : json::object();
    out.summary = r.summary;
    out.error = r.error;
    out.changes = ctx.changes().records();
    if (artifacts != nullptr) {
        artifacts->proposal = ctx.proposal();
        artifacts->deferredProposal = ctx.deferredProposal();
        artifacts->deferredResult = ctx.deferredResult();
    }
    const auto referenced = [this](const fs::path& p) { return referencedOutside(p); };
    if (!r.success) {
        // A failed call leaves nothing behind: its snapshot (when it had its own) goes back first, so the
        // file check sees the document as restored, then its own files.
        if (implicit) {
            implicit->rollback();
        }
        const auto files = callFiles.rollback(referenced);
        if (!files.kept.empty() || !files.deleted.empty()) {
            out.warnings.push_back(Warning{"call.filesRolledBack",
                                           fmt::format("{} file(s) deleted, {} kept", files.deleted.size(),
                                                       files.kept.size()),
                                           {}});
        }
        if (joined != nullptr) {
            ++joined->calls;
        }
    } else if (joined != nullptr) {
        ++joined->calls;
        joined->files.absorb(std::move(callFiles));
        if (accepted) {
            joined->partial = true;
            if (std::find(joined->irreversible.begin(), joined->irreversible.end(), name) == joined->irreversible.end()) {
                joined->irreversible.push_back(name);
            }
            out.warnings.push_back(Warning{"transaction.partial",
                                           fmt::format("'{}' is outside what transaction '{}' can roll back", name,
                                                       joined->id),
                                           {}});
        }
    } else {
        if (implicit) {
            implicit->commit();
        }
        callFiles.commit();
        FileClaims::instance().committed(key);
    }
    if (joined != nullptr) {
        out.transaction = joined->id;
    } else if (implicit) {
        out.transaction = implicitId;
    }
    return out;
}

// ---- files -------------------------------------------------------------------------------------------

bool Session::referencedOutside(const fs::path& file) {
    app::Engine& engine = host_->engine();
    std::error_code ec;
    const fs::path target = fs::weakly_canonical(file, ec);
    if (ec) {
        return false;
    }
    const fs::path projectDir = PathPolicy(permissions(), &engine).projectFolder();
    fs::path sceneDir;
    if (!engine.compositionPath().empty()) {
        sceneDir = fs::absolute(engine.compositionPath(), ec).parent_path();
    }
    const auto names = [&](const fs::path& candidate) {
        std::error_code e;
        return !candidate.empty() && fs::weakly_canonical(candidate, e) == target && !e;
    };
    const auto matches = [&](const std::string& s, const fs::path& base) {
        if (s.empty() || s.size() > 4096) {
            return false;
        }
        const fs::path p(s);
        if (p.is_absolute()) {
            return names(p);
        }
        return (!base.empty() && names(base / p)) || (!projectDir.empty() && names(projectDir / p)) ||
               (!sceneDir.empty() && names(sceneDir / p));
    };
    if (names(engine.audioPath()) || names(engine.environmentPath())) {
        return true;
    }
    // The document as restored: the parameter domain, the composition and the sequence.
    std::vector<std::string> strings;
    collectStrings(ai::SnapshotStore::captureDocument(engine), strings);
    for (const std::string& s : strings) {
        if (matches(s, sceneDir)) {
            return true;
        }
    }
    // What is saved on disk: a save inside the transaction cannot be un-written.
    for (const fs::path& onDisk : {engine.projectPath(), engine.compositionPath()}) {
        if (onDisk.empty()) {
            continue;
        }
        const fs::path dir = fs::absolute(onDisk, ec).parent_path();
        std::vector<std::string> saved;
        collectStrings(readJsonFile(onDisk), saved);
        for (const std::string& s : saved) {
            if (matches(s, dir)) {
                return true;
            }
        }
    }
    return false;
}

// ---- explicit transactions ---------------------------------------------------------------------------------

json Session::transactionJson(const OpenTransaction& t) const {
    return json{{"id", t.id},
                {"label", t.label},
                {"state", "open"},
                {"partial", t.partial},
                {"calls", t.calls},
                {"irreversible", t.irreversible},
                {"createdFiles", t.files.createdFiles()},
                {"rollbackPoint", t.rollbackPoint.empty() ? json(nullptr) : json(t.rollbackPoint)},
                {"backedBy", std::string(sink_->kind())}};
}

ai::ToolResult Session::beginTransaction(const std::string& label) {
    if (explicit_) {
        return ai::ToolResult::failure(ai::ToolErrorCode::FailedPrecondition,
                                       fmt::format("transaction '{}' is already open", explicit_->id),
                                       "a session has one explicit transaction at a time: commit or roll it back first",
                                       json{{"field", "transaction"}});
    }
    if (!sink_->available()) {
        return ai::ToolResult::failure(ai::ToolErrorCode::Unavailable, "this session has no way to roll back",
                                       "the host installed no transaction sink");
    }
    OpenTransaction t;
    t.id = fmt::format("txn-{}", nextTransaction_++);
    t.label = label.empty() ? t.id : label;
    // Measured before the sink opens, so the change list is taken against the same instant the rollback
    // point is (the Orchestrator's order since ADR-094).
    t.before = ai::SnapshotStore::captureDocument(host_->engine());
    t.txn = std::make_unique<ai::Transaction>(*sink_, t.label);
    t.rollbackPoint = sink_->rollbackPoint();
    t.files = CompensationLog(transactionKey(t.id));
    explicit_ = std::move(t);
    return ai::ToolResult::ok(transactionJson(*explicit_), fmt::format("opened {}", explicit_->id));
}

ai::ToolResult Session::commitTransaction(const std::string& id) {
    if (!explicit_ || explicit_->id != id) {
        return ai::ToolResult::failure(ai::ToolErrorCode::FailedPrecondition,
                                       fmt::format("there is no open transaction '{}'", id),
                                       "transaction.status reports the open one", json{{"field", "id"}});
    }
    OpenTransaction& t = *explicit_;
    const json after = ai::SnapshotStore::captureDocument(host_->engine());
    const std::vector<std::string> changed = ai::SnapshotStore::changedParameters(t.before, after);
    json out = transactionJson(t);
    t.txn->commit();
    out["state"] = "committed";
    out["changedTargets"] = changed;
    out["editState"] = sink_->committedEditState();
    // What a later undo of this transaction will not reach: the calls it accepted as irreversible, and any
    // file it overwrote without keeping a copy.
    out["notRestorable"] = json{{"tools", t.irreversible}, {"files", t.files.unrestorable()}};
    t.files.commit();
    FileClaims::instance().committed(transactionKey(t.id));
    explicit_.reset();
    return ai::ToolResult::ok(std::move(out), fmt::format("committed {}: {} parameter value(s) changed", id, changed.size()));
}

ai::ToolResult Session::rollbackTransaction(const std::string& id) {
    if (!explicit_ || explicit_->id != id) {
        return ai::ToolResult::failure(ai::ToolErrorCode::FailedPrecondition,
                                       fmt::format("there is no open transaction '{}'", id),
                                       "transaction.status reports the open one", json{{"field", "id"}});
    }
    OpenTransaction& t = *explicit_;
    json out = transactionJson(t);
    // The document first, so the file check reads the project as it is restored.
    t.txn->rollback();
    const CompensationLog::Outcome files = t.files.rollback([this](const fs::path& p) { return referencedOutside(p); });
    FileClaims::instance().rolledBack(transactionKey(t.id));
    out["state"] = "rolled-back";
    out["files"] = files.toJson();
    out["notRestored"] = json{{"tools", t.irreversible}, {"files", files.kept}};
    explicit_.reset();
    // The summary is built before `out` is moved (the unsequenced read capability.list once shipped).
    const std::string summary = fmt::format("rolled back {}{}", id, out.value("partial", false) ? " (partial)" : "");
    return ai::ToolResult::ok(std::move(out), summary);
}

ai::ToolResult Session::transactionStatus(const std::string& id) const {
    if (!explicit_) {
        if (!id.empty()) {
            return ai::ToolResult::failure(ai::ToolErrorCode::NotFound, fmt::format("no open transaction '{}'", id),
                                           "it was committed or rolled back; transaction.begin opens a new one",
                                           json{{"field", "id"}});
        }
        return ai::ToolResult::ok(json{{"state", "none"}}, "no transaction open");
    }
    if (!id.empty() && explicit_->id != id) {
        return ai::ToolResult::failure(ai::ToolErrorCode::NotFound, fmt::format("no open transaction '{}'", id),
                                       fmt::format("the open one is '{}'", explicit_->id), json{{"field", "id"}});
    }
    return ai::ToolResult::ok(transactionJson(*explicit_), fmt::format("{} open", explicit_->id));
}

// ---- discovery -----------------------------------------------------------------------------------------------

ToolAvailability Session::availability(std::string_view name) {
    ToolAvailability out;
    const ai::Tool* tool = registry_.find(name);
    if (tool == nullptr) {
        out.unavailableReason = "not in this build's catalogue";
        out.deniedReason = out.unavailableReason;
        return out;
    }
    out.supported = true;
    ai::ToolContext ctx(host_->engine());
    prepareContext(ctx, pathsNow());
    out.unavailableReason = unavailability(*tool, ctx);
    if (out.unavailableReason.empty()) {
        if (auto live = livePolicyRefusal(*tool)) {
            out.unavailableReason = live->message;
        }
    }
    out.available = out.unavailableReason.empty();
    if (auto denied = permissionDenial(*tool)) {
        out.deniedReason = denied->message;
    }
    out.permitted = out.deniedReason.empty();
    return out;
}

json Session::info(const json& toolNames) {
    app::Engine& engine = host_->engine();
    const PathPolicy paths = pathsNow();
    json permissionsJson = paths.permissions().toJson();
    const fs::path folder = paths.projectFolder();
    json project{{"state", engine.projectPath().empty() ? "none" : "open"},
                 {"file", engine.projectPath().empty() ? json(nullptr) : json(engine.projectPath().string())},
                 {"folder", folder.empty() ? json(nullptr) : json(folder.string())},
                 {"writable", paths.projectWritable()},
                 {"hasComposition", engine.composition() != nullptr}};
    std::size_t available = 0;
    std::size_t permitted = 0;
    json unavailable = json::array();
    json denied = json::array();
    for (const ai::Tool* tool : registry_.all()) {
        const ToolAvailability a = availability(tool->definition.name);
        if (a.available) {
            ++available;
        } else {
            unavailable.push_back(json{{"name", tool->definition.name}, {"reason", a.unavailableReason}});
        }
        if (a.permitted) {
            ++permitted;
        } else {
            denied.push_back(json{{"name", tool->definition.name}, {"reason", a.deniedReason}});
        }
    }
    json out{{"session", id_},
             {"state", state() == SessionState::Ready ? "ready" : "closing"},
             {"client", client_.toJson()},
             {"policy", hostPolicyName(policy_)},
             {"showRunning", policy_ == HostPolicy::Live && host_->showRunning()},
             {"permissions", std::move(permissionsJson)},
             {"version", version().toJson()},
             {"project", std::move(project)},
             {"transaction", explicit_ ? transactionJson(*explicit_) : json(nullptr)},
             {"transactionSink", std::string(sink_->kind())},
             {"rollbackPoints", snapshots_.size()},
             {"tools", json{{"supported", registry_.size()},
                            {"available", available},
                            {"permitted", permitted},
                            {"unavailable", std::move(unavailable)},
                            {"notPermitted", std::move(denied)}}}};
    if (toolNames.is_array()) {
        json detail = json::array();
        for (const json& n : toolNames) {
            if (n.is_string()) {
                detail.push_back(availability(n.get<std::string>()).toJson(n.get<std::string>()));
            }
        }
        out["toolDetail"] = std::move(detail);
    }
    return out;
}

} // namespace avgen::api
