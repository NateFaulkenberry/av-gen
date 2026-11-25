// `avgen call` (ADR-2155, Engine API stage S2; 02-session-api.md section 10): one session, one call or a list of
// them, the envelope printed, the process gone. Each invocation is a fresh session, so nothing carries over
// between two of them except what was saved; use `avgen serve` for a conversation.
//
// The calling thread is the host thread (`EngineHost::bindHostThread`), so each body runs inline: there is no
// second thread and no loop to service.

#include "api/engine_host.hpp"

#include "api/version.hpp"
#include "app/engine.hpp"
#include "core/log.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <thread>

#include <cerrno>
#include <cstdio>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

namespace avgen::api {
namespace {

namespace fs = std::filesystem;
using nlohmann::json;

constexpr int kExitOk = 0;
constexpr int kExitToolFailed = 1;
constexpr int kExitUsage = 2;
constexpr int kExitHost = 7;

struct PlannedCall {
    std::string tool;
    json arguments = json::object();
    Irreversible irreversible = Irreversible::Refuse;
};

int usage(const std::string& why) {
    std::fprintf(stderr, "avgen call: %s\n%s", why.c_str(), callUsage());
    return kExitUsage;
}

// A calls file: `[{"tool": "...", "arguments": {...}, "irreversible": false}, ...]`, or the same list under
// `{"calls": [...]}`. `name` is accepted for `tool`, as `tools/call` names it.
Result<std::vector<PlannedCall>> readCallsFile(const fs::path& file) {
    std::ifstream in(file);
    if (!in) {
        return fail("cannot read '{}'", file.string());
    }
    const json doc = json::parse(in, nullptr, false);
    if (doc.is_discarded()) {
        return fail("'{}' is not JSON", file.string());
    }
    const json& list = doc.is_object() && doc.contains("calls") ? doc.at("calls") : doc;
    if (!list.is_array()) {
        return fail("'{}' must hold a list of calls, or an object with one under \"calls\"", file.string());
    }
    std::vector<PlannedCall> out;
    for (std::size_t i = 0; i < list.size(); ++i) {
        const json& c = list[i];
        const char* key = c.is_object() && c.contains("tool") ? "tool" : "name";
        if (!c.is_object() || !c.contains(key) || !c.at(key).is_string()) {
            return fail("call {} in '{}' has no string \"tool\"", i, file.string());
        }
        PlannedCall p;
        p.tool = c.at(key).get<std::string>();
        if (c.contains("arguments") && !c.at("arguments").is_null()) {
            if (!c.at("arguments").is_object()) {
                return fail("call {} ('{}') has arguments that are not an object", i, p.tool);
            }
            p.arguments = c.at("arguments");
        }
        if (c.contains("irreversible") && c.at("irreversible").is_boolean() && c.at("irreversible").get<bool>()) {
            p.irreversible = Irreversible::Accept;
        }
        out.push_back(std::move(p));
    }
    return out;
}

void writeOut(int fd, const std::string& text) {
    std::size_t done = 0;
    while (done < text.size()) {
        const ssize_t n = ::write(fd, text.data() + done, text.size() - done);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) {
                continue;
            }
            return;
        }
        done += static_cast<std::size_t>(n);
    }
}

} // namespace

const char* callUsage() {
    return "usage: avgen call <tool> ['<json arguments>'] [options]\n"
           "       avgen call --calls <file.json> [options]\n"
           "  Runs one tool (or a list of calls, in one session) and prints the envelope as JSON on stdout.\n"
           "  No window, no settings file, no OSC or MIDI listener. The log is on stderr. A GPU device only if\n"
           "  a render job starts (under the machine's GPU lock); jobs still running after the calls are run\n"
           "  to the end before the process exits (job.wait in the list returns their results).\n"
           "  --calls <file>       a JSON list of {\"tool\", \"arguments\"?, \"irreversible\"?}; prints\n"
           "                       {\"success\", \"calls\": [{\"tool\", \"envelope\"}]}\n"
           "  --project <p.json>   open this project first; its folder becomes a read and write root\n"
           "  --save               after the calls, if they all succeeded, save the project (project.save)\n"
           "  --root <dir>         a folder the session may read and write (repeatable)\n"
           "  --read-root <dir>    a folder it may read (repeatable)\n"
           "  --write-root <dir>   a folder it may write, and so read (repeatable)\n"
           "  --allow-subprocess   tools may start child processes\n"
           "  exit: 0 success, 1 a tool failed (the envelope is still printed), 2 usage, 7 host failure\n";
}

int runCallCommand(int argc, char** argv, RenderBackendFactory renderBackend) {
    HostFlags flags;
    std::optional<std::string> tool;
    std::optional<json> arguments;
    std::optional<fs::path> callsFile;
    std::optional<fs::path> project;
    bool save = false;
    for (int i = 2; i < argc; ++i) {
        const std::string_view a = argv[i];
        if (a == "--help" || a == "-h") {
            std::fputs(callUsage(), stdout);
            return kExitOk;
        }
        if (a == "--calls" || a == "--project") {
            if (i + 1 >= argc) {
                return usage(std::string(a) + " needs a file");
            }
            (a == "--calls" ? callsFile : project) = fs::path(argv[++i]);
            continue;
        }
        if (a == "--save") {
            save = true;
            continue;
        }
        auto consumed = parseHostFlag(argc, argv, i, flags);
        if (!consumed) {
            return usage(consumed.error().message);
        }
        if (*consumed) {
            continue;
        }
        if (a.starts_with("--")) {
            return usage("unknown argument '" + std::string(a) + "'");
        }
        if (!tool) {
            tool = std::string(a);
        } else if (!arguments) {
            json parsed = json::parse(a, nullptr, false);
            if (parsed.is_discarded() || !parsed.is_object()) {
                return usage("the arguments must be one JSON object, e.g. '{\"name\": \"demo\"}'");
            }
            arguments = std::move(parsed);
        } else {
            return usage("unexpected argument '" + std::string(a) + "'");
        }
    }
    if (tool.has_value() == callsFile.has_value()) {
        return usage(callsFile ? "pass a tool or --calls, not both" : "name a tool, or pass --calls <file>");
    }
    std::vector<PlannedCall> plan;
    if (callsFile) {
        auto read = readCallsFile(*callsFile);
        if (!read) {
            return usage(read.error().message);
        }
        plan = std::move(*read);
    } else {
        plan.push_back(PlannedCall{*tool, arguments.value_or(json::object()), Irreversible::Refuse});
    }
    std::error_code ec;
    fs::path projectFile;
    if (project) {
        projectFile = fs::weakly_canonical(fs::absolute(*project, ec), ec);
        if (ec || !fs::is_regular_file(projectFile, ec)) {
            return usage("--project '" + project->string() + "' is not a file");
        }
        // The person named this project on the command line: its folder is theirs to read and write, whatever
        // folder that is (the Studio rule, owner decision 1).
        flags.permissions.openProjectFolder = true;
    }

    auto protocol = protectStdout(); // stdout carries the envelope and nothing else
    if (!protocol) {
        log::error("call: {}", protocol.error().message);
        return kExitHost;
    }
    const int out = *protocol;
    const auto finish = [out](int code) {
        restoreStdout(out);
        return code;
    };

    EngineHostOptions hostOptions;
    hostOptions.renderBackend = std::move(renderBackend);
    EngineHost host(std::move(hostOptions));
    host.bindHostThread();
    SessionOptions so;
    so.client = ClientInfo{ClientKind::Cli, "avgen-call", version().engine};
    so.permissions = std::move(flags.permissions);
    so.permissions.liveControl = false;
    auto created = Session::create(std::move(so), host);
    if (!created) {
        log::error("call: {}", created.error().message);
        return finish(kExitHost);
    }
    Session& session = **created;
    if (!projectFile.empty()) {
        Result<void> loaded;
        if (!session.onHost([&](app::Engine& engine) { loaded = engine.loadProject(projectFile); }) || !loaded) {
            log::error("call: cannot open '{}': {}", projectFile.string(),
                       loaded ? std::string("the host did not run it") : loaded.error().message);
            return finish(kExitHost);
        }
        log::info("call: opened {}", projectFile.string());
    }

    bool allOk = true;
    json results = json::array();
    for (const PlannedCall& c : plan) {
        CallOptions options;
        options.irreversible = c.irreversible;
        const CallResult r = session.call(c.tool, c.arguments, options);
        allOk = allOk && r.success;
        results.push_back(json{{"tool", c.tool}, {"envelope", r.toWire()}});
        log::info("call: {} -> {}{}", c.tool, r.success ? "ok" : "failed",
                  r.summary.empty() ? std::string{} : ": " + r.summary);
    }
    if (save) {
        if (!allOk) {
            log::warn("call: --save skipped: a call failed");
        } else {
            const CallResult r = session.call("project.save");
            allOk = r.success;
            if (r.success) {
                log::info("call: --save: {}", r.summary);
            } else {
                log::error("call: --save failed: {}", r.error ? r.error->message : r.summary);
            }
        }
    }
    // ADR-2159: a render the calls started and did not wait for still finishes: this is the host thread, and nothing
    // else will step it once the process exits.
    for (bool more = host.stepJobs(); more; more = host.stepJobs()) {
        if (host.jobStats().queued > 0 && !host.jobStats().running) {
            std::this_thread::sleep_for(std::chrono::milliseconds{100}); // waiting for the GPU lock
        }
    }
    for (const JobRecord& r : session.jobs().list()) {
        log::info("call: {} {}", r.id, jobStateName(r.state));
    }
    session.close(); // rolls back an explicit transaction a --calls list left open; never saves

    const json printed = callsFile ? json{{"success", allOk}, {"calls", std::move(results)}}
                                   : results.at(0).at("envelope");
    writeOut(out, printed.dump(2, ' ', false, json::error_handler_t::replace) + "\n");
    return finish(allOk ? kExitOk : kExitToolFailed);
}

} // namespace avgen::api
