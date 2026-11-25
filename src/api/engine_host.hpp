#pragma once

// `avgen::api::EngineHost`: the host every external client's session runs in (ADR-2155, Engine API stage S2;
// 02-session-api.md section 4). `avgen serve` and `avgen call` use it; Studio does not (it keeps a LocalHost over
// `Application::engine_` until stage S10).
//
// It owns:
//   - one Offline `app::Engine`, with live control off: no OSC port, no MIDI input, no audio device;
//   - the host-thread queue, and the loop that services it (`run`).
//
// It has no `Application`, no window, no ImGui, no settings file and no listener.
//
// The GPU half (ADR-2159, stage S6) is composed in, not inherited from `Application`:
//   - a `RenderBackend` (app/offline_render.hpp), made by `EngineHostOptions::renderBackend`. It creates the
//     `gpu::Context` and the `ShaderLibrary(defaultSearchDirs(exe))` lazily, on the first job's start, so a session
//     that never renders never creates a device. This file lives in avgen_core, which cannot link `gpu::Context`:
//     a host built without a backend cannot render at all (`canRender()` false, GPU tools `Unavailable`), which is
//     what the CPU suite and every CPU-only client get;
//   - a FIFO of render jobs, capacity `queueCapacity` (4) counting the one running; a fifth is `Busy`;
//   - one GPU job at a time, stepped in slices of about `sliceMs` between `session.pump()`s, so calls stay
//     responsive while it runs. While a job's pipelines compile (72 s measured cold), the compile's wait hook pumps
//     the queue too, and reports progress into the job's record;
//   - the machine's GPU lock (api/gpu_lock.hpp), taken per job before the device is touched and released when the
//     job ends. A job waiting for it polls between pumps; an idle host holds nothing.
//
// Threads: the thread that calls `run` (or, for a one-shot client, `bindHostThread`) is the host thread. It is
// the only one that touches the engine. `stop` and `wake` are safe from any thread.

#include "api/session.hpp"
#include "core/error.hpp"

#include "api/gpu_lock.hpp"
#include "api/jobs.hpp"

#include <atomic>
#include <deque>
#include <filesystem>
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
    // Offline; render jobs run their own Offline engines whatever this says.
    std::optional<app::TimePolicy> timePolicy;

    // ADR-2159: the GPU half. Empty: this host cannot render.
    RenderBackendFactory renderBackend;
    // The GPU lock's directory; empty: `GpuLock::defaultPath()` (gpu-lock.sh's) at each acquire.
    std::filesystem::path gpuLockPath;
    // How long a job waits for the lock before it fails `Busy`. Negative: AVGEN_GPU_LOCK_TIMEOUT, else 3600 s
    // (the script's default).
    double gpuLockTimeoutSeconds = -1.0;
    std::size_t queueCapacity = 4;
    double sliceMs = 50.0;
};

// What `EngineHost` reports about a job beside the RenderJob's own numbers (for tests and results).
struct EngineHostJobStats {
    std::size_t queued = 0;
    bool running = false;
    bool deviceCreated = false;
    bool lockHeld = false;
};

class EngineHost final : public Host {
public:
    explicit EngineHost(EngineHostOptions options = {});
    ~EngineHost() override;
    EngineHost(const EngineHost&) = delete;
    EngineHost& operator=(const EngineHost&) = delete;

    [[nodiscard]] app::Engine& engine() override { return local_.engine(); }
    [[nodiscard]] ai::MainThreadQueue& queue() override { return local_.queue(); }
    void installHooks(ai::ToolContext& ctx) override { local_.installHooks(ctx); }
    [[nodiscard]] std::vector<app::TimePolicy> offeredTimePolicies() override { return local_.offeredTimePolicies(); }
    // ADR-2159: a backend is installed and no device creation has failed. No device is created to answer.
    [[nodiscard]] bool canRender() override;
    [[nodiscard]] std::optional<ai::ToolError> submitJob(JobSubmission submission) override;
    bool stepJobs() override;
    void cancelJobs(const JobTable& table) override;
    [[nodiscard]] EngineHostJobStats jobStats() const;

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

    EngineHostOptions options_;
    std::unique_ptr<app::Engine> engine_;
    LocalHost local_; // the queue and the (absent) hooks: the host behaviour LocalHost already has
    std::atomic<bool> stop_{false};
    // ---- the GPU half (host thread only) ----
    std::unique_ptr<RenderBackend> backend_; // made on the first submission
    bool deviceFailed_ = false;
    std::deque<std::unique_ptr<Pending>> pending_;
    std::unique_ptr<Pending> active_;
    std::unique_ptr<GpuLock> lock_;
    bool inStep_ = false;
    bool waitingForLock_ = false;
    std::shared_ptr<struct JobWarningSink> warnings_;
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
