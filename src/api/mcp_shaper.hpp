#pragma once

// `avgen mcp`: the Model Context Protocol face of the one Session (ADR-2162, Engine API stage S9;
// 02-session-api.md section 10, 01-api-contracts.md section 5.3).
//
// This is a RESULT SHAPER and a NAME MAP, nothing else. `avgen mcp` is `avgen serve`'s loop
// (api/jsonrpc_server.cpp, `ServeProtocol::Mcp`): the same reader, writer, host thread and `Session::call`.
// What differs is only the shape of four answers, built here from what the JSON-RPC server already builds:
//
//   initialize   -> {protocolVersion, capabilities: {tools, logging}, serverInfo, instructions, _meta}
//   tools/list   -> each catalogue entry as an MCP Tool: `mcpName`, annotation hints, inputSchema, and an
//                   outputSchema that describes the envelope (whose `result` is the tool's own outputSchema)
//   tools/call   -> `mcpName` back to the tool through the table (never by reversing the string), then
//                   `Session::call`; the envelope becomes CallToolResult{content, structuredContent, isError}
//   progress     -> `notifications/progress {progressToken, progress, total, message}`
//
// No function here touches an engine, a session or a file; each takes JSON and returns JSON, so the shapes are
// tested against fixtures without a server.

#include <nlohmann/json.hpp>

#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace avgen::api {

// The MCP revision this server speaks, and the only one. It is the newest revision that opens with the
// `initialize` handshake (a "legacy" revision in 2026-07-28's terms). 2026-07-28 replaced the handshake with
// per-request `_meta` and `server/discover`; a dual-era client probes `server/discover`, gets `-32601` here (a
// non-modern error) and falls back to `initialize`, as that revision's stdio compatibility rules say.
inline constexpr const char* kMcpProtocolRevision = "2025-11-25";

// MCP's own error codes (JSON-RPC's). An unknown tool is a protocol error in MCP, unlike in `avgen serve`, where it
// is a tool result: MCP says so (2025-11-25, server/tools, "Error Handling").
inline constexpr int kMcpInvalidRequest = -32600;
inline constexpr int kMcpMethodNotFound = -32601;
inline constexpr int kMcpInvalidParams = -32602;
inline constexpr int kMcpInternalError = -32603;

// The `_meta` keys this server reads on `tools/call` and writes on its results. `avgen/` is this server's prefix.
inline constexpr const char* kMcpMetaTransaction = "avgen/transaction";   // join the open explicit transaction
inline constexpr const char* kMcpMetaIrreversible = "avgen/irreversible"; // true, "accept" or "refuse"
inline constexpr const char* kMcpMetaDryRun = "avgen/dryRun";             // check, run nothing
inline constexpr const char* kMcpMetaTool = "avgen/tool";                 // on a listed tool: its canonical name
inline constexpr const char* kMcpMetaServer = "avgen/server";             // on initialize: serve's own answer

// The name table: `mcpName` -> tool name and back, built from the catalogue entries' `mcpName` fields (which
// `toolCatalogue` generates). A lookup is a table lookup; an MCP name that is not in it has no tool.
class McpNameTable {
public:
    // `tools` is `toolCatalogue(...).at("tools")`, or `tools/list`'s array (the same entries).
    explicit McpNameTable(const nlohmann::json& tools);
    [[nodiscard]] std::optional<std::string> toolFor(std::string_view mcpName) const;
    [[nodiscard]] std::optional<std::string> mcpNameFor(std::string_view tool) const;
    [[nodiscard]] std::size_t size() const { return toTool_.size(); }
    [[nodiscard]] const std::map<std::string, std::string, std::less<>>& entries() const { return toTool_; }

private:
    std::map<std::string, std::string, std::less<>> toTool_;
    std::map<std::string, std::string, std::less<>> toMcp_;
};

// `initialize`'s result. `avgenInitialize` is what `avgen serve` answers for the same session (protocol, server,
// versions, api, session, permissions, capabilities); it rides along under `_meta["avgen/server"]` -- without the
// JSON-RPC protocol's own name and capabilities, with `gpu` and `render` -- so an MCP client that wants the API
// level or the session's roots finds them without a second request.
[[nodiscard]] nlohmann::json mcpInitializeResult(const nlohmann::json& avgenInitialize);

// One catalogue entry (with `availability`, as `avgen serve`'s tools/list gives it, or without) as an MCP Tool.
[[nodiscard]] nlohmann::json mcpTool(const nlohmann::json& entry);
// `tools/list`'s result from `avgen serve`'s tools array. One page: there is never a `nextCursor`.
[[nodiscard]] nlohmann::json mcpToolsListResult(const nlohmann::json& tools);
// The envelope as a JSON Schema (an object at the root, as MCP requires), with `result` the tool's outputSchema or,
// on failure, null. A tool without an outputSchema gets `result: {}` (anything).
[[nodiscard]] nlohmann::json mcpOutputSchema(const nlohmann::json& toolOutputSchema);

// The envelope (`CallResult::toWire()`) as a CallToolResult: `structuredContent` is the envelope, unchanged;
// `isError` is `!success`; `content[0]` is a text block, the summary and then the compact JSON of the envelope.
[[nodiscard]] nlohmann::json mcpCallToolResult(const nlohmann::json& envelope);

// `notifications/progress`'s params. `total` is 1: AV Gen's progress is a fraction.
[[nodiscard]] nlohmann::json mcpProgressParams(const nlohmann::json& progressToken, double progress,
                                               const std::string& message);

// The log levels MCP names (RFC 5424's), mapped to and from the engine's.
[[nodiscard]] std::optional<int> mcpLogLevelToSpd(std::string_view level);   // spdlog::level::level_enum value
[[nodiscard]] const char* mcpLogLevelName(int spdLevel);

// One row per method or notification, as the protocol document lists them.
struct McpMethod {
    std::string_view name;
    std::string_view kind; // "request", "notification" (client to server) or "server notification"
    std::string_view summary;
};
[[nodiscard]] std::span<const McpMethod> mcpMethods();
[[nodiscard]] bool mcpKnownClientMethod(std::string_view method);

// docs/api/mcp.md (a generated artefact, api/describe.hpp): the methods, the shapes, the name table and a client
// configuration, from the tables above and the catalogue.
[[nodiscard]] std::string mcpMarkdown(const nlohmann::json& catalogue);

} // namespace avgen::api
