#pragma once

// Render and capture jobs (ADR-2159, Engine API stage S6; 02-session-api.md sections 3.6 and 8).
//
// Ownership is split the way the plan splits it:
//   - the SESSION owns the job RECORDS (`JobTable`): id, the request, the snapshot it renders, outputs, progress,
//     the result, the error. The table is thread-safe: the reader thread of `avgen serve` waits on it
//     (`job.wait` blocks the caller, never the host thread), the host thread writes it;
//   - the HOST owns the GPU work: the device, the shaders, the `RenderJob` objects, the FIFO, the machine's GPU
//     lock, and the stepping between pumps (api/engine_host.hpp). It reaches the GPU through a `RenderBackend`,
//     which lives in the application layer (app/offline_render.hpp) because avgen_core cannot link a device.
//     A host without one cannot render, which is what keeps every CPU-only command device-free by construction.
//
//   render.* call --> Queued --> Preparing --> Running --> Succeeded
//                     (FIFO)     (load the     (slices      | Failed    (error; partial outputs listed)
//                                snapshot,     between      | Cancelled (job.cancel; partial outputs listed)
//                                GPU lock,     pumps)
//                                device,
//                                compile)

#include "ai/tool_api.hpp"
#include "core/error.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace avgen::ai {
class CancelToken;
} // namespace avgen::ai

namespace avgen::api {

enum class JobState : std::uint8_t { Queued, Preparing, Running, Succeeded, Failed, Cancelled };
[[nodiscard]] const char* jobStateName(JobState state);
[[nodiscard]] bool jobFinished(JobState state);

struct JobRecord {
    std::string id;     // "job-3"
    std::string tool;   // render.capture, render.start
    std::string kind;   // capture, sequence
    JobState state = JobState::Queued;
    std::string phase = "queued"; // what it is doing now, in words a client can show
    double progress = 0.0;        // 0..1 over the whole job
    std::uint64_t framesDone = 0; // frames of the range rendered
    std::uint64_t framesTotal = 0;
    nlohmann::json request = nlohmann::json::object(); // the normalised arguments
    std::string snapshot;                               // the project copy it renders
    nlohmann::json outputs = nlohmann::json::array();   // [{path, frame?, sha256, bytes}], partial ones too
    nlohmann::json result = nullptr;                    // on success: hashes, timing, lock, warnings
    std::optional<ai::ToolError> error;
    bool cancelRequested = false;
    std::chrono::system_clock::time_point created{};
    std::chrono::system_clock::time_point finished{};

    [[nodiscard]] nlohmann::json toJson() const;
};

class JobTable {
public:
    // Called (outside the table's lock) on every state or phase change, and on progress at most every
    // `kProgressPeriod`: the event a transport turns into `notifications/progress`.
    using Listener = std::function<void(const nlohmann::json& event)>;
    static constexpr std::chrono::milliseconds kProgressPeriod{250};

    // A new Queued record. Returns its id.
    [[nodiscard]] std::string create(std::string tool, std::string kind, nlohmann::json request,
                                     std::string snapshot);
    // Changes a record under the lock, then wakes waiters and tells the listener. False for an unknown id.
    bool update(const std::string& id, const std::function<void(JobRecord&)>& change);
    [[nodiscard]] std::optional<JobRecord> get(const std::string& id) const;
    [[nodiscard]] std::vector<JobRecord> list() const;
    // Asks a job to stop. False when there is no such job or it has already finished.
    bool requestCancel(const std::string& id);
    [[nodiscard]] bool cancelRequested(const std::string& id) const;
    // Forgets a record (a submission the host refused: the call failed, so no job exists).
    void discard(const std::string& id);
    // Blocks the CALLING thread until the job finishes, `timeout` passes or `cancel` fires. The record as it is
    // then; nothing for an unknown id.
    [[nodiscard]] std::optional<JobRecord> wait(const std::string& id, std::chrono::milliseconds timeout,
                                                const ai::CancelToken* cancel = nullptr) const;
    // Jobs not yet finished.
    [[nodiscard]] std::size_t unfinished() const;
    void setListener(Listener listener);

private:
    mutable std::mutex mutex_;
    mutable std::condition_variable changed_;
    std::map<std::string, JobRecord> jobs_;
    std::vector<std::string> order_;
    std::map<std::string, std::chrono::steady_clock::time_point> lastProgressEvent_;
    std::uint64_t next_ = 1;
    std::shared_ptr<Listener> listener_;
};

// ---- the GPU half's seam ---------------------------------------------------------------------------------

enum class RenderFormat : std::uint8_t { Png, Exr, Video };
[[nodiscard]] const char* renderFormatName(RenderFormat format);

// `play`: render (and discard) every frame from 0 up to the range's start, so the first frame has the history a
// play-through had. `seek`: jump to the start (the engine's seek, plus RenderJob's temporal pre-roll).
enum class RenderHistory : std::uint8_t { Play, Seek };
[[nodiscard]] const char* renderHistoryName(RenderHistory history);

struct RenderRequest {
    std::string jobId;
    bool capture = false;                 // one frame, written to `file`
    std::filesystem::path project;        // the snapshot (a project copy in the session's scratch directory)
    std::filesystem::path outputDir;      // where the RenderJob writes: the sequence folder, or a staging folder
    std::filesystem::path file;           // capture: the PNG/EXR to deliver; video: the movie
    RenderFormat format = RenderFormat::Png;
    double startSeconds = 0.0;
    double endSeconds = 0.0;              // exclusive
    double fps = 24.0;
    std::uint32_t width = 960;
    std::uint32_t height = 540;
    std::string tier;                     // empty: the project's
    RenderHistory history = RenderHistory::Play;
};

struct GpuJobProgress {
    std::string phase;            // "history", "rendering", "finishing"
    std::uint64_t framesDone = 0; // of the range
    std::uint64_t framesTotal = 0;
    std::uint64_t historyDone = 0; // discarded frames before the range (history: play)
    std::uint64_t historyTotal = 0;
    std::uint64_t framesWritten = 0;
};

struct GpuJobOutcome {
    bool cancelled = false;
    std::string error; // empty on success
    std::vector<std::uint64_t> frameHashes;
    std::uint64_t sequenceHash = 0;
    std::uint64_t framesWritten = 0;
    std::uint64_t gpuErrors = 0;
    std::uint64_t pipelinesCreated = 0; // pipeline creations that landed during start, compiled or from a cache
};

// What a pipeline compile is doing, reported about every 30 ms while a job's start waits for one.
struct CompileProgress {
    std::uint64_t started = 0; // pipeline creations begun in this process
    std::uint64_t landed = 0;  // of those, finished
    std::string waitingFor;    // the pipeline being waited on, when known
};
using CompileWaitFn = std::function<void(const CompileProgress&)>;

// One render on the device. Host thread only.
class GpuRenderJob {
public:
    virtual ~GpuRenderJob() = default;
    // Builds the renderer, warms up, positions the engine. The GPU lock is held. While it waits for a pipeline to
    // compile it calls `onCompileWait` (on this thread), which the host uses to report progress and keep pumping.
    [[nodiscard]] virtual Result<void> start(const CompileWaitFn& onCompileWait) = 0;
    // Renders for about `budgetSeconds`. True when the job is complete, failed or cancelled.
    [[nodiscard]] virtual bool step(double budgetSeconds) = 0;
    virtual void cancel() = 0;
    [[nodiscard]] virtual GpuJobProgress progress() const = 0;
    [[nodiscard]] virtual GpuJobOutcome outcome() const = 0;
};

class RenderBackend {
public:
    virtual ~RenderBackend() = default;
    // Creates the device and the shader library the first time (the GPU lock is held). Idempotent.
    [[nodiscard]] virtual Result<void> ensureDevice() = 0;
    [[nodiscard]] virtual bool deviceCreated() const = 0;
    // A job over the request: the CPU half only (a fresh Offline engine loaded from the snapshot,
    // `requireProjectScene`). No device is touched until `start`.
    [[nodiscard]] virtual Result<std::unique_ptr<GpuRenderJob>> create(const RenderRequest& request) = 0;
};

using RenderBackendFactory = std::function<std::unique_ptr<RenderBackend>()>;

// "%016x", the form the logs and every result use for a 64-bit frame hash (a JSON number would lose bits).
[[nodiscard]] std::string hashHex(std::uint64_t hash);

} // namespace avgen::api
