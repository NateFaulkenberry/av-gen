#pragma once

// The path tracer's job wrapper (ADR-351, spec sections 36, 37, 64).
//
// This is the thing that makes the renderer reachable. Everything under it -- the snapshot, the
// BVH, the integrator, the denoiser, the EXR writer -- worked and was tested before this existed,
// and none of it could be run by a person: a subsystem with no caller is not done, and no test
// says so, because the tests check that the thing works rather than that anybody can reach it.
//
// THREADING (spec section 36). The job owns exactly ONE thread of its own: a coordinator that runs
// the stages in order. It does not create a pool. The per-batch worker threads belong to
// `PathTracer::render`, which creates and JOINS them inside each sample batch, so they exist only
// while a batch is running and their count is `TraceSettings::threads`. There is therefore no
// second long-lived scheduler competing with anything.
//
// `app::JobSystem` is deliberately not used, and cannot be: it is a two-worker FIFO with no
// parallel-for, and its own header forbids waiting on it from a render thread. A trace occupying
// one of its two workers for minutes would starve world generation and the AI control plane, which
// are what it exists for.

#include "core/error.hpp"
#include "pathtrace/denoise.hpp"
#include "pathtrace/path_tracer.hpp"
#include "pathtrace/snapshot.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

namespace avgen::pathtrace {

// The states spec section 36 names, in the order they occur.
enum class TraceJobState : std::uint8_t {
    Queued,
    BuildingScene,          // flattening the evaluated scene into a Snapshot
    BuildingAcceleration,   // Embree BVH
    Rendering,
    Denoising,
    Writing,
    Complete,
    Cancelled,
    Failed,
};

[[nodiscard]] std::string_view traceJobStateName(TraceJobState s);
[[nodiscard]] bool traceJobStateIsTerminal(TraceJobState s);

struct TraceProgress {
    TraceJobState state = TraceJobState::Queued;
    std::string stage;              // the state's human name, for a UI that wants one string

    // Overall completion. `fractionKnown` is false for stages that genuinely cannot report --
    // a BVH build gives no intermediate signal, so the job says so rather than inventing a number
    // that creeps. Spec section 36 asks for real progress; an interpolated bar is not real.
    float fraction = 0.0f;
    bool fractionKnown = false;

    std::uint32_t samplesDone = 0;
    std::uint32_t samplesTotal = 0;
    double elapsedSeconds = 0.0;
    std::string error;

    [[nodiscard]] bool finished() const { return traceJobStateIsTerminal(state); }
};

struct TraceJobRequest {
    std::filesystem::path project;     // an ordinary AV Gen project file
    double seconds = 0.0;              // timeline time to render
    TraceSettings settings;
    std::filesystem::path output;      // .exr
    bool writeAovs = false;            // one multi-layer EXR instead of beauty-only
    bool denoise = false;
    [[nodiscard]] Result<void> validate() const;
};

class TraceJob {
public:
    explicit TraceJob(TraceJobRequest request);
    ~TraceJob();
    TraceJob(const TraceJob&) = delete;
    TraceJob& operator=(const TraceJob&) = delete;

    // Runs every stage on the calling thread. This is what the CLI uses.
    [[nodiscard]] Result<void> run();

    // Runs the same stages on the job's own thread and returns immediately, so a UI keeps its
    // frame. Poll `progress()`; `wait()` joins.
    void start();
    void wait();

    // Safe at any time and from any thread. Sets a flag the stages poll -- between sample batches,
    // and before denoising and before writing (spec section 37). Never terminates a thread.
    void cancel();

    [[nodiscard]] TraceProgress progress() const;
    [[nodiscard]] TraceJobState state() const { return state_.load(); }
    [[nodiscard]] bool done() const { return traceJobStateIsTerminal(state_.load()); }

    // Valid once the job is Complete. Empty otherwise.
    [[nodiscard]] const Framebuffer& framebuffer() const { return framebuffer_; }
    [[nodiscard]] const TraceStats& stats() const { return stats_; }
    [[nodiscard]] const CapabilityReport& capabilities() const { return capabilities_; }

private:
    Result<void> execute();
    void setState(TraceJobState s);

    TraceJobRequest request_;
    std::atomic<TraceJobState> state_{TraceJobState::Queued};
    std::atomic<bool> cancel_{false};
    std::atomic<std::uint32_t> samplesDone_{0};
    std::chrono::steady_clock::time_point started_{};

    mutable std::mutex mutex_;
    std::string error_;

    Framebuffer framebuffer_;
    TraceStats stats_{};
    CapabilityReport capabilities_;
    std::thread thread_;
};

} // namespace avgen::pathtrace
