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

#include <filesystem>
#include <string>
#include <vector>

namespace avgen::app {

class Engine;

struct RecordOptions {
    double tailSeconds = 1.0;     // kept after the last goal event, or after `maxSeconds`
    double maxSeconds = 40.0;     // the longest a goal is recorded for, from when it is given
    double keyEverySeconds = 0.1; // the recording's key spacing (Linear keys; the body is exact on them)
    std::filesystem::path scratchDir;
};

struct RecordReport {
    directing::Plan plan;             // the input plan with its live performances recorded
    double recordMs = 0.0;
    double checkMs = 0.0;
    double replayWorstMetres = 0.0;   // body vs recording, at every key, played
    double scrubWorstMetres = 0.0;    // scrub vs play, every entity, at the probes
    std::vector<std::string> notes;   // one line per performance, for the diff
};

// `compilation` must be a live-tier compilation whose goal performances compiled (not blocked).
[[nodiscard]] Result<RecordReport> recordLivePerformances(Engine& live, const directing::Compilation& compilation,
                                                          const RecordOptions& options);

} // namespace avgen::app
