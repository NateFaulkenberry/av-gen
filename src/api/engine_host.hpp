#pragma once

// `avgen::api::EngineHost`: the host every session runs in (ADR-2155, Engine API stage S2; 02-session-api.md
// section 4) -- `avgen serve` and `avgen call`, and since stage S10 (ADR-2165) Studio too.
//
// It owns, or borrows:
//   - an `app::Engine`. `avgen serve`/`avgen call` construct it with an engine of its own: Offline, live control
//     off (no OSC port, no MIDI input, no audio device), the Stepped time policy. Studio constructs it over
//     `Application::engine_`, which stays Studio's (03-boundaries.md section 2.1: the host borrows an engine);
//   - the host-thread queue, and the loop that services it (`run`), or -- in Studio -- the frame loop that pumps it.
//
// The GPU half (ADR-2159, stage S6) is composed in, not inherited from `Application`:
//   - a `RenderBackend` (app/offline_render.hpp), made by `EngineHostOptions::renderBackend`. It creates the
//     `gpu::Context` and the `ShaderLibrary(defaultSearchDirs(exe))` lazily, on the first job's start, so a session
//     that never renders never creates a device. Studio asks for it at start-up (`ensureDevice`) because its live
//     renderer is built from the host's device and shaders. This file lives in avgen_core, which cannot link
//     `gpu::Context`: a host built without a backend cannot render at all (`canRender()` false, GPU tools
//     `Unavailable`), which is what the CPU suite and every CPU-only client get;
//   - a FIFO of render jobs. API jobs (`submitJob`, from render.capture/render.start) count against `queueCapacity`
//     (4, counting the one running; a fifth is `Busy`). The operator's own jobs (`submitLocal`: Studio's Render panel
//     and render queue, headless --render and --queue) do not, and may be submitted HELD -- a queue entry that waits
//     for the panel's Run queue (`releaseJob`);
//   - one GPU job at a time, stepped in slices of about `sliceMs` between `session.pump()`s (or once per UI frame),
//     so calls stay responsive while it runs. While a job's pipelines compile (72 s measured cold), the compile's
//     wait hook reports progress into the job's record, and -- unless `pumpWhileCompiling` is off -- pumps the queue;
//   - the machine's GPU lock (api/gpu_lock.hpp), taken per job before the device is touched and released when the
//     job ends. A job waiting for it polls between pumps; an idle host holds nothing.
//
// The show (ADR-2167): `showState()` says whether a show is running, with reasons (api/show_state.hpp). While one
// is:
//   - no GPU job starts: a new API submission is refused `Busy` with `details {field: "show", reasons}`; a job queued
//     before the show stays queued ("held: show active"); a job already rendering is paused between frames ("paused:
//     show active") and resumes when the show stops;
//   - `updateShow` -- called once per frame by the host that has a show (Studio) -- holds the machine GPU lock,
//     taking it with `tryAcquire` only, never waiting. If another process holds it the show runs anyway; the
//     contention is logged, reported (`showLock`) and retried about once a second.
//
// Threads: the thread that calls `run` (or, for a one-shot client, `bindHostThread`) is the host thread. It is
// the only one that touches the engine. `stop` and `wake` are safe from any thread.

#include "api/session.hpp"
#include "core/error.hpp"

#include "api/gpu_lock.hpp"
#include "api/jobs.hpp"
#include "api/show_state.hpp"

#include <atomic>
#include <chrono>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <optional>
#include <vector>

namespace avgen::app {
class Engine;
} // namespace avgen::app

namespace avgen::api {

struct EngineHostOptions {
    // ADR-2154: the host's time policy. Empty: `Stepped` -- an API session costs nothing between calls and its
    // world advances only on transport.step (02-session-api.md section 3.4). The host offers Stepped and
    // Offline; render jobs run their own Offline engines whatever this says. Ignored by a host over a borrowed
    // engine: that engine's owner sets its clock.
    std::optional<app::TimePolicy> timePolicy;

    // ADR-2159: the GPU half. Empty: this host cannot render.
    RenderBackendFactory renderBackend;
    // The GPU lock's directory; empty: `GpuLock::defaultPath()` (gpu-lock.sh's) at each acquire.
    std::filesystem::path gpuLockPath;
    // How long a job waits for the lock before it fails `Busy`. Negative: AVGEN_GPU_LOCK_TIMEOUT, else 3600 s
    // (the script's default).
    double gpuLockTimeoutSeconds = -1.0;
    // How often a waiting job polls the lock. 0: at every `stepJobs` (`run` already sleeps 250 ms between polls);
    // Studio, which steps once per UI frame, polls four times a second.
    double gpuLockPollSeconds = 0.0;
    // ADR-2167: what the lock's `holder` file says while a show holds it, and how often a show whose lock another
    // process holds tries again.
    std::string showLockHolder = "avgen-studio-show";
    double showLockRetrySeconds = 1.0;
    std::size_t queueCapacity = 4;
    double sliceMs = 50.0;
    // While a job's start waits for a pipeline compile, also pump the host queue (calls stay answered). Studio
    // turns it off: its own pipeline wait hook keeps the window alive, and its assistant's calls wait for the frame.
    bool pumpWhileCompiling = true;
};

// What `EngineHost` reports about a job beside the RenderJob's own numbers (for tests and results).
struct EngineHostJobStats {
    std::size_t queued = 0; // waiting to start, held ones included
    std::size_t held = 0;   // of those, held by the operator (a queue entry not yet released)
    bool running = false;
    bool deviceCreated = false;
    bool lockHeld = false;
    bool pausedForShow = false;
};

// ADR-2165: one of the operator's own GPU jobs. The record is already in `table` (id `jobId`); `prepare` is the CPU
// half (a fresh Offline engine loaded from a project file, and the job over the operator's full render settings),
// run when the job reaches the front of the FIFO, before the lock. No device is touched until the job starts.
struct LocalJobSubmission {
    std::shared_ptr<JobTable> table;
    std::string jobId;
    std::function<Result<std::unique_ptr<GpuRenderJob>>()> prepare;
    bool held = false;      // waits for `releaseJob`
    std::string heldPhase;  // what the record says meanwhile
};

class EngineHost final : public Host {
public:
    // A host with an engine of its own (`avgen serve`, `avgen call`, the tests).
    explicit EngineHost(EngineHostOptions options = {});
    // ADR-2165: a host over an engine someone else owns (Studio's `Application::engine_`). Its live control and
    // time policy are the owner's; nothing here changes them.
    EngineHost(app::Engine& borrowed, EngineHostOptions options);
    ~EngineHost() override;
    EngineHost(const EngineHost&) = delete;
    EngineHost& operator=(const EngineHost&) = delete;

    [[nodiscard]] app::Engine& engine() override { return local_.engine(); }
    [[nodiscard]] ai::MainThreadQueue& queue() override { return local_.queue(); }
    void installHooks(ai::ToolContext& ctx) override { local_.installHooks(ctx); }
    [[nodiscard]] std::vector<app::TimePolicy> offeredTimePolicies() override { return local_.offeredTimePolicies(); }
    [[nodiscard]] bool showRunning() override { return local_.showRunning(); }
    [[nodiscard]] std::optional<double> showRemainingSeconds() override { return local_.showRemainingSeconds(); }
    [[nodiscard]] ShowState showState() override { return local_.showState(); }
    [[nodiscard]] std::optional<ShowLockState> showLock() override;
    // ADR-2159: a backend is installed and no device creation has failed. No device is created to answer.
    [[nodiscard]] bool canRender() override;
    [[nodiscard]] std::optional<ai::ToolError> submitJob(JobSubmission submission) override;
    bool stepJobs() override;
    void cancelJobs(const JobTable& table) override;
    [[nodiscard]] EngineHostJobStats jobStats() const;

    // The hooks, the show and the offered time policies: what an application installs (Studio does it through
    // this, as it did through its LocalHost).
    [[nodiscard]] LocalHost& local() { return local_; }

    // ---- ADR-2165: the operator's jobs and device -------------------------------------------------------
    // The backend and its device, now, without the GPU lock: Studio's live renderer is built from them at start-up,
    // as it always was. An error when this host has no backend or the device cannot be created.
    [[nodiscard]] Result<RenderBackend*> ensureDevice();
    // The backend, when one has been made (by `ensureDevice` or the first submission).
    [[nodiscard]] RenderBackend* backend() { return backend_.get(); }
    [[nodiscard]] std::optional<ai::ToolError> submitLocal(LocalJobSubmission submission);
    // A held job may start (FIFO order among the startable). False when it is not a held job of `table`.
    bool releaseJob(const JobTable& table, const std::string& id);
    // Stops one job: a queued or held one before it starts, a running one after its current frame.
    bool cancelJob(const JobTable& table, const std::string& id);
    // Any job queued or running.
    [[nodiscard]] bool hasJobs() const { return active_ != nullptr || !pending_.empty(); }
    // A job will be stepped by the next `stepJobs`: one is running, or one can start. False while a show holds them
    // and while only held entries wait. What Studio's frame reads where it used to read "a render job exists".
    [[nodiscard]] bool jobInLoop();

    // ---- ADR-2167: the show -----------------------------------------------------------------------------
    // Host thread, once per frame: reads `showState()` and, on its edges, takes (tryAcquire, never waiting) or
    // releases the machine GPU lock. Returns the lock's state.
    const ShowLockState& updateShow();

    // Makes the calling thread the host thread without running the loop: a one-shot client (`avgen call`)
    // that calls `Session::call` from this thread, which then runs each body inline.
    void bindHostThread();

    // The loop, on the calling thread (which becomes the host thread), until `stop`: `session.pump()`, then
    // -- when nothing is queued -- sleep until a call arrives. It never ticks the engine while idle.
    void run(Session& session);
    // Ends `run` after the item it is running, from any thread.
    void stop();
    [[nodiscard]] bool stopping() const { return stop_.load(std::memory_order_acquire); }

private:
    struct Pending;
    void finishActive();
    void failJob(Pending& job, ai::ToolError error);
    [[nodiscard]] Result<void> ensureBackend();
    void ensureLock();
    // The job's GPU work is over: release the lock unless a show holds it.
    void releaseJobLock();
    // While a show runs: mark every waiting job held and the running one paused, once; and back again after.
    void holdForShow(const ShowState& show);
    void resumeAfterShow();

    EngineHostOptions options_;
    std::unique_ptr<app::Engine> engine_; // empty for a host over a borrowed engine
    LocalHost local_; // the queue, the hooks and the show: the host behaviour LocalHost already has
    std::atomic<bool> stop_{false};
    // ---- the GPU half (host thread only) ----
    std::unique_ptr<RenderBackend> backend_; // made on the first submission (or by ensureDevice)
    bool deviceFailed_ = false;
    std::deque<std::unique_ptr<Pending>> pending_;
    std::unique_ptr<Pending> active_;
    std::unique_ptr<GpuLock> lock_;
    bool jobHoldsLock_ = false;
    bool inStep_ = false;
    bool waitingForLock_ = false;
    std::chrono::steady_clock::time_point nextLockPoll_{};
    std::shared_ptr<struct JobWarningSink> warnings_;
    // ---- the show ----
    ShowLockState showLock_;
    std::chrono::steady_clock::time_point showRetryAt_{};
    bool heldForShow_ = false;
    std::string heldForShowReasons_;
};

// The flags `avgen serve` and `avgen call` share: `--root d` (a read and write root), `--read-root d`,
// `--write-root d` (each repeatable) and `--allow-subprocess`. Every root is made absolute against the
// working directory here, once, on the command line -- a session never resolves anything against it.
struct HostFlags {
    SessionPermissions permissions;
};
// Consumes the flag at `argv[i]` (and its value) when it is one of these, advancing `i`. Returns false when it
// is not one of them; an error when it is one but malformed (no value, or a root that does not exist).
[[nodiscard]] Result<bool> parseHostFlag(int argc, char** argv, int& i, HostFlags& flags);

// stdout protection (02-session-api.md section 10): duplicates fd 1 for the protocol and points fd 1 at stderr,
// so a stray `printf` anywhere in the process lands in the log instead of in the stream. Returns the protocol
// descriptor. Flushes stdout first, so nothing buffered before the switch goes to the wrong place.
[[nodiscard]] Result<int> protectStdout();
// Undoes it: flushes stdout, points fd 1 back at what the protocol descriptor names, and closes that descriptor.
// For an in-process caller (the tests) that must get its own stdout back.
void restoreStdout(int protocolFd);

// `avgen call ...` (api/cli_call.cpp). Returns the exit code: 0 success, 1 a tool failed (its envelope is still
// printed), 2 usage, 7 internal or host failure. `renderBackend` is the GPU half (ADR-2159); empty: CPU only.
[[nodiscard]] int runCallCommand(int argc, char** argv, RenderBackendFactory renderBackend = {});
[[nodiscard]] const char* callUsage();

} // namespace avgen::api
