#include "api/engine_host.hpp"

#include "app/engine.hpp"
#include "core/hash.hpp"
#include "core/log.hpp"

#include <fmt/format.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <system_error>
#include <thread>
#include <unistd.h>

namespace avgen::api {

namespace fs = std::filesystem;
using nlohmann::json;
using Clock = std::chrono::steady_clock;

// The warnings a job logs on the host thread while it starts and steps (the job's `warnings` in its result). One
// sink for the host's life: spdlog's sink list is not safe to change while another thread logs.
struct JobWarningSink final : spdlog::sinks::base_sink<std::mutex> {
    std::thread::id host;
    std::vector<std::string>* target = nullptr; // set only while a job slice runs (guarded by the sink's mutex)

    void collect(std::vector<std::string>* into) {
        const std::lock_guard lock(mutex_);
        host = std::this_thread::get_id();
        target = into;
    }

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        if (target != nullptr && msg.level >= spdlog::level::warn && std::this_thread::get_id() == host &&
            target->size() < 64) {
            target->emplace_back(msg.payload.data(), msg.payload.size());
        }
    }
    void flush_() override {}
};

struct EngineHost::Pending {
    std::shared_ptr<JobTable> table;
    RenderRequest request;
    std::unique_ptr<GpuRenderJob> job;
    Clock::time_point submitted = Clock::now();
    Clock::time_point loadBegan{};
    Clock::time_point lockWaitBegan{};
    Clock::time_point startBegan{};
    Clock::time_point renderBegan{};
    double loadSeconds = 0.0;
    double lockWaitSeconds = 0.0;
    double startSeconds = 0.0;
    bool lockWaited = false;
    bool lockHeldByAncestor = false;
    std::string lockPath;
    std::string lockHolderSeen;
    bool deviceCreatedHere = false;
    std::vector<std::string> warnings;
    [[nodiscard]] const std::string& id() const { return request.jobId; }
};

namespace {

ai::ToolError jobError(ai::ToolErrorCode code, std::string message, std::string recovery = {}, json details = nullptr) {
    return ai::ToolError{code, std::move(message), std::move(recovery), std::move(details)};
}

double secondsSince(Clock::time_point t) { return std::chrono::duration<double>(Clock::now() - t).count(); }

double lockTimeout(double configured) {
    if (configured >= 0.0) {
        return configured;
    }
    if (const char* env = std::getenv("AVGEN_GPU_LOCK_TIMEOUT"); env != nullptr && env[0] != '\0') {
        char* end = nullptr;
        const double v = std::strtod(env, &end);
        if (end != env && v >= 0.0) {
            return v;
        }
    }
    return 3600.0;
}

json fileEntry(const fs::path& path, std::optional<std::uint64_t> frame) {
    json e{{"path", path.string()}};
    if (frame) {
        e["frame"] = *frame;
    }
    std::error_code ec;
    e["bytes"] = fs::file_size(path, ec);
    if (auto sha = sha256File(path)) {
        e["sha256"] = *sha;
    } else {
        e["sha256"] = nullptr;
    }
    return e;
}

std::string framePath(std::uint64_t index, RenderFormat format) {
    return fmt::format("frame_{:06d}.{}", index, format == RenderFormat::Exr ? "exr" : "png");
}

} // namespace

EngineHost::EngineHost(EngineHostOptions options)
    : options_(std::move(options)), engine_(std::make_unique<app::Engine>(app::EngineMode::Offline)), local_(*engine_) {
    // ADR-2133/2155: an API host opens no OSC port and no MIDI input, whatever a project it opens says. Nothing
    // here or in the clients calls setLiveOsc.
    engine_->setLiveControl(false);
    // ADR-2154: Stepped by default; nothing advances until a client asks (transport.step).
    const app::TimePolicy policy = options_.timePolicy.value_or(app::TimePolicy::Stepped);
    if (auto r = engine_->setTimePolicy(policy); !r) {
        log::warn("engine host: time policy refused ({}); keeping '{}'", r.error().message,
                  app::timePolicyName(engine_->timePolicy()));
    }
    local_.setOfferedTimePolicies({app::TimePolicy::Stepped, app::TimePolicy::Offline});
    if (options_.renderBackend) {
        warnings_ = std::make_shared<JobWarningSink>();
        spdlog::default_logger()->sinks().push_back(warnings_);
    }
}

EngineHost::~EngineHost() {
    stop();
    // Every waiter is released before the engine goes: a client thread blocked in Session::call must not wait
    // on a pump that is never coming.
    local_.queue().shutdown();
    // The jobs go before the backend that owns their device, and the lock goes with them.
    active_.reset();
    pending_.clear();
    backend_.reset();
    lock_.reset();
    if (warnings_) {
        auto& sinks = spdlog::default_logger()->sinks();
        sinks.erase(std::remove(sinks.begin(), sinks.end(), std::static_pointer_cast<spdlog::sinks::sink>(warnings_)),
                    sinks.end());
    }
}

void EngineHost::bindHostThread() { local_.queue().bindToCurrentThread(); }

void EngineHost::run(Session& session) {
    ai::MainThreadQueue& q = local_.queue();
    q.bindToCurrentThread();
    while (!stopping()) {
        session.pump();
        // ADR-2159: one job slice between pumps. A job waiting for the machine's GPU lock is polled every quarter
        // second; an idle host sleeps until a call arrives.
        const bool jobsLeft = stepJobs();
        if (stopping()) {
            break;
        }
        if (q.pending() == 0) {
            if (!jobsLeft) {
                q.waitForWork(); // a call queued, stop() or the queue shut down; never a timer
            } else if (waitingForLock_) {
                q.waitForWork(std::chrono::milliseconds{250});
            }
        }
    }
}

// ---- the GPU half (ADR-2159) ------------------------------------------------------------------------

bool EngineHost::canRender() { return static_cast<bool>(options_.renderBackend) && !deviceFailed_; }

EngineHostJobStats EngineHost::jobStats() const {
    EngineHostJobStats s;
    s.queued = pending_.size();
    s.running = active_ != nullptr;
    s.deviceCreated = backend_ && backend_->deviceCreated();
    s.lockHeld = lock_ && lock_->held();
    return s;
}

std::optional<ai::ToolError> EngineHost::submitJob(JobSubmission submission) {
    if (!canRender()) {
        return jobError(ai::ToolErrorCode::Unavailable,
                        deviceFailed_ ? "this host could not create a GPU device" : "this host has no GPU half",
                        "run it in avgen serve or avgen call on a machine with a GPU", json{{"field", "gpu"}});
    }
    const std::size_t inFlight = pending_.size() + (active_ ? 1 : 0);
    if (inFlight >= options_.queueCapacity) {
        return jobError(ai::ToolErrorCode::Busy,
                        fmt::format("the render queue is full: {} job(s) queued or running (capacity {})", inFlight,
                                    options_.queueCapacity),
                        "job.wait for one of them, or job.cancel one, then submit again",
                        json{{"field", "queue"}, {"capacity", options_.queueCapacity}});
    }
    if (!backend_) {
        backend_ = options_.renderBackend();
        if (!backend_) {
            deviceFailed_ = true;
            return jobError(ai::ToolErrorCode::Unavailable, "this host's GPU half could not be made");
        }
    }
    auto p = std::make_unique<Pending>();
    p->table = std::move(submission.table);
    p->request = std::move(submission.request);
    log::info("api: {} queued ({} ahead of it)", p->id(), inFlight);
    pending_.push_back(std::move(p));
    local_.queue().wake();
    return std::nullopt;
}

void EngineHost::cancelJobs(const JobTable& table) {
    const auto mine = [&](const std::unique_ptr<Pending>& p) { return p && p->table.get() == &table; };
    for (const auto& p : pending_) {
        if (mine(p)) {
            p->table->requestCancel(p->id());
        }
    }
    if (mine(active_)) {
        active_->table->requestCancel(active_->id());
        active_->job->cancel();
    }
}

void EngineHost::failJob(Pending& job, ai::ToolError error) {
    log::warn("api: {} failed: {}", job.id(), error.message);
    if (lock_) {
        lock_->release();
    }
    job.table->update(job.id(), [&](JobRecord& r) {
        r.state = JobState::Failed;
        r.phase = "failed";
        r.error = std::move(error);
        r.result = json{{"warnings", job.warnings}};
    });
}

bool EngineHost::stepJobs() {
    if (inStep_ || !backend_) {
        return active_ != nullptr || !pending_.empty();
    }
    inStep_ = true;
    struct Reset {
        bool& flag;
        ~Reset() { flag = false; }
    } reset{inStep_};
    waitingForLock_ = false;

    if (!active_) {
        // Jobs cancelled while queued never start.
        while (!pending_.empty() && pending_.front()->table->cancelRequested(pending_.front()->id())) {
            Pending& p = *pending_.front();
            if (lock_) {
                lock_->release();
            }
            p.table->update(p.id(), [](JobRecord& r) {
                r.state = JobState::Cancelled;
                r.phase = "cancelled before it started";
            });
            pending_.pop_front();
        }
        if (pending_.empty()) {
            return false;
        }
        Pending& p = *pending_.front();
        if (warnings_) {
            warnings_->collect(&p.warnings);
        }
        struct StopCollecting {
            JobWarningSink* sink;
            ~StopCollecting() {
                if (sink != nullptr) {
                    sink->collect(nullptr);
                }
            }
        } stopCollecting{warnings_.get()};

        // PREPARING, CPU half: a fresh Offline engine from the snapshot. Before the lock: the GPU is not involved.
        if (!p.job) {
            p.loadBegan = Clock::now();
            p.table->update(p.id(), [](JobRecord& r) {
                r.state = JobState::Preparing;
                r.phase = "loading the project snapshot";
            });
            auto made = backend_->create(p.request);
            p.loadSeconds = secondsSince(p.loadBegan);
            if (!made) {
                failJob(p, jobError(ai::ToolErrorCode::FailedPrecondition,
                                    fmt::format("the render could not load its snapshot: {}", made.error().message),
                                    "project.validate reports what the project needs"));
                pending_.pop_front();
                return !pending_.empty();
            }
            p.job = std::move(*made);
            p.lockWaitBegan = Clock::now();
        }

        // The machine's GPU lock, before the device is touched.
        if (!lock_) {
            lock_ = std::make_unique<GpuLock>(options_.gpuLockPath);
        }
        const GpuLockAttempt attempt = lock_->tryAcquire();
        p.lockPath = lock_->path().string();
        if (attempt == GpuLockAttempt::Busy) {
            p.lockWaited = true;
            const double waited = secondsSince(p.lockWaitBegan);
            const auto& holder = lock_->lastHolder();
            p.lockHolderSeen = holder ? fmt::format("'{}' (pid {})", holder->holder, holder->pid) : "unknown";
            const double limit = lockTimeout(options_.gpuLockTimeoutSeconds);
            if (waited > limit) {
                failJob(p, jobError(ai::ToolErrorCode::Busy,
                                    fmt::format("timed out after {:.0f} s waiting for the machine's GPU lock '{}', held "
                                                "by {}",
                                                waited, p.lockPath, p.lockHolderSeen),
                                    "retry when the GPU is free (tools/gpu-lock.sh serialises GPU work)",
                                    json{{"field", "gpuLock"}, {"path", p.lockPath}}));
                pending_.pop_front();
                return !pending_.empty();
            }
            const std::string phase = fmt::format("waiting for the GPU lock (held by {})", p.lockHolderSeen);
            p.table->update(p.id(), [&](JobRecord& r) {
                r.state = JobState::Preparing;
                r.phase = phase;
            });
            waitingForLock_ = true;
            return true;
        }
        p.lockWaitSeconds = secondsSince(p.lockWaitBegan);
        p.lockHeldByAncestor = attempt == GpuLockAttempt::HeldByAncestor;
        if (p.lockHeldByAncestor && lock_->lastHolder()) {
            p.lockHolderSeen = fmt::format("'{}' (pid {})", lock_->lastHolder()->holder, lock_->lastHolder()->pid);
        }

        // The device, lazily, under the lock.
        p.startBegan = Clock::now();
        const bool hadDevice = backend_->deviceCreated();
        if (!hadDevice) {
            p.table->update(p.id(), [](JobRecord& r) { r.phase = "creating the GPU device"; });
        }
        if (auto r = backend_->ensureDevice(); !r) {
            deviceFailed_ = true;
            failJob(p, jobError(ai::ToolErrorCode::Unavailable,
                                fmt::format("no GPU device could be created: {}", r.error().message),
                                "this machine or session has no usable GPU"));
            pending_.pop_front();
            return !pending_.empty();
        }
        p.deviceCreatedHere = !hadDevice;
        p.table->update(p.id(), [](JobRecord& r) { r.phase = "compiling pipelines and warming up"; });
        // While the start waits for a compile: say how far it has got, and keep answering calls.
        Pending* raw = &p;
        auto started = p.job->start([this, raw](const CompileProgress& c) {
            const std::string phase = "compiling pipelines";
            const std::string message = fmt::format("{} of {} pipeline(s) compiled{}{}", c.landed, c.started,
                                                    c.waitingFor.empty() ? "" : "; waiting for ", c.waitingFor);
            raw->table->update(raw->id(), [&](JobRecord& r) {
                r.phase = phase + ": " + message;
            });
            local_.queue().pump(ai::MainThreadQueue::kDefaultBudgetMs);
        });
        p.startSeconds = secondsSince(p.startBegan);
        if (!started) {
            failJob(p, jobError(ai::ToolErrorCode::Internal,
                                fmt::format("the render did not start: {}", started.error().message),
                                "the host log has the renderer's error"));
            pending_.pop_front();
            return !pending_.empty();
        }
        active_ = std::move(pending_.front());
        pending_.pop_front();
        active_->renderBegan = Clock::now();
        active_->table->update(active_->id(), [](JobRecord& r) {
            r.state = JobState::Running;
            r.phase = "rendering";
        });
        return true;
    }

    // RUNNING: one slice.
    Pending& a = *active_;
    if (warnings_) {
        warnings_->collect(&a.warnings);
    }
    if (a.table->cancelRequested(a.id())) {
        a.job->cancel();
    }
    const bool done = a.job->step(options_.sliceMs / 1000.0);
    if (warnings_) {
        warnings_->collect(nullptr);
    }
    const GpuJobProgress g = a.job->progress();
    const std::uint64_t work = g.historyTotal + g.framesTotal;
    a.table->update(a.id(), [&](JobRecord& r) {
        r.framesDone = g.framesDone;
        r.framesTotal = g.framesTotal;
        r.progress = work == 0 ? 0.0
                               : static_cast<double>(g.historyDone + g.framesDone) / static_cast<double>(work);
        r.phase = g.historyDone < g.historyTotal
                      ? fmt::format("playing history: {} of {} frame(s) before the range", g.historyDone, g.historyTotal)
                      : "rendering";
    });
    if (done) {
        finishActive();
    }
    return active_ != nullptr || !pending_.empty();
}

void EngineHost::finishActive() {
    std::unique_ptr<Pending> owned = std::move(active_);
    Pending& a = *owned;
    const GpuJobOutcome out = a.job->outcome();
    const GpuJobProgress g = a.job->progress();
    const double renderSeconds = secondsSince(a.renderBegan);
    // The job's GPU work is over (finish() drained the ring): the next job, or another process, may have the GPU.
    lock_->release();

    const RenderRequest& q = a.request;
    json outputs = json::array();
    std::error_code ec;
    if (q.format == RenderFormat::Video) {
        if (fs::exists(q.file, ec)) {
            outputs.push_back(fileEntry(q.file, std::nullopt));
        }
    } else if (q.capture) {
        const fs::path staged = q.outputDir / framePath(0, q.format);
        if (out.error.empty() && !out.cancelled && fs::exists(staged, ec)) {
            fs::create_directories(q.file.parent_path(), ec);
            fs::rename(staged, q.file, ec);
            if (ec) { // across volumes: copy, then drop the staged one
                ec.clear();
                fs::copy_file(staged, q.file, fs::copy_options::overwrite_existing, ec);
                std::error_code ignored;
                fs::remove(staged, ignored);
            }
            if (!ec) {
                outputs.push_back(fileEntry(q.file, std::uint64_t{0}));
            }
        }
        std::error_code ignored;
        fs::remove_all(q.outputDir, ignored);
    } else {
        for (std::uint64_t i = 0; i < g.framesTotal; ++i) {
            const fs::path f = q.outputDir / framePath(i, q.format);
            if (fs::exists(f, ec)) {
                outputs.push_back(fileEntry(f, i));
            }
        }
    }
    json hashes = json::array();
    for (const std::uint64_t h : out.frameHashes) {
        hashes.push_back(hashHex(h));
    }
    const double total = secondsSince(a.submitted);
    json result{
        {"kind", q.capture ? "capture" : "sequence"},
        {"format", renderFormatName(q.format)},
        {"history", renderHistoryName(q.history)},
        {"range", json{{"start", q.startSeconds}, {"end", q.endSeconds}, {"fps", q.fps}, {"frames", g.framesTotal}}},
        {"size", json{{"width", q.width}, {"height", q.height}}},
        {"frames", out.frameHashes.size()},
        {"framesWritten", out.framesWritten},
        {"historyFrames", g.historyTotal},
        {"frameHashes", std::move(hashes)},
        {"sequenceHash", hashHex(out.sequenceHash)},
        {"frameHash", out.frameHashes.empty() ? json(nullptr) : json(hashHex(out.frameHashes.back()))},
        {"outputs", outputs},
        {"snapshot", q.project.string()},
        {"timing",
         json{{"queuedSeconds", std::chrono::duration<double>(a.loadBegan - a.submitted).count()},
              {"loadSeconds", a.loadSeconds},
              {"lockWaitSeconds", a.lockWaitSeconds},
              {"startSeconds", a.startSeconds},
              {"renderSeconds", renderSeconds},
              {"totalSeconds", total},
              {"deviceCreated", a.deviceCreatedHere},
              {"pipelinesCreated", out.pipelinesCreated}}},
        {"lock",
         json{{"path", a.lockPath},
              {"waited", a.lockWaited},
              {"waitedSeconds", a.lockWaitSeconds},
              {"heldByAncestor", a.lockHeldByAncestor},
              {"holderSeen", a.lockHolderSeen.empty() ? json(nullptr) : json(a.lockHolderSeen)}}},
        {"gpuErrors", out.gpuErrors},
        {"warnings", a.warnings}};
    const JobState state = !out.error.empty() ? JobState::Failed
                           : out.cancelled    ? JobState::Cancelled
                                              : JobState::Succeeded;
    log::info("api: {} {} in {:.2f} s ({} frame(s), sequence hash {})", a.id(), jobStateName(state), total,
              out.frameHashes.size(), hashHex(out.sequenceHash));
    a.table->update(a.id(), [&](JobRecord& r) {
        r.state = state;
        r.phase = jobStateName(state);
        r.outputs = outputs;
        r.framesDone = g.framesDone;
        r.result = result;
        if (state == JobState::Succeeded) {
            r.progress = 1.0;
        }
        if (state == JobState::Failed) {
            r.error = jobError(ai::ToolErrorCode::Internal, out.error, "the host log has the renderer's error");
        }
    });
}

void EngineHost::stop() {
    stop_.store(true, std::memory_order_release);
    local_.queue().wake();
}

// ---- flags -------------------------------------------------------------------------------------------

Result<bool> parseHostFlag(int argc, char** argv, int& i, HostFlags& flags) {
    const std::string_view a = argv[i];
    if (a == "--allow-subprocess") {
        flags.permissions.subprocess = true;
        return true;
    }
    const bool read = a == "--read-root";
    const bool write = a == "--write-root";
    const bool both = a == "--root";
    if (!read && !write && !both) {
        return false;
    }
    if (i + 1 >= argc) {
        return fail("{} needs a directory", a);
    }
    std::error_code ec;
    const fs::path given = argv[++i];
    const fs::path root = fs::weakly_canonical(fs::absolute(given, ec), ec);
    if (ec || !fs::is_directory(root, ec)) {
        return fail("{} '{}' is not a directory", a, given.string());
    }
    // --root is both: a read root, so the listings (`asset.list_importable`, `world.list_recipes`) walk it, and a
    // write root, so a bare project name lands in it.
    if (read || both) {
        flags.permissions.readRoots.push_back(root);
    }
    if (write || both) {
        flags.permissions.writeRoots.push_back(root);
    }
    return true;
}

Result<int> protectStdout() {
    std::fflush(stdout);
    const int protocol = ::dup(STDOUT_FILENO);
    if (protocol < 0) {
        return fail("cannot duplicate stdout for the protocol: {}", std::error_code(errno, std::generic_category()).message());
    }
    if (::dup2(STDERR_FILENO, STDOUT_FILENO) < 0) {
        ::close(protocol);
        return fail("cannot point stdout at stderr: {}", std::error_code(errno, std::generic_category()).message());
    }
    return protocol;
}

void restoreStdout(int protocolFd) {
    std::fflush(stdout);
    (void)::dup2(protocolFd, STDOUT_FILENO);
    ::close(protocolFd);
}

} // namespace avgen::api
