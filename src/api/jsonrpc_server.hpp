#pragma once

// `avgen serve --stdio`: newline-delimited JSON-RPC 2.0 over a pair of file descriptors (ADR-2155, Engine API
// stage S2; 02-session-api.md sections 6, 9 and 10). The protocol is documented in docs/api/protocol.md, which is
// generated from the tables below (`protocolMarkdown`), so the document and the server cannot disagree.
//
// Threads:
//   - the HOST thread (the one `serve` is called on) owns the engine and runs `EngineHost::run`;
//   - the READER thread reads lines, parses them, validates the envelope and -- through `Session::call`, on its
//     own thread -- the tool arguments against the immutable registry, then waits for the host to run the body.
//     It never touches the engine. Requests are answered one at a time, in the order they arrive;
//   - every write (responses and notifications, from either thread) goes through one writer mutex, a whole line
//     at a time.
//
// A tool failure is a successful JSON-RPC response whose `result` is the envelope with `success: false`. Only a
// fault in the protocol itself -- a line that is not JSON, an envelope that is not JSON-RPC 2.0, an unknown
// method, malformed `params`, a call before `initialize` -- is a JSON-RPC error. An unknown TOOL is a tool result
// (`NOT_FOUND`), not an unknown method.
//
// Shutdown: a `shutdown` request, or end of input, closes the session on the host thread -- an open explicit
// transaction is rolled back -- and ends the loop. The project is never saved for the client.
//
// ADR-2162: the same loop speaks MCP (`avgen mcp`, `ServeProtocol::Mcp`). Only the method table and the shape of
// the answers differ, and the shapes come from api/mcp_shaper.hpp; the reader, the writer, the host thread, the
// argument checks and `Session::call` are this file's, for both protocols.

#include "ai/tool_api.hpp"
#include "api/engine_host.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::api {

// The protocol's own version, separate from the API level (which versions the tool catalogue).
inline constexpr int kProtocolVersion = 1;
inline constexpr const char* kProtocolName = "avgen-jsonrpc";

// One row per method or notification the server knows, in the order the protocol document lists them.
struct RpcMethod {
    std::string_view name;
    std::string_view kind;   // "request" (client to server), "notification" (client to server, no reply) or
                             // "server notification" (server to client)
    std::string_view params; // what it takes
    std::string_view result; // what it answers
    std::string_view summary;
};
[[nodiscard]] std::span<const RpcMethod> rpcMethods();

// The JSON-RPC error codes the server answers with (02-session-api.md section 9).
inline constexpr int kParseError = -32700;
inline constexpr int kInvalidRequest = -32600;
inline constexpr int kMethodNotFound = -32601;
inline constexpr int kInvalidParams = -32602;
inline constexpr int kInternalError = -32603;
inline constexpr int kNotInitialized = -32002;

struct RpcErrorRow {
    int code;
    std::string_view name;
    std::string_view when;
};
[[nodiscard]] std::span<const RpcErrorRow> rpcErrors();

// docs/api/protocol.md (a generated artefact, api/describe.hpp).
[[nodiscard]] std::string protocolMarkdown();

// Which protocol the server speaks on the stream (ADR-2162).
enum class ServeProtocol : std::uint8_t {
    JsonRpc, // `avgen serve --stdio`: docs/api/protocol.md
    Mcp,     // `avgen mcp`: docs/api/mcp.md
};

struct ServeOptions {
    HostFlags flags;
    ServeProtocol protocol = ServeProtocol::JsonRpc;
    // Tools added beside the standard catalogue (a test's probe), as `SessionOptions::extraTools`.
    std::vector<ai::Tool> extraTools;
    // ADR-2159: the host's GPU half. Empty: a CPU host (render tools `UNAVAILABLE`).
    RenderBackendFactory renderBackend;
};

// Runs a server reading requests from `inFd` and writing to `outFd` until `shutdown` or end of input, with the
// calling thread as the host thread. Returns 0, or 7 when the session could not be created. Closes neither fd.
[[nodiscard]] int serve(ServeOptions options, int inFd, int outFd);

// `avgen serve --stdio [--root d] [--read-root d] [--write-root d] [--allow-subprocess]`, and with
// `ServeProtocol::Mcp` `avgen mcp [--stdio] [the same flags]`: protects stdout and serves stdin/stdout. Exit code
// 0, 2 for usage, 7 for a host failure.
[[nodiscard]] int runServeCommand(int argc, char** argv, RenderBackendFactory renderBackend = {},
                                  ServeProtocol protocol = ServeProtocol::JsonRpc);
[[nodiscard]] const char* serveUsage();
[[nodiscard]] const char* mcpUsage();

} // namespace avgen::api
