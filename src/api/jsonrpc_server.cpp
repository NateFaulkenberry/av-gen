#include "api/jsonrpc_server.hpp"

#include "ai/tool_context.hpp"
#include "api/describe.hpp"
#include "api/mcp_shaper.hpp"
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
     "`{requestId, tool, progress, message}`, or for a job `{job, tool, state, phase, progress, framesDone, "
     "framesTotal}`",
     "none",
     "Sent while a `tools/call` runs a tool that reports progress (`requestId` is the call's `id`), and for a render "
     "job (`job` is its id) on every state or phase change -- waiting for the GPU lock, compiling pipelines, "
     "playing history, rendering, the end -- and at most every 250 ms between. `progress` is 0 to 1."},
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
< {"id":1,"jsonrpc":"2.0","result":{"api":2,"capabilities":{"batch":true,"cancellation":false,"concurrency":"sequential","dryRun":true,"gpu":true,"jobs":true,"notifications":["notifications/progress","notifications/log"],"render":true,"tools":{"count":87,"listChanged":false},"transactions":{"explicit":true,"perSession":1}},"permissions":{"gpu":true,"liveControl":false,"openProjectFolder":false,"projectAccess":"read-write","readRoots":["/work"],"subprocess":false,"writeRoots":["/work"]},"protocol":{"name":"avgen-jsonrpc","version":1},"server":{"name":"avgen","version":"0.3.0"},"session":{"client":{"kind":"json-rpc","name":"poc-client","version":"1"},"id":1,"policy":"authoring"},"versions":{...}}}
> {"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"project.create","arguments":{"path":"poc","scene":"empty"}}}
< {"id":2,"jsonrpc":"2.0","result":{"changes":[],"job":null,"result":{"exists":true,"file":"/work/poc/poc.json","name":"poc"},"success":true,"summary":"created 'poc'","transaction":null,"warnings":[]}}
> {"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"asset.import","arguments":{"file":"../resonator.glb"}}}
< {"id":3,"jsonrpc":"2.0","result":{"changes":[],"job":null,"result":{"copied":["/work/poc/assets/models/resonator.glb"],"id":"asset://project/models/resonator.glb","path":"/work/poc/assets/models/resonator.glb",...},"success":true,"summary":"asset imported into project","transaction":null,"warnings":[]}}
> {"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"render.capture","arguments":{"frame":24,"fps":24,"width":480,"height":270,"path":"renders/a.png"}}}
< {"id":4,"jsonrpc":"2.0","result":{"changes":[],"job":"job-1","result":{"frames":1,"history":"play","historyFrames":24,"job":"job-1","path":"/work/poc/renders/a.png","state":"queued",...},"success":true,"summary":"job-1 queued: 1 frame(s) at 480x270 -> /work/poc/renders/a.png","transaction":null,"warnings":[]}}
< {"jsonrpc":"2.0","method":"notifications/progress","params":{"framesDone":0,"framesTotal":1,"job":"job-1","phase":"compiling pipelines: 41 of 120 pipeline(s) compiled; waiting for scene-lit","progress":0.0,"state":"preparing","tool":"render.capture"}}
> {"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"job.wait","arguments":{"id":"job-1","timeoutMs":600000}}}
< {"id":5,"jsonrpc":"2.0","result":{"changes":[],"job":"job-1","result":{"id":"job-1","state":"succeeded","outputs":[{"bytes":201734,"frame":0,"path":"/work/poc/renders/a.png","sha256":"ca97..."}],"result":{"frameHash":"645df906acfb383c","frameHashes":["645df906acfb383c"],"lock":{"heldByAncestor":false,"waited":false,...},"timing":{"deviceCreated":true,"startSeconds":74.06,...},...},"timedOut":false,...},"success":true,"summary":"job-1 succeeded","transaction":null,"warnings":[]}}
> {"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"no.such_tool"}}
< {"id":6,"jsonrpc":"2.0","result":{"changes":[],"error":{"code":"NOT_FOUND","message":"no tool named 'no.such_tool'","recovery":"call capability.list_tools for the available tool names"},"job":null,"result":null,"success":false,"summary":"no tool named 'no.such_tool'","transaction":null,"warnings":[]}}
> {"jsonrpc":"2.0","id":7,"method":"tools/delete"}
< {"error":{"code":-32601,"message":"no method 'tools/delete'"},"id":7,"jsonrpc":"2.0"}
> {"jsonrpc":"2.0","id":8,"method":"shutdown"}
< {"id":8,"jsonrpc":"2.0","result":{"closed":true,"rolledBack":null}})";

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
    Server(Session& session, EngineHost& host, int inFd, int outFd, ServeProtocol protocol)
        : session_(session), host_(host), in_(inFd), out_(outFd), protocol_(protocol),
          catalogue_(toolCatalogue(session.tools()).at("tools")), names_(catalogue_) {}

    void start() {
        // ADR-2159: a job's state and progress reach the client as notifications, from the host thread, through
        // the one writer. Sent only once the client has initialized. ADR-2162: over MCP, only to the progress
        // token of the call waiting on that job (MCP progress belongs to a request).
        session_.jobs().setListener([this](const json& event) {
            if (!initialized_.load(std::memory_order_acquire)) {
                return;
            }
            if (protocol_ == ServeProtocol::Mcp) {
                std::string message = event.value("state", std::string{});
                if (const std::string phase = event.value("phase", std::string{}); !phase.empty()) {
                    message += ": " + phase;
                }
                mcpProgress(event.value("progress", 0.0), message, event.value("job", std::string{}));
                return;
            }
            send(json{{"jsonrpc", "2.0"}, {"method", "notifications/progress"}, {"params", event}});
        });
        forwarder_ = std::make_shared<LogForwarder>(*this);
        spdlog::default_logger()->sinks().push_back(forwarder_);
        reader_ = std::thread([this] { readerLoop(); });
    }

    // After the host loop has ended.
    void join() {
        if (reader_.joinable()) {
            reader_.join();
        }
        session_.jobs().setListener({});
        auto& sinks = spdlog::default_logger()->sinks();
        sinks.erase(std::remove(sinks.begin(), sinks.end(), forwarder_), sinks.end());
        forwarder_->detach();
    }

    void forwardLog(spdlog::level::level_enum level, std::string_view text) {
        if (tInWriter || !initialized_.load(std::memory_order_acquire) ||
            level < logLevel_.load(std::memory_order_acquire)) {
            return;
        }
        if (protocol_ == ServeProtocol::Mcp) {
            if (!clientReady_.load(std::memory_order_acquire)) {
                return; // nothing unasked-for before notifications/initialized
            }
            send(json{{"jsonrpc", "2.0"},
                      {"method", "notifications/message"},
                      {"params", json{{"level", mcpLogLevelName(level)}, {"logger", "avgen"}, {"data", std::string(text)}}}});
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
            if (protocol_ == ServeProtocol::Mcp) {
                send(errorResponse(nullptr, kInvalidRequest, "MCP has no batches: send one request per line"));
                return;
            }
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
        if (protocol_ == ServeProtocol::Mcp) {
            return dispatchMcp(method, id, params);
        }
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
        json out = describeServer(clientInfo);
        initialized_.store(true, std::memory_order_release);
        return response(id, std::move(out));
    }

    // What `initialize` answers about this server and session, for either protocol (MCP carries it in `_meta`).
    json describeServer(const json& clientInfo) {
        return json{{"protocol", json{{"name", kProtocolName}, {"version", kProtocolVersion}}},
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
                       {"render", host_.canRender()},
                       {"jobs", true}}}};
    }

    json toolsList(const json& id) {
        auto tools = listedTools();
        if (!tools) {
            return errorResponse(id, kInternalError, "the host did not answer");
        }
        return response(id, json{{"tools", std::move(*tools)}});
    }

    // The catalogue with each tool's availability in this session now, or nothing when the host did not answer.
    std::optional<json> listedTools() {
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
            return std::nullopt;
        }
        return tools;
    }

    json toolsCall(const json& id, const json& params) {
        if (!params.is_object()) {
            return errorResponse(id, kInvalidParams, "tools/call takes an object {name, arguments?, ...}");
        }
        if (!params.contains("name") || !params.at("name").is_string() || params.at("name").get<std::string>().empty()) {
            return errorResponse(id, kInvalidParams, "tools/call needs a string name");
        }
        const std::string name = params.at("name").get<std::string>();
        json args;
        if (auto bad = callArguments(params, args)) {
            return errorResponse(id, kInvalidParams, *bad);
        }
        CallOptions options;
        bool dryRun = false;
        if (auto bad = callOptions(params, "transaction", "irreversible", "dryRun", options, dryRun)) {
            return errorResponse(id, kInvalidParams, *bad);
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

    // `arguments`, absent or null meaning {}. An error message when it is not an object.
    static std::optional<std::string> callArguments(const json& params, json& args) {
        args = json::object();
        if (params.contains("arguments") && !params.at("arguments").is_null()) {
            if (!params.at("arguments").is_object()) {
                return "arguments must be an object";
            }
            args = params.at("arguments");
        }
        return std::nullopt;
    }

    // The call options both protocols take, from `in` under the given keys (JSON-RPC: the params themselves;
    // MCP: `_meta` with the `avgen/` keys). An error message when one is malformed.
    static std::optional<std::string> callOptions(const json& in, const char* transactionKey, const char* irreversibleKey,
                                                  const char* dryRunKey, CallOptions& options, bool& dryRun) {
        if (in.contains(transactionKey) && !in.at(transactionKey).is_null()) {
            if (!in.at(transactionKey).is_string()) {
                return fmt::format("{} must be a string", transactionKey);
            }
            options.transaction = in.at(transactionKey).get<std::string>();
        }
        if (in.contains(irreversibleKey) && !in.at(irreversibleKey).is_null()) {
            const json& i = in.at(irreversibleKey);
            if (i.is_boolean()) {
                options.irreversible = i.get<bool>() ? Irreversible::Accept : Irreversible::Refuse;
            } else if (i == "accept" || i == "refuse") {
                options.irreversible = i == "accept" ? Irreversible::Accept : Irreversible::Refuse;
            } else {
                return fmt::format("{} must be a boolean, \"accept\" or \"refuse\"", irreversibleKey);
            }
        }
        dryRun = false;
        if (in.contains(dryRunKey) && !in.at(dryRunKey).is_null()) {
            if (!in.at(dryRunKey).is_boolean()) {
                return fmt::format("{} must be a boolean", dryRunKey);
            }
            dryRun = in.at(dryRunKey).get<bool>();
        }
        return std::nullopt;
    }

    // ---- MCP (ADR-2162): the same session and calls, MCP's method table and shapes ---------------------

    json dispatchMcp(const std::string& method, const json& id, const json& params) {
        if (!mcpKnownClientMethod(method)) {
            return errorResponse(id, kMethodNotFound, fmt::format("no method '{}'", method));
        }
        if (method == "notifications/initialized") {
            clientReady_.store(true, std::memory_order_release);
            return response(id, json::object()); // dropped: it is a notification
        }
        if (method == "notifications/cancelled") {
            return response(id, json::object()); // dropped: no cancellation
        }
        if (method == "ping") {
            return response(id, json::object());
        }
        if (method == "initialize") {
            return mcpInitialize(id, params);
        }
        if (!initialized_.load(std::memory_order_acquire)) {
            return errorResponse(id, kMcpInvalidRequest,
                                 fmt::format("'{}' before initialize: send initialize first", method));
        }
        if (method == "logging/setLevel") {
            const json level = params.is_object() ? params.value("level", json()) : json();
            const auto spd = level.is_string() ? mcpLogLevelToSpd(level.get<std::string>()) : std::nullopt;
            if (!spd) {
                return errorResponse(id, kInvalidParams,
                                     "level must be one of debug, info, notice, warning, error, critical, alert, emergency");
            }
            logLevel_.store(static_cast<spdlog::level::level_enum>(*spd), std::memory_order_release);
            return response(id, json::object());
        }
        if (method == "tools/list") {
            if (params.is_object() && params.contains("cursor") && !params.at("cursor").is_null()) {
                return errorResponse(id, kInvalidParams, "invalid cursor: the list has one page");
            }
            auto tools = listedTools();
            if (!tools) {
                return errorResponse(id, kInternalError, "the host did not answer");
            }
            return response(id, mcpToolsListResult(*tools));
        }
        return mcpToolsCall(id, params);
    }

    json mcpInitialize(const json& id, const json& params) {
        if (initialized_.load(std::memory_order_acquire)) {
            return errorResponse(id, kInvalidRequest, "the session is already initialized");
        }
        if (!params.is_object() || !params.contains("protocolVersion") || !params.at("protocolVersion").is_string()) {
            return errorResponse(id, kInvalidParams, "initialize takes {protocolVersion, capabilities, clientInfo}");
        }
        json clientInfo = json{{"kind", clientKindName(session_.client().kind)}, {"name", ""}, {"version", ""}};
        if (params.contains("clientInfo")) {
            const json& c = params.at("clientInfo");
            if (!c.is_object() || (c.contains("name") && !c.at("name").is_string()) ||
                (c.contains("version") && !c.at("version").is_string())) {
                return errorResponse(id, kInvalidParams, "clientInfo must be {name: string, version: string}");
            }
            clientInfo["name"] = c.value("name", std::string{});
            clientInfo["version"] = c.value("version", std::string{});
        }
        const std::string asked = params.at("protocolVersion").get<std::string>();
        log::info("mcp: initialized by '{}' {} (asked for revision {}, answering {})",
                  clientInfo["name"].get<std::string>(), clientInfo["version"].get<std::string>(), asked,
                  kMcpProtocolRevision);
        json out = mcpInitializeResult(describeServer(clientInfo));
        initialized_.store(true, std::memory_order_release);
        return response(id, std::move(out));
    }

    json mcpToolsCall(const json& id, const json& params) {
        if (!params.is_object() || !params.contains("name") || !params.at("name").is_string()) {
            return errorResponse(id, kInvalidParams, "tools/call takes {name, arguments?, _meta?}");
        }
        const std::string mcpName = params.at("name").get<std::string>();
        const std::optional<std::string> name = names_.toolFor(mcpName);
        if (!name) {
            return errorResponse(id, kMcpInvalidParams, fmt::format("Unknown tool: {}", mcpName),
                                 json{{"name", mcpName}, {"recovery", "call tools/list for the tool names"}});
        }
        json args;
        if (auto bad = callArguments(params, args)) {
            return errorResponse(id, kInvalidParams, *bad);
        }
        const json meta = params.contains("_meta") && params.at("_meta").is_object() ? params.at("_meta") : json::object();
        CallOptions options;
        bool dryRun = false;
        if (auto bad = callOptions(meta, kMcpMetaTransaction, kMcpMetaIrreversible, kMcpMetaDryRun, options, dryRun)) {
            return errorResponse(id, kInvalidParams, *bad);
        }
        if (dryRun) {
            return response(id, mcpCallToolResult(dryRunEnvelope(*name, args, options).toWire()));
        }
        const json token = meta.contains("progressToken") ? meta.at("progressToken") : json();
        const bool tracked = token.is_string() || token.is_number_integer();
        if (tracked) {
            // job.wait reports the waited job's progress; any tool reports its own.
            const std::string job = *name == "job.wait" && args.contains("id") && args.at("id").is_string()
                                        ? args.at("id").get<std::string>()
                                        : std::string{};
            const std::lock_guard lock(progressMutex_);
            progress_ = McpProgress{token, job, -1.0, {}, true};
            options.onProgress = [this](float progress, const std::string& message) {
                mcpProgress(static_cast<double>(progress), message, {});
            };
        }
        const json envelope = session_.call(*name, args, options).toWire();
        if (tracked) {
            const std::lock_guard lock(progressMutex_);
            progress_ = McpProgress{}; // no progress for this token after its response
        }
        return response(id, mcpCallToolResult(envelope));
    }

    // A progress notification to the running call's token: the tool's own (`job` empty) or, for job.wait, the
    // waited job's. MCP's progress must increase: a repeat of the last message at no more progress is not sent, and
    // a new message (a new phase) at no more progress is nudged up, and dropped at the top.
    void mcpProgress(double progress, const std::string& message, const std::string& job) {
        const std::lock_guard lock(progressMutex_);
        if (!progress_.active || (!job.empty() && job != progress_.job)) {
            return;
        }
        progress = std::clamp(progress, 0.0, 1.0);
        if (progress <= progress_.last) {
            if (message == progress_.message) {
                return;
            }
            progress = progress_.last + 1e-4;
            if (progress > 1.0) {
                return;
            }
        }
        progress_.last = progress;
        progress_.message = message;
        send(json{{"jsonrpc", "2.0"},
                  {"method", "notifications/progress"},
                  {"params", mcpProgressParams(progress_.token, progress, message)}});
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

    struct McpProgress {
        json token;
        std::string job; // job.wait's job, or empty
        double last = -1.0;
        std::string message; // the last one sent
        bool active = false;
    };

    Session& session_;
    EngineHost& host_;
    int in_;
    int out_;
    const ServeProtocol protocol_;
    const json catalogue_;
    const McpNameTable names_;
    std::mutex progressMutex_; // taken before writeMutex_, never after
    McpProgress progress_;     // guarded by progressMutex_
    std::atomic<bool> clientReady_{false}; // MCP: notifications/initialized arrived
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
           "settings file and creates no window.\n"
           "- **The GPU is lazy.** No device exists until the first render job starts; a session that never renders "
           "never creates one. `initialize` answers `capabilities.gpu` (a device can be made here) and "
           "`capabilities.render`.\n"
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
    out += "\n## Render jobs\n\n";
    out += "`render.capture` and `render.start` return `{job}` at once; the work runs on the host between calls, so "
           "the session keeps answering. A job's life:\n\n"
           "```text\n"
           "queued -> preparing -> running -> succeeded\n"
           "                                | failed     (error; partial outputs listed)\n"
           "                                | cancelled  (job.cancel; partial outputs listed)\n"
           "```\n\n"
           "- **queued**: FIFO, one GPU job at a time; four queued or running at most (a fifth is `BUSY`).\n"
           "- **preparing**: a fresh offline engine is loaded from the job's snapshot -- the session's in-memory "
           "project, written with `Engine::writeProjectCopy` into the session's scratch directory when the call was "
           "made, so a render right after `parameter.set` shows it and nothing is saved. Then the machine's GPU lock "
           "(below), the device on the first job, and the pipeline compile (72 s measured on a cold Metal cache; "
           "`phase` counts the pipelines).\n"
           "- **running**: `history: play` renders and discards the frames before the range first; then the range, "
           "each frame hashed as it is read back.\n"
           "- **job.wait** blocks only the caller (the server's reader waits on the job table, not the host), so "
           "requests after it wait their turn but the job keeps running. `job.status`, `job.list` and `job.cancel` "
           "never block.\n"
           "- **Results**: `frameHashes` (FNV-1a over each frame's read-back pixels, 16 hex digits), `sequenceHash`, "
           "`frameHash` (the last), `outputs` `[{path, frame, bytes, sha256}]`, `timing` (queued, load, lock wait, "
           "start -- device and compile -- render, total; `deviceCreated` for the cold job; `pipelinesCreated`), "
           "`lock` `{path, waited, waitedSeconds, heldByAncestor, holderSeen}`, `warnings` and `gpuErrors`.\n\n";
    out += "## The machine's GPU lock\n\n";
    out += "GPU work on one machine is serialised by a lock directory that `tools/gpu-lock.sh` and the server share. "
           "The server takes it for each job, after loading the snapshot and before touching the device, and "
           "releases it when the job ends; an idle server holds nothing. The protocol, which a client wrapping its "
           "own GPU work in the script already follows:\n\n"
           "1. The lock is the directory `${TMPDIR:-/tmp}/avgen-gpu.lock`, created with `mkdir` (atomic). The path "
           "depends only on the environment, never on a source tree.\n"
           "2. The winner writes its pid to `pid.<pid>` and renames it to `pid` (so it is never read half-written), "
           "and its name to `holder` (`AVGEN_AGENT`, else `avgen-<pid>`).\n"
           "3. A waiter that reads a `pid` naming a dead process removes the directory -- only if `pid` still names "
           "that process -- and tries again. A directory with no `pid` is removed once it is older than 30 s.\n"
           "4. A holder removes the directory only while `pid` is still its own.\n"
           "5. **Held by an ancestor.** When `pid` is the server's own process or one of its ancestors -- the server "
           "was started under `gpu-lock.sh`, which holds the lock in the wrapper shell and exports nothing -- the "
           "lock counts as held for the server: it neither waits nor creates nor removes anything.\n"
           "6. A job waits for the lock as long as `AVGEN_GPU_LOCK_TIMEOUT` seconds (default 3600, the script's), "
           "reporting `phase: \"waiting for the GPU lock (held by '<holder>' (pid <n>))\"`, then fails `BUSY`. "
           "`job.cancel` stops the wait.\n\n"
           "One difference from the script, in the safe direction: a pid owned by another user (`kill -0` answers "
           "EPERM) is alive here; the script would reclaim it.\n";
    out += "\n## Example session\n\n";
    out += "`>` is what the client writes, `<` what the server answers (long values elided with `...`). Started as "
           "`avgen serve --stdio --root /work`, with `resonator.glb` in `/work`; the render's numbers are the S6 measurement's. A relative path is relative to the open "
           "project's folder, so the import names `../resonator.glb` from inside `/work/poc`.\n\n";
    out += "```text\n";
    out += kTranscript;
    out += "\n```\n";
    return out;
}

const char* serveUsage() {
    return "usage: avgen serve --stdio [--root <dir>]... [--read-root <dir>]... [--write-root <dir>]... "
           "[--allow-subprocess]\n"
           "  JSON-RPC 2.0, one message per line, on stdin and stdout (docs/api/protocol.md). No window, no\n"
           "  settings file, no OSC or MIDI listener; a GPU device only once a render job starts, under the\n"
           "  machine's GPU lock (tools/gpu-lock.sh's). The log is on stderr.\n"
           "  --root <dir>         a folder the session may read and write (repeatable)\n"
           "  --read-root <dir>    a folder it may read (repeatable)\n"
           "  --write-root <dir>   a folder it may write, and so read (repeatable)\n"
           "  --allow-subprocess   tools may start child processes (performance.profile_scene and its family)\n";
}

int serve(ServeOptions options, int inFd, int outFd) {
    EngineHostOptions hostOptions;
    hostOptions.renderBackend = std::move(options.renderBackend);
    EngineHost host(std::move(hostOptions));
    SessionOptions so;
    so.client = options.protocol == ServeProtocol::Mcp ? ClientInfo{ClientKind::Mcp, "mcp", ""}
                                                       : ClientInfo{ClientKind::JsonRpc, "json-rpc", ""};
    so.permissions = std::move(options.flags.permissions);
    so.permissions.liveControl = false;
    so.extraTools = std::move(options.extraTools);
    auto session = Session::create(std::move(so), host);
    if (!session) {
        log::error("serve: {}", session.error().message);
        return 7;
    }
    Server server(**session, host, inFd, outFd, options.protocol);
    server.start();
    host.run(**session);
    server.join();
    (*session)->close(); // host thread; a no-op when shutdown or end of input already closed it
    return 0;
}

const char* mcpUsage() {
    return "usage: avgen mcp [--stdio] [--root <dir>]... [--read-root <dir>]... [--write-root <dir>]... "
           "[--allow-subprocess]\n"
           "  The Model Context Protocol on stdin and stdout (docs/api/mcp.md): avgen serve's server and flags, with\n"
           "  MCP's methods and result shapes. Client configuration: {\"mcpServers\": {\"avgen\": {\"command\":\n"
           "  \"/path/to/avgen\", \"args\": [\"mcp\", \"--root\", \"/path/to/work\"]}}}. The log is on stderr.\n";
}

int runServeCommand(int argc, char** argv, RenderBackendFactory renderBackend, ServeProtocol wire) {
    const bool mcp = wire == ServeProtocol::Mcp;
    const char* command = mcp ? "avgen mcp" : "avgen serve";
    const char* usage = mcp ? mcpUsage() : serveUsage();
    bool stdio = mcp; // stdio is MCP's only transport here; `--stdio` is accepted and changes nothing
    HostFlags flags;
    for (int i = 2; i < argc; ++i) {
        const std::string_view a = argv[i];
        if (a == "--stdio") {
            stdio = true;
            continue;
        }
        if (a == "--help" || a == "-h") {
            std::fputs(usage, stdout);
            return 0;
        }
        auto consumed = parseHostFlag(argc, argv, i, flags);
        if (!consumed) {
            std::fprintf(stderr, "%s: %s\n%s", command, consumed.error().message.c_str(), usage);
            return 2;
        }
        if (!*consumed) {
            std::fprintf(stderr, "%s: unknown argument '%s'\n%s", command, argv[i], usage);
            return 2;
        }
    }
    if (!stdio) {
        std::fprintf(stderr, "%s: pass --stdio (the only transport)\n%s", command, usage);
        return 2;
    }
    auto protocol = protectStdout();
    if (!protocol) {
        log::error("serve: {}", protocol.error().message);
        return 7;
    }
    std::signal(SIGPIPE, SIG_IGN); // a client that closes our stdout ends the session through a write error
    log::info("{} serving {} on stdio ({}; no window, no listeners)", versionLine(),
              mcp ? fmt::format("MCP {}", kMcpProtocolRevision) : std::string("JSON-RPC"),
              renderBackend ? "GPU on the first render" : "CPU host: no GPU");
    ServeOptions options;
    options.flags = std::move(flags);
    options.protocol = wire;
    options.renderBackend = std::move(renderBackend);
    const int code = serve(std::move(options), STDIN_FILENO, *protocol);
    restoreStdout(*protocol);
    return code;
}

} // namespace avgen::api
