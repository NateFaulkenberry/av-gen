#include "api/jsonrpc_server.hpp"

#include "ai/tool_context.hpp"
#include "api/describe.hpp"
#include "api/version.hpp"
#include "app/engine.hpp"
#include "core/log.hpp"

#include <fmt/format.h>
#include <spdlog/sinks/base_sink.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <unistd.h>

namespace avgen::api {
namespace {

using nlohmann::json;

constexpr RpcMethod kMethods[] = {
    {"initialize", "request", "`{clientInfo?: {name, version}, logLevel?: \"error\"|\"warn\"|\"info\"|\"debug\"|\"off\"}`",
     "`{protocol, server, versions, api, session, permissions, capabilities}`",
     "Must be the first request. Answers the build's versions (`api::version()`), the API level, the session's "
     "permissions and what this server can do. `logLevel` sets which log lines reach the client as "
     "`notifications/log` (default `warn`). A second `initialize` is refused."},
    {"notifications/initialized", "notification", "none", "no reply",
     "Accepted and ignored, for clients that send it after `initialize`."},
    {"tools/list", "request", "none", "`{tools: [...]}`",
     "The catalogue: each tool as `schemas/tool-catalogue.json` lists it, plus `availability` "
     "`{available, permitted, unavailableReason?, deniedReason?}` for this session at this moment."},
    {"tools/call", "request",
     "`{name, arguments?, transaction?, dryRun?, irreversible?}`",
     "the call envelope (`docs/api/transactions.md`)",
     "Runs one tool through `Session::call` and answers its envelope: `success`, `result`, `summary`, `changes`, "
     "`warnings`, `transaction`, `job`, and `error` on failure. A tool failure is a successful response with "
     "`success: false`. `transaction` joins the open explicit transaction; `irreversible: true` (or `\"accept\"`) "
     "accepts a call a rollback cannot undo; `dryRun: true` checks the name, the arguments, the permissions and "
     "availability and runs nothing."},
    {"shutdown", "request", "none", "`{closed: true, rolledBack: <transaction id or null>}`",
     "Closes the session -- an open explicit transaction is rolled back, and nothing is saved -- answers, and "
     "ends the process. End of input does the same without the answer."},
    {"notifications/progress", "server notification",
     "`{requestId, tool, progress, message}`", "none",
     "Sent while a `tools/call` runs a tool that reports progress; `requestId` is the call's `id`, `progress` is "
     "0 to 1."},
    {"notifications/log", "server notification", "`{level, message}`", "none",
     "A line of the engine's own log at or above the `logLevel` the client chose. The full log is on stderr."},
};

constexpr RpcErrorRow kErrors[] = {
    {kParseError, "parse error", "a line is not JSON"},
    {kInvalidRequest, "invalid request",
     "not a JSON-RPC 2.0 request: not an object, `jsonrpc` is not \"2.0\", no string `method`, an `id` that is not "
     "a string, an integer or null, `params` that are neither an object nor an array; also a second `initialize`"},
    {kMethodNotFound, "method not found",
     "an unknown METHOD. An unknown tool is not this: it is a `tools/call` result with error `NOT_FOUND`"},
    {kInvalidParams, "invalid params",
     "a method's `params` are malformed: `tools/call` without a string `name`, `arguments` that are not an object, "
     "a `transaction` that is not a string, a `dryRun` that is not a boolean, an unknown `logLevel`"},
    {kNotInitialized, "not initialized", "any request but `initialize` or `shutdown` before `initialize`"},
    {kInternalError, "internal error", "the server failed while answering; the session is unchanged or rolled back"},
};

constexpr const char* kTranscript = R"(> {"jsonrpc":"2.0","id":1,"method":"initialize","params":{"clientInfo":{"name":"poc-client","version":"1"}}}
< {"id":1,"jsonrpc":"2.0","result":{"api":1,"capabilities":{"batch":true,"cancellation":false,"concurrency":"sequential","dryRun":true,"gpu":false,"jobs":false,"notifications":["notifications/progress","notifications/log"],"render":false,"tools":{"count":81,"listChanged":false},"transactions":{"explicit":true,"perSession":1}},"permissions":{"gpu":true,"liveControl":false,"openProjectFolder":false,"projectAccess":"read-write","readRoots":["/work"],"subprocess":false,"writeRoots":["/work"]},"protocol":{"name":"avgen-jsonrpc","version":1},"server":{"name":"avgen","version":"0.3.0"},"session":{"client":{"kind":"json-rpc","name":"poc-client","version":"1"},"id":1,"policy":"authoring"},"versions":{...}}}
> {"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"project.create","arguments":{"name":"poc"}}}
< {"id":2,"jsonrpc":"2.0","result":{"changes":[],"job":null,"result":{"exists":true,"file":"/work/poc/poc.json","name":"poc"},"success":true,"summary":"created 'poc'","transaction":null,"warnings":[]}}
> {"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"asset.import","arguments":{"file":"../resonator.glb"}}}
< {"id":3,"jsonrpc":"2.0","result":{"changes":[],"job":null,"result":{"copied":["/work/poc/assets/models/resonator.glb"],"id":"asset://project/models/resonator","path":"/work/poc/assets/models/resonator.glb",...},"success":true,"summary":"asset imported into project","transaction":null,"warnings":[]}}
> {"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"no.such_tool"}}
< {"id":4,"jsonrpc":"2.0","result":{"changes":[],"error":{"code":"NOT_FOUND","message":"no tool named 'no.such_tool'","recovery":"call capability.list_tools for the available tool names"},"job":null,"result":null,"success":false,"summary":"no tool named 'no.such_tool'","transaction":null,"warnings":[]}}
> {"jsonrpc":"2.0","id":5,"method":"tools/delete"}
< {"error":{"code":-32601,"message":"no method 'tools/delete'"},"id":5,"jsonrpc":"2.0"}
> {"jsonrpc":"2.0","id":6,"method":"shutdown"}
< {"id":6,"jsonrpc":"2.0","result":{"closed":true,"rolledBack":null}})";

std::string cell(std::string_view s) {
    std::string out(s);
    std::size_t at = 0;
    while ((at = out.find('|', at)) != std::string::npos) {
        out.replace(at, 1, "\\|");
        at += 2;
    }
    return out;
}

std::optional<spdlog::level::level_enum> logLevelNamed(std::string_view name) {
    if (name == "error") return spdlog::level::err;
    if (name == "warn") return spdlog::level::warn;
    if (name == "info") return spdlog::level::info;
    if (name == "debug") return spdlog::level::debug;
    if (name == "off") return spdlog::level::off;
    return std::nullopt;
}

const char* logLevelName(spdlog::level::level_enum level) {
    switch (level) {
    case spdlog::level::trace: return "trace";
    case spdlog::level::debug: return "debug";
    case spdlog::level::info: return "info";
    case spdlog::level::warn: return "warn";
    case spdlog::level::err: return "error";
    case spdlog::level::critical: return "critical";
    default: return "info";
    }
}

// True while this thread is inside the writer, so a log line the writer itself causes is not forwarded back
// into the writer (which would deadlock on its own mutex).
thread_local bool tInWriter = false;

class Server;

// Forwards the engine's log to the client as `notifications/log`, at or above the level it chose.
class LogForwarder final : public spdlog::sinks::base_sink<std::mutex> {
public:
    explicit LogForwarder(Server& server) : server_(&server) {}
    void detach() {
        const std::lock_guard lock(mutex_);
        server_ = nullptr;
    }

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override;
    void flush_() override {}

private:
    Server* server_;
};

class Server {
public:
    Server(Session& session, EngineHost& host, int inFd, int outFd)
        : session_(session), host_(host), in_(inFd), out_(outFd),
          catalogue_(toolCatalogue(session.tools()).at("tools")) {}

    void start() {
        forwarder_ = std::make_shared<LogForwarder>(*this);
        spdlog::default_logger()->sinks().push_back(forwarder_);
        reader_ = std::thread([this] { readerLoop(); });
    }

    // After the host loop has ended.
    void join() {
        if (reader_.joinable()) {
            reader_.join();
        }
        auto& sinks = spdlog::default_logger()->sinks();
        sinks.erase(std::remove(sinks.begin(), sinks.end(), forwarder_), sinks.end());
        forwarder_->detach();
    }

    void forwardLog(spdlog::level::level_enum level, std::string_view text) {
        if (tInWriter || !initialized_.load(std::memory_order_acquire) ||
            level < logLevel_.load(std::memory_order_acquire)) {
            return;
        }
        send(json{{"jsonrpc", "2.0"},
                  {"method", "notifications/log"},
                  {"params", json{{"level", logLevelName(level)}, {"message", std::string(text)}}}});
    }

private:
    // ---- writing -----------------------------------------------------------------------------------

    void send(const json& message) {
        const std::string line = message.dump(-1, ' ', false, json::error_handler_t::replace) + "\n";
        const std::lock_guard lock(writeMutex_);
        tInWriter = true;
        std::size_t done = 0;
        while (done < line.size() && !broken_) {
            const ssize_t n = ::write(out_, line.data() + done, line.size() - done);
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                broken_ = true; // the client went away; the reader ends at its next line or at EOF
                break;
            }
            done += static_cast<std::size_t>(n);
        }
        tInWriter = false;
    }

    static json response(const json& id, json result) {
        return json{{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}};
    }
    static json errorResponse(const json& id, int code, std::string message, json data = nullptr) {
        json e{{"code", code}, {"message", std::move(message)}};
        if (!data.is_null()) {
            e["data"] = std::move(data);
        }
        return json{{"jsonrpc", "2.0"}, {"id", id}, {"error", std::move(e)}};
    }

    // ---- reading -----------------------------------------------------------------------------------

    void readerLoop() {
        std::string buffer;
        char chunk[65536];
        bool eof = false;
        while (!done_ && !eof) {
            const ssize_t n = ::read(in_, chunk, sizeof chunk);
            if (n < 0 && errno == EINTR) {
                continue;
            }
            if (n <= 0) {
                eof = true;
            } else {
                buffer.append(chunk, static_cast<std::size_t>(n));
            }
            std::size_t start = 0;
            for (std::size_t nl; !done_ && (nl = buffer.find('\n', start)) != std::string::npos; start = nl + 1) {
                handleLine(std::string_view(buffer).substr(start, nl - start));
            }
            buffer.erase(0, start);
            if (eof && !done_ && !buffer.empty()) {
                handleLine(buffer); // a last line without its newline
            }
        }
        if (!done_) {
            const json rolledBack = closeSession();
            log::info("serve: end of input; session closed{}",
                      rolledBack.is_string() ? fmt::format(", transaction '{}' rolled back", rolledBack.get<std::string>())
                                             : std::string{});
        }
        host_.stop();
    }

    void handleLine(std::string_view line) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
            line.remove_suffix(1);
        }
        if (line.find_first_not_of(" \t") == std::string_view::npos) {
            return;
        }
        json message = json::parse(line, nullptr, false);
        if (message.is_discarded()) {
            send(errorResponse(nullptr, kParseError, "the line is not JSON"));
            return;
        }
        if (message.is_array()) {
            if (message.empty()) {
                send(errorResponse(nullptr, kInvalidRequest, "an empty batch"));
                return;
            }
            json replies = json::array();
            for (const json& m : message) {
                if (auto r = handleMessage(m)) {
                    replies.push_back(std::move(*r));
                }
            }
            if (!replies.empty()) {
                send(replies);
            }
            return;
        }
        if (auto r = handleMessage(message)) {
            send(*r);
        }
    }

    // The reply, or nothing for a notification.
    std::optional<json> handleMessage(const json& m) {
        if (!m.is_object()) {
            return errorResponse(nullptr, kInvalidRequest, "a request must be a JSON object");
        }
        const bool notification = !m.contains("id");
        json id = nullptr;
        if (!notification) {
            const json& given = m.at("id");
            if (!given.is_string() && !given.is_number_integer() && !given.is_null()) {
                return errorResponse(nullptr, kInvalidRequest, "id must be a string, an integer or null");
            }
            id = given;
        }
        const auto reply = [&](json r) -> std::optional<json> {
            if (notification) {
                return std::nullopt;
            }
            return r;
        };
        if (!m.contains("jsonrpc") || m.at("jsonrpc") != "2.0") {
            return reply(errorResponse(id, kInvalidRequest, "jsonrpc must be \"2.0\""));
        }
        if (!m.contains("method") || !m.at("method").is_string()) {
            return reply(errorResponse(id, kInvalidRequest, "method must be a string"));
        }
        const json params = m.contains("params") ? m.at("params") : json(nullptr);
        if (!params.is_null() && !params.is_object() && !params.is_array()) {
            return reply(errorResponse(id, kInvalidRequest, "params must be an object or an array"));
        }
        const std::string method = m.at("method").get<std::string>();
        try {
            return reply(dispatch(method, id, params));
        } catch (const std::exception& e) {
            log::error("serve: '{}' failed: {}", method, e.what());
            return reply(errorResponse(id, kInternalError, fmt::format("internal error: {}", e.what())));
        } catch (...) {
            return reply(errorResponse(id, kInternalError, "internal error"));
        }
    }

    json dispatch(const std::string& method, const json& id, const json& params) {
        const bool known = std::any_of(std::begin(kMethods), std::end(kMethods), [&](const RpcMethod& r) {
            return r.name == method && r.kind != "server notification";
        });
        if (!known) {
            return errorResponse(id, kMethodNotFound, fmt::format("no method '{}'", method));
        }
        if (method == "notifications/initialized") {
            return response(id, json::object()); // dropped: it is a notification
        }
        if (method == "initialize") {
            return initialize(id, params);
        }
        if (method == "shutdown") {
            const json rolledBack = closeSession();
            done_ = true;
            return response(id, json{{"closed", true}, {"rolledBack", rolledBack}});
        }
        if (!initialized_.load(std::memory_order_acquire)) {
            return errorResponse(id, kNotInitialized, fmt::format("'{}' before initialize: send initialize first", method));
        }
        if (method == "tools/list") {
            return toolsList(id);
        }
        return toolsCall(id, params);
    }

    json initialize(const json& id, const json& params) {
        if (initialized_.load(std::memory_order_acquire)) {
            return errorResponse(id, kInvalidRequest, "the session is already initialized");
        }
        if (!params.is_null() && !params.is_object()) {
            return errorResponse(id, kInvalidParams, "initialize takes an object");
        }
        json clientInfo = json{{"kind", clientKindName(session_.client().kind)}, {"name", ""}, {"version", ""}};
        if (params.is_object() && params.contains("clientInfo")) {
            const json& c = params.at("clientInfo");
            if (!c.is_object() || (c.contains("name") && !c.at("name").is_string()) ||
                (c.contains("version") && !c.at("version").is_string())) {
                return errorResponse(id, kInvalidParams, "clientInfo must be {name: string, version: string}");
            }
            clientInfo["name"] = c.value("name", std::string{});
            clientInfo["version"] = c.value("version", std::string{});
        }
        if (params.is_object() && params.contains("logLevel")) {
            const json& l = params.at("logLevel");
            const auto level = l.is_string() ? logLevelNamed(l.get<std::string>()) : std::nullopt;
            if (!level) {
                return errorResponse(id, kInvalidParams, "logLevel must be one of error, warn, info, debug, off");
            }
            logLevel_.store(*level, std::memory_order_release);
        }
        log::info("serve: initialized by '{}' {}", clientInfo["name"].get<std::string>(),
                  clientInfo["version"].get<std::string>());
        json out{{"protocol", json{{"name", kProtocolName}, {"version", kProtocolVersion}}},
                 {"server", json{{"name", "avgen"}, {"version", version().engine}}},
                 {"versions", version().toJson()},
                 {"api", kApiLevel},
                 {"session", json{{"id", session_.id()}, {"client", clientInfo}, {"policy", hostPolicyName(session_.policy())}}},
                 {"permissions", session_.permissions().toJson()},
                 {"capabilities",
                  json{{"tools", json{{"count", session_.tools().size()}, {"listChanged", false}}},
                       {"transactions", json{{"explicit", true}, {"perSession", 1}}},
                       {"dryRun", true},
                       {"batch", true},
                       {"concurrency", "sequential"},
                       {"cancellation", false},
                       {"notifications", json::array({"notifications/progress", "notifications/log"})},
                       {"gpu", host_.canRender()},
                       {"render", false},
                       {"jobs", false}}}};
        initialized_.store(true, std::memory_order_release);
        return response(id, std::move(out));
    }

    json toolsList(const json& id) {
        json tools = catalogue_;
        bool ran = session_.onHost([&](app::Engine&) {
            for (json& t : tools) {
                const ToolAvailability a = session_.availability(t.at("name").get<std::string>());
                json av{{"available", a.available}, {"permitted", a.permitted}};
                if (!a.unavailableReason.empty()) {
                    av["unavailableReason"] = a.unavailableReason;
                }
                if (!a.deniedReason.empty()) {
                    av["deniedReason"] = a.deniedReason;
                }
                t["availability"] = std::move(av);
            }
        });
        if (!ran) {
            return errorResponse(id, kInternalError, "the host did not answer");
        }
        return response(id, json{{"tools", std::move(tools)}});
    }

    json toolsCall(const json& id, const json& params) {
        if (!params.is_object()) {
            return errorResponse(id, kInvalidParams, "tools/call takes an object {name, arguments?, ...}");
        }
        if (!params.contains("name") || !params.at("name").is_string() || params.at("name").get<std::string>().empty()) {
            return errorResponse(id, kInvalidParams, "tools/call needs a string name");
        }
        const std::string name = params.at("name").get<std::string>();
        json args = json::object();
        if (params.contains("arguments") && !params.at("arguments").is_null()) {
            if (!params.at("arguments").is_object()) {
                return errorResponse(id, kInvalidParams, "arguments must be an object");
            }
            args = params.at("arguments");
        }
        CallOptions options;
        if (params.contains("transaction") && !params.at("transaction").is_null()) {
            if (!params.at("transaction").is_string()) {
                return errorResponse(id, kInvalidParams, "transaction must be a string");
            }
            options.transaction = params.at("transaction").get<std::string>();
        }
        if (params.contains("irreversible") && !params.at("irreversible").is_null()) {
            const json& i = params.at("irreversible");
            if (i.is_boolean()) {
                options.irreversible = i.get<bool>() ? Irreversible::Accept : Irreversible::Refuse;
            } else if (i == "accept" || i == "refuse") {
                options.irreversible = i == "accept" ? Irreversible::Accept : Irreversible::Refuse;
            } else {
                return errorResponse(id, kInvalidParams, "irreversible must be a boolean, \"accept\" or \"refuse\"");
            }
        }
        bool dryRun = false;
        if (params.contains("dryRun") && !params.at("dryRun").is_null()) {
            if (!params.at("dryRun").is_boolean()) {
                return errorResponse(id, kInvalidParams, "dryRun must be a boolean");
            }
            dryRun = params.at("dryRun").get<bool>();
        }
        if (dryRun) {
            return response(id, dryRunEnvelope(name, args, options).toWire());
        }
        options.onProgress = [this, id, name](float progress, const std::string& message) {
            send(json{{"jsonrpc", "2.0"},
                      {"method", "notifications/progress"},
                      {"params", json{{"requestId", id}, {"tool", name}, {"progress", progress}, {"message", message}}}});
        };
        return response(id, session_.call(name, args, options).toWire());
    }

    // What the call would meet, without running it: the name, the arguments (on this thread, as Session::call
    // checks them), then the permissions and availability (the session's capability answer, on the host).
    CallResult dryRunEnvelope(const std::string& name, const json& args, const CallOptions& options) {
        const ai::Tool* tool = session_.tools().find(name);
        if (tool == nullptr) {
            return CallResult::refused(ai::ToolRegistry::unknownTool(name));
        }
        if (auto e = ai::ToolRegistry::checkArguments(*tool, args)) {
            return CallResult::refused(std::move(*e));
        }
        ToolAvailability a;
        if (!session_.onHost([&](app::Engine&) { a = session_.availability(name); })) {
            return CallResult::refused(ai::ToolError{ai::ToolErrorCode::Cancelled, "the host did not answer", "retry", nullptr});
        }
        if (!a.permitted) {
            return CallResult::refused(ai::ToolError{ai::ToolErrorCode::PermissionDenied, a.deniedReason, {}, nullptr});
        }
        if (!a.available) {
            return CallResult::refused(ai::ToolError{ai::ToolErrorCode::Unavailable, a.unavailableReason, {}, nullptr});
        }
        CallResult out;
        out.result = json{{"dryRun", true},
                          {"tool", name},
                          {"executionClass", ai::executionClassName(tool->definition.annotations.effectiveExecutionClass())},
                          {"transaction", options.transaction ? json(*options.transaction) : json(nullptr)}};
        out.summary = fmt::format("'{}' would run; nothing was applied", name);
        return out;
    }

    // Closes the session on the host thread (rolling back an open explicit transaction) and answers which
    // transaction that was, or null. Idempotent.
    json closeSession() {
        json rolledBack = nullptr;
        if (session_.state() == SessionState::Ready) {
            const CallResult status = session_.call("transaction.status");
            if (status.success && status.result.value("state", std::string{}) == "open") {
                rolledBack = status.result.value("id", std::string{});
            }
        }
        (void)session_.onHost([&](app::Engine&) { session_.close(); });
        return rolledBack;
    }

    Session& session_;
    EngineHost& host_;
    int in_;
    int out_;
    const json catalogue_;
    std::mutex writeMutex_;
    bool broken_ = false; // guarded by writeMutex_
    std::atomic<bool> initialized_{false};
    std::atomic<spdlog::level::level_enum> logLevel_{spdlog::level::warn};
    bool done_ = false; // reader thread only
    std::thread reader_;
    std::shared_ptr<LogForwarder> forwarder_;
};

void LogForwarder::sink_it_(const spdlog::details::log_msg& msg) {
    if (server_ != nullptr) {
        server_->forwardLog(msg.level, std::string_view(msg.payload.data(), msg.payload.size()));
    }
}

} // namespace

std::span<const RpcMethod> rpcMethods() { return kMethods; }
std::span<const RpcErrorRow> rpcErrors() { return kErrors; }

std::string protocolMarkdown() {
    std::string out;
    out += "# The AV Gen JSON-RPC protocol\n\n";
    out += "<!-- Generated by `avgen --describe all` from src/api/jsonrpc_server.cpp (ADR-2155). Do not edit by hand: "
           "run tools/gen_api.sh. -->\n\n";
    out += fmt::format("Protocol `{}` version {}. `avgen serve --stdio` speaks it on stdin and stdout; the tools it "
                       "serves are in [tools.md](tools.md), their error codes in [errors.md](errors.md), and the "
                       "transaction rules in [transactions.md](transactions.md).\n\n",
                       kProtocolName, kProtocolVersion);
    out += "## Framing\n\n";
    out += "- **JSON-RPC 2.0, one message per line.** Each request, response and notification is a single line of "
           "JSON ending in `\\n`. A batch (a JSON array of requests) is answered with an array of the replies.\n"
           "- **stdout is the protocol and nothing else.** At start the server duplicates fd 1 for the protocol and "
           "points fd 1 at stderr, so a stray `printf` in the engine lands in the log, never in the stream. The log "
           "is on stderr.\n"
           "- **One call at a time.** Requests are answered in the order they arrive; a long tool call delays the "
           "next request.\n"
           "- **No listener.** stdio only: the process opens no socket, no OSC port and no MIDI input, reads no "
           "settings file and creates no window. It does not touch the GPU: tools that need it answer "
           "`UNAVAILABLE` (\"no GPU host in this session yet\").\n"
           "- **Session.** One session per process, created at start with the command line's permissions "
           "(`--root`, `--read-root`, `--write-root`, `--allow-subprocess`). `initialize` must come first.\n"
           "- **Shutdown.** `shutdown`, or end of input, closes the session: an open explicit transaction is "
           "rolled back, and the project is never saved for you (call `project.save`).\n\n";
    out += "## Methods and notifications\n\n";
    out += "| Method | Kind | Params | Result | What it does |\n|---|---|---|---|---|\n";
    for (const RpcMethod& m : kMethods) {
        out += fmt::format("| `{}` | {} | {} | {} | {} |\n", m.name, m.kind, cell(m.params), cell(m.result), cell(m.summary));
    }
    out += "\n## Errors\n\n";
    out += "A **tool** failure is not a protocol error: it is a successful response whose `result` has "
           "`success: false` and an `error` `{code, message, recovery, details?}` with a code from "
           "[errors.md](errors.md). These are the protocol's own errors, in the JSON-RPC `error` object:\n\n";
    out += "| Code | Name | When |\n|---|---|---|\n";
    for (const RpcErrorRow& e : kErrors) {
        out += fmt::format("| `{}` | {} | {} |\n", e.code, e.name, cell(e.when));
    }
    out += "\n## Example session\n\n";
    out += "`>` is what the client writes, `<` what the server answers (long values elided with `...`). Started as "
           "`avgen serve --stdio --root /work`, with `resonator.glb` in `/work`. A relative path is relative to the open "
           "project's folder, so the import names `../resonator.glb` from inside `/work/poc`.\n\n";
    out += "```text\n";
    out += kTranscript;
    out += "\n```\n";
    return out;
}

const char* serveUsage() {
    return "usage: avgen serve --stdio [--root <dir>]... [--read-root <dir>]... [--write-root <dir>]... "
           "[--allow-subprocess]\n"
           "  JSON-RPC 2.0, one message per line, on stdin and stdout (docs/api/protocol.md). No window, no GPU,\n"
           "  no settings file, no OSC or MIDI listener. The log is on stderr.\n"
           "  --root <dir>         a folder the session may read and write (repeatable)\n"
           "  --read-root <dir>    a folder it may read (repeatable)\n"
           "  --write-root <dir>   a folder it may write, and so read (repeatable)\n"
           "  --allow-subprocess   tools may start child processes (performance.profile_scene and its family)\n";
}

int serve(ServeOptions options, int inFd, int outFd) {
    EngineHost host;
    SessionOptions so;
    so.client = ClientInfo{ClientKind::JsonRpc, "json-rpc", ""};
    so.permissions = std::move(options.flags.permissions);
    so.permissions.liveControl = false;
    so.extraTools = std::move(options.extraTools);
    auto session = Session::create(std::move(so), host);
    if (!session) {
        log::error("serve: {}", session.error().message);
        return 7;
    }
    Server server(**session, host, inFd, outFd);
    server.start();
    host.run(**session);
    server.join();
    (*session)->close(); // host thread; a no-op when shutdown or end of input already closed it
    return 0;
}

int runServeCommand(int argc, char** argv) {
    bool stdio = false;
    HostFlags flags;
    for (int i = 2; i < argc; ++i) {
        const std::string_view a = argv[i];
        if (a == "--stdio") {
            stdio = true;
            continue;
        }
        if (a == "--help" || a == "-h") {
            std::fputs(serveUsage(), stdout);
            return 0;
        }
        auto consumed = parseHostFlag(argc, argv, i, flags);
        if (!consumed) {
            std::fprintf(stderr, "avgen serve: %s\n%s", consumed.error().message.c_str(), serveUsage());
            return 2;
        }
        if (!*consumed) {
            std::fprintf(stderr, "avgen serve: unknown argument '%s'\n%s", argv[i], serveUsage());
            return 2;
        }
    }
    if (!stdio) {
        std::fprintf(stderr, "avgen serve: pass --stdio (the only transport)\n%s", serveUsage());
        return 2;
    }
    auto protocol = protectStdout();
    if (!protocol) {
        log::error("serve: {}", protocol.error().message);
        return 7;
    }
    std::signal(SIGPIPE, SIG_IGN); // a client that closes our stdout ends the session through a write error
    log::info("{} serving JSON-RPC on stdio (CPU host: no GPU, no window, no listeners)", versionLine());
    const int code = serve(ServeOptions{std::move(flags), {}}, STDIN_FILENO, *protocol);
    restoreStdout(*protocol);
    return code;
}

} // namespace avgen::api
