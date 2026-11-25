// ADR-2155 (Engine API stage S2): `avgen serve --stdio` and `avgen call`, the CPU-only external interface.
//
// The server is driven in-process over a pipe pair: `api::serve` runs on a thread of its own (which becomes the
// host thread), and the test is the client, writing request lines and reading reply lines. `avgen call` is
// driven through `api::runCallCommand` with fd 1 pointed at a file, which is what a shell redirect does.

#include "ai/main_thread_queue.hpp"
#include "ai/tool_api.hpp"
#include "ai/tool_context.hpp"
#include "api/describe.hpp"
#include "api/engine_host.hpp"
#include "api/jsonrpc_server.hpp"
#include "api/session.hpp"
#include "api/version.hpp"
#include "app/engine.hpp"
#include "core/log.hpp"
#include "support/gltf_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <libproc.h>
#endif

using namespace avgen;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

fs::path freshDir(const std::string& name) {
    const fs::path dir = fs::temp_directory_path() / ("avgen_serve_" + name + "_" + std::to_string(getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    return fs::weakly_canonical(dir);
}

ai::Tool testTool(std::string name, ai::ToolAnnotations a, ai::ToolFn fn) {
    ai::Tool t;
    t.definition.name = std::move(name);
    t.definition.title = "test";
    t.definition.description = "a test-only tool";
    t.definition.inputSchema = ai::schema::object(json::object());
    t.definition.annotations = std::move(a);
    t.execute = std::move(fn);
    return t;
}

ai::ToolAnnotations reading() {
    ai::ToolAnnotations a;
    a.readOnly = true;
    return a;
}

// A JSON-RPC client over two pipes, with the server on its own thread.
class Client {
public:
    explicit Client(api::ServeOptions options, std::optional<int> outFdOverride = std::nullopt) {
        int in[2];
        int out[2];
        REQUIRE(::pipe(in) == 0);
        REQUIRE(::pipe(out) == 0);
        toServer_ = in[1];
        fromServer_ = out[0];
        const int serverIn = in[0];
        const int serverOut = outFdOverride.value_or(out[1]);
        serverOutOwned_ = outFdOverride ? -1 : out[1];
        if (outFdOverride) {
            ::close(out[1]);
            ::close(out[0]);
            fromServer_ = -1;
        }
        server_ = std::thread([this, options = std::move(options), serverIn, serverOut]() mutable {
            exitCode_ = api::serve(std::move(options), serverIn, serverOut);
            ::close(serverIn);
            if (serverOutOwned_ >= 0) {
                ::close(serverOutOwned_);
            }
        });
    }
    ~Client() {
        closeInput();
        join();
        if (fromServer_ >= 0) {
            ::close(fromServer_);
        }
    }

    void sendLine(const std::string& line) {
        const std::string l = line + "\n";
        REQUIRE(::write(toServer_, l.data(), l.size()) == static_cast<ssize_t>(l.size()));
    }
    void send(const json& message) { sendLine(message.dump()); }
    json request(int id, const std::string& method, json params = nullptr) {
        json m{{"jsonrpc", "2.0"}, {"id", id}, {"method", method}};
        if (!params.is_null()) {
            m["params"] = std::move(params);
        }
        send(m);
        return next();
    }
    // The next reply that is not a server notification.
    json next() {
        while (true) {
            json m = readMessage();
            if (m.is_object() && m.contains("method") && !m.contains("id")) {
                notifications.push_back(m);
                continue;
            }
            return m;
        }
    }
    json readMessage(std::chrono::milliseconds timeout = std::chrono::seconds{20}) {
        REQUIRE(fromServer_ >= 0);
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (true) {
            if (const auto nl = buffer_.find('\n'); nl != std::string::npos) {
                const std::string line = buffer_.substr(0, nl);
                buffer_.erase(0, nl + 1);
                const json m = json::parse(line, nullptr, false);
                INFO(line);
                REQUIRE_FALSE(m.is_discarded());
                return m;
            }
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
            REQUIRE(left.count() > 0);
            pollfd p{fromServer_, POLLIN, 0};
            if (::poll(&p, 1, static_cast<int>(left.count())) <= 0) {
                continue;
            }
            char chunk[65536];
            const ssize_t n = ::read(fromServer_, chunk, sizeof chunk);
            REQUIRE(n > 0);
            buffer_.append(chunk, static_cast<std::size_t>(n));
        }
    }
    void closeInput() {
        if (toServer_ >= 0) {
            ::close(toServer_);
            toServer_ = -1;
        }
    }
    int join() {
        if (server_.joinable()) {
            server_.join();
        }
        return exitCode_;
    }
    // Everything the server wrote after the last reply read, until it closed its end.
    std::string drain() {
        std::string all = buffer_;
        buffer_.clear();
        char chunk[65536];
        ssize_t n;
        while ((n = ::read(fromServer_, chunk, sizeof chunk)) > 0) {
            all.append(chunk, static_cast<std::size_t>(n));
        }
        return all;
    }

    std::vector<json> notifications;

private:
    int toServer_ = -1;
    int fromServer_ = -1;
    int serverOutOwned_ = -1;
    std::string buffer_;
    std::thread server_;
    int exitCode_ = -1;
};

json initialize(Client& c, int id = 1) {
    const json r = c.request(id, "initialize", json{{"clientInfo", {{"name", "test-client"}, {"version", "1"}}}});
    REQUIRE(r.contains("result"));
    return r.at("result");
}

int errorCode(const json& reply) {
    INFO(reply.dump());
    REQUIRE(reply.contains("error"));
    return reply.at("error").at("code").get<int>();
}

const json& envelope(const json& reply) {
    INFO(reply.dump());
    REQUIRE(reply.contains("result"));
    REQUIRE_FALSE(reply.contains("error"));
    return reply.at("result");
}

std::string toolCode(const json& env) {
    return env.contains("error") ? env.at("error").at("code").get<std::string>() : std::string("none");
}

api::ServeOptions rootedAt(const fs::path& root) {
    api::ServeOptions o;
    o.flags.permissions.readRoots = {root};
    o.flags.permissions.writeRoots = {root};
    return o;
}

#if defined(__APPLE__)
int socketCount() {
    const int bytes = proc_pidinfo(getpid(), PROC_PIDLISTFDS, 0, nullptr, 0);
    std::vector<proc_fdinfo> fds(static_cast<std::size_t>(bytes) / sizeof(proc_fdinfo) + 16);
    const int got = proc_pidinfo(getpid(), PROC_PIDLISTFDS, 0, fds.data(), static_cast<int>(fds.size() * sizeof(proc_fdinfo)));
    int sockets = 0;
    for (int i = 0; i < got / static_cast<int>(sizeof(proc_fdinfo)); ++i) {
        if (fds[static_cast<std::size_t>(i)].proc_fdtype == PROX_FDTYPE_SOCKET) {
            ++sockets;
        }
    }
    return sockets;
}
#endif

// Runs `avgen call ...` in-process with fd 1 sent to a file, as `avgen call ... > out.json` does. Returns the exit
// code and what was printed.
std::pair<int, std::string> runCall(std::vector<std::string> args, const fs::path& capture) {
    args.insert(args.begin(), {"avgen", "call"});
    std::vector<char*> argv;
    for (std::string& a : args) {
        argv.push_back(a.data());
    }
    std::fflush(stdout);
    const int saved = ::dup(STDOUT_FILENO);
    const int file = ::open(capture.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    REQUIRE(file >= 0);
    ::dup2(file, STDOUT_FILENO);
    ::close(file);
    const int code = api::runCallCommand(static_cast<int>(argv.size()), argv.data());
    std::fflush(stdout);
    ::dup2(saved, STDOUT_FILENO);
    ::close(saved);
    std::ifstream in(capture);
    return {code, std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>())};
}

void writeText(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << text;
}

} // namespace

TEST_CASE("serve: initialize, tools/list and tools/call over a pipe, with the protocol's error mapping",
          "[api][serve]") {
    const fs::path root = freshDir("protocol");
    api::ServeOptions o = rootedAt(root);
    ai::ToolAnnotations gpu = reading();
    gpu.permissions = {"gpu"};
    o.extraTools.push_back(testTool("test.gpu_probe", gpu, [](const json&, ai::ToolContext&) {
        return ai::ToolResult::ok(json::object(), "ran"); // must never run on the CPU host
    }));
    Client c(std::move(o));

    // Before initialize: -32002, and nothing runs.
    CHECK(errorCode(c.request(1, "tools/list")) == api::kNotInitialized);
    CHECK(errorCode(c.request(2, "tools/call", json{{"name", "capability.list"}})) == api::kNotInitialized);
    // Protocol faults, answered with JSON-RPC errors.
    c.sendLine("this is not json");
    const json parse = c.next();
    CHECK(errorCode(parse) == api::kParseError);
    CHECK(parse.at("id").is_null());
    c.send(json{{"jsonrpc", "1.0"}, {"id", 3}, {"method", "initialize"}});
    CHECK(errorCode(c.next()) == api::kInvalidRequest);
    c.send(json{{"jsonrpc", "2.0"}, {"id", 4}});
    CHECK(errorCode(c.next()) == api::kInvalidRequest);
    c.send(json{{"jsonrpc", "2.0"}, {"id", 5}, {"method", "initialize"}, {"params", 7}});
    CHECK(errorCode(c.next()) == api::kInvalidRequest);
    c.sendLine("[]");
    CHECK(errorCode(c.next()) == api::kInvalidRequest);

    const json init = initialize(c, 10);
    CHECK(init.at("api") == api::kApiLevel);
    CHECK(init.at("versions") == api::version().toJson());
    CHECK(init.at("protocol").at("version") == api::kProtocolVersion);
    CHECK(init.at("permissions").at("liveControl") == false);
    CHECK(init.at("permissions").at("writeRoots") == json::array({root.string()}));
    CHECK(init.at("capabilities").at("gpu") == false);
    CHECK(init.at("session").at("client").at("name") == "test-client");
    CHECK(errorCode(c.request(11, "initialize")) == api::kInvalidRequest); // once only
    CHECK(errorCode(c.request(12, "initialize", json{{"logLevel", "loud"}})) == api::kInvalidRequest);

    // tools/list: the catalogue, with this session's availability on each.
    const json list = c.request(13, "tools/list");
    const json& tools = list.at("result").at("tools");
    const std::size_t catalogueSize = api::makeCatalogueRegistry().size();
    CHECK(tools.size() == catalogueSize + 1);
    bool sawGpu = false;
    for (const json& t : tools) {
        INFO(t.at("name"));
        REQUIRE(t.contains("availability"));
        CHECK(t.contains("inputSchema"));
        if (t.at("name") == "test.gpu_probe") {
            sawGpu = true;
            CHECK(t.at("availability").at("available") == false);
            CHECK(t.at("availability").at("unavailableReason") == "no GPU host in this session yet");
        }
    }
    CHECK(sawGpu);

    // An unknown METHOD is -32601; an unknown TOOL is a tool result, NOT_FOUND.
    CHECK(errorCode(c.request(14, "tools/delete")) == api::kMethodNotFound);
    const json unknown = envelope(c.request(15, "tools/call", json{{"name", "no.such_tool"}}));
    CHECK(unknown.at("success") == false);
    CHECK(toolCode(unknown) == "NOT_FOUND");
    // Malformed params are -32602; malformed ARGUMENTS are a tool result.
    CHECK(errorCode(c.request(16, "tools/call", json::object())) == api::kInvalidParams);
    CHECK(errorCode(c.request(17, "tools/call", json{{"name", "capability.list"}, {"arguments", json::array()}})) ==
          api::kInvalidParams);
    CHECK(errorCode(c.request(18, "tools/call", json{{"name", "capability.list"}, {"dryRun", "yes"}})) ==
          api::kInvalidParams);
    CHECK(errorCode(c.request(19, "tools/call", json{{"name", "capability.list"}, {"transaction", 3}})) ==
          api::kInvalidParams);
    const json badArgs = envelope(c.request(20, "tools/call", json{{"name", "parameter.set"}, {"arguments", {{"path", 3}}}}));
    CHECK(toolCode(badArgs) == "INVALID_ARGUMENTS");

    // A tool that runs: the full envelope, every key present.
    const json ok = envelope(c.request(21, "tools/call", json{{"name", "capability.list"}}));
    CHECK(ok.at("success") == true);
    for (const char* key : {"success", "result", "summary", "changes", "warnings", "transaction", "job"}) {
        CHECK(ok.contains(key));
    }
    // A GPU tool on the CPU host: UNAVAILABLE, and its body never ran.
    const json gpuCall = envelope(c.request(22, "tools/call", json{{"name", "test.gpu_probe"}}));
    CHECK(toolCode(gpuCall) == "UNAVAILABLE");

    // dryRun: checked, not run.
    const json dry = envelope(c.request(23, "tools/call", json{{"name", "project.create"}, {"arguments", {{"path", "dry"}}}, {"dryRun", true}}));
    CHECK(dry.at("success") == true);
    CHECK(dry.at("result").at("dryRun") == true);
    CHECK_FALSE(fs::exists(root / "dry"));
    const json dryBad = envelope(c.request(24, "tools/call", json{{"name", "parameter.set"}, {"arguments", {{"path", 3}}}, {"dryRun", true}}));
    CHECK(toolCode(dryBad) == "INVALID_ARGUMENTS");

    // A notification gets no reply: the next reply is the next request's.
    c.send(json{{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}});
    c.send(json{{"jsonrpc", "2.0"}, {"method", "tools/call"}, {"params", {{"name", "capability.list"}}}});
    CHECK(c.request(25, "tools/call", json{{"name", "capability.list"}}).at("id") == 25);
    // A batch: one array of replies, the notification in it unanswered.
    c.sendLine(R"([{"jsonrpc":"2.0","id":26,"method":"tools/call","params":{"name":"capability.list"}},)"
               R"({"jsonrpc":"2.0","method":"notifications/initialized"},)"
               R"({"jsonrpc":"2.0","id":27,"method":"nope"}])");
    const json batch = c.next();
    REQUIRE(batch.is_array());
    REQUIRE(batch.size() == 2);
    CHECK(batch[0].at("id") == 26);
    CHECK(errorCode(batch[1]) == api::kMethodNotFound);

    const json bye = c.request(28, "shutdown");
    CHECK(bye.at("result").at("closed") == true);
    CHECK(bye.at("result").at("rolledBack").is_null());
    CHECK(c.join() == 0); // the server ends on shutdown without waiting for end of input
}

TEST_CASE("serve: with a root, a project is created and an asset imported; end of input rolls back the open "
          "transaction",
          "[api][serve]") {
    const fs::path root = freshDir("eof");
    const fs::path glb = testsupport::writeTriangleGlb("serve_eof.glb");
    fs::copy_file(glb, root / "tri.glb", fs::copy_options::overwrite_existing);
    Client c(rootedAt(root));
    initialize(c);
    const json created = envelope(c.request(2, "tools/call", json{{"name", "project.create"}, {"arguments", {{"path", "demo"}}}}));
    REQUIRE(created.at("success") == true);
    CHECK(fs::exists(root / "demo" / "demo.json"));

    const json begun = envelope(c.request(3, "tools/call", json{{"name", "transaction.begin"}, {"arguments", {{"label", "import"}}}}));
    REQUIRE(begun.at("success") == true);
    const std::string txn = begun.at("result").at("id").get<std::string>();
    const json imported = envelope(c.request(4, "tools/call", json{{"name", "asset.import"},
                                                                    {"arguments", {{"file", (root / "tri.glb").string()}}},
                                                                    {"transaction", txn},
                                                                    {"irreversible", true}}));
    INFO(imported.dump());
    REQUIRE(imported.at("success") == true);
    const fs::path copy = imported.at("result").at("copied").at(0).get<std::string>();
    CHECK(fs::exists(copy));

    // End of input, with the transaction still open: the session closes and rolls it back, deleting the copy.
    c.closeInput();
    CHECK(c.join() == 0);
    CHECK_FALSE(fs::exists(copy));
    CHECK(fs::exists(root / "demo" / "demo.json")); // made outside the transaction: kept
    CHECK(fs::exists(root / "tri.glb"));             // the source is never touched
}

TEST_CASE("serve: progress and the log reach the client as notifications", "[api][serve]") {
    api::ServeOptions o;
    o.extraTools.push_back(testTool("test.progress", reading(), [](const json&, ai::ToolContext& ctx) {
        if (ctx.onProgress) {
            ctx.onProgress(0.5f, "half way");
        }
        log::warn("test.progress: a warning for the client");
        log::info("test.progress: an info line below the client's level");
        return ai::ToolResult::ok(json::object(), "done");
    }));
    // The process-wide level is whatever the last test left; this one needs warnings to reach the sinks.
    const auto level = spdlog::get_level();
    spdlog::set_level(spdlog::level::info);
    Client c(std::move(o));
    REQUIRE(c.request(1, "initialize", json{{"logLevel", "warn"}}).contains("result"));
    REQUIRE(envelope(c.request(2, "tools/call", json{{"name", "test.progress"}})).at("success") == true);
    spdlog::set_level(level);
    bool progress = false;
    bool logged = false;
    for (const json& n : c.notifications) {
        if (n.at("method") == "notifications/progress") {
            progress = true;
            CHECK(n.at("params").at("requestId") == 2);
            CHECK(n.at("params").at("tool") == "test.progress");
            CHECK(n.at("params").at("message") == "half way");
        }
        if (n.at("method") == "notifications/log") {
            const std::string message = n.at("params").at("message").get<std::string>();
            logged = logged || message == "test.progress: a warning for the client";
            CHECK(message.find("below the client's level") == std::string::npos);
            CHECK(n.at("params").at("level") != "info");
        }
    }
    CHECK(progress);
    CHECK(logged);
    c.request(4, "shutdown");
}

TEST_CASE("The host's idle wait: released by queued work, by wake and by shutdown, and never early", "[api][serve]") {
    ai::MainThreadQueue q;
    q.bindToCurrentThread();
    // Work queued from another thread releases the wait, and the waiter is told there is work.
    std::atomic<bool> ran{false};
    std::thread poster([&] { CHECK(q.run([&] { ran = true; }, {}, std::chrono::seconds{10})); });
    CHECK(q.waitForWork());
    q.pump();
    poster.join();
    CHECK(ran);
    // wake() releases it with nothing to do; a wake before the wait is not lost.
    std::thread waker([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
        q.wake();
    });
    const auto start = std::chrono::steady_clock::now();
    CHECK_FALSE(q.waitForWork());
    CHECK(std::chrono::steady_clock::now() - start >= std::chrono::milliseconds{40}); // it slept, it did not spin
    waker.join();
    q.wake();
    CHECK_FALSE(q.waitForWork());
    q.shutdown();
    CHECK_FALSE(q.waitForWork());
}

TEST_CASE("serve: shutdown rolls back the open transaction and says which", "[api][serve]") {
    const fs::path root = freshDir("shutdown");
    Client c(rootedAt(root));
    initialize(c);
    const json begun = envelope(c.request(2, "tools/call", json{{"name", "transaction.begin"}}));
    const std::string txn = begun.at("result").at("id").get<std::string>();
    const json bye = c.request(3, "shutdown");
    CHECK(bye.at("result").at("rolledBack") == txn);
    CHECK(c.join() == 0);
}

TEST_CASE("serve: a printf to stdout inside a tool body does not reach the protocol stream", "[api][serve]") {
    // fd 1 is a pipe the test reads, standing in for the client end of `avgen serve`'s stdout. protectStdout
    // moves the protocol to a duplicate and points fd 1 at stderr, as runServeCommand does.
    int pipeFds[2];
    REQUIRE(::pipe(pipeFds) == 0);
    std::fflush(stdout);
    const int saved = ::dup(STDOUT_FILENO);
    ::dup2(pipeFds[1], STDOUT_FILENO);
    ::close(pipeFds[1]);
    auto protocol = api::protectStdout();
    REQUIRE(protocol.has_value());

    api::ServeOptions o;
    o.extraTools.push_back(testTool("test.stray_printf", reading(), [](const json&, ai::ToolContext&) {
        std::printf("STRAY printf from a tool body\n");
        std::fflush(stdout);
        (void)::write(STDOUT_FILENO, "STRAY write(1)\n", 15);
        std::fputs("STRAY fputs\n", stdout); // left buffered on purpose
        return ai::ToolResult::ok(json{{"ran", true}}, "printed");
    }));
    std::string written;
    {
        Client c(std::move(o), *protocol); // the server writes to the protocol descriptor
        // The server writes to the protocol descriptor, so the client reads the pipe behind fd 1's original target
        // directly (Client's own reader is off when the output is overridden).
        const auto readLine = [&]() {
            std::string line;
            char ch;
            while (::read(pipeFds[0], &ch, 1) == 1 && ch != '\n') {
                line += ch;
            }
            return line;
        };
        c.send(json{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"}});
        written += readLine() + "\n";
        c.send(json{{"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/call"}, {"params", {{"name", "test.stray_printf"}}}});
        written += readLine() + "\n";
        c.send(json{{"jsonrpc", "2.0"}, {"id", 3}, {"method", "shutdown"}});
        written += readLine() + "\n";
        CHECK(c.join() == 0);
    }
    std::fflush(stdout);
    ::dup2(saved, STDOUT_FILENO); // the test's own stdout back; the pipe now has no writer but the protocol fd
    ::close(saved);
    ::close(*protocol);
    char chunk[4096];
    ssize_t n;
    while ((n = ::read(pipeFds[0], chunk, sizeof chunk)) > 0) {
        written.append(chunk, static_cast<std::size_t>(n));
    }
    ::close(pipeFds[0]);

    INFO(written);
    CHECK(written.find("STRAY") == std::string::npos);
    std::size_t lines = 0;
    std::size_t start = 0;
    for (std::size_t nl; (nl = written.find('\n', start)) != std::string::npos; start = nl + 1) {
        const std::string line = written.substr(start, nl - start);
        if (line.empty()) {
            continue;
        }
        const json m = json::parse(line, nullptr, false);
        CHECK_FALSE(m.is_discarded());
        CHECK(m.value("jsonrpc", std::string{}) == "2.0");
        ++lines;
    }
    CHECK(lines == 3);
    CHECK(written.find("\"ran\":true") != std::string::npos);
}

#if defined(__APPLE__)
TEST_CASE("serve: the server and its engine open no socket", "[api][serve]") {
    const fs::path root = freshDir("sockets");
    const int before = socketCount();
    Client c(rootedAt(root));
    initialize(c);
    REQUIRE(envelope(c.request(2, "tools/call", json{{"name", "project.create"}, {"arguments", {{"path", "s"}}}})).at("success") == true);
    REQUIRE(envelope(c.request(3, "tools/call", json{{"name", "capability.list"}})).at("success") == true);
    // While it serves, with a project open: no OSC port, no MIDI client socket, no listener of any kind.
    CHECK(socketCount() == before);
    c.request(4, "shutdown");
    c.join();
}
#endif

TEST_CASE("serve: every tool called with {} on a fresh session with no project answers a structured result",
          "[api][serve][fuzz]") {
    const ai::ToolRegistry catalogue = api::makeCatalogueRegistry();
    const std::set<std::string> codes = [] {
        std::set<std::string> s;
        for (const ai::ToolErrorCode c : ai::kAllToolErrorCodes) {
            s.insert(ai::toolErrorCodeName(c));
        }
        return s;
    }();
    for (const std::string& name : catalogue.names()) {
        INFO(name);
        api::EngineHost host;
        host.bindHostThread();
        api::SessionOptions so;
        so.client = api::ClientInfo{api::ClientKind::JsonRpc, "fuzz", "1"};
        auto session = api::Session::create(std::move(so), host);
        REQUIRE(session.has_value());
        const api::CallResult r = (*session)->call(name, json::object());
        const json wire = r.toWire();
        INFO(wire.dump());
        CHECK(wire.at("success").is_boolean());
        if (!r.success) {
            REQUIRE(wire.contains("error"));
            CHECK(codes.contains(wire.at("error").at("code").get<std::string>()));
            CHECK_FALSE(wire.at("error").at("message").get<std::string>().empty());
            CHECK(wire.at("error").at("code") != "INTERNAL"); // a throw caught is still a tool that assumed too much
        }
        (*session)->close();
    }
}

TEST_CASE("avgen call: one tool, its envelope on stdout, and the exit codes", "[api][serve][call]") {
    const fs::path dir = freshDir("call_single");
    const fs::path capture = dir / "out.json";

    auto [ok, okText] = runCall({"capability.list"}, capture);
    CHECK(ok == 0);
    const json env = json::parse(okText, nullptr, false);
    REQUIRE_FALSE(env.is_discarded());
    CHECK(env.at("success") == true);

    auto [missing, missingText] = runCall({"no.such_tool", "{}"}, capture);
    CHECK(missing == 1); // a tool failure: the envelope is still printed
    const json missingEnv = json::parse(missingText, nullptr, false);
    REQUIRE_FALSE(missingEnv.is_discarded());
    CHECK(toolCode(missingEnv) == "NOT_FOUND");

    CHECK(runCall({}, capture).first == 2);
    CHECK(runCall({"capability.list", "[1]"}, capture).first == 2);
    CHECK(runCall({"capability.list", "--root", (dir / "absent").string()}, capture).first == 2);
    CHECK(runCall({"capability.list", "--frobnicate"}, capture).first == 2);
    CHECK(runCall({"--calls", (dir / "absent.json").string()}, capture).first == 2);
}

TEST_CASE("avgen call --calls: the evaluation's probe sequence, refused without roots and run with them",
          "[api][serve][call]") {
    // 01-evaluation/evidence/poc-today/probe.ai.json's calls, in `--calls` form. The model is this suite's own GLB
    // at the probe's path, under a read root; the probe's relative "assets/column.glb" would resolve against the
    // project folder project.create just made, so it is named absolutely here (S3 adds asset:// ids).
    const fs::path root = freshDir("probe");
    const fs::path content = root / "content";
    fs::create_directories(content / "assets");
    fs::copy_file(testsupport::writeTriangleGlb("serve_probe.glb"), content / "assets" / "column.glb",
                  fs::copy_options::overwrite_existing);
    const std::string model = (content / "assets" / "column.glb").string();
    const json calls = json::array({
        json{{"tool", "capability.list"}},
        json{{"tool", "project.create"}, {"arguments", {{"path", "poc-from-api"}, {"scene", "empty"}}}},
        json{{"tool", "asset.import"}, {"arguments", {{"file", model}}}},
        json{{"tool", "scene.create_node"}, {"arguments", {{"name", "column2"}, {"asset", model}, {"position", {3.0, 0.0, 0.0}}}}},
        json{{"tool", "camera.set"}, {"arguments", {{"position", {4.0, 3.0, 8.0}}, {"target", {0.0, 2.0, 0.0}}, {"fovDegrees", 45.0}}}},
        json{{"tool", "modulation.create"},
             {"arguments", {{"source", "audio.treble"}, {"target", "nodes/column/scale"}, {"amount", 0.3}, {"op", "add"}}}},
        json{{"tool", "lighting.list"}},
    });
    writeText(root / "probe.calls.json", calls.dump(2));
    const fs::path capture = root / "out.json";

    const auto envelopes = [](const std::string& text) {
        const json doc = json::parse(text, nullptr, false);
        REQUIRE_FALSE(doc.is_discarded());
        std::map<std::string, json> byTool;
        for (const json& c : doc.at("calls")) {
            byTool[c.at("tool").get<std::string>()] = c.at("envelope");
        }
        return byTool;
    };

    // No roots: what the evaluation saw -- the project and the import refused.
    auto [refusedCode, refusedText] = runCall({"--calls", (root / "probe.calls.json").string()}, capture);
    CHECK(refusedCode == 1);
    auto refused = envelopes(refusedText);
    CHECK(toolCode(refused.at("project.create")) == "PERMISSION_DENIED");
    CHECK(toolCode(refused.at("asset.import")) == "PERMISSION_DENIED");
    CHECK(refused.at("capability.list").at("success") == true);

    // With roots: the project is created and the model imported.
    auto [code, text] = runCall({"--calls", (root / "probe.calls.json").string(), "--write-root", (root / "projects").string(),
                                 "--read-root", content.string()},
                                capture);
    // --write-root names a folder that must exist.
    CHECK(code == 2);
    fs::create_directories(root / "projects");
    std::tie(code, text) = runCall({"--calls", (root / "probe.calls.json").string(), "--write-root",
                                    (root / "projects").string(), "--read-root", content.string()},
                                   capture);
    auto ran = envelopes(text);
    INFO(text);
    CHECK(ran.at("project.create").at("success") == true);
    CHECK(fs::exists(root / "projects" / "poc-from-api" / "poc-from-api.json"));
    CHECK(ran.at("asset.import").at("success") == true);
    CHECK(fs::exists(root / "projects" / "poc-from-api" / "assets" / "models" / "column.glb"));
    CHECK(ran.at("lighting.list").at("success") == true);
    // With S3 (ADR-2156) a created project has a scene: the node is made from the model, and the camera is set.
    CHECK(ran.at("scene.create_node").at("success") == true);
    CHECK(ran.at("camera.set").at("success") == true);
    CHECK(code == 1); // the probe's route targets nodes/column, a node that is not there

    // A project with a scene, opened with --project: the node is created from the model under the read root.
    const fs::path project = root / "projects" / "column" / "column.json";
    writeText(project, R"({"format": "avgen-project", "version": 4,
                          "assets": {"scene": {"kind": "composition", "path": "column.scene.json"}}})");
    writeText(project.parent_path() / "column.scene.json",
              R"({"format": "avgen-scene", "version": 1, "name": "column", "nodes": []})");
    const json nodeCall = json::array({json{{"tool", "scene.create_node"},
                                            {"arguments", {{"name", "column2"}, {"asset", model}, {"position", {3.0, 0.0, 0.0}}}}},
                                       json{{"tool", "scene.get_node"}, {"arguments", {{"name", "column2"}}}}});
    writeText(root / "node.calls.json", nodeCall.dump());
    std::tie(code, text) = runCall({"--calls", (root / "node.calls.json").string(), "--project", project.string(),
                                    "--read-root", content.string(), "--save"},
                                   capture);
    INFO(text);
    CHECK(code == 0);
    auto made = envelopes(text);
    CHECK(made.at("scene.create_node").at("success") == true);
    CHECK(made.at("scene.get_node").at("success") == true);
}

TEST_CASE("The protocol document is generated from the server's own tables", "[api][serve][catalogue]") {
    const std::string doc = api::protocolMarkdown();
    for (const api::RpcMethod& m : api::rpcMethods()) {
        CHECK(doc.find("`" + std::string(m.name) + "`") != std::string::npos);
    }
    for (const api::RpcErrorRow& e : api::rpcErrors()) {
        CHECK(doc.find("`" + std::to_string(e.code) + "`") != std::string::npos);
    }
    bool listed = false;
    for (const api::Artefact& a : api::generatedArtefacts(api::makeCatalogueRegistry())) {
        listed = listed || a.relativePath == "docs/api/protocol.md";
    }
    CHECK(listed);
}
