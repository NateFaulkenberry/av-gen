#include "app/directing_evaluate.hpp"

#include "app/directing_context.hpp"
#include "app/engine.hpp"
#include "directing/compiler.hpp"
#include "directing/resolver.hpp"
#include "scene/camera_rig.hpp"
#include "scene/composition.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <mutex>
#include <signal.h>
#include <spawn.h>
#include <sstream>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

extern char** environ;

namespace avgen::app {
namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

std::string envOr(const char* name, std::string fallback = {}) {
    const char* v = std::getenv(name);
    return v != nullptr && *v != '\0' ? std::string(v) : std::move(fallback);
}

fs::path currentExecutable() {
#if defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
        std::error_code ec;
        const fs::path p = fs::weakly_canonical(fs::path(buffer.c_str()), ec);
        return ec ? fs::path(buffer.c_str()) : p;
    }
    return {};
#else
    std::error_code ec;
    return fs::read_symlink("/proc/self/exe", ec);
#endif
}

bool executable(const fs::path& p) { return !p.empty() && ::access(p.c_str(), X_OK) == 0; }

std::string readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

// The last few lines a child said on stderr, for an error message a person can act on.
std::string tail(const std::string& text, std::size_t chars = 600) {
    std::string t = text;
    while (!t.empty() && (t.back() == '\n' || t.back() == ' ')) {
        t.pop_back();
    }
    if (t.size() > chars) {
        t = "..." + t.substr(t.size() - chars);
    }
    return t.empty() ? std::string() : ": " + t;
}

std::string seconds(double s) { return fmt::format("{:.4f}", s); }

std::string commandLine(const std::vector<std::string>& argv) {
    std::string out;
    for (const std::string& a : argv) {
        out += (out.empty() ? "" : " ") + (a.find(' ') != std::string::npos ? "'" + a + "'" : a);
    }
    return out;
}

// A scratch engine on the copy, as a render sees it: offline, no live control, every body simulated.
Result<std::unique_ptr<Engine>> loadForEvaluation(const fs::path& copy) {
    auto engine = std::make_unique<Engine>(EngineMode::Offline);
    engine->setLiveControl(false);
    if (auto loaded = engine->loadProject(copy); !loaded) {
        return fail("evaluate: the scratch copy does not load: {}", loaded.error().message);
    }
    scene::DetailLimits limits = engine->detailLimits();
    limits.entityDistanceCull = false;
    engine->setDetailLimits(limits);
    return engine;
}

// The adapter's shot list: one entry per camera shot, index-aligned with the saved scene's
// `cameraDirection.shots`, which is how the adapter pairs them.
json shotsDocument(const scene::CameraDirection& direction) {
    json shots = json::array();
    for (std::size_t i = 0; i < direction.shots.size(); ++i) {
        const scene::CameraShot& s = direction.shots[i];
        const scene::CameraRig* rig = direction.find(s.camera);
        json one{{"id", fmt::format("s{:02d}", i + 1)},
                 {"start", s.startSeconds},
                 {"end", s.endSeconds},
                 {"label", s.label.empty() && rig != nullptr ? rig->name : s.label}};
        if (rig != nullptr && !rig->aimNode.empty()) {
            one["subject"] = rig->aimNode;
        } else if (rig != nullptr && !rig->followNode.empty()) {
            one["subject"] = rig->followNode;
        }
        shots.push_back(std::move(one));
    }
    return json{{"shots", std::move(shots)}};
}

Result<void> writeJson(const fs::path& path, const json& doc) {
    std::ofstream out(path);
    if (!out) {
        return fail("cannot write {}", path.string());
    }
    out << doc.dump(1);
    return {};
}

} // namespace

// ---- options ---------------------------------------------------------------------------------------

EvaluatorOptions evaluatorOptionsFrom(const std::optional<fs::path>& critic, const std::optional<std::string>& criticUrl) {
    EvaluatorOptions o;
    o.critic = critic ? *critic : fs::path(envOr("AVGEN_CRITIC"));
    o.criticUrl = criticUrl ? *criticUrl : envOr("AVGEN_CRITIC_URL");
    o.adapter = envOr("AVGEN_CRITIC_ADAPTER");
    o.python = envOr("AVGEN_CRITIC_PYTHON");
    o.avgen = envOr("AVGEN_RENDERER");
    o.castTrace = envOr("AVGEN_CAST_TRACE");
    std::istringstream prefix(envOr("AVGEN_RENDER_PREFIX"));
    for (std::string word; prefix >> word;) {
        o.renderPrefix.push_back(word);
    }
    unsigned w = 0;
    unsigned h = 0;
    if (const std::string size = envOr("AVGEN_EVALUATE_SIZE"); std::sscanf(size.c_str(), "%ux%u", &w, &h) == 2 && w > 0 && h > 0) {
        o.width = w;
        o.height = h;
    }
    return o;
}

Result<EvaluatorOptions> resolveEvaluatorOptions(EvaluatorOptions o) {
    if (o.critic.empty()) {
        return fail("no evaluator is configured: start AV Gen with --critic <path to the Creative Critic's CLI, e.g. "
                    "~/Documents/GitHub/creative-critic/.venv/bin/critic>, or set AVGEN_CRITIC");
    }
    if (!executable(o.critic)) {
        return fail("the Creative Critic's CLI is not at {} (or is not executable): set --critic or AVGEN_CRITIC to it",
                    o.critic.string());
    }
    // `<repo>/.venv/bin/critic`: the repository is three levels up, and it carries the adapter and the
    // Python it was installed into.
    const fs::path bin = o.critic.parent_path();
    const fs::path repo = bin.parent_path().parent_path();
    if (o.python.empty()) {
        o.python = bin / "python";
    }
    if (o.adapter.empty()) {
        o.adapter = repo / "adapters" / "avgen" / "avgen_adapter.py";
    }
    if (!fs::is_regular_file(o.adapter)) {
        return fail("the Critic's AV Gen adapter is not at {}: set AVGEN_CRITIC_ADAPTER", o.adapter.string());
    }
    if (!executable(o.python)) {
        return fail("the Python that runs the Critic's adapter is not at {}: set AVGEN_CRITIC_PYTHON", o.python.string());
    }
    if (o.avgen.empty()) {
        o.avgen = currentExecutable();
    }
    if (!executable(o.avgen)) {
        return fail("the renderer is not at {}: set AVGEN_RENDERER to the avgen binary", o.avgen.string());
    }
    if (o.castTrace.empty()) {
        o.castTrace = o.avgen.parent_path().parent_path() / "tools" / "avgen_cast_trace";
    }
    if (!executable(o.castTrace)) {
        return fail("avgen_cast_trace is not at {}: build it, or set AVGEN_CAST_TRACE", o.castTrace.string());
    }
    for (const std::string& word : o.renderPrefix) {
        if (word.find('/') != std::string::npos && !executable(word)) {
            return fail("the render prefix names {}, which is not an executable (AVGEN_RENDER_PREFIX)", word);
        }
        break; // only the program itself is a path; the rest are its arguments
    }
    return o;
}

// ---- running a child ---------------------------------------------------------------------------------

Result<ProcessOutcome> runProcess(const std::vector<std::string>& argv, const fs::path& workDir,
                                  const std::atomic<bool>* cancel, double timeoutSeconds) {
    if (argv.empty()) {
        return fail("no command to run");
    }
    static std::atomic<std::uint64_t> serial{0};
    const std::uint64_t n = ++serial;
    const fs::path outFile = workDir / fmt::format(".child-{}-{}.out", static_cast<long long>(::getpid()), n);
    const fs::path errFile = workDir / fmt::format(".child-{}-{}.err", static_cast<long long>(::getpid()), n);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, outFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, errFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    std::vector<char*> args;
    args.reserve(argv.size() + 1);
    for (const std::string& a : argv) {
        args.push_back(const_cast<char*>(a.c_str()));
    }
    args.push_back(nullptr);
    pid_t pid = 0;
    const int rc = ::posix_spawnp(&pid, argv[0].c_str(), &actions, nullptr, args.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (rc != 0) {
        return fail("cannot start {}: {}", argv[0], std::strerror(rc));
    }
    ProcessOutcome out;
    const auto begun = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point killAt{};
    bool terminated = false;
    int status = 0;
    for (;;) {
        const pid_t r = ::waitpid(pid, &status, WNOHANG);
        if (r == pid) {
            break;
        }
        if (r < 0 && errno != EINTR) {
            status = -1;
            break;
        }
        const auto now = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(now - begun).count();
        const bool stop = (cancel != nullptr && cancel->load()) || elapsed > timeoutSeconds;
        if (stop && !terminated) {
            out.cancelled = cancel != nullptr && cancel->load();
            out.timedOut = !out.cancelled;
            ::kill(pid, SIGTERM);
            terminated = true;
            killAt = now + std::chrono::seconds(5);
        } else if (terminated && now > killAt) {
            ::kill(pid, SIGKILL);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    out.exitCode = status < 0 ? -1 : WIFEXITED(status) ? WEXITSTATUS(status) : WIFSIGNALED(status) ? 128 + WTERMSIG(status) : -1;
    out.out = readFile(outFile);
    out.err = readFile(errFile);
    std::error_code ec;
    fs::remove(outFile, ec);
    fs::remove(errFile, ec);
    return out;
}

// ---- the Critic --------------------------------------------------------------------------------------

std::vector<std::string> criticSubmitCommand(const EvaluatorOptions& options, const fs::path& inputs, double videoStart,
                                             const std::string& mode, const std::string& track, const std::string& label) {
    std::vector<std::string> argv{options.critic.string()};
    if (!options.criticUrl.empty()) {
        argv.insert(argv.end(), {"--url", options.criticUrl});
    }
    argv.insert(argv.end(), {"submit", "--inputs", inputs.string(), "--video-start", seconds(videoStart), "--mode", mode,
                             "--session", options.session, "--track", track});
    if (!label.empty()) {
        argv.insert(argv.end(), {"--label", label});
    }
    // --wait: block on the event stream until the job ends (no polling). --json: one document on stdout.
    // --strict: a PARTIAL job exits 5, so it can never be read as a full one. --no-autostart: never
    // start an evaluator (and never the default one on :8765) on anybody's behalf.
    argv.insert(argv.end(), {"--wait", "--json", "--strict", "--quiet", "--no-autostart", "--timeout",
                             fmt::format("{:.0f}", options.timeoutSeconds)});
    return argv;
}

Result<directing::EvaluationReport> readCriticOutcome(const ProcessOutcome& outcome, const std::string& criticUrl) {
    const std::string where = criticUrl.empty() ? std::string("its default address ($CRITIC_URL, else http://127.0.0.1:8765)")
                                                : criticUrl;
    if (outcome.cancelled) {
        return fail("the evaluation was cancelled");
    }
    if (outcome.timedOut) {
        return fail("the Critic did not answer in time{}", tail(outcome.err));
    }
    switch (outcome.exitCode) {
    case 0:
    case 5: break;
    case 4:
        return fail("the Creative Critic is not reachable at {} (exit 4): start it with `critic start --daemon`{}", where,
                    tail(outcome.err));
    case 2: return fail("the Critic's job failed (exit 2){}", tail(outcome.err));
    case 3: return fail("the Critic's job was cancelled (exit 3){}", tail(outcome.err));
    default: return fail("the Critic exited {}{}", outcome.exitCode, tail(outcome.err));
    }
    const json submitted = json::parse(outcome.out, nullptr, false);
    if (submitted.is_discarded() || !submitted.is_object()) {
        return fail("the Critic's answer is not the JSON `critic submit --json` prints{}", tail(outcome.out));
    }
    const std::string reportPath = submitted.value("report_json", std::string());
    if (reportPath.empty() || !fs::is_regular_file(reportPath)) {
        return fail("the Critic finished job {} without a report on disk (report_json: '{}')",
                    submitted.value("job_id", std::string("?")), reportPath);
    }
    const json report = json::parse(readFile(reportPath), nullptr, false);
    if (report.is_discarded() || !report.is_object() || report.value("schema", std::string()) != "critic.report/1") {
        return fail("{} is not a critic.report/1 document", reportPath);
    }
    directing::EvaluationReport r = directing::reportFromCritic(report, submitted);
    if (outcome.exitCode == 5) {
        r.partial = true; // --strict said so; the report's own completeness says the same
    }
    return r;
}

// ---- the pipeline ------------------------------------------------------------------------------------

Result<directing::EvaluationReport> evaluateFromCopy(const fs::path& copy, const ai::EvaluationRequest& request,
                                                     const EvaluatorOptions& given, const std::atomic<bool>* cancel,
                                                     const RecordProgress& progress) {
    const auto say = [&](const std::string& phase) {
        if (progress) {
            progress(phase);
        }
    };
    const auto cancelled = [&] { return cancel != nullptr && cancel->load(); };
    auto resolved = resolveEvaluatorOptions(given);
    if (!resolved) {
        return std::unexpected(resolved.error());
    }
    const EvaluatorOptions& options = *resolved;
    const fs::path dir = copy.parent_path() / (copy.stem().string() + "-evaluate");
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        return fail("evaluate: cannot make {}: {}", dir.string(), ec.message());
    }

    // ---- 2. the film the plan would make, on the copy -------------------------------------------------
    say("loading a scratch copy of the project");
    auto scratch = loadForEvaluation(copy);
    if (!scratch) {
        return std::unexpected(scratch.error());
    }
    Engine& engine = **scratch;
    directing::SceneFacts facts = sceneFactsFor(engine);
    directing::PlanParse parsed = directing::parsePlan(request.plan);
    if (!parsed.plan) {
        return fail("evaluate: the plan does not parse");
    }
    std::vector<directing::ItemSpan> spans;
    if (request.installed) {
        directing::Plan plan = *parsed.plan;
        spans = directing::planItemSpans(plan, directing::resolvePlanTimes(plan, facts.music), facts);
    } else {
        say("installing the candidate on the copy");
        const directing::Compilation compiled = directing::compilePlan(*parsed.plan, facts);
        spans = directing::planItemSpans(compiled.plan, compiled.validation.times, facts);
        if (auto r = installCompilation(engine, compiled); !r) {
            return fail("evaluate: the candidate does not install on the copy: {}", r.error().message);
        }
    }
    const fs::path project = dir / "project.json";
    const fs::path scene = dir / "scene.json";
    if (auto r = engine.writeProjectCopy(project); !r) {
        return fail("evaluate: cannot write the candidate's project: {}", r.error().message);
    }
    if (auto r = engine.saveComposition(scene); !r) {
        return fail("evaluate: cannot write the candidate's scene: {}", r.error().message);
    }
    const fs::path shots = dir / "shots.json";
    if (auto r = writeJson(shots, shotsDocument(engine.composition() != nullptr ? engine.composition()->cameraDirection()
                                                                                  : scene::CameraDirection{}));
        !r) {
        return std::unexpected(r.error());
    }
    const std::string range = fmt::format("{}:{}", seconds(request.from), seconds(request.until));
    const auto run = [&](const std::vector<std::string>& argv) -> Result<ProcessOutcome> {
        if (cancelled()) {
            return fail("the evaluation was cancelled");
        }
        auto r = runProcess(argv, dir, cancel, options.timeoutSeconds);
        if (!r) {
            return std::unexpected(r.error());
        }
        if (r->cancelled) {
            return fail("the evaluation was cancelled");
        }
        return r;
    };

    // ---- 3. the clip ------------------------------------------------------------------------------------
    say(fmt::format("rendering {:.2f}-{:.2f} s at {}x{}", request.from, request.until, options.width, options.height));
    const fs::path clip = dir / "clip.mov";
    std::vector<std::string> render = options.renderPrefix;
    render.insert(render.end(), {options.avgen.string(), "--project", project.string(), "--render", clip.string(), "--range",
                                 range, "--size", fmt::format("{}x{}", options.width, options.height)});
    auto rendered = run(render);
    if (!rendered) {
        return std::unexpected(rendered.error());
    }
    if (rendered->exitCode != 0 || !fs::is_regular_file(clip)) {
        return fail("evaluate: the render failed (exit {}; {}){}", rendered->exitCode, commandLine(render), tail(rendered->err));
    }

    // ---- 4. where the bodies were ------------------------------------------------------------------------
    say("tracing the cast");
    const fs::path cast = dir / "cast.json";
    const std::vector<std::string> trace{options.castTrace.string(), "--project", project.string(), "--start",
                                         seconds(request.from), "--seconds", seconds(request.until - request.from),
                                         "--hz", "20", "--out", cast.string()};
    auto traced = run(trace);
    if (!traced) {
        return std::unexpected(traced.error());
    }
    if (traced->exitCode != 0 || !fs::is_regular_file(cast)) {
        return fail("evaluate: the cast trace failed (exit {}){}", traced->exitCode, tail(traced->err));
    }

    // ---- 5. the evaluator's inputs ------------------------------------------------------------------------
    say("writing the evaluator's inputs");
    const fs::path inputsDir = dir / "critic";
    const std::vector<std::string> adapt{options.python.string(), options.adapter.string(), "--project", project.string(),
                                         "--scene", scene.string(), "--shots", shots.string(), "--cast", cast.string(),
                                         "--video", clip.string(), "--range", range, "--width", std::to_string(options.width),
                                         "--height", std::to_string(options.height), "--out", inputsDir.string()};
    auto adapted = run(adapt);
    if (!adapted) {
        return std::unexpected(adapted.error());
    }
    const fs::path inputs = inputsDir / "inputs.json";
    if (adapted->exitCode != 0 || !fs::is_regular_file(inputs)) {
        return fail("evaluate: the Critic's AV Gen adapter failed (exit {}){}", adapted->exitCode, tail(adapted->err));
    }

    // ---- 6-7. the judgement ----------------------------------------------------------------------------------
    say(fmt::format("evaluating ({})", request.mode));
    const std::string track = fmt::format("{}@{:.1f}-{:.1f}", request.planId, request.from, request.until);
    auto judged = run(criticSubmitCommand(options, inputs, request.from, request.mode, track, request.label));
    if (!judged) {
        return std::unexpected(judged.error());
    }
    auto report = readCriticOutcome(*judged, options.criticUrl);
    if (!report) {
        return std::unexpected(report.error());
    }
    report->planId = request.planId;
    report->revision = request.revision;
    report->candidate = request.candidate;
    report->label = request.label;
    report->from = request.from;
    report->until = request.until;
    report->mode = request.mode;
    directing::attributeFindings(*report, spans);
    if (!options.keepFiles) {
        // The clip and the inputs go; the Critic keeps its own copy of what it judged in its job folder.
        fs::remove_all(dir, ec);
    }
    return report;
}

// ---- the hook --------------------------------------------------------------------------------------------

namespace {

class EvaluationHandle final : public ai::DeferredResult {
public:
    ~EvaluationHandle() override {
        cancel_ = true;
        if (thread_.joinable()) {
            thread_.join();
        }
        std::error_code ec;
        fs::remove(copy_, ec);
    }

    Result<void> start(Engine& live, ai::EvaluationRequest request, EvaluatorOptions options) {
        // Refused here, before a copy is written, when there is no evaluator at all: a missing Critic
        // is a clear error, never a silent pass.
        if (auto ok = resolveEvaluatorOptions(options); !ok) {
            return std::unexpected(ok.error());
        }
        auto copy = writeRecordingCopy(live, options.scratchDir);
        if (!copy) {
            return std::unexpected(copy.error());
        }
        copy_ = *copy;
        thread_ = std::thread([this, request = std::move(request), options = std::move(options)] {
            auto r = evaluateFromCopy(copy_, request, options, &cancel_, [this](const std::string& p) {
                std::lock_guard lock(mutex_);
                phase_ = p;
            });
            std::lock_guard lock(mutex_);
            result_ = std::move(r);
            done_ = true;
        });
        return {};
    }
    [[nodiscard]] bool done() const override { return done_.load(); }
    [[nodiscard]] std::string phase() const override {
        std::lock_guard lock(mutex_);
        return phase_;
    }
    [[nodiscard]] Result<nlohmann::json> take() override {
        if (thread_.joinable()) {
            thread_.join();
        }
        std::lock_guard lock(mutex_);
        if (!result_) {
            return fail("the evaluation has not finished");
        }
        if (!*result_) {
            return std::unexpected(result_->error());
        }
        json out = (*result_)->toJson();
        out["summary"] = fmt::format("{}{}", (*result_)->partial ? "PARTIAL: " : "", (*result_)->headline);
        return out;
    }
    void cancel() override { cancel_ = true; }
    void settle(Engine& engine, const nlohmann::json& value) override {
        if (auto report = directing::EvaluationReport::fromJson(value)) {
            engine.directingEvaluations().push_back(std::move(*report));
        }
    }

private:
    std::thread thread_;
    std::atomic<bool> cancel_{false};
    std::atomic<bool> done_{false};
    mutable std::mutex mutex_;
    std::string phase_;
    std::optional<Result<directing::EvaluationReport>> result_;
    fs::path copy_;
};

} // namespace

ai::EvaluationHook makeEvaluationHook(EvaluatorOptions options) {
    return [options](Engine& engine, const ai::EvaluationRequest& request) -> Result<std::shared_ptr<ai::DeferredResult>> {
        auto handle = std::make_shared<EvaluationHandle>();
        if (auto r = handle->start(engine, request, options); !r) {
            return std::unexpected(r.error());
        }
        return handle;
    };
}

} // namespace avgen::app
