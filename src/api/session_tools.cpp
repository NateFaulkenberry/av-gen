// The session tools (ADR-2153): `session.info` and `transaction.*`. Registered in the same registry as the
// engine tools, so every client -- Studio's assistant, a future `avgen serve`, a test -- sees them, and each
// is published in the catalogue with an output schema.
//
// They are thin: the state they report and change is the Session's, reached through `ToolContext::session()`.
// A context with no session (a test driving the registry directly) gets FailedPrecondition, not a guess.

#include "ai/tool_api.hpp"
#include "ai/tool_context.hpp"
#include "api/session.hpp"

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
                           {"notRestored", anything("rollback: {tools, files} it could not put back")}},
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
                        {"permissions", anything("The session's fixed permissions")},
                        {"version", anything("api::version(): engine, revision, api level, formats")},
                        {"project", anything("{state, file, folder, writable, hasComposition}")},
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
}

} // namespace avgen::api
