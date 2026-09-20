#pragma once

// Adaptive render scale for the editor viewport (interactive-performance pass, §15-§17).
//
// ---- the problem this exists for -----------------------------------------------------------------
//
// The editor renders the world into the canvas at the display's *backing scale*, so a maximised
// window on a Retina screen asks for several times the pixel count any benchmark quotes
// (`docs/application-performance.md` §7, and the log line `application.cpp` prints at frame 60).
// On `examples/world/glowmere-valley-2-multicam.json` that is single-digit frames per second, and
// the lever that fixes it -- ADR-084's `canvasRenderScale` -- has been a manual slider in Settings
// since it was built. A previous session diagnosed exactly this on Tree of Life and filed it as
// "awaiting the owner". A quality lever nobody touches is not a quality lever.
//
// ---- what was measured, and why the controller has the shape it does ------------------------------
//
// `tools/resolution_sweep.sh` on the multicam film, 2068x1326 down to 724x464, minima over repeats
// under `tools/gpu-lock.sh` (ADR-170). The GPU frame is very close to **affine in pixel count**:
//
//     GPU ms  =  4.63 ms  +  8.45 ms per megapixel        (Apple M2 Max, release, 3 repeats)
//
// A least-squares fit over the five points from 2.74 down to 0.69 Mpx has a maximum residual of
// **0.24 ms**, which is smaller than the spread between repeats at any one point. Two things
// follow.
//
// **ADR-137's prior is wrong in this range and this is what replaces it.** That ADR deferred
// dynamic resolution behind a measurement that the scene pass is "only 44% resolution-dependent",
// taken at 640x400 against 1280x800. Between 2.74 and 1.38 Mpx -- the range an editor canvas
// actually lives in -- the local log-log slope is **0.80**, not 0.44. The old number is not wrong
// about where it was taken: below about 1 Mpx the affine law's fixed term dominates and the slope
// collapses, which is exactly the 44% region. It was measured in the part of the curve the editor
// never visits.
//
// **The fixed 4.5 ms is the floor and the ladder stops above it.** Scaling past the point where the
// fixed term dominates spends image quality and buys nothing, so the ladder's lowest rung is 0.5
// (a quarter of the pixels) and not ADR-084's 0.25.
//
// ---- the control law -----------------------------------------------------------------------------
//
// A short ladder of rungs, one decision per dwell period, and a deliberately *pessimistic*
// prediction: the next rung's cost is estimated with the naive proportional model
// (`cost * pixelRatio`), which the affine law says always **under**-estimates the saving. So the
// controller never drops further than the evidence supports; when one step is not enough it takes
// another at the next decision. Downward it may take up to two rungs at once, because a person at
// 5 fps should not wait eight seconds for the ladder to walk down. Upward it takes one rung and
// only when the higher rung is predicted to fit with margin, which is what stops it oscillating
// across the budget.
//
// ---- §33/§34: this changes presentation and nothing else -------------------------------------------
//
// The rung is applied as `QualitySettings::renderScale` (ADR-137): the scene renders into a smaller
// HDR target and the tonemap filters it up into the output the canvas asked for. Authoritative
// scene state, parameters, the timeline and the flattened `scene::Scene` are untouched -- the
// renderer reads them and does not write back. An offline render cannot see this at all: the
// Offline tier pins `renderScale = 1.0` and `QualityPolicy::assertOfflineIsUncompromised()` is the
// test that says so. The controller is only ever driven from the live editor's frame loop, and
// `RenderJob` sizes its own targets from `RenderSettings`.
//
// One consequence is load-bearing and is the reason `SceneRenderer::resize` was split: a rung
// change reallocates the render targets, and until this pass that reallocation also reset the
// particle pools. A presentation change that throws away thirty seconds of simulation is not a
// presentation change. `resetScreenHistory()` is the narrower reset resize now uses.

#include <array>
#include <cstdint>
#include <span>

namespace avgen::app {

// The rungs, coarsest last. Linear scale, so the pixel count is the square: 1.00, 0.72, 0.50,
// 0.34, 0.25 of the canvas. Five rungs rather than a continuous knob because every change
// reallocates a render target and invalidates the screen-space history, so the set of distinct
// costs the application can pay should be small and nameable.
inline constexpr std::array<float, 5> kRenderScaleRungs{1.0f, 0.85f, 0.71f, 0.58f, 0.5f};

struct InteractiveResolutionSettings {
    // Off is the honest default for the *type*; the editor turns it on (see `application.cpp`).
    // Every other consumer of a QualitySettings -- the render job, the benchmark harness, the GPU
    // tests -- gets a controller that does nothing unless somebody asked for one.
    bool enabled = false;
    // The frame the controller is trying to fit the GPU into. 16.67 ms is one frame at 60 Hz;
    // §3's playhead budget. It is the *GPU* budget, not the wall clock: the wall clock contains
    // costs a resolution cannot touch, and aiming a resolution controller at them would make it
    // scale the world down to punish the CPU.
    double budgetMs = 16.67;
    // The lowest rung the controller may reach, as an index into `kRenderScaleRungs`.
    std::size_t floorRung = kRenderScaleRungs.size() - 1;
    // Frames at a rung before another decision may be taken. A decision costs a render-target
    // reallocation and a screen-space history reset, so decisions have to be rare compared to
    // frames; 30 is half a second at 60 Hz and about six seconds at the 5 fps this is for.
    int dwellFrames = 30;
    // The decision is taken on the median of this many recent GPU samples, so one stalled frame
    // (a shader compile, another agent's process) cannot move the rung.
    int windowFrames = 20;
    // A higher rung must be predicted to fit inside `budgetMs * raiseMargin` before the controller
    // will climb back. Below 1.0 by enough that a frame sitting exactly on the budget does not
    // oscillate between two rungs for as long as it is watched.
    double raiseMargin = 0.80;
    // The GPU must be the binding constraint before a resolution is worth reducing. When the wall
    // clock is much longer than the GPU frame, the frame is waiting on the main thread and
    // shrinking the world buys image quality for nothing -- which is the measured situation on
    // Tree of Life (wall 18.42 ms against a GPU 14.68) and NOT the one on the multicam film.
    // Expressed as "the GPU must be at least this fraction of the wall clock".
    double gpuShareToAct = 0.70;
};

// Pure decision logic: no GPU, no renderer, no clock of its own. Everything it knows arrives
// through `note()`, which makes it a unit test rather than a benchmark.
class InteractiveResolution {
public:
    struct Decision {
        std::size_t rung = 0;
        bool changed = false;
    };

    void configure(const InteractiveResolutionSettings& s);
    [[nodiscard]] const InteractiveResolutionSettings& settings() const { return settings_; }

    // One frame's measurement. `gpuMs < 0` means the GPU timeline had nothing to report (the first
    // frames, or a build without timestamps) and the frame is ignored rather than counted as free.
    Decision note(double gpuMs, double wallMs);

    [[nodiscard]] float scale() const { return kRenderScaleRungs[rung_]; }
    [[nodiscard]] std::size_t rung() const { return rung_; }
    // Put the ladder back at the top and forget the history. Used when the controller is switched
    // off, and when the thing being measured changes underneath it (a new project, a new canvas).
    void reset();

    // §42-style counters, so a run can say what the controller did rather than be asked to be
    // believed. Counts, not durations, so they survive a contended machine intact (ADR-170).
    struct Stats {
        std::uint64_t drops = 0;        // decisions that lowered the rung
        std::uint64_t raises = 0;       // decisions that raised it
        std::uint64_t framesReduced = 0;// frames rendered below rung 0
        std::uint64_t framesSeen = 0;
        std::uint64_t heldByCpu = 0;    // decisions declined because the GPU was not the binder
    };
    [[nodiscard]] const Stats& stats() const { return stats_; }

private:
    [[nodiscard]] double medianGpu() const;

    InteractiveResolutionSettings settings_{};
    std::size_t rung_ = 0;
    int sinceDecision_ = 0;
    std::array<double, 64> gpu_{};
    std::array<double, 64> wall_{};
    std::size_t count_ = 0;
    std::size_t cursor_ = 0;
    Stats stats_{};
};

} // namespace avgen::app
