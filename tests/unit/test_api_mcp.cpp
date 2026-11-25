// ADR-2162 (Engine API stage S9): `avgen mcp`, the MCP adapter over the one Session.
//
// P1: one scripted list of calls gives the same envelopes through `Session::call` (in-process), through `avgen
// serve`'s JSON-RPC loop and through `avgen mcp` (`structuredContent`), each over a pipe pair with the server on a
// thread of its own. The shapes of `initialize`, `tools/list` and `tools/call` are checked against fixtures in
// tests/data/mcp that name the pinned revision. The name map is unique, pattern-valid and total, and an MCP name
// that is not in it is a structured protocol error.

#include "ai/tool_api.hpp"
#include "ai/tool_context.hpp"
#include "api/describe.hpp"
#include "api/engine_host.hpp"
#include "api/jobs.hpp"
#include "api/jsonrpc_server.hpp"
#include "api/mcp_shaper.hpp"
#include "api/session.hpp"
#include "api/version.hpp"
#include "core/log.hpp"
#include "support/gltf_fixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <poll.h>
#include <unistd.h>

using namespace avgen;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

fs::path freshDir(const std::string& name) {
    const fs::path dir = fs::temp_directory_path() / ("avgen_mcp_" + name + "_" + std::to_string(getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    return fs::weakly_canonical(dir);
}

json readJson(const fs::path& p) {
    std::ifstream in(p);
    REQUIRE(in.good());
    const json j = json::parse(in, nullptr, false);
    REQUIRE_FALSE(j.is_discarded());
    return j;
}

fs::path fixture(const std::string& name) { return fs::path(AVGEN_SOURCE_DIR) / "tests/data/mcp" / name; }

// A fixture value "<any>" matches anything; otherwise objects need the same keys, arrays the same length, and
// scalars the same value. `where` names the first difference.
bool matches(const json& actual, const json& expected, std::string& where, const std::string& at = "") {
    if (expected.is_string() && expected.get<std::string>() == "<any>") {
        return true;
    }
    if (expected.is_object()) {
        if (!actual.is_object()) {
            where = at + " is not an object";
            return false;
        }
        for (const auto& [k, v] : expected.items()) {
            if (!actual.contains(k)) {
                where = at + "/" + k + " is missing";
                return false;
            }
            if (!matches(actual.at(k), v, where, at + "/" + k)) {
                return false;
            }
        }
        for (const auto& [k, v] : actual.items()) {
            if (!expected.contains(k)) {
                where = at + "/" + k + " is not in the fixture";
                return false;
            }
        }
        return true;
    }
    if (expected.is_array()) {
        if (!actual.is_array() || actual.size() != expected.size()) {
            where = at + " is not an array of " + std::to_string(expected.size());
            return false;
        }
        for (std::size_t i = 0; i < expected.size(); ++i) {
            if (!matches(actual[i], expected[i], where, at + "/" + std::to_string(i))) {
                return false;
            }
        }
        return true;
    }
    if (actual != expected) {
        where = at + ": " + actual.dump() + " is not " + expected.dump();
        return false;
    }
    return true;
}

ai::Tool testTool(std::string name, ai::ToolFn fn) {
    ai::Tool t;
    t.definition.name = std::move(name);
    t.definition.title = "test";
    t.definition.description = "a test-only tool";
    t.definition.inputSchema = ai::schema::object(json::object());
    t.definition.annotations.readOnly = true;
    t.execute = std::move(fn);
    return t;
}

api::ServeOptions rootedAt(const fs::path& root, api::ServeProtocol protocol) {
    api::ServeOptions o;
    o.protocol = protocol;
    o.flags.permissions.readRoots = {root};
    o.flags.permissions.writeRoots = {root};
    return o;
}

// A client over two pipes, with `api::serve` on its own thread (which becomes the host thread).
class Pipe {
public:
    explicit Pipe(api::ServeOptions options) {
        int in[2];
        int out[2];
        REQUIRE(::pipe(in) == 0);
        REQUIRE(::pipe(out) == 0);
        toServer_ = in[1];
        fromServer_ = out[0];
        server_ = std::thread([this, options = std::move(options), serverIn = in[0], serverOut = out[1]]() mutable {
            exitCode_ = api::serve(std::move(options), serverIn, serverOut);
            ::close(serverIn);
            ::close(serverOut);
        });
    }
    ~Pipe() {
        closeInput();
        join();
        ::close(fromServer_);
    }
    void sendLine(const std::string& line) {
        const std::string l = line + "\n";
        REQUIRE(::write(toServer_, l.data(), l.size()) == static_cast<ssize_t>(l.size()));
    }
    json request(int id, const std::string& method, json params = nullptr) {
        json m{{"jsonrpc", "2.0"}, {"id", id}, {"method", method}};
        if (!params.is_null()) {
            m["params"] = std::move(params);
        }
        sendLine(m.dump());
        return next();
    }
    void notify(const std::string& method) { sendLine(json{{"jsonrpc", "2.0"}, {"method", method}}.dump()); }
    // The next reply; server notifications on the way are kept in `notifications`.
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
    json readMessage() {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{30};
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
    std::vector<json> notifications;

private:
    int toServer_ = -1;
    int fromServer_ = -1;
    std::string buffer_;
    std::thread server_;
    int exitCode_ = -1;
};

json mcpInitialize(Pipe& p) {
    const json r = p.request(1, "initialize",
                             json{{"protocolVersion", api::kMcpProtocolRevision},
                                  {"capabilities", json::object()},
                                  {"clientInfo", {{"name", "test-mcp"}, {"version", "1"}}}});
    INFO(r.dump());
    REQUIRE(r.contains("result"));
    p.notify("notifications/initialized");
    return r.at("result");
}

json jsonRpcInitialize(Pipe& p) {
    const json r = p.request(1, "initialize", json{{"clientInfo", {{"name", "test-rpc"}, {"version", "1"}}}});
    REQUIRE(r.contains("result"));
    return r.at("result");
}

std::vector<json> progressOf(const Pipe& p) {
    std::vector<json> out;
    for (const json& n : p.notifications) {
        if (n.at("method") == "notifications/progress") {
            out.push_back(n);
        }
    }
    return out;
}

int errorCode(const json& reply) {
    INFO(reply.dump());
    REQUIRE(reply.contains("error"));
    return reply.at("error").at("code").get<int>();
}

// ---- P1: the script ------------------------------------------------------------------------------------------

// One scripted call. `$asset` and `$txn` in the arguments (and `transaction`) are replaced by the id the import
// and `transaction.begin` returned in the same run, so each run is self-consistent.
struct Step {
    std::string tool;
    json args;
    bool inTransaction = false;
};

const json kRim = {{"name", "rim"},           {"type", "point"}, {"position", {2.5, 4.0, -2.0}},
                   {"color", {0.4, 0.6, 1.0}}, {"intensity", 40.0}, {"range", 12.0}};

std::vector<Step> script(const fs::path& root) {
    return {
        {"project.create", json{{"path", "poc"}, {"scene", "empty"}}},
        {"asset.import", json{{"file", (root / "column.glb").string()}}},
        {"scene.create_node", json{{"name", "column"}, {"asset", "$asset"}}},
        {"document.patch",
         json{{"document", "scene"}, {"patch", json::array({{{"op", "add"}, {"path", "/lights"}, {"value", json::array({kRim})}}})}}},
        {"parameter.set", json{{"path", "nodes/column/rotation"}, {"value", {0.0, 20.0, 0.0}}}},
        {"modulation.create", json{{"source", "audio.bass"}, {"target", "nodes/column/position"}, {"amount", 0.5}}},
        {"project.validate", json::object()},
        {"transaction.begin", json{{"label", "p1"}}},
        {"parameter.set", json{{"path", "nodes/column/rotation"}, {"value", {0.0, 70.0, 0.0}}}, true},
        {"transaction.rollback", json{{"id", "$txn"}}},
        {"parameter.get", json{{"paths", {"nodes/column/rotation"}}}},
        {"no.such_tool", json::object()}, // a tool failure: the same envelope everywhere (MCP: by its name `no_such_tool`, below)
    };
}

json substitute(json v, const std::string& asset, const std::string& txn) {
    if (v.is_string()) {
        if (v == "$asset") return asset;
        if (v == "$txn") return txn;
        return v;
    }
    if (v.is_structured()) {
        for (auto& e : v) {
            e = substitute(e, asset, txn);
        }
    }
    return v;
}

// Prepares the root a run starts from: empty, with the GLB the import reads. Every run uses the same absolute
// path, so the paths in the results are comparable.
void resetRoot(const fs::path& root, const fs::path& glb) {
    fs::remove_all(root);
    fs::create_directories(root);
    fs::copy_file(glb, root / "column.glb");
}

// Runs the script through `call(tool, args, transaction)`, which answers the envelope.
template <class Call>
std::vector<json> runScript(const fs::path& root, Call&& call) {
    std::vector<json> out;
    std::string asset;
    std::string txn;
    for (const Step& s : script(root)) {
        const json args = substitute(s.args, asset, txn);
        const json env = call(s.tool, args, s.inTransaction ? std::optional<std::string>(txn) : std::nullopt);
        if (s.tool == "asset.import" && env.at("success") == true) {
            asset = env.at("result").at("id").get<std::string>();
        }
        if (s.tool == "transaction.begin" && env.at("success") == true) {
            txn = env.at("result").at("id").get<std::string>();
        }
        out.push_back(env);
    }
    return out;
}

std::vector<json> runDirect(const fs::path& root) {
    api::EngineHost host;
    host.bindHostThread();
    api::SessionOptions so;
    so.client = api::ClientInfo{api::ClientKind::Test, "p1", "1"};
    so.permissions.readRoots = {root};
    so.permissions.writeRoots = {root};
    auto session = api::Session::create(std::move(so), host);
    REQUIRE(session.has_value());
    auto out = runScript(root, [&](const std::string& tool, const json& args, const std::optional<std::string>& txn) {
        api::CallOptions o;
        o.transaction = txn;
        return (*session)->call(tool, args, o).toWire();
    });
    (*session)->close();
    return out;
}

std::vector<json> runJsonRpc(const fs::path& root) {
    Pipe p(rootedAt(root, api::ServeProtocol::JsonRpc));
    jsonRpcInitialize(p);
    int id = 10;
    auto out = runScript(root, [&](const std::string& tool, const json& args, const std::optional<std::string>& txn) {
        json params{{"name", tool}, {"arguments", args}};
        if (txn) {
            params["transaction"] = *txn;
        }
        const json r = p.request(id++, "tools/call", params);
        INFO(r.dump());
        REQUIRE(r.contains("result"));
        return r.at("result");
    });
    p.closeInput();
    CHECK(p.join() == 0);
    return out;
}

std::vector<json> runMcp(const fs::path& root) {
    Pipe p(rootedAt(root, api::ServeProtocol::Mcp));
    mcpInitialize(p);
    // The client learns the names from tools/list: each tool's MCP name, and its canonical name in `_meta`.
    const json listed = p.request(2, "tools/list");
    std::map<std::string, std::string> mcpNames;
    for (const json& t : listed.at("result").at("tools")) {
        mcpNames[t.at("_meta").at(api::kMcpMetaTool).get<std::string>()] = t.at("name").get<std::string>();
    }
    int id = 10;
    auto out = runScript(root, [&](const std::string& tool, const json& args, const std::optional<std::string>& txn) {
        // The client knows tools by their MCP names, from the table; the unknown one by the name a client would
        // guess for it.
        const auto known = mcpNames.find(tool);
        const std::string mcpName = known != mcpNames.end() ? known->second : api::mcpToolName(tool);
        json params{{"name", mcpName}, {"arguments", args}};
        if (txn) {
            params["_meta"] = json{{api::kMcpMetaTransaction, *txn}};
        }
        const json r = p.request(id++, "tools/call", params);
        INFO(r.dump());
        if (r.contains("error")) {
            // An unknown name is a protocol error over MCP; its envelope is what the session answers for it, which
            // the other two paths carry. Rebuild nothing: report the error so the comparison shows it.
            return json{{"mcpError", r.at("error")}};
        }
        const json& result = r.at("result");
        REQUIRE(result.contains("structuredContent"));
        CHECK(result.at("isError") == !result.at("structuredContent").at("success").get<bool>());
        const json& text = result.at("content").at(0);
        CHECK(text.at("type") == "text");
        CHECK(text.at("text").get<std::string>().find(result.at("structuredContent").at("summary").get<std::string>()) == 0);
        return result.at("structuredContent");
    });
    p.closeInput();
    CHECK(p.join() == 0);
    return out;
}

// ---- a fake GPU half, for the job-progress mapping (no device) ----------------------------------------------

class FakeJob final : public api::GpuRenderJob {
public:
    explicit FakeJob(api::RenderRequest q) : q_(std::move(q)) {
        total_ = static_cast<std::uint64_t>(std::max(1.0, std::ceil((q_.endSeconds - q_.startSeconds) * q_.fps - 1e-9)));
    }
    Result<void> start(const api::CompileWaitFn& onWait) override {
        if (onWait) {
            onWait(api::CompileProgress{2, 1, "fake-pipeline"});
        }
        std::error_code ec;
        fs::create_directories(q_.outputDir, ec);
        return {};
    }
    bool step(double) override {
        std::this_thread::sleep_for(std::chrono::milliseconds{20});
        std::ofstream(q_.outputDir / fmt::format("frame_{:06d}.png", done_)) << "frame" << done_;
        hashes_.push_back(0x645df906acfb383cull + done_);
        return ++done_ >= total_;
    }
    void cancel() override {}
    api::GpuJobProgress progress() const override {
        api::GpuJobProgress g;
        g.framesDone = done_;
        g.framesTotal = total_;
        g.framesWritten = done_;
        return g;
    }
    api::GpuJobOutcome outcome() const override {
        api::GpuJobOutcome o;
        o.frameHashes = hashes_;
        o.framesWritten = done_;
        return o;
    }

private:
    api::RenderRequest q_;
    std::uint64_t total_ = 1;
    std::uint64_t done_ = 0;
    std::vector<std::uint64_t> hashes_;
};

class FakeBackend final : public api::RenderBackend {
public:
    Result<void> ensureDevice() override {
        device_ = true;
        return {};
    }
    bool deviceCreated() const override { return device_; }
    Result<std::unique_ptr<api::GpuRenderJob>> create(const api::RenderRequest& q) override {
        return std::unique_ptr<api::GpuRenderJob>(std::make_unique<FakeJob>(q));
    }

private:
    bool device_ = false;
};

// Points the machine GPU lock (`$TMPDIR/avgen-gpu.lock`) at a private folder while it lives, so a fake job never
// takes the real one.
struct PrivateGpuLock {
    std::string saved;
    bool had = false;
    explicit PrivateGpuLock(const fs::path& dir) {
        if (const char* t = std::getenv("TMPDIR")) {
            saved = t;
            had = true;
        }
        ::setenv("TMPDIR", dir.c_str(), 1);
    }
    ~PrivateGpuLock() {
        if (had) {
            ::setenv("TMPDIR", saved.c_str(), 1);
        } else {
            ::unsetenv("TMPDIR");
        }
    }
};

} // namespace

TEST_CASE("P1: the same calls give the same envelopes through Session::call, JSON-RPC and MCP",
          "[api][mcp][p1]") {
    const fs::path root = freshDir("p1");
    const fs::path glb = testsupport::writeTriangleGlb("mcp_p1_column.glb");
    resetRoot(root, glb);
    const std::vector<json> direct = runDirect(root);
    resetRoot(root, glb);
    const std::vector<json> rpc = runJsonRpc(root);
    resetRoot(root, glb);
    const std::vector<json> mcp = runMcp(root);

    const std::vector<Step> steps = script(root);
    REQUIRE(direct.size() == steps.size());
    REQUIRE(rpc.size() == steps.size());
    REQUIRE(mcp.size() == steps.size());
    for (std::size_t i = 0; i < steps.size(); ++i) {
        INFO("step " << i << ": " << steps[i].tool);
        INFO("direct: " << direct[i].dump());
        INFO("json-rpc: " << rpc[i].dump());
        INFO("mcp: " << mcp[i].dump());
        if (steps[i].tool == "no.such_tool") {
            // The one place the protocols differ, on purpose: an unknown name is a tool result (NOT_FOUND) in the
            // session and over JSON-RPC, and a protocol error (-32602) over MCP, as MCP specifies.
            CHECK(direct[i] == rpc[i]);
            CHECK(direct[i].at("error").at("code") == "NOT_FOUND");
            REQUIRE(mcp[i].contains("mcpError"));
            CHECK(mcp[i].at("mcpError").at("code") == api::kMcpInvalidParams);
            continue;
        }
        CHECK(direct[i].at("success") == true);
        CHECK(direct[i].at("result") == rpc[i].at("result"));
        CHECK(direct[i].at("result") == mcp[i].at("result"));
        CHECK(direct[i] == rpc[i]); // the whole envelope: summary, changes, warnings, transaction
        CHECK(direct[i] == mcp[i]);
    }
    // The rollback put the rotation back to the value set outside the transaction, on every path.
    const json rotation = direct.at(10).at("result").at("parameters").at(0).at("value");
    CHECK(rotation.at(1).get<double>() == 20.0);
}

TEST_CASE("MCP shapes: initialize, tools/list and tools/call match the fixtures of the pinned revision",
          "[api][mcp][shapes]") {
    const fs::path root = freshDir("shapes");
    api::ServeOptions o = rootedAt(root, api::ServeProtocol::Mcp);
    Pipe p(std::move(o));

    const json init = readJson(fixture("initialize.json"));
    REQUIRE(init.at("revision") == api::kMcpProtocolRevision);
    REQUIRE(init.at("request").at("params").at("protocolVersion") == api::kMcpProtocolRevision);
    p.sendLine(init.at("request").dump());
    const json initReply = p.next();
    std::string where;
    INFO(initReply.dump(2));
    CHECK(matches(initReply, init.at("response"), where));
    INFO(where);
    // The `avgen serve` answer rides along, whole.
    const json& server = initReply.at("result").at("_meta").at(api::kMcpMetaServer);
    CHECK(server.at("api") == api::kApiLevel);
    CHECK(server.at("session").at("client").at("kind") == "mcp");
    p.notify("notifications/initialized");

    const json list = readJson(fixture("tools-list.json"));
    REQUIRE(list.at("revision") == api::kMcpProtocolRevision);
    p.sendLine(list.at("request").dump());
    const json listReply = p.next();
    const json& tools = listReply.at("result").at("tools");
    CHECK(tools.size() == api::makeCatalogueRegistry().size());
    CHECK_FALSE(listReply.at("result").contains("nextCursor"));
    for (const json& expected : list.at("tools")) {
        const auto it = std::find_if(tools.begin(), tools.end(), [&](const json& t) { return t.at("name") == expected.at("name"); });
        REQUIRE(it != tools.end());
        where.clear();
        INFO(it->dump(2));
        CHECK(matches(*it, expected, where));
        INFO(where);
    }
    for (const json& t : tools) {
        INFO(t.at("name"));
        CHECK(t.at("inputSchema").at("type") == "object");
        CHECK(t.at("outputSchema").at("type") == "object");
        CHECK(t.at("annotations").at("openWorldHint") == false);
    }

    const json call = readJson(fixture("tools-call.json"));
    REQUIRE(call.at("revision") == api::kMcpProtocolRevision);
    for (const json& exchange : call.at("exchanges")) {
        p.sendLine(exchange.at("request").dump());
        const json reply = p.next();
        where.clear();
        INFO(reply.dump(2));
        CHECK(matches(reply, exchange.at("response"), where));
        INFO(where);
    }
    p.closeInput();
    CHECK(p.join() == 0);
}

TEST_CASE("MCP lifecycle: ping before initialize, nothing else; one initialize; no batches; unknown methods",
          "[api][mcp][lifecycle]") {
    Pipe p(api::ServeOptions{{}, api::ServeProtocol::Mcp, {}, {}});
    CHECK(p.request(1, "ping").at("result") == json::object());
    CHECK(errorCode(p.request(2, "tools/list")) == api::kMcpInvalidRequest);
    CHECK(errorCode(p.request(3, "initialize", json{{"capabilities", json::object()}})) == api::kMcpInvalidParams);
    // A client asking for another revision is answered with the pinned one.
    const json r = p.request(4, "initialize", json{{"protocolVersion", "2024-11-05"}, {"capabilities", json::object()}});
    CHECK(r.at("result").at("protocolVersion") == api::kMcpProtocolRevision);
    CHECK(errorCode(p.request(5, "initialize", json{{"protocolVersion", api::kMcpProtocolRevision}})) ==
          api::kMcpInvalidRequest);
    p.notify("notifications/initialized"); // no reply
    p.notify("notifications/cancelled");   // no reply
    CHECK(errorCode(p.request(6, "server/discover")) == api::kMcpMethodNotFound); // a 2026-07-28 probe: fall back
    CHECK(errorCode(p.request(7, "shutdown")) == api::kMcpMethodNotFound);        // serve's, not MCP's
    p.sendLine(R"([{"jsonrpc":"2.0","id":8,"method":"ping"}])");
    CHECK(errorCode(p.next()) == api::kMcpInvalidRequest);
    CHECK(errorCode(p.request(9, "tools/list", json{{"cursor", "x"}})) == api::kMcpInvalidParams);
    CHECK(p.request(10, "logging/setLevel", json{{"level", "error"}}).at("result") == json::object());
    CHECK(errorCode(p.request(11, "logging/setLevel", json{{"level", "loud"}})) == api::kMcpInvalidParams);
    CHECK(p.request(12, "ping").at("result") == json::object()); // still answering: the notifications got no reply
    p.closeInput();
    CHECK(p.join() == 0);
}

TEST_CASE("MCP name map: unique, pattern-valid and total; a name not in it is a structured error",
          "[api][mcp][names]") {
    const json catalogue = api::toolCatalogue(api::makeCatalogueRegistry());
    const api::McpNameTable names(catalogue.at("tools"));
    const std::regex pattern("^[a-zA-Z0-9_-]{1,64}$");
    REQUIRE(names.size() == catalogue.at("tools").size()); // unique: the table refuses a duplicate
    for (const json& t : catalogue.at("tools")) {
        const std::string name = t.at("name").get<std::string>();
        const auto mcp = names.mcpNameFor(name);
        REQUIRE(mcp.has_value()); // total
        INFO(name << " -> " << *mcp);
        CHECK(std::regex_match(*mcp, pattern));
        CHECK(names.toolFor(*mcp) == name);
    }
    // The table, not the string: a name it does not hold has no tool, whatever reversing it would give.
    const json fake = json::array({json{{"name", "a.b"}, {"mcpName", "custom_name"}}});
    const api::McpNameTable custom(fake);
    CHECK(custom.toolFor("custom_name") == std::optional<std::string>("a.b"));
    CHECK_FALSE(custom.toolFor("a_b").has_value());
    CHECK_FALSE(names.toolFor("parameter.set").has_value()); // the canonical name is not an MCP name
    // A duplicate MCP name is refused when the table is built.
    CHECK_THROWS(api::McpNameTable(json::array({json{{"name", "a.b"}, {"mcpName", "a_b"}}, json{{"name", "a-b"}, {"mcpName", "a_b"}}})));

    Pipe p(api::ServeOptions{{}, api::ServeProtocol::Mcp, {}, {}});
    mcpInitialize(p);
    for (const char* bad : {"no_such_tool", "parameter.set", ""}) {
        const json r = p.request(2, "tools/call", json{{"name", bad}, {"arguments", json::object()}});
        INFO(r.dump());
        CHECK(errorCode(r) == api::kMcpInvalidParams);
        CHECK(r.at("error").at("data").at("name") == bad);
        CHECK(r.at("error").at("message") == fmt::format("Unknown tool: {}", bad));
    }
    // A known name with bad arguments is a tool execution error: isError, with the envelope's INVALID_ARGUMENTS.
    const json r = p.request(3, "tools/call", json{{"name", "parameter_set"}, {"arguments", {{"nope", 1}}}});
    INFO(r.dump());
    CHECK(r.at("result").at("isError") == true);
    CHECK(r.at("result").at("structuredContent").at("error").at("code") == "INVALID_ARGUMENTS");
}

TEST_CASE("MCP progress: a tool's progress and a waited job's reach the call's progressToken, and only it",
          "[api][mcp][progress]") {
    const fs::path root = freshDir("progress");
    PrivateGpuLock lock(root / "tmp");
    fs::create_directories(root / "tmp");
    api::ServeOptions o = rootedAt(root, api::ServeProtocol::Mcp);
    o.extraTools.push_back(testTool("test.progress", [](const json&, ai::ToolContext& ctx) {
        if (ctx.onProgress) {
            ctx.onProgress(0.5f, "half way");
            ctx.onProgress(0.5f, "still half way"); // not an increase, a new message: nudged up
            ctx.onProgress(0.5f, "still half way"); // a repeat at no more progress: not sent
        }
        return ai::ToolResult::ok(json::object(), "done");
    }));
    o.renderBackend = [] { return std::make_unique<FakeBackend>(); };
    Pipe p(std::move(o));
    mcpInitialize(p);

    REQUIRE(p.request(2, "tools/call", json{{"name", "test_progress"}}).at("result").at("isError") == false);
    CHECK(progressOf(p).empty()); // no token, no progress

    REQUIRE(p.request(3, "tools/call", json{{"name", "test_progress"}, {"_meta", {{"progressToken", "tok-3"}}}})
                .at("result")
                .at("isError") == false);
    const std::vector<json> told = progressOf(p);
    REQUIRE(told.size() == 2);
    CHECK(told[0].at("params").at("progressToken") == "tok-3");
    CHECK(told[0].at("params").at("progress") == 0.5);
    CHECK(told[0].at("params").at("total") == 1.0);
    CHECK(told[0].at("params").at("message") == "half way");
    CHECK(told[1].at("params").at("progress").get<double>() > 0.5);
    p.notifications.clear();

    // A render job through the fake backend: render_capture answers at once, job_wait with a token gets the job's
    // progress, increasing, and the response comes after the last of it.
    const json created = p.request(4, "tools/call", json{{"name", "project_create"}, {"arguments", {{"path", "proj"}, {"scene", "empty"}}}});
    REQUIRE(created.at("result").at("isError") == false);
    const json captured = p.request(5, "tools/call",
                                    json{{"name", "render_capture"},
                                         {"arguments", {{"path", "renders/a.png"}, {"frame", 2}, {"width", 64}, {"height", 36}}}});
    INFO(captured.dump());
    REQUIRE(captured.at("result").at("isError") == false);
    const std::string job = captured.at("result").at("structuredContent").at("job").get<std::string>();
    const json waited = p.request(6, "tools/call",
                                  json{{"name", "job_wait"},
                                       {"arguments", {{"id", job}, {"timeoutMs", 20000}}},
                                       {"_meta", {{"progressToken", 6}}}});
    INFO(waited.dump());
    REQUIRE(waited.at("result").at("isError") == false);
    CHECK(waited.at("result").at("structuredContent").at("result").at("state") == "succeeded");
    double last = -1.0;
    const std::vector<json> jobProgress = progressOf(p);
    CHECK_FALSE(jobProgress.empty()); // the fake's frames take 20 ms each, so the wait sees the job run
    for (const json& n : jobProgress) {
        INFO(n.dump());
        CHECK(n.at("params").at("progressToken") == 6);
        CHECK(n.at("params").at("progress").get<double>() > last);
        last = n.at("params").at("progress").get<double>();
    }
    p.notifications.clear();
    CHECK(p.request(7, "ping").at("result") == json::object());
    CHECK(progressOf(p).empty()); // nothing for token 6 after its response
    p.closeInput();
    CHECK(p.join() == 0);
}

TEST_CASE("MCP result shaping is pure: structuredContent is the envelope, isError is !success", "[api][mcp][shapes]") {
    api::CallResult ok;
    ok.result = json{{"x", 1}};
    ok.summary = "did x";
    const json shaped = api::mcpCallToolResult(ok.toWire());
    CHECK(shaped.at("structuredContent") == ok.toWire());
    CHECK(shaped.at("isError") == false);
    CHECK(shaped.at("content").at(0).at("text") == "did x\n" + ok.toWire().dump());
    const api::CallResult bad = api::CallResult::refused(ai::ToolError{ai::ToolErrorCode::NotFound, "nope", "", nullptr});
    CHECK(api::mcpCallToolResult(bad.toWire()).at("isError") == true);
    // A tool's outputSchema is the envelope's `result`, nullable for a failure.
    const json schema = api::mcpOutputSchema(json{{"type", "object"}});
    CHECK(schema.at("properties").at("result").at("anyOf").at(1) == json{{"type", "null"}});
    CHECK(api::mcpOutputSchema(json()).at("properties").at("result") == json::object());
}

TEST_CASE("docs/api/mcp.md is generated from the shaper's tables and names the pinned revision", "[api][mcp][catalogue]") {
    const std::string md = api::mcpMarkdown(api::toolCatalogue(api::makeCatalogueRegistry()));
    CHECK(md.find(api::kMcpProtocolRevision) != std::string::npos);
    CHECK(md.find("\"mcpServers\"") != std::string::npos);
    CHECK(md.find("| `parameter_set` | `parameter.set` |") != std::string::npos);
    for (const api::McpMethod& m : api::mcpMethods()) {
        CHECK(md.find(fmt::format("| `{}` |", m.name)) != std::string::npos);
    }
}
