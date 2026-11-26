#include "api/engine_host.hpp"

#include "app/engine.hpp"
#include "core/hash.hpp"
#include "core/log.hpp"

#include <fmt/format.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
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
    // When the job was accepted, on the file system's clock: a frame file older than this is not this job's
    // output, even if it has this job's name (a reused output folder).
    fs::file_time_type acceptedOnDisk = fs::file_time_type::clock::now();
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
    // ADR-2165: the operator's own job (Studio's Render panel and queue, headless --render/--queue): its CPU half
    // is `prepare`, it does not count against an API client's capacity, and it may wait HELD for Run queue.
    std::function<Result<std::unique_ptr<GpuRenderJob>>()> prepare;
    bool local = false;
    bool held = false;
    bool lockTaken = false;
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

std::string heldForShowPhase(const std::string& reasons) { return "held: show active (" + reasons + ")"; }

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

EngineHost::EngineHost(app::Engine& borrowed, EngineHostOptions options)
    : options_(std::move(options)), local_(borrowed) {
    // ADR-2165: the engine is the owner's -- its live control, its time policy and the policies its loop honours
    // (LocalHost's default: the engine's own setting, until the owner offers more).
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
    for (const auto& p : pending_) {
        s.held += p->held ? 1 : 0;
    }
    s.running = active_ != nullptr;
    s.deviceCreated = backend_ && backend_->deviceCreated();
    s.lockHeld = lock_ && lock_->held();
    s.pausedForShow = heldForShow_ && active_ != nullptr;
    return s;
}

std::optional<ShowLockState> EngineHost::showLock() { return showLock_; }

bool EngineHost::jobInLoop() {
    if (!backend_ || (active_ == nullptr && pending_.empty())) {
        return false;
    }
    if (local_.showState().active) {
        return false;
    }
    if (active_) {
        return true;
    }
    return std::any_of(pending_.begin(), pending_.end(), [](const std::unique_ptr<Pending>& p) { return !p->held; });
}

Result<void> EngineHost::ensureBackend() {
    if (backend_) {
        return {};
    }
    if (!options_.renderBackend) {
        return fail("this host has no GPU half");
    }
    backend_ = options_.renderBackend();
    if (!backend_) {
        deviceFailed_ = true;
        return fail("this host's GPU half could not be made");
    }
    return {};
}

Result<RenderBackend*> EngineHost::ensureDevice() {
    if (auto r = ensureBackend(); !r) {
        return std::unexpected(r.error());
    }
    if (auto r = backend_->ensureDevice(); !r) {
        deviceFailed_ = true;
        return std::unexpected(r.error());
    }
    return backend_.get();
}

void EngineHost::ensureLock() {
    if (!lock_) {
        lock_ = std::make_unique<GpuLock>(options_.gpuLockPath);
    }
}

void EngineHost::releaseJobLock() {
    jobHoldsLock_ = false;
    if (!lock_) {
        return;
    }
    if (showLock_.showActive && showLock_.held) {
        // ADR-2167: the show still holds it. Say so in the lock's own `holder`, for whoever waits.
        lock_->setHolderName(options_.showLockHolder);
        return;
    }
    lock_->release();
}

std::optional<ai::ToolError> EngineHost::submitJob(JobSubmission submission) {
    if (!canRender()) {
        return jobError(ai::ToolErrorCode::Unavailable,
                        deviceFailed_ ? "this host could not create a GPU device" : "this host has no GPU half",
                        "run it in avgen serve or avgen call on a machine with a GPU", json{{"field", "gpu"}});
    }
    // ADR-2167: no GPU job starts while a show runs -- whoever asks, the Studio assistant included.
    if (const ShowState show = showState(); show.active) {
        json details{{"field", "show"}, {"reasons", show.reasonsJson()}};
        if (show.remainingSeconds && *show.remainingSeconds > 0.0) {
            details["retryAfterMs"] = static_cast<std::int64_t>(std::ceil(*show.remainingSeconds * 1000.0));
        }
        return jobError(ai::ToolErrorCode::Busy,
                        fmt::format("a show is running ({}): no GPU job starts until it stops", show.reasonsText()),
                        "retry when the show stops", std::move(details));
    }
    // The operator's own jobs (a render queue in Studio) do not count against an API client's capacity.
    const auto apiInFlight = static_cast<std::size_t>(
        std::count_if(pending_.begin(), pending_.end(), [](const std::unique_ptr<Pending>& p) { return !p->local; }) +
        (active_ && !active_->local ? 1 : 0));
    if (apiInFlight >= options_.queueCapacity) {
        return jobError(ai::ToolErrorCode::Busy,
                        fmt::format("the render queue is full: {} job(s) queued or running (capacity {})", apiInFlight,
                                    options_.queueCapacity),
                        "job.wait for one of them, or job.cancel one, then submit again",
                        json{{"field", "queue"}, {"capacity", options_.queueCapacity}});
    }
    if (auto r = ensureBackend(); !r) {
        return jobError(ai::ToolErrorCode::Unavailable, r.error().message);
    }
    const std::size_t inFlight = pending_.size() + (active_ ? 1 : 0);
    auto p = std::make_unique<Pending>();
    p->table = std::move(submission.table);
    p->request = std::move(submission.request);
    log::info("api: {} queued ({} ahead of it)", p->id(), inFlight);
    pending_.push_back(std::move(p));
    local_.queue().wake();
    return std::nullopt;
}

std::optional<ai::ToolError> EngineHost::submitLocal(LocalJobSubmission submission) {
    if (!canRender()) {
        return jobError(ai::ToolErrorCode::Unavailable,
                        deviceFailed_ ? "this host could not create a GPU device" : "this host has no GPU half");
    }
    if (!submission.table || !submission.prepare) {
        return jobError(ai::ToolErrorCode::Internal, "a local job needs a record and a prepare step");
    }
    if (auto r = ensureBackend(); !r) {
        return jobError(ai::ToolErrorCode::Unavailable, r.error().message);
    }
    auto p = std::make_unique<Pending>();
    p->table = std::move(submission.table);
    p->request.jobId = submission.jobId;
    p->prepare = std::move(submission.prepare);
    p->local = true;
    p->held = submission.held;
    if (p->held) {
        const std::string phase = submission.heldPhase.empty() ? std::string("held") : submission.heldPhase;
        p->table->update(p->id(), [&](JobRecord& r) { r.phase = phase; });
    }
    pending_.push_back(std::move(p));
    local_.queue().wake();
    return std::nullopt;
}

bool EngineHost::releaseJob(const JobTable& table, const std::string& id) {
    for (const auto& p : pending_) {
        if (p->table.get() == &table && p->id() == id && p->held) {
            p->held = false;
            p->table->update(p->id(), [&](JobRecord& r) { r.phase = heldForShow_ ? heldForShowPhase(heldForShowReasons_)
                                                                                    : std::string("queued"); });
            local_.queue().wake();
            return true;
        }
    }
    return false;
}

bool EngineHost::cancelJob(const JobTable& table, const std::string& id) {
    if (active_ && active_->table.get() == &table && active_->id() == id) {
        active_->table->requestCancel(id);
        active_->job->cancel();
        return true;
    }
    for (const auto& p : pending_) {
        if (p->table.get() == &table && p->id() == id) {
            return p->table->requestCancel(id);
        }
    }
    return false;
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
    log::warn("{}: {} failed: {}", job.local ? "render" : "api", job.id(), error.message);
    releaseJobLock();
    job.table->update(job.id(), [&](JobRecord& r) {
        r.state = JobState::Failed;
        r.phase = "failed";
        r.error = std::move(error);
        r.result = json{{"warnings", job.warnings}};
    });
}

void EngineHost::holdForShow(const ShowState& show) {
    const std::string reasons = show.reasonsText();
    if (heldForShow_ && reasons == heldForShowReasons_) {
        return;
    }
    if (!heldForShow_ && (active_ || !pending_.empty())) {
        log::info("jobs: held while the show runs ({}): {} waiting, {} paused", reasons, pending_.size(),
                  active_ ? 1 : 0);
    }
    heldForShow_ = true;
    heldForShowReasons_ = reasons;
    const std::string held = heldForShowPhase(reasons);
    for (const auto& p : pending_) {
        if (!p->held) {
            p->table->update(p->id(), [&](JobRecord& r) { r.phase = held; });
        }
    }
    if (active_) {
        active_->table->update(active_->id(), [&](JobRecord& r) { r.phase = "paused: show active (" + reasons + ")"; });
    }
}

void EngineHost::resumeAfterShow() {
    if (!heldForShow_) {
        return;
    }
    heldForShow_ = false;
    heldForShowReasons_.clear();
    if (active_ || !pending_.empty()) {
        log::info("jobs: the show stopped; {} waiting, {} resuming", pending_.size(), active_ ? 1 : 0);
    }
    for (const auto& p : pending_) {
        if (!p->held) {
            p->table->update(p->id(), [](JobRecord& r) { r.phase = "queued"; });
        }
    }
    if (active_) {
        active_->table->update(active_->id(), [](JobRecord& r) { r.phase = "rendering"; });
    }
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

    // Jobs cancelled while queued (or held) never start.
    for (auto it = pending_.begin(); it != pending_.end();) {
        Pending& p = **it;
        if (!p.table->cancelRequested(p.id())) {
            ++it;
            continue;
        }
        if (p.lockTaken) {
            releaseJobLock();
        }
        p.table->update(p.id(), [](JobRecord& r) {
            r.state = JobState::Cancelled;
            r.phase = "cancelled before it started";
        });
        it = pending_.erase(it);
    }

    // ADR-2167: nothing starts and nothing renders while a show runs. A running job keeps its place and resumes
    // after the frame it had rendered; a cancel still reaches it (it stops at once: it is between frames).
    if (const ShowState show = showState(); show.active) {
        holdForShow(show);
        if (active_ && active_->table->cancelRequested(active_->id())) {
            active_->job->cancel();
            (void)active_->job->step(0.0);
            finishActive();
        }
        return active_ != nullptr || !pending_.empty();
    }
    resumeAfterShow();

    if (!active_) {
        // The first job that may start: FIFO order, skipping the operator's held queue entries.
        const auto next = std::find_if(pending_.begin(), pending_.end(),
                                       [](const std::unique_ptr<Pending>& p) { return !p->held; });
        if (next == pending_.end()) {
            return !pending_.empty();
        }
        Pending& p = **next;
        const auto drop = [&] {
            pending_.erase(std::find_if(pending_.begin(), pending_.end(),
                                        [&](const std::unique_ptr<Pending>& q) { return q.get() == &p; }));
        };
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
            auto made = p.prepare ? p.prepare() : backend_->create(p.request);
            p.loadSeconds = secondsSince(p.loadBegan);
            if (!made) {
                // The operator's job reports the loader's own words, as the Render panel and --render always did.
                failJob(p, p.local ? jobError(ai::ToolErrorCode::FailedPrecondition, made.error().message)
                                   : jobError(ai::ToolErrorCode::FailedPrecondition,
                                              fmt::format("the render could not load its snapshot: {}",
                                                          made.error().message),
                                              "project.validate reports what the project needs"));
                drop();
                return !pending_.empty();
            }
            p.job = std::move(*made);
            p.lockWaitBegan = Clock::now();
        }

        // The machine's GPU lock, before the device is touched.
        if (!p.lockTaken) {
            const auto now = Clock::now();
            if (p.lockWaited && now < nextLockPoll_) {
                waitingForLock_ = true;
                return true;
            }
            ensureLock();
            if (!lock_->ownsDirectory()) {
                lock_->setHolderName(GpuLock::defaultHolder()); // a job's own name, not a past show's
            }
            const GpuLockAttempt attempt = lock_->tryAcquire();
            p.lockPath = lock_->path().string();
            if (attempt == GpuLockAttempt::Busy) {
                p.lockWaited = true;
                nextLockPoll_ = now + std::chrono::duration_cast<Clock::duration>(
                                          std::chrono::duration<double>(options_.gpuLockPollSeconds));
                const double waited = secondsSince(p.lockWaitBegan);
                const auto& holder = lock_->lastHolder();
                p.lockHolderSeen = holder ? fmt::format("'{}' (pid {})", holder->holder, holder->pid) : "unknown";
                const double limit = lockTimeout(options_.gpuLockTimeoutSeconds);
                if (waited > limit) {
                    failJob(p, jobError(ai::ToolErrorCode::Busy,
                                        fmt::format("timed out after {:.0f} s waiting for the machine's GPU lock '{}', "
                                                    "held by {}",
                                                    waited, p.lockPath, p.lockHolderSeen),
                                        "retry when the GPU is free (tools/gpu-lock.sh serialises GPU work)",
                                        json{{"field", "gpuLock"}, {"path", p.lockPath}}));
                    drop();
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
            p.lockTaken = true;
            jobHoldsLock_ = true;
            p.lockWaitSeconds = secondsSince(p.lockWaitBegan);
            p.lockHeldByAncestor = attempt == GpuLockAttempt::HeldByAncestor;
            if (p.lockHeldByAncestor && lock_->lastHolder()) {
                p.lockHolderSeen = fmt::format("'{}' (pid {})", lock_->lastHolder()->holder, lock_->lastHolder()->pid);
            }
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
            drop();
            return !pending_.empty();
        }
        p.deviceCreatedHere = !hadDevice;
        p.table->update(p.id(), [](JobRecord& r) { r.phase = "compiling pipelines and warming up"; });
        // While the start waits for a compile: say how far it has got, and (unless the host's own hook keeps it
        // alive) keep answering calls.
        Pending* raw = &p;
        const bool pump = options_.pumpWhileCompiling;
        auto started = p.job->start([this, raw, pump](const CompileProgress& c) {
            const std::string phase = "compiling pipelines";
            const std::string message = fmt::format("{} of {} pipeline(s) compiled{}{}", c.landed, c.started,
                                                    c.waitingFor.empty() ? "" : "; waiting for ", c.waitingFor);
            raw->table->update(raw->id(), [&](JobRecord& r) {
                r.phase = phase + ": " + message;
            });
            if (pump) {
                local_.queue().pump(ai::MainThreadQueue::kDefaultBudgetMs);
            }
        });
        p.startSeconds = secondsSince(p.startBegan);
        if (!started) {
            failJob(p, p.local ? jobError(ai::ToolErrorCode::Internal, started.error().message)
                               : jobError(ai::ToolErrorCode::Internal,
                                          fmt::format("the render did not start: {}", started.error().message),
                                          "the host log has the renderer's error"));
            drop();
            return !pending_.empty();
        }
        for (auto it = pending_.begin(); it != pending_.end(); ++it) {
            if (it->get() == &p) {
                active_ = std::move(*it);
                pending_.erase(it);
                break;
            }
        }
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
    releaseJobLock();

    const RenderRequest& q = a.request;
    json outputs = json::array();
    std::error_code ec;
    if (a.local) {
        // The operator's job writes where its render settings say; its RenderJob lists that, not this record.
    } else if (q.format == RenderFormat::Video) {
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
        // Only the frames THIS job rendered (hashed) and wrote (on disk since it was accepted). A cancelled or
        // failed sequence in a reused folder must not list, with their sha256s, frames an earlier render left there.
        const std::uint64_t rendered = std::min<std::uint64_t>(g.framesTotal, out.frameHashes.size());
        for (std::uint64_t i = 0; i < rendered; ++i) {
            const fs::path f = q.outputDir / framePath(i, q.format);
            if (fs::exists(f, ec) && fs::last_write_time(f, ec) >= a.acceptedOnDisk && !ec) {
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
    log::info("{}: {} {} in {:.2f} s ({} frame(s), sequence hash {})", a.local ? "render" : "api", a.id(),
              jobStateName(state), total, out.frameHashes.size(), hashHex(out.sequenceHash));
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
            r.error = a.local ? jobError(ai::ToolErrorCode::Internal, out.error)
                              : jobError(ai::ToolErrorCode::Internal, out.error, "the host log has the renderer's error");
        }
    });
}

// ---- the show (ADR-2167) ----------------------------------------------------------------------------

const ShowLockState& EngineHost::updateShow() {
    const ShowState show = showState();
    const auto now = Clock::now();
    if (show.active && !showLock_.showActive) {
        showLock_ = ShowLockState{};
        showLock_.showActive = true;
        showRetryAt_ = now;
        log::info("show: started ({})", show.reasonsText());
    } else if (!show.active && showLock_.showActive) {
        const bool held = showLock_.held;
        const ShowLockState ended = showLock_;
        showLock_ = ShowLockState{};
        if (held && lock_) {
            if (jobHoldsLock_) {
                lock_->setHolderName(GpuLock::defaultHolder()); // the paused job keeps it, under its own name
            } else {
                lock_->release();
            }
        }
        log::info("show: ended; the machine GPU lock {} ({} attempt(s), the dearest {:.2f} ms{})",
                  held ? "released" : "was never ours", ended.attempts, ended.maxAttemptMs,
                  ended.contended ? fmt::format(", shared with '{}' (pid {})", ended.holder, ended.holderPid) : "");
        return showLock_;
    }
    if (!show.active || showLock_.held || now < showRetryAt_) {
        return showLock_;
    }
    // One round of the lock's protocol, never a wait: a show cannot wait for an agent's test suite.
    ensureLock();
    const auto t0 = Clock::now();
    lock_->setHolderName(options_.showLockHolder); // written by the acquire, or in place over a paused job's
    const GpuLockAttempt attempt = lock_->tryAcquire();
    const double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    ++showLock_.attempts;
    showLock_.lastAttemptMs = ms;
    showLock_.maxAttemptMs = std::max(showLock_.maxAttemptMs, ms);
    showLock_.path = lock_->path().string();
    if (attempt != GpuLockAttempt::Busy) {
        const bool wasContended = showLock_.contended;
        showLock_.held = true;
        showLock_.contended = false;
        if (const auto& h = lock_->lastHolder(); attempt == GpuLockAttempt::HeldByAncestor && h) {
            showLock_.holderPid = h->pid;
            showLock_.holder = h->holder;
        } else {
            showLock_.holderPid = 0;
            showLock_.holder.clear();
        }
        log::info("show: holding the machine GPU lock {} ({}{}; {:.2f} ms)", showLock_.path,
                  attempt == GpuLockAttempt::Acquired ? "acquired" : "held by the process that started this one",
                  wasContended ? ", after sharing the GPU" : "", ms);
        return showLock_;
    }
    const auto& h = lock_->lastHolder();
    const long pid = h ? h->pid : 0;
    const std::string name = h ? h->holder : std::string("unknown");
    if (!showLock_.contended || pid != showLock_.holderPid) {
        log::warn("show: the machine GPU lock {} is held by '{}' (pid {}); the show runs anyway, sharing the GPU, "
                  "and takes the lock when it is free ({:.2f} ms)",
                  showLock_.path, name, pid, ms);
    }
    showLock_.contended = true;
    showLock_.holderPid = pid;
    showLock_.holder = name;
    showRetryAt_ = now + std::chrono::duration_cast<Clock::duration>(
                             std::chrono::duration<double>(options_.showLockRetrySeconds));
    return showLock_;
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
