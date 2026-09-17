#pragma once

// ============================================================================================
// TEMPORARY INSTRUMENTATION -- UI responsiveness investigation, phase 2.
// docs/investigations/ui-responsiveness.md, section "Phase 2 -- instrumentation added".
// Delete this header and every `probe2::` reference when the investigation closes.
// ============================================================================================
//
// Why a header of inline globals rather than passing a profiler down: the costs phase 2 has to
// attribute are spread across three static libraries (avgen_core holds EntityWorld, avgen_app
// holds Engine, the loop that owns the PhaseProfiler is in Application), and threading an
// instrument through all three would be a refactor -- which the brief forbids. These are
// main-thread-only counters that the frame loop reads and zeroes once per frame.
//
// Nothing here changes behaviour. Every field is written by a `steady_clock` difference or an
// increment; no control flow reads any of them.

#include <chrono>
#include <cstdint>

namespace avgen::probe2 {

struct Frame {
    // ---- Engine::seekSeconds, wherever it is called from ------------------------------------
    std::uint64_t seeks = 0;          // how many seeks this frame asked for
    double seekMs = 0.0;              // total wall-clock inside Engine::seekSeconds
    double entitySeekMs = 0.0;        // ...of which EntityWorld::seek (the re-simulation)
    double directorResetMs = 0.0;     // ...of which the director/camera-state reset
    std::uint64_t entitySimSteps = 0; // fixed 1/60 s steps the re-simulation integrated
    std::uint64_t entitySimBodies = 0; // steps x entities: the actual inner-loop count

    // ---- the analysis catch-up loop in Engine::update ----------------------------------------
    double analysisCatchupMs = 0.0;
    std::uint64_t analysisFramesConsumed = 0;

    // ---- Engine::update, by stage -------------------------------------------------------------
    double updControlMs = 0.0;
    double updSignalsMs = 0.0;
    double updModulationMs = 0.0;
    double updControllerMs = 0.0; // controller_->update(): the scene, incl. ensureBuilt/flatten
    double updOtherMs = 0.0;

    // ---- SceneRenderer::uploadMeshes ---------------------------------------------------------
    // The all-or-nothing GPU re-upload. Counted, not only timed: "render.record spiked" is a
    // correlation, "it destroyed and re-created 860 buffers" is an attribution.
    std::uint64_t meshUploadPasses = 0; // times uploadMeshes did NOT take its early-out
    std::uint64_t meshesUploaded = 0;   // buffers re-created
    double meshUploadMs = 0.0;
    std::uint64_t texturesUploaded = 0;
    double textureUploadMs = 0.0;
    double environmentMs = 0.0;

    // ---- the SDL input queue, as the frame loop drained it ------------------------------------
    // The application already times `input->present ms` from the newest *motion* event. These add
    // the press (a timeline click is a button, not a motion), the oldest event still in the batch
    // (which is what a backlog looks like) and the batch size.
    std::uint64_t newestInputNs = 0;  // SDL_GetTicksNS timestamp of the newest event this frame
    std::uint64_t oldestInputNs = 0;  // ...and of the oldest
    std::uint64_t inputEvents = 0;    // every SDL event the callback saw
    std::uint64_t pointerEvents = 0;  // of which motion + button

    void note(std::uint64_t ns) {
        if (ns == 0) {
            return;
        }
        newestInputNs = ns > newestInputNs ? ns : newestInputNs;
        oldestInputNs = (oldestInputNs == 0 || ns < oldestInputNs) ? ns : oldestInputNs;
    }

    void clear() { *this = Frame{}; }
};

// One instance, main thread only. `inline` so the three libraries share it.
inline Frame& frame() {
    static Frame f;
    return f;
}

// A scope that adds its elapsed milliseconds to one `double` member.
class Add {
public:
    explicit Add(double& sink) : sink_(&sink), start_(std::chrono::steady_clock::now()) {}
    ~Add() {
        *sink_ += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_)
                      .count();
    }
    Add(const Add&) = delete;
    Add& operator=(const Add&) = delete;

private:
    double* sink_;
    std::chrono::steady_clock::time_point start_;
};

} // namespace avgen::probe2
