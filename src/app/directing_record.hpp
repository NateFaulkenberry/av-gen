#pragma once

// Recording a live performance into a baked one (ADR-763, Slice 4).
//
// A goal performance is live: where the body goes and when it arrives is the simulation's. The only
// route from that to reproducible content is to run it and keep what happened. This plays a SCRATCH
// COPY of the project (ADR-753: the person's project is not touched) from zero at a fixed 60 Hz, with
// the entity cull lifted and no audio (audio-reactive signals are not replayed by a seek yet, ADR-870),
// samples the character's body and clip every frame, and hears its goal events. The result is the
// same plan, with each live performance carrying a `recording`: a scripted actor and the times of its
// events -- which the compiler installs as an ordinary performer (ADR-758), so it bakes, scrubs and
// renders like any scripted performance, and cues on its events bake at their recorded times.
//
// Then it checks itself: the recorded plan is installed on a second scratch copy and played, and the
// body must be where the recording says at every key; and a scrub into the recording must land where
// that play did (ADR-800's method). Worst differences are reported and stored in the recording.

#include "core/error.hpp"
#include "directing/compiler.hpp"

#include <atomic>
#include <mutex>
#include <optional>
#include <filesystem>
#include <functional>
#include <future>
#include <string>
#include <thread>
#include <vector>

namespace avgen::app {

class Engine;

struct RecordOptions {
    double tailSeconds = 1.0;     // kept after the last goal event, or after `maxSeconds`
    double maxSeconds = 40.0;     // the longest a goal is recorded for, from when it is given
    double keyEverySeconds = 0.1; // the recording's key spacing (Linear keys; the body is exact on them)
    std::filesystem::path scratchDir;
    const std::atomic<bool>* cancel = nullptr; // polled every frame of every play
};
using RecordProgress = std::function<void(const std::string& phase)>;

struct RecordReport {
    directing::Plan plan;             // the input plan with its live performances recorded
    double recordMs = 0.0;
    double checkMs = 0.0;
    double replayWorstMetres = 0.0;   // body vs recording, at every key, played
    double scrubWorstMetres = 0.0;    // scrub vs play, every entity, at the probes
    std::vector<std::string> notes;   // one line per performance, for the diff
};

// `compilation` must be a live-tier compilation whose goal performances compiled (not blocked).
// Synchronous: writes the copy, records, checks.
[[nodiscard]] Result<RecordReport> recordLivePerformances(Engine& live, const directing::Compilation& compilation,
                                                          const RecordOptions& options);

// The two halves, for running it off the editor's frames: the copy is written on the main thread
// (it reads the live engine), and everything else touches only that file and the scratch engines it
// loads, so it can run on a thread of its own.
[[nodiscard]] Result<std::filesystem::path> writeRecordingCopy(Engine& live, const std::filesystem::path& scratchDir);
[[nodiscard]] Result<RecordReport> recordFromCopy(const std::filesystem::path& copy,
                                                  const directing::Compilation& compilation,
                                                  const RecordOptions& options, const RecordProgress& progress = {});

// A recording on a thread of its own: `start` on the main thread, then poll. The copy is deleted when
// the recording is done or the job is destroyed (which cancels it and waits).
class RecordingJob {
public:
    RecordingJob() = default;
    ~RecordingJob();
    RecordingJob(const RecordingJob&) = delete;
    RecordingJob& operator=(const RecordingJob&) = delete;

    [[nodiscard]] Result<void> start(Engine& live, directing::Compilation compilation, RecordOptions options);
    [[nodiscard]] bool running() const;
    [[nodiscard]] std::string phase() const;
    // The result once, when it is ready; nothing while running (or after it was taken).
    [[nodiscard]] std::optional<Result<RecordReport>> take();
    void cancel();

private:
    void finish();
    std::thread thread_;
    std::atomic<bool> cancel_{false};
    std::atomic<bool> done_{false};
    mutable std::mutex mutex_;
    std::string phase_;
    std::optional<Result<RecordReport>> result_;
    std::filesystem::path copy_;
};

} // namespace avgen::app
