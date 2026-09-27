// The evaluator hook (ADR-931): `director.evaluate` renders a scratch copy of a span with a plan in it
// and asks the Creative Critic; `director.compare` diffs two kept evaluations.
//
// Nothing here needs a GPU or a running Critic. The host's pipeline is driven end to end with
// stand-in programs laid out the way the real ones are -- a Critic repository with `.venv/bin/critic`,
// its Python and `adapters/avgen/avgen_adapter.py`, a renderer and a cast tracer -- each a small shell
// script that records how it was called and writes what the real one would. So what is checked is the
// engine's side of the conversation: the scratch copy has the candidate in it and the project does
// not, every command line is the one the Critic's guide gives, and every way the Critic can answer --
// a report, a PARTIAL report, not running, a failed job, nonsense -- reads the way it must. The live
// run against a real Critic on a private port is recorded in ADR-931.

#include "ai/director_tools.hpp"
#include "ai/tool_api.hpp"
#include "ai/tool_context.hpp"
#include "app/directing_context.hpp"
#include "app/directing_evaluate.hpp"
#include "app/directing_record.hpp"
#include "app/engine.hpp"
#include "directing/evaluation.hpp"
#include "directing/plan.hpp"
#include "support/project_round_trip.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

using namespace avgen;
using Catch::Matchers::ContainsSubstring;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

fs::path labProject() { return fs::path(AVGEN_SOURCE_DIR) / "tests/data/setpieces/setpiece-lab.json"; }

void writeScript(const fs::path& path, const std::string& body) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path);
    out << "#!/bin/sh\n" << body;
    out.close();
    fs::permissions(path, fs::perms::owner_all | fs::perms::group_read | fs::perms::others_read);
}

std::string readText(const fs::path& p) {
    std::ifstream in(p);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// The report as the Critic writes it: Python's `json` writes an undefined measurement as a bare NaN or
// Infinity, which strict JSON refuses -- the first live report carried `"median_offset_ms": NaN`.
std::string asPythonWrites(const json& report) {
    std::string text = report.dump();
    const std::string anchor = "\"schema\":\"critic.report/1\"";
    const std::size_t at = text.find(anchor);
    REQUIRE(at != std::string::npos);
    text.insert(at + anchor.size(), ",\"sync\":{\"median_offset_ms\":NaN,\"worst\":Infinity,\"best\":-Infinity}");
    return text;
}

// A critic.report/1 with one issue at 13-15 s (inside the west set piece's lift) and one at 90 s
// (inside nothing the plan made), one strength, and two scored dimensions.
json criticReport(bool partial) {
    return json{{"schema", "critic.report/1"},
                {"summary", {{"headline", "2 issue(s) over 1 shot(s) [preview]"}, {"counts", {{"high", 1}, {"medium", 1}}}}},
                {"completeness", {{"status", partial ? "partial" : "complete"}, {"missing", partial ? json::array({"audio_reactivity"}) : json::array()}}},
                {"job", {{"id", "job_stub"}, {"mode", "preview"}}},
                {"dimensions", {{"composition", {{"score", 0.64}, {"status", "scored"}}},
                                {"motion", {{"score", 0.81}, {"status", "scored"}}},
                                {"audio_reactivity", {{"score", 0.0}, {"status", "not_evaluated"}}}}},
                {"findings", json::array({{{"id", "F001"}, {"key", "repeated_composition:s01"}, {"rule", "repeated_composition"},
                                           {"dimension", "composition"}, {"severity", "high"}, {"kind", "issue"},
                                           {"title", "the lift is framed like the last one"}, {"shot", "s01"},
                                           {"time", {13.0, 15.0}}, {"confidence", 0.8},
                                           {"recommendations", json::array({{{"action", "vary the framing"}}})}},
                                          {{"id", "F002"}, {"key", "stale_hold:s02"}, {"rule", "stale_hold"}, {"severity", "medium"},
                                           {"kind", "issue"}, {"title", "nothing new after 4 s (NaN in a string stays)"},
                                           {"time", {90.0, 94.0}}}})},
                {"strengths", json::array({{{"id", "S001"}, {"key", "clean_exposure:s01"}, {"title", "clean exposure"},
                                            {"time", {12.0, 14.0}}}})}};
}

// The stand-ins, laid out as the real programs are. `exitCode` is what the fake Critic exits with.
struct StandIns {
    testsupport::ScratchDir dir{"evaluate_standins"};
    fs::path repo;
    fs::path critic;
    fs::path avgen;
    fs::path trace;
    fs::path log;

    explicit StandIns(int exitCode = 0) {
        repo = dir / "creative-critic";
        critic = repo / ".venv/bin/critic";
        log = dir / "calls.log";
        const fs::path report = dir / "report.json";
        std::ofstream(report) << asPythonWrites(criticReport(exitCode == 5));
        // Every stand-in appends its argv to the log, one line per call.
        const std::string record = fmt::format("echo \"$(basename \"$0\") $*\" >> '{}'\n", log.string());
        writeScript(critic, record + (exitCode == 4 ? "echo 'critic: evaluator not running at http://127.0.0.1:9' >&2\n"
                                                    : fmt::format("printf '%s' '{}'\n",
                                                                  json{{"job_id", "job_stub"}, {"status", "completed"},
                                                                       {"report_json", report.string()},
                                                                       {"headline", "2 issue(s)"}, {"total_ms", 1234}}
                                                                      .dump())) +
                                    fmt::format("exit {}\n", exitCode));
        // The adapter is run as `<python> <adapter> ...`: the Python stand-in runs it with sh.
        writeScript(repo / ".venv/bin/python", "exec /bin/sh \"$@\"\n");
        writeScript(repo / "adapters/avgen/avgen_adapter.py",
                    record + "while [ $# -gt 0 ]; do if [ \"$1\" = --out ]; then out=$2; fi; shift; done\n"
                             "mkdir -p \"$out\" && echo '{}' > \"$out/inputs.json\"\n");
        // The renderer writes a clip where --render says, and keeps the project it was given.
        avgen = dir / "build/src/avgen";
        writeScript(avgen, record + fmt::format("while [ $# -gt 0 ]; do case \"$1\" in --render) clip=$2;; --project) "
                                                "project=$2;; esac; shift; done\n"
                                                "echo clip > \"$clip\"; cp \"$project\" '{}'\n",
                                                (dir / "rendered-project.json").string()));
        trace = dir / "build/tools/avgen_cast_trace";
        writeScript(trace, record + "while [ $# -gt 0 ]; do if [ \"$1\" = --out ]; then out=$2; fi; shift; done\n"
                                    "echo '{\"entities\": {}}' > \"$out\"\n");
        // A render prefix, as tools/gpu-lock.sh is one: it records itself and runs the rest.
        writeScript(dir / "lock.sh", record + "exec \"$@\"\n");
    }

    [[nodiscard]] app::EvaluatorOptions options() const {
        app::EvaluatorOptions o;
        o.critic = critic;
        o.criticUrl = "http://127.0.0.1:9";
        o.avgen = avgen;
        o.renderPrefix = {(dir / "lock.sh").string()};
        o.scratchDir = dir.path();
        o.timeoutSeconds = 60.0;
        return o;
    }
};

json westPlan(float hoverHeight = 23.0f) {
    return json{{"schemaVersion", 1}, {"id", "ufo"}, {"title", "UFO activity"}, {"tier", "baked"},
                {"setPieces", json::array({{{"key", "west"}, {"template", "abduction"}, {"craft", "saucer"},
                                            {"at", {{"seconds", 12}}}, {"place", {{"point", {-120, -60}}}},
                                            {"set", {{"animals", 1}, {"approachSeconds", 6}, {"hoverHeight", hoverHeight}}}}})}};
}

ai::EvaluationRequest candidate(const app::Engine& engine, const json& plan) {
    (void)engine;
    ai::EvaluationRequest r;
    r.plan = plan;
    r.planId = "ufo";
    r.revision = 1;
    r.candidate = directing::fingerprint(plan);
    r.from = 10.0;
    r.until = 16.0;
    r.label = "first try";
    return r;
}

} // namespace

TEST_CASE("a missing Critic is a clear error, never a silent pass", "[evaluate][adr931]") {
    auto none = app::resolveEvaluatorOptions(app::EvaluatorOptions{});
    REQUIRE_FALSE(none.has_value());
    CHECK_THAT(none.error().message, ContainsSubstring("--critic"));
    CHECK_THAT(none.error().message, ContainsSubstring("AVGEN_CRITIC"));
    app::EvaluatorOptions nowhere;
    nowhere.critic = "/nonexistent/creative-critic/.venv/bin/critic";
    auto missing = app::resolveEvaluatorOptions(nowhere);
    REQUIRE_FALSE(missing.has_value());
    CHECK_THAT(missing.error().message, ContainsSubstring("/nonexistent/creative-critic"));
    // The hook refuses before it writes a copy or starts a thread.
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(labProject()).has_value());
    const ai::EvaluationHook hook = app::makeEvaluationHook(app::EvaluatorOptions{});
    auto started = hook(engine, candidate(engine, westPlan()));
    REQUIRE_FALSE(started.has_value());
    CHECK_THAT(started.error().message, ContainsSubstring("no evaluator is configured"));
    // Control: with the stand-ins in place the same resolution succeeds and derives the rest.
    StandIns standIns;
    auto ok = app::resolveEvaluatorOptions(standIns.options());
    REQUIRE(ok.has_value());
    CHECK(ok->adapter == standIns.repo / "adapters/avgen/avgen_adapter.py");
    CHECK(ok->python == standIns.repo / ".venv/bin/python");
    CHECK(ok->castTrace == standIns.trace);
}

TEST_CASE("a child process: its output, its exit code, and cancellation", "[evaluate][adr931]") {
    testsupport::ScratchDir dir("evaluate_process");
    auto r = app::runProcess({"/bin/sh", "-c", "echo out; echo err >&2; exit 3"}, dir.path());
    REQUIRE(r.has_value());
    CHECK(r->exitCode == 3);
    CHECK(r->out == "out\n");
    CHECK(r->err == "err\n");
    CHECK_FALSE(r->cancelled);
    // A child that would run for half a minute stops within a second of being cancelled.
    std::atomic<bool> cancel{false};
    std::thread canceller([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        cancel = true;
    });
    const auto begun = std::chrono::steady_clock::now();
    auto slow = app::runProcess({"/bin/sleep", "30"}, dir.path(), &cancel);
    canceller.join();
    REQUIRE(slow.has_value());
    CHECK(slow->cancelled);
    CHECK(std::chrono::steady_clock::now() - begun < std::chrono::seconds(5));
    // Something that does not exist cannot be started, and says what it was.
    auto absent = app::runProcess({"/nonexistent/program"}, dir.path());
    CHECK((!absent.has_value() || absent->exitCode != 0));
}

TEST_CASE("the Critic's answer: a report, a partial report, or a clear error", "[evaluate][adr931]") {
    testsupport::ScratchDir dir("evaluate_answer");
    const fs::path reportPath = dir / "report.json";
    std::ofstream(reportPath) << asPythonWrites(criticReport(false));
    const json submitted{{"job_id", "job_stub"}, {"report_json", reportPath.string()}, {"total_ms", 900}};
    app::ProcessOutcome done;
    done.exitCode = 0;
    done.out = submitted.dump(1);
    auto report = app::readCriticOutcome(done, "http://127.0.0.1:9");
    REQUIRE(report.has_value());
    CHECK_FALSE(report->partial);
    CHECK(report->jobId == "job_stub");
    CHECK(report->evaluator == "creative-critic");
    REQUIRE(report->dimensions.size() == 2); // "not_evaluated" is not a score
    REQUIRE(report->findings.size() == 3);
    CHECK(report->findings[0].key == "repeated_composition:s01");
    CHECK(report->findings[0].start == 13.0);
    CHECK(report->findings[0].recommendations == std::vector<std::string>{"vary the framing"});
    CHECK(report->findings[2].kind == "strength");
    // The bare NaN and Infinity were read as nothing, and a "NaN" inside a string was left alone.
    CHECK(report->findings[1].title == "nothing new after 4 s (NaN in a string stays)");

    // PARTIAL (exit 5 under --strict): a report, marked so, never mistaken for a full one.
    app::ProcessOutcome partial = done;
    partial.exitCode = 5;
    auto p = app::readCriticOutcome(partial, "");
    REQUIRE(p.has_value());
    CHECK(p->partial);

    // Not running (exit 4): an error that says where it looked and how to start it.
    app::ProcessOutcome down;
    down.exitCode = 4;
    down.err = "critic: evaluator not running at http://127.0.0.1:9\n";
    auto d = app::readCriticOutcome(down, "http://127.0.0.1:9");
    REQUIRE_FALSE(d.has_value());
    CHECK_THAT(d.error().message, ContainsSubstring("not reachable at http://127.0.0.1:9"));
    CHECK_THAT(d.error().message, ContainsSubstring("critic start --daemon"));
    // A failed job (exit 2), and an answer that is not the Critic's.
    app::ProcessOutcome failed;
    failed.exitCode = 2;
    CHECK_THAT(app::readCriticOutcome(failed, "").error().message, ContainsSubstring("failed"));
    app::ProcessOutcome nonsense;
    nonsense.exitCode = 0;
    nonsense.out = "not json";
    CHECK_FALSE(app::readCriticOutcome(nonsense, "").has_value());
    app::ProcessOutcome noReport = done;
    noReport.out = json{{"job_id", "x"}, {"report_json", (dir / "absent.json").string()}}.dump();
    CHECK_FALSE(app::readCriticOutcome(noReport, "").has_value());
}

TEST_CASE("the pipeline renders a scratch copy with the candidate in it and asks the Critic as its guide says",
          "[evaluate][adr931]") {
    StandIns standIns;
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(labProject()).has_value());
    const nlohmann::json stagingBefore = stage::stagingToJson(engine.composition()->staging());
    auto copy = app::writeRecordingCopy(engine, standIns.dir.path());
    REQUIRE(copy.has_value());
    app::EvaluatorOptions options = standIns.options();
    options.keepFiles = true;
    std::vector<std::string> phases;
    auto report = app::evaluateFromCopy(*copy, candidate(engine, westPlan()), options, nullptr,
                                        [&](const std::string& p) { phases.push_back(p); });
    INFO((report ? std::string() : report.error().message));
    REQUIRE(report.has_value());
    // The report is the plan's: its identity, and each finding keyed by the items its span overlaps.
    CHECK(report->planId == "ufo");
    CHECK(report->label == "first try");
    CHECK(report->from == 10.0);
    CHECK(report->until == 16.0);
    CHECK(report->candidate == directing::fingerprint(westPlan()));
    REQUIRE(report->findings.size() == 3);
    CHECK(report->findings[0].items == std::vector<std::string>{"west"}); // 13-15 s: inside west's lift
    CHECK(report->findings[1].items.empty());                            // 90 s: nothing the plan made
    // The copy had the candidate in it (the renderer was handed its set piece) ...
    const json rendered = json::parse(readText(standIns.dir / "rendered-project.json"));
    REQUIRE(rendered.contains("staging"));
    CHECK(rendered["staging"].dump().find("setpiece/west") != std::string::npos);
    // ... and the person's project did not: no plan, the same staging.
    CHECK(engine.directingPlans().empty());
    CHECK(stage::stagingToJson(engine.composition()->staging()) == stagingBefore);
    // Every command as the guide gives it, in order, the renderer behind the prefix.
    const std::string calls = readText(standIns.log);
    INFO(calls);
    const auto at = [&](const char* s) { return calls.find(s); };
    CHECK(at("lock.sh ") < at("avgen --project"));
    CHECK(at("avgen --project") < at("avgen_cast_trace"));
    CHECK(at("avgen_cast_trace") < at("avgen_adapter.py"));
    CHECK(at("avgen_adapter.py") < at("critic "));
    CHECK_THAT(calls, ContainsSubstring("--range 10.0000:16.0000 --size 960x540"));
    CHECK_THAT(calls, ContainsSubstring("--start 10.0000 --seconds 6.0000"));
    CHECK_THAT(calls, ContainsSubstring("--url http://127.0.0.1:9 submit --inputs"));
    CHECK_THAT(calls, ContainsSubstring("--video-start 10.0000 --mode preview"));
    CHECK_THAT(calls, ContainsSubstring("--wait --json --strict"));
    CHECK_THAT(calls, ContainsSubstring("--no-autostart"));
    CHECK_THAT(calls, ContainsSubstring("--label first try"));
    CHECK(phases.size() >= 5);
}

TEST_CASE("the pipeline says so when the Critic is down, and keeps a partial report as partial", "[evaluate][adr931]") {
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(labProject()).has_value());
    {
        StandIns down(4);
        auto copy = app::writeRecordingCopy(engine, down.dir.path());
        REQUIRE(copy.has_value());
        auto report = app::evaluateFromCopy(*copy, candidate(engine, westPlan()), down.options());
        REQUIRE_FALSE(report.has_value());
        CHECK_THAT(report.error().message, ContainsSubstring("not reachable"));
    }
    {
        StandIns partial(5);
        auto copy = app::writeRecordingCopy(engine, partial.dir.path());
        REQUIRE(copy.has_value());
        auto report = app::evaluateFromCopy(*copy, candidate(engine, westPlan()), partial.options());
        REQUIRE(report.has_value());
        CHECK(report->partial);
    }
}

TEST_CASE("director.evaluate refuses what it cannot evaluate, and says why", "[evaluate][directing][adr931]") {
    ai::ToolRegistry registry;
    ai::registerDirectorTools(registry);
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(labProject()).has_value());
    ai::ToolContext ctx(engine);
    // No evaluator installed: unavailable, and how to supply one.
    ai::ToolResult r = registry.invoke("director.evaluate", json{{"plan", westPlan()}, {"from", 10}, {"until", 16}}, ctx);
    REQUIRE_FALSE(r.success);
    CHECK(r.error->code == ai::ToolErrorCode::Unavailable);
    CHECK_THAT(r.error->message, ContainsSubstring("--critic"));
    // With one: a span that is not one to render, a mode that is not one, a plan nobody holds.
    int started = 0;
    ctx.setEvaluationHook([&](app::Engine&, const ai::EvaluationRequest&) -> Result<std::shared_ptr<ai::DeferredResult>> {
        ++started;
        return fail("stub: not started");
    });
    r = registry.invoke("director.evaluate", json{{"plan", westPlan()}, {"from", 16}, {"until", 10}}, ctx);
    CHECK(r.error->code == ai::ToolErrorCode::InvalidArguments);
    r = registry.invoke("director.evaluate", json{{"plan", westPlan()}, {"from", 0}, {"until", 90}}, ctx);
    CHECK(r.error->code == ai::ToolErrorCode::InvalidArguments); // longer than a minute
    r = registry.invoke("director.evaluate", json{{"plan", westPlan()}, {"from", 10}, {"until", 16}, {"mode", "fastest"}}, ctx);
    CHECK(r.error->code == ai::ToolErrorCode::InvalidArguments);
    r = registry.invoke("director.evaluate", json{{"planId", "nobody"}, {"from", 10}, {"until", 16}}, ctx);
    CHECK(r.error->code == ai::ToolErrorCode::NotFound);
    CHECK(started == 0);
    // A good request reaches the hook, with the candidate's identity; the hook's refusal is reported.
    r = registry.invoke("director.evaluate", json{{"plan", westPlan()}, {"from", 10}, {"until", 16}}, ctx);
    CHECK(started == 1);
    REQUIRE_FALSE(r.success);
    CHECK_THAT(r.error->message, ContainsSubstring("stub: not started"));
    // director.compare with nothing kept: not found, and what to do.
    r = registry.invoke("director.compare", json{{"planId", "ufo"}}, ctx);
    REQUIRE_FALSE(r.success);
    CHECK(r.error->code == ai::ToolErrorCode::NotFound);
}

TEST_CASE("evaluations are kept per plan revision, saved beside the plans, and compared", "[evaluate][directing][adr931]") {
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(labProject()).has_value());
    directing::EvaluationReport first = directing::reportFromCritic(criticReport(false));
    first.planId = "ufo";
    first.revision = 1;
    first.label = "first try";
    directing::attributeFindings(first, {{"west", 4.0, 26.0}});
    directing::EvaluationReport second = first;
    second.label = "lower hover";
    second.findings.erase(second.findings.begin()); // the repeated framing is gone
    second.dimensions = {{"composition", 0.78}, {"motion", 0.80}};
    engine.directingEvaluations() = {first, second};
    // Saved, and read back as they were.
    testsupport::ScratchDir dir("evaluate_saved");
    auto trip = testsupport::saveAndReload(engine, dir / "p.json");
    REQUIRE(trip.has_value());
    REQUIRE(trip->saved.contains("directingEvaluations"));
    REQUIRE(trip->reloaded->directingEvaluations().size() == 2);
    CHECK(trip->reloaded->directingEvaluations()[0] == first);
    CHECK(trip->reloaded->directingEvaluations()[1] == second);
    // A project with none writes no key (byte-stable for every project that never evaluated).
    app::Engine plain(app::EngineMode::Offline);
    REQUIRE(plain.loadProject(labProject()).has_value());
    CHECK_FALSE(plain.projectDocument(dir / "q.json").contains("directingEvaluations"));
    // director.compare: the last two by default, and by label.
    ai::ToolRegistry registry;
    ai::registerDirectorTools(registry);
    ai::ToolContext ctx(*trip->reloaded);
    const ai::ToolResult r = registry.invoke("director.compare", json{{"planId", "ufo"}}, ctx);
    INFO(r.value.dump());
    REQUIRE(r.success);
    CHECK(r.value["resolved"] == json::array({"repeated_composition:s01"}));
    CHECK(r.value["new"].empty());
    CHECK(r.value["persisting"] == json::array({"stale_hold:s02"}));
    bool compositionImproved = false;
    for (const json& d : r.value["dimensions"]) {
        compositionImproved = compositionImproved || (d["name"] == "composition" && d["verdict"] == "improved");
    }
    CHECK(compositionImproved);
    const ai::ToolResult byLabel =
        registry.invoke("director.compare", json{{"planId", "ufo"}, {"a", "lower hover"}, {"b", "first try"}}, ctx);
    REQUIRE(byLabel.success);
    CHECK(byLabel.value["new"] == json::array({"repeated_composition:s01"})); // the other way round
}
