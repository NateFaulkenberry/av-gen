// The session tools (ADR-2153): `session.info` and `transaction.*`; and (ADR-2154) `project.diff_saved` and
// `transport.get_policy` / `set_policy` / `step`, which read or change state the Session and its Host own (the
// revisions' on-demand check, the host's time policy) rather than the project document. Registered in the same registry as the
// engine tools, so every client -- Studio's assistant, a future `avgen serve`, a test -- sees them, and each
// is published in the catalogue with an output schema.
//
// They are thin: the state they report and change is the Session's, reached through `ToolContext::session()`.
// A context with no session (a test driving the registry directly) gets FailedPrecondition, not a guess.

#include "ai/tool_api.hpp"
#include "ai/tool_context.hpp"
#include "api/session.hpp"
#include "app/engine.hpp"

#include <cmath>

#include <nlohmann/json.hpp>

namespace avgen::api {
namespace {

using nlohmann::json;
namespace schema = ai::schema;

ai::ToolResult noSession(std::string_view tool) {
    return ai::ToolResult::failure(ai::ToolErrorCode::FailedPrecondition,
                                   fmt::format("'{}' runs inside a session, and this call has none", tool),
                                   "call it through api::Session::call");
}

ai::ToolAnnotations sessionAnnotations(bool readOnly) {
    ai::ToolAnnotations a;
    a.readOnly = readOnly;
    a.mutatesSession = !readOnly;
    a.idempotent = readOnly;
    a.undoable = false;
    a.requiresMainThread = true;
    return a;
}

json anything(std::string description) { return json{{"description", std::move(description)}}; }

json revisionsOutput() {
    return schema::object({{"project", schema::integer("Bumped by every authored change to the project")},
                           {"scene", schema::integer("Bumped by every authored change to the composition")},
                           {"savedAt", schema::integer("`project` at the last save, open or new document")},
                           {"dirty", schema::boolean("project != savedAt: changed since the last save")}},
                          {"project", "scene", "savedAt", "dirty"});
}

json timePolicyOutput() {
    const std::vector<std::string> names = {"running", "stopped-frozen", "stopped-ambient", "stepped", "offline"};
    return schema::object(
        {{"policy", schema::string("The host's setting", names)},
         {"effective", schema::string("What governs time now: running while a live transport plays", names)},
         {"offered", schema::array(schema::string("policy", names), "What transport.set_policy accepts on this host")},
         {"renderTime", schema::number("The world clock, seconds")},
         {"stepped", schema::integer("transport.step: frames advanced")}},
        {"policy", "effective", "offered", "renderTime"});
}

json transactionOutput() {
    return schema::object({{"id", schema::string("The transaction's id, e.g. txn-3")},
                           {"label", schema::string("What it is for")},
                           {"state", schema::string("open, committed, rolled-back, or none (status with nothing open)", {"open", "committed", "rolled-back", "none"})},
                           {"partial", schema::boolean("A call it accepted as irreversible is outside what it can "
                                                       "roll back")},
                           {"calls", schema::integer("Calls made inside it")},
                           {"irreversible", schema::array(schema::string("tool name"),
                                                          "Tools it accepted that a rollback cannot reach")},
                           {"createdFiles", schema::array(schema::string("path"), "Files its calls created")},
                           {"rollbackPoint", anything("The snapshot id it restores on rollback, or null")},
                           {"backedBy", schema::string("What backs it: project snapshot, or undo history")},
                           {"changedTargets", schema::array(schema::string("parameter path"),
                                                            "commit: parameter values that changed")},
                           {"editState", schema::integer("commit: the editor history state it produced, 0 for none")},
                           {"notRestorable", anything("commit: {tools, files} a later undo will not reach")},
                           {"files", anything("rollback: {deleted, restored, kept}")},
                           {"notRestored", anything("rollback: {tools, files} it could not put back")},
                           {"revisions", revisionsOutput()}},
                          {}, true);
}

void add(ai::ToolRegistry& r, std::string name, std::string title, std::string description, json input,
         ai::ToolAnnotations annotations, json output, ai::ToolFn fn) {
    ai::Tool tool;
    tool.definition.name = std::move(name);
    tool.definition.title = std::move(title);
    tool.definition.description = std::move(description);
    tool.definition.inputSchema = std::move(input);
    tool.definition.outputSchema = std::move(output);
    tool.definition.annotations = std::move(annotations);
    tool.execute = std::move(fn);
    r.add(std::move(tool));
}

} // namespace

void registerSessionTools(ai::ToolRegistry& registry) {
    add(registry, "session.info", "Session information",
        "Who this session is and what it may do: the client, the permissions (read roots, write roots, whether the "
        "open project's folder is writable, GPU, child processes), the engine and API versions, the project's "
        "state, the open transaction, and for every tool whether it is supported (in this build), available (usable "
        "in this host now: its hooks installed, a composition when it needs one) and permitted (allowed by these "
        "permissions). Pass `tools` for the three answers on named tools.",
        schema::object({{"tools", schema::array(schema::string("tool name"),
                                                "Tools to answer supported/available/permitted for, by name")}}),
        sessionAnnotations(true),
        schema::object({{"session", schema::integer("The session's id in this process")},
                        {"state", schema::string("ready or closing")},
                        {"client", anything("{kind, name, version}")},
                        {"policy", schema::string("authoring or live", {"authoring", "live"})},
                        {"showRunning", schema::boolean("A live host is running a show now")},
                        {"show", anything("ADR-2167/2176: {active, reasons[] (output | share | performance), "
                                          "remainingSeconds|null, gpuLock: null | {held, contended, holder: null | "
                                          "{pid, name}, path, attempts, lastAttemptMs, maxAttemptMs}}")},
                        {"permissions", anything("The session's fixed permissions")},
                        {"version", anything("api::version(): engine, revision, api level, formats")},
                        {"project", anything("{state, file, folder, writable, hasComposition}")},
                        {"revisions", revisionsOutput()},
                        {"timePolicy", timePolicyOutput()},
                        {"transaction", anything("The open explicit transaction, or null")},
                        {"transactionSink", schema::string("What backs a transaction")},
                        {"rollbackPoints", schema::integer("Snapshots this session holds")},
                        {"tools", anything("{supported, available, permitted, unavailable[], notPermitted[]}")},
                        {"toolDetail", schema::array(anything("{name, supported, available, permitted, reasons}"),
                                                     "Per named tool, when `tools` was passed")}},
                       {}, true),
        [](const json& args, ai::ToolContext& ctx) -> ai::ToolResult {
            Session* session = ctx.session();
            if (session == nullptr) {
                return noSession("session.info");
            }
            json info = session->info(args.contains("tools") ? args.at("tools") : json(nullptr));
            const auto summary = fmt::format("session {}: {} of {} tool(s) available, {} permitted",
                                             info.at("session").get<std::uint64_t>(),
                                             info.at("tools").at("available").get<std::size_t>(),
                                             info.at("tools").at("supported").get<std::size_t>(),
                                             info.at("tools").at("permitted").get<std::size_t>());
            return ai::ToolResult::ok(std::move(info), summary);
        });

    add(registry, "transaction.begin", "Begin a transaction",
        "Open an explicit transaction: the calls that name its id are rolled back together by transaction.rollback, "
        "or kept as one change (in Studio, one undo) by transaction.commit. One per session; a second begin is "
        "refused. While it is open, a mutating call that does not name it is refused. A call that cannot be undone "
        "(project saves, imports) is refused inside it unless the call passes irreversible: accept, which makes the "
        "transaction partial.",
        schema::object({{"label", schema::string("What the transaction is for; an undo menu shows it")}}),
        sessionAnnotations(false), transactionOutput(),
        [](const json& args, ai::ToolContext& ctx) -> ai::ToolResult {
            Session* session = ctx.session();
            return session == nullptr ? noSession("transaction.begin")
                                      : session->beginTransaction(args.value("label", std::string{}));
        });

    add(registry, "transaction.commit", "Commit a transaction",
        "Close the open transaction, keeping its changes. Reports the parameter values that changed and what a later "
        "undo cannot reach (the irreversible calls it accepted).",
        schema::object({{"id", schema::string("The id transaction.begin returned")}}, {"id"}), sessionAnnotations(false),
        transactionOutput(),
        [](const json& args, ai::ToolContext& ctx) -> ai::ToolResult {
            Session* session = ctx.session();
            return session == nullptr ? noSession("transaction.commit")
                                      : session->commitTransaction(args.at("id").get<std::string>());
        });

    add(registry, "transaction.rollback", "Roll back a transaction",
        "Close the open transaction, putting the project back as it was at begin: parameters, routes, presets, the "
        "timeline, the composition and the sequence. Files its calls created are deleted unless the project or scene "
        "still names them, another transaction uses them, or they are render outputs; files it replaced get their "
        "previous contents back. Reports what it could not restore.",
        schema::object({{"id", schema::string("The id transaction.begin returned")}}, {"id"}), sessionAnnotations(false),
        transactionOutput(),
        [](const json& args, ai::ToolContext& ctx) -> ai::ToolResult {
            Session* session = ctx.session();
            return session == nullptr ? noSession("transaction.rollback")
                                      : session->rollbackTransaction(args.at("id").get<std::string>());
        });

    add(registry, "transaction.status", "Transaction status",
        "The open transaction (calls, whether it is partial, the files it created), or state none.",
        schema::object({{"id", schema::string("Optional: the id to check; omit for the open one")}}),
        sessionAnnotations(true), transactionOutput(),
        [](const json& args, ai::ToolContext& ctx) -> ai::ToolResult {
            Session* session = ctx.session();
            return session == nullptr ? noSession("transaction.status")
                                      : session->transactionStatus(args.value("id", std::string{}));
        });

    // ---- ADR-2154 ---------------------------------------------------------------------------------------

    ai::ToolAnnotations diff = sessionAnnotations(true);
    diff.expensive = true;
    diff.idempotent = false; // it may clear a false "dirty"
    add(registry, "project.diff_saved", "Compare with the saved project",
        "Serialise the project and compare it with what was last saved or opened: the exact answer to \"would a "
        "save write anything different?\". Expensive (a full serialisation: up to hundreds of milliseconds on a "
        "large project), so ask only when it matters -- before closing. session.info's `revisions.dirty` is the "
        "cheap answer, and errs only towards dirty: a value edited and then set back. When nothing differs and "
        "the revisions said dirty, this clears it. A difference the revisions did not record is reported as "
        "`unrecorded` with its paths and left as it is.",
        schema::object({}), diff,
        schema::object({{"compared", schema::boolean("False before the first save or open: nothing to compare")},
                        {"differs", schema::boolean("A save would write a different document")},
                        {"cleared", schema::boolean("The revisions said dirty and nothing differed; now clean")},
                        {"unrecorded", schema::boolean("Something differs that no revision recorded")},
                        {"paths", schema::array(schema::string("JSON pointer"), "The first differing paths")},
                        {"pathCount", schema::integer("How many paths differ")},
                        {"ms", schema::number("What the comparison cost")},
                        {"revisions", revisionsOutput()}},
                       {"compared", "differs", "cleared", "unrecorded", "paths", "pathCount", "ms", "revisions"}),
        [](const json&, ai::ToolContext& ctx) -> ai::ToolResult {
            app::Engine& engine = ctx.engine();
            const app::SavedComparison c = engine.compareWithSaved(app::UnrecordedChange::Report);
            json out{{"compared", c.compared}, {"differs", c.differs},     {"cleared", c.cleared},
                     {"unrecorded", c.unrecorded}, {"paths", c.paths}, {"pathCount", c.pathCount},
                     {"ms", c.ms},             {"revisions", revisionsJson(engine.revisions())}};
            const std::string summary = !c.compared  ? "nothing saved or opened to compare with"
                                        : c.differs  ? fmt::format("{} path(s) differ from the saved project{}", c.pathCount,
                                                                   c.unrecorded ? " (not recorded as a revision)" : "")
                                        : c.cleared  ? "no difference: the project is clean again"
                                                     : "no difference";
            return ai::ToolResult::ok(std::move(out), summary);
        });

    add(registry, "transport.get_policy", "Time policy",
        "What the world clock does while nothing is playing, and which policies this host offers. running: the "
        "transport plays. stopped-ambient: parked, the world keeps moving on the wall clock (rigs, wind, water). "
        "stopped-frozen: parked, time does not advance, so two reads with no edit between them agree. stepped: "
        "nothing advances until transport.step. offline: the fixed-step clock of a render.",
        schema::object({}), sessionAnnotations(true), timePolicyOutput(),
        [](const json&, ai::ToolContext& ctx) -> ai::ToolResult {
            Session* session = ctx.session();
            if (session == nullptr) {
                return noSession("transport.get_policy");
            }
            json out = session->timePolicyJson();
            const std::string summary = fmt::format("time policy {} ({} now)", out.at("policy").get<std::string>(),
                                                    out.at("effective").get<std::string>());
            return ai::ToolResult::ok(std::move(out), summary);
        });

    ai::ToolAnnotations setPolicy = sessionAnnotations(false);
    setPolicy.idempotent = true;
    add(registry, "transport.set_policy", "Set the time policy",
        "Choose what the world clock does while the transport is parked. Refused (FAILED_PRECONDITION) for a "
        "policy this host does not offer: transport.get_policy lists them. Never changes a render: every render "
        "job has its own offline engine.",
        schema::object({{"policy", schema::string("The policy", {"stopped-frozen", "stopped-ambient", "stepped",
                                                                 "running", "offline"})}},
                       {"policy"}),
        setPolicy, timePolicyOutput(),
        [](const json& args, ai::ToolContext& ctx) -> ai::ToolResult {
            Session* session = ctx.session();
            return session == nullptr ? noSession("transport.set_policy")
                                      : session->setTimePolicy(args.at("policy").get<std::string>());
        });

    add(registry, "transport.step", "Step time",
        "Under the stepped policy: advance the world by `frames` frames (or by `seconds`, rounded to whole frames) "
        "at `fps` (default: the project's render rate), by PLAYING -- the same update a running frame takes, "
        "deltaTime 1/fps each -- not by seeking. renderTime moves by exactly frames/fps. To get the state a "
        "playback reaches at time t, step there; sequencer.set_playhead seeks instead.",
        schema::object({{"frames", schema::integer("Frames to advance", 1, 100000)},
                        {"seconds", schema::number("Or seconds to advance, rounded to whole frames", 0.0)},
                        {"fps", schema::number("Frames per second; default the project's render rate", 1.0, 1000.0)}}),
        sessionAnnotations(false), timePolicyOutput(),
        [](const json& args, ai::ToolContext& ctx) -> ai::ToolResult {
            Session* session = ctx.session();
            if (session == nullptr) {
                return noSession("transport.step");
            }
            app::Engine& engine = ctx.engine();
            if (args.contains("frames") == args.contains("seconds")) {
                return ai::ToolResult::failure(ai::ToolErrorCode::InvalidArguments, "pass exactly one of frames or seconds",
                                               "frames: 30, or seconds: 1.0", json{{"field", "frames"}});
            }
            const double fps = args.value("fps", engine.renderSettings().fps);
            const int frames = args.contains("frames")
                                   ? args.at("frames").get<int>()
                                   : static_cast<int>(std::llround(args.at("seconds").get<double>() * fps));
            if (frames < 1) {
                return ai::ToolResult::failure(ai::ToolErrorCode::InvalidArguments,
                                               fmt::format("{} s at {} fps is less than one frame", args.value("seconds", 0.0), fps),
                                               "step at least one frame", json{{"field", "seconds"}});
            }
            if (engine.timePolicy() != app::TimePolicy::Stepped) {
                return ai::ToolResult::failure(
                    ai::ToolErrorCode::FailedPrecondition,
                    fmt::format("transport.step needs the stepped time policy; this host's is '{}'",
                                app::timePolicyName(engine.timePolicy())),
                    "transport.set_policy {policy: stepped}, where the host offers it", json{{"field", "policy"}});
            }
            auto stepped = engine.stepFrames(frames, fps);
            if (!stepped) {
                return ai::ToolResult::failure(ai::ToolErrorCode::FailedPrecondition, stepped.error().message, "");
            }
            json out = session->timePolicyJson();
            out["stepped"] = frames;
            return ai::ToolResult::ok(std::move(out), fmt::format("stepped {} frame(s) to {:.4f} s", frames,
                                                                  engine.lastRenderTime()));
        });
}

} // namespace avgen::api
