#pragma once

// The asynchronous job system (ADR-064, milestone 6 of the cinematic upgrade).
//
// `app::RenderJob` already runs a render on its own thread with progress and cancellation, and it
// does that well. What it cannot do is be one of several things queued up, report which *stage* of
// itself it is in, or say honestly that it does not know how long it has left. This is the general
// version of it: a queue, workers, staged progress, cancellation that actually stops, and an ETA
// that refuses to lie.
//
// The rules it exists to enforce:
//
//   - Nothing here blocks the render thread or the audio thread. A job runs on a worker; the UI
//     and the renderer read a snapshot.
//   - Progress is honest. A job that cannot estimate its completion reports the stage it is in and
//     "calculating", not a number somebody would act on. Inventing 72% is worse than admitting
//     ignorance, because a fabricated bar is indistinguishable from a real one.
//   - Cancellation is prompt. A job checks `shouldCancel()` between units of work and unwinds; a
//     Cancel button that leaves the application unresponsive for minutes is not cancellation.
//   - An optional job's failure is not the caller's failure. Jobs record an error and finish; what
//     that means is the caller's decision.

#include "core/error.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace avgen::app {

enum class JobState : std::uint8_t { Queued, Preparing, Running, Paused, Completed, Cancelled, Failed };
[[nodiscard]] const char* jobStateName(JobState s);
[[nodiscard]] bool jobStateIsTerminal(JobState s);

using JobId = std::uint64_t;

// What a job looks like from outside. A snapshot, copied under the lock, so a UI can read it
// without holding anything the worker needs.
struct JobStatus {
    JobId id = 0;
    std::string type;
    std::string name;
    JobState state = JobState::Queued;

    int stageIndex = 0;          // 0-based
    int stageCount = 1;
    std::string stageName;
    float stageProgress = 0.0f;  // 0..1 within the current stage

    // Overall progress, and whether it means anything. A job whose stages have no known length
    // leaves `progressKnown` false and the UI shows the stage instead of a bar.
    float progress = 0.0f;
    bool progressKnown = false;

    double elapsedSeconds = 0.0;
    // Negative when unknown. Callers must check rather than formatting a negative duration, which
    // is the whole reason it is not an optional wrapped in a happy default.
    double estimatedRemainingSeconds = -1.0;

    std::string currentOperation;   // "Generating variation 7 / 16"
    std::string error;
    bool cancellable = true;
    std::vector<std::string> logs;
};

// The handle a running job uses to talk to the world. Everything on it is safe to call from the
// worker thread and nothing on it blocks for long.
class JobContext {
public:
    virtual ~JobContext() = default;

    // Declares the stages up front, so the UI can show "3 / 6" from the first frame instead of
    // discovering the shape of the work as it goes.
    virtual void setStages(std::vector<std::string> stages) = 0;
    virtual void beginStage(int index) = 0;
    // 0..1 within the stage. A stage that cannot measure itself simply never calls this, and the
    // job's overall progress stays unknown rather than becoming a guess.
    virtual void setStageProgress(float fraction) = 0;
    virtual void setOperation(std::string text) = 0;
    virtual void log(std::string line) = 0;
    // True when the job has been asked to stop. Long work must poll this.
    [[nodiscard]] virtual bool shouldCancel() const = 0;
    // Blocks while the job is paused and returns false if it was cancelled while paused.
    [[nodiscard]] virtual bool waitWhilePaused() = 0;
};

// The work itself. Returning an error marks the job Failed; returning after `shouldCancel()` goes
// true marks it Cancelled, whatever it returns.
using JobBody = std::function<Result<void>(JobContext&)>;

struct JobRequest {
    std::string type;        // "render", "ai.depth", ...
    std::string name;        // shown to a person
    JobBody body;
    bool cancellable = true;
};

// A queue with a fixed pool of workers. Jobs start in submission order; a job already running is
// not preempted.
class JobSystem {
public:
    explicit JobSystem(unsigned workers = 2);
    ~JobSystem();
    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    [[nodiscard]] JobId submit(JobRequest request);

    // Asks a job to stop. Returns false if there is no such job. A queued job is cancelled
    // immediately; a running one is cancelled when it next polls.
    bool cancel(JobId id);
    bool pause(JobId id);
    bool resume(JobId id);
    void cancelAll();

    [[nodiscard]] std::vector<JobStatus> statuses() const;
    [[nodiscard]] bool status(JobId id, JobStatus& out) const;
    [[nodiscard]] std::size_t pending() const;
    [[nodiscard]] bool idle() const;

    // Blocks until a job reaches a terminal state. For tests and for shutdown; never call it from
    // the render or audio thread.
    bool waitFor(JobId id, std::chrono::milliseconds timeout = std::chrono::milliseconds{30000});
    void waitAll(std::chrono::milliseconds timeout = std::chrono::milliseconds{30000});

    // Removes finished jobs from the list. The UI keeps them until a person dismisses them, so this
    // is explicit rather than automatic.
    void clearFinished();

private:
    struct Job;
    class Context;

    void workerLoop();
    [[nodiscard]] std::shared_ptr<Job> findLocked(JobId id) const;

    mutable std::mutex mutex_;
    std::condition_variable work_;
    mutable std::condition_variable done_;
    std::deque<std::shared_ptr<Job>> queue_;
    std::vector<std::shared_ptr<Job>> all_;
    std::vector<std::thread> workers_;
    JobId nextId_ = 1;
    bool stopping_ = false;
};

} // namespace avgen::app
