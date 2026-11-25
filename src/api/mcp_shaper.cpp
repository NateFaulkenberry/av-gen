#include "api/mcp_shaper.hpp"

#include <fmt/format.h>
#include <spdlog/common.h>

#include <algorithm>
#include <stdexcept>

namespace avgen::api {
namespace {

using nlohmann::json;

constexpr McpMethod kMcpMethods[] = {
    {"initialize", "request",
     "Must be the first request. Takes `{protocolVersion, capabilities, clientInfo}`; answers `{protocolVersion, "
     "capabilities: {tools: {listChanged: false}, logging: {}}, serverInfo, instructions, _meta}`. `protocolVersion` "
     "is always the pinned revision: the client's when it asked for that one, else the pinned one, for the client "
     "to accept or disconnect. `_meta[\"avgen/server\"]` is what `avgen serve`'s `initialize` answers (the API "
     "level, the versions, the session and its permissions). A second `initialize` is refused."},
    {"notifications/initialized", "notification",
     "The client is ready. Requests sent before it are still answered (the server is lenient), and the server sends "
     "no notification it was not asked for before it."},
    {"ping", "request", "Answers `{}`. Allowed at any time, also before `initialize`."},
    {"tools/list", "request",
     "Every tool, in one page (no `nextCursor`; a `cursor` is refused `-32602`). Each is the catalogue entry "
     "shaped as an MCP Tool: `name` is its `mcpName`, `annotations` carry `readOnlyHint`, `destructiveHint`, "
     "`idempotentHint` and `openWorldHint: false`, and `_meta` names the canonical tool and its availability in "
     "this session."},
    {"tools/call", "request",
     "Takes `{name, arguments?, _meta?}`. `name` is looked up in the name table (an unknown one is `-32602`), then "
     "the tool runs through `Session::call`, as for every client. Answers `CallToolResult {content, "
     "structuredContent, isError}`. `_meta` may carry `progressToken`, `avgen/transaction` (join the open "
     "explicit transaction), `avgen/irreversible` (`true`, `\"accept\"` or `\"refuse\"`) and `avgen/dryRun`."},
    {"logging/setLevel", "request",
     "Takes `{level}` (`debug`, `info`, `notice`, `warning`, `error`, `critical`, `alert`, `emergency`): which of "
     "the engine's log lines reach the client as `notifications/message`. The default is `warning`."},
    {"notifications/cancelled", "notification",
     "Accepted and ignored: a call runs to its end (the server does not support cancellation; `job.cancel` stops a "
     "render job)."},
    {"notifications/progress", "server notification",
     "`{progressToken, progress, total: 1, message}`, sent only while the `tools/call` that supplied "
     "`_meta.progressToken` runs: the tool's own progress, and for `job.wait` the waited job's state and phase "
     "(waiting for the GPU lock, compiling pipelines, playing history, rendering). `progress` only increases; a repeat of the last message with no more progress is not sent."},
    {"notifications/message", "server notification",
     "`{level, logger: \"avgen\", data}`: an engine log line at or above the level `logging/setLevel` chose."},
};

std::string cell(std::string_view s) {
    std::string out(s);
    std::size_t at = 0;
    while ((at = out.find('|', at)) != std::string::npos) {
        out.replace(at, 1, "\\|");
        at += 2;
    }
    return out;
}

bool flag(const json& annotations, const char* key) { return annotations.value(key, false); }

// `avgen serve`'s initialize answer without what describes the JSON-RPC protocol itself (its name and version,
// batches, its notification names), which would be wrong here; `gpu` and `render` are kept, at the top.
json serverMeta(const json& avgenInitialize) {
    json meta = avgenInitialize;
    const json capabilities = meta.value("capabilities", json::object());
    meta.erase("protocol");
    meta.erase("capabilities");
    meta["gpu"] = capabilities.value("gpu", false);
    meta["render"] = capabilities.value("render", false);
    return meta;
}

} // namespace

McpNameTable::McpNameTable(const json& tools) {
    for (const json& t : tools) {
        const std::string name = t.at("name").get<std::string>();
        const std::string mcp = t.at("mcpName").get<std::string>();
        if (!toTool_.emplace(mcp, name).second || !toMcp_.emplace(name, mcp).second) {
            throw std::invalid_argument(fmt::format("the MCP name '{}' (tool '{}') is not unique", mcp, name));
        }
    }
}

std::optional<std::string> McpNameTable::toolFor(std::string_view mcpName) const {
    const auto it = toTool_.find(mcpName);
    return it == toTool_.end() ? std::nullopt : std::optional<std::string>(it->second);
}

std::optional<std::string> McpNameTable::mcpNameFor(std::string_view tool) const {
    const auto it = toMcp_.find(tool);
    return it == toMcp_.end() ? std::nullopt : std::optional<std::string>(it->second);
}

json mcpInitializeResult(const json& avgenInitialize) {
    const json& server = avgenInitialize.at("server");
    const int api = avgenInitialize.at("api").get<int>();
    return json{
        {"protocolVersion", kMcpProtocolRevision},
        {"capabilities", json{{"tools", json{{"listChanged", false}}}, {"logging", json::object()}}},
        {"serverInfo", json{{"name", server.at("name")}, {"title", "AV Gen"}, {"version", server.at("version")}}},
        {"instructions",
         fmt::format("AV Gen engine, API level {}. Every tool returns its envelope as structuredContent: success, "
                     "result, summary, changes, warnings, transaction, job (and error on failure). Paths are "
                     "relative to the open project's folder; the session may read and write only its roots "
                     "(_meta[\"avgen/server\"].permissions). Renders (render_capture, render_start) return a job "
                     "id at once: wait with job_wait. Output files are returned as paths. Nothing is saved unless "
                     "you call project_save.",
                     api)},
        {"_meta", json{{kMcpMetaServer, serverMeta(avgenInitialize)}}}};
}

json mcpOutputSchema(const json& toolOutputSchema) {
    const json result = toolOutputSchema.is_object() && !toolOutputSchema.empty()
                            ? json{{"anyOf", json::array({toolOutputSchema, json{{"type", "null"}}})}}
                            : json::object();
    const json nullableString = json{{"type", json::array({"string", "null"})}};
    return json{
        {"type", "object"},
        {"description",
         "The AV Gen call envelope. `result` is the tool's value (null on failure); `error` is present only on "
         "failure."},
        {"properties",
         json{{"success", json{{"type", "boolean"}}},
              {"result", result},
              {"summary", json{{"type", "string"}}},
              {"changes", json{{"type", "array"},
                               {"items", json{{"type", "object"},
                                              {"properties", json{{"target", json{{"type", "string"}}},
                                                                  {"detail", json{{"type", "string"}}},
                                                                  {"clamped", json{{"type", "boolean"}}}}}}}}},
              {"warnings", json{{"type", "array"},
                                {"items", json{{"type", "object"},
                                               {"properties", json{{"code", json{{"type", "string"}}},
                                                                   {"message", json{{"type", "string"}}},
                                                                   {"pointer", json{{"type", "string"}}}}}}}}},
              {"transaction", nullableString},
              {"job", nullableString},
              {"error", json{{"type", "object"},
                             {"properties", json{{"code", json{{"type", "string"}}},
                                                 {"message", json{{"type", "string"}}},
                                                 {"recovery", json{{"type", "string"}}},
                                                 {"details", json{{"type", "object"}}}}},
                             {"required", json::array({"code", "message"})}}}}},
        {"required", json::array({"success", "result", "summary", "changes", "warnings", "transaction", "job"})}};
}

json mcpTool(const json& entry) {
    const json& a = entry.at("annotations");
    json meta{{kMcpMetaTool, entry.at("name")},
              {"avgen/executionClass", a.value("executionClass", std::string{})},
              {"avgen/permissions", a.value("permissions", json::array())},
              {"avgen/since", entry.value("since", 0)}};
    if (entry.contains("availability")) {
        meta["avgen/availability"] = entry.at("availability");
    }
    return json{{"name", entry.at("mcpName")},
                {"title", entry.at("title")},
                {"description", entry.at("description")},
                {"inputSchema", entry.at("inputSchema")},
                {"outputSchema", mcpOutputSchema(entry.value("outputSchema", json()))},
                {"annotations", json{{"title", entry.at("title")},
                                     {"readOnlyHint", flag(a, "readOnly")},
                                     {"destructiveHint", flag(a, "destructive")},
                                     {"idempotentHint", flag(a, "idempotent")},
                                     {"openWorldHint", false}}},
                {"_meta", std::move(meta)}};
}

json mcpToolsListResult(const json& tools) {
    json out = json::array();
    for (const json& t : tools) {
        out.push_back(mcpTool(t));
    }
    return json{{"tools", std::move(out)}};
}

json mcpCallToolResult(const json& envelope) {
    const bool success = envelope.at("success").get<bool>();
    std::string text = envelope.value("summary", std::string{});
    text += "\n";
    text += envelope.dump(-1, ' ', false, json::error_handler_t::replace);
    return json{{"content", json::array({json{{"type", "text"}, {"text", std::move(text)}}})},
                {"structuredContent", envelope},
                {"isError", !success}};
}

json mcpProgressParams(const json& progressToken, double progress, const std::string& message) {
    json p{{"progressToken", progressToken}, {"progress", progress}, {"total", 1.0}};
    if (!message.empty()) {
        p["message"] = message;
    }
    return p;
}

std::optional<int> mcpLogLevelToSpd(std::string_view level) {
    if (level == "debug") return spdlog::level::debug;
    if (level == "info" || level == "notice") return spdlog::level::info;
    if (level == "warning") return spdlog::level::warn;
    if (level == "error") return spdlog::level::err;
    if (level == "critical" || level == "alert" || level == "emergency") return spdlog::level::critical;
    return std::nullopt;
}

const char* mcpLogLevelName(int spdLevel) {
    switch (spdLevel) {
    case spdlog::level::trace:
    case spdlog::level::debug: return "debug";
    case spdlog::level::info: return "info";
    case spdlog::level::warn: return "warning";
    case spdlog::level::err: return "error";
    case spdlog::level::critical: return "critical";
    default: return "info";
    }
}

std::span<const McpMethod> mcpMethods() { return kMcpMethods; }

bool mcpKnownClientMethod(std::string_view method) {
    return std::any_of(std::begin(kMcpMethods), std::end(kMcpMethods),
                       [&](const McpMethod& m) { return m.name == method && m.kind != "server notification"; });
}

std::string mcpMarkdown(const json& catalogue) {
    const json& tools = catalogue.at("tools");
    std::string out;
    out += "# AV Gen over MCP\n\n";
    out += "<!-- Generated by `avgen --describe all` from src/api/mcp_shaper.cpp and the tool catalogue (ADR-2162). "
           "Do not edit by hand: run tools/gen_api.sh. -->\n\n";
    out += fmt::format(
        "`avgen mcp` serves the engine's tools to a Model Context Protocol client on stdin and stdout, protocol "
        "revision **{}**. It is `avgen serve` (see [protocol.md](protocol.md)) with a different result shaper: the "
        "same loop, the same session, the same `Session::call`, the same {} tools ([tools.md](tools.md)). Nothing "
        "here is a second implementation of a tool, so a tool behaves the same through Studio, `avgen call`, "
        "`avgen serve` and `avgen mcp`.\n\n",
        kMcpProtocolRevision, tools.size());
    out += "## Client configuration\n\n"
           "Most MCP clients take a `mcpServers` block. Give the session a root: the folder it may read and write "
           "(projects, imported assets, renders). Without one, every file tool is refused `PERMISSION_DENIED`.\n\n"
           "```json\n"
           "{\n"
           "  \"mcpServers\": {\n"
           "    \"avgen\": {\n"
           "      \"command\": \"/path/to/avgen\",\n"
           "      \"args\": [\"mcp\", \"--root\", \"/path/to/work\"]\n"
           "    }\n"
           "  }\n"
           "}\n"
           "```\n\n"
           "The flags are `avgen serve`'s: `--root <dir>` (read and write), `--read-root <dir>`, "
           "`--write-root <dir>` (each repeatable) and `--allow-subprocess`. The log is on stderr. The process opens "
           "no socket and no window; a GPU device is made only when the first render job starts, under the machine's "
           "GPU lock.\n\n";
    out += "## Protocol revision\n\n";
    out += fmt::format(
        "The server speaks `{}` and only that, the newest revision that opens with `initialize`. `initialize` always "
        "answers it; a client that cannot speak it disconnects. Revision 2026-07-28 replaced the handshake with "
        "per-request `_meta` and `server/discover`; a client that speaks both probes `server/discover`, receives "
        "`-32601` (method not found) and falls back to `initialize`. The revision is one constant, "
        "`api::kMcpProtocolRevision`, and the shape tests name it.\n\n",
        kMcpProtocolRevision);
    out += "## Methods and notifications\n\n| Method | Kind | What it does |\n|---|---|---|\n";
    for (const McpMethod& m : kMcpMethods) {
        out += fmt::format("| `{}` | {} | {} |\n", m.name, m.kind, cell(m.summary));
    }
    out += "\nAny other method is `-32601`. A batch (a JSON array) is `-32600`: this revision has no batches. A request "
           "before `initialize` (except `ping`) is `-32600`. End of input closes the session: an open explicit "
           "transaction is rolled back, and nothing is saved.\n\n";
    out += "## Results\n\n"
           "`tools/call` answers a `CallToolResult`:\n\n"
           "- `structuredContent` is the envelope every client receives (`docs/api/protocol.md`): `success`, `result`, "
           "`summary`, `changes`, `warnings`, `transaction`, `job`, and `error {code, message, recovery, details?}` "
           "on failure. It is the same JSON `avgen serve` answers for the same call.\n"
           "- `isError` is `!success`. A tool failure, including invalid arguments and a refused permission, is a "
           "result with `isError: true`, so the model can read the error and retry.\n"
           "- `content[0]` is a text block: the summary, a newline, and the envelope as compact JSON, for clients "
           "that do not read `structuredContent`.\n"
           "- **Files are paths.** A capture or render lists its outputs as absolute paths in the job record "
           "(`job_wait`'s `result.outputs[].path`, with `sha256` and the frame hashes). No image is inlined.\n"
           "- Each tool's `outputSchema` describes the envelope, with the tool's own output schema under `result` "
           "(or `{}` for a tool that has none yet).\n\n"
           "An **unknown tool name** is a protocol error, as MCP has it: `-32602` with `data {name, recovery}`. "
           "`avgen serve` answers the same mistake with a tool result (`NOT_FOUND`); that is the one place the two "
           "differ.\n\n";
    out += "## Transactions\n\n"
           "MCP has no field for them, so they ride in `tools/call`'s `_meta`: `{\"avgen/transaction\": \"txn-1\"}` "
           "joins the explicit transaction `transaction_begin` opened, and `{\"avgen/irreversible\": true}` accepts "
           "a call a rollback cannot undo. While one is open, a mutating call that does not name it is refused "
           "`FAILED_PRECONDITION` ([transactions.md](transactions.md)). A client that cannot set `_meta` should not "
           "open an explicit transaction: every call outside one runs in an implicit transaction of its own, rolled "
           "back if it fails.\n\n";
    out += "## The name table\n\n"
           "MCP clients accept tool names matching `^[a-zA-Z0-9_-]{1,64}$`; AV Gen's are `domain.verb`. Each tool's "
           "`mcpName` is generated into the catalogue, and `tools/call` maps it back through this table, never by "
           "reversing the string.\n\n"
           "| MCP name | Tool | readOnly | destructive | idempotent |\n|---|---|---|---|---|\n";
    for (const json& t : tools) {
        const json& a = t.at("annotations");
        const auto yes = [&](const char* key) { return flag(a, key) ? "yes" : ""; };
        out += fmt::format("| `{}` | `{}` | {} | {} | {} |\n", t.at("mcpName").get<std::string>(),
                           t.at("name").get<std::string>(), yes("readOnly"), yes("destructive"), yes("idempotent"));
    }
    return out;
}

} // namespace avgen::api
