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

// ---- ADR-1080..1089: the LIVE quality ladder ------------------------------------------------------
//
// The controller above was one lever (the render scale) aimed at one constant (16.67 ms). The live
// quality work (`docs/live-quality/00-brief.md`) keeps its law -- the median of the last 20 GPU
// samples, a dwell between decisions, up to two rungs down at once, one rung up only with margin,
// and the CPU-bound hold -- and changes three things about what it moves and what it aims at:
//
//  * **The budget comes from a target frame rate** the performer picks (60, 90 or 120 fps), with 12%
//    headroom, in one function: `liveBudget()` (ADR-1080). It never reads the display's refresh rate:
//    that is where the picture is shown, not what the performer asked for.
//  * **A rung is a bundle of existing `QualitySettings` fields**, not just a scale: the render scale,
//    the volume march's resolution and step scale, motion blur, depth of field and the shadow
//    cascades and atlas (ADR-1083). Five named levels -- Ultra, High, Medium, Low, Emergency -- so a
//    performer can be told which one is in force in a word.
//  * **The order the levers engage in is the project's** (`live.qualityStrategy`, ADR-1084): pixel-
//    bound scenes give up resolution first, scenes whose cost is fixed give up effects first. One
//    table per strategy, chosen by data; no scene is named anywhere in code.
//
// And one thing about how it climbs back (ADR-1085): a rung that only switches effects back on has
// no pixel ratio to predict its cost by, so the controller remembers the cost ratio it *measured*
// across each step on the way down and uses it on the way up, and it climbs only after the frame
// has fit comfortably for a sustained stretch (`raiseHoldFrames`), not after one good window.
//
// LIVE only. `RenderJob` and the headless runner never construct this, and the Offline tier's
// promise (`QualityPolicy::assertOfflineIsUncompromised`) includes the two effect gates this adds.

#include "rendering/render_quality.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <initializer_list>
#include <string_view>

namespace avgen::app {

// ---- the budget (ADR-1080) ---------------------------------------------------------------------------

// The one place the live frame budget is calculated. `targetFps` is the performer's choice; the GPU
// budget the controller aims at is the frame it implies minus `kLiveBudgetHeadroom`, which leaves
// room for the work the GPU timestamps do not cover (the UI, the projector's copy, the compositor)
// and for the noise a median of twenty frames still carries.
inline constexpr double kLiveBudgetHeadroom = 0.12;
inline constexpr int kLiveTargetFpsMin = 24;
inline constexpr int kLiveTargetFpsMax = 240;
// The targets the Settings panel offers. Any value in [min, max] loads from a settings file.
inline constexpr std::array<int, 3> kLiveTargetChoices{60, 90, 120};

struct LiveBudget {
    double targetFps = 60.0;
    double targetFrameMs = 1000.0 / 60.0;
    double qualityBudgetMs = 1000.0 / 60.0 * (1.0 - kLiveBudgetHeadroom);
};

[[nodiscard]] constexpr LiveBudget liveBudget(double targetFps) {
    const double fps = targetFps < kLiveTargetFpsMin   ? static_cast<double>(kLiveTargetFpsMin)
                       : targetFps > kLiveTargetFpsMax ? static_cast<double>(kLiveTargetFpsMax)
                                                       : targetFps;
    const double frame = 1000.0 / fps;
    return {fps, frame, frame * (1.0 - kLiveBudgetHeadroom)};
}

// ---- the ladder (ADR-1083, ADR-1084) -------------------------------------------------------------------

enum class LiveQualityLevel : std::uint8_t { Ultra, High, Medium, Low, Emergency };
inline constexpr std::size_t kLiveQualityLevels = 5;

[[nodiscard]] constexpr const char* liveQualityLevelName(LiveQualityLevel level) {
    switch (level) {
    case LiveQualityLevel::Ultra: return "Ultra";
    case LiveQualityLevel::High: return "High";
    case LiveQualityLevel::Medium: return "Medium";
    case LiveQualityLevel::Low: return "Low";
    case LiveQualityLevel::Emergency: return "Emergency";
    }
    return "Ultra";
}
// Lower-case tokens for settings files and the command line ("ultra" .. "emergency"). Header-only
// (like the rest of the names here) because the settings and project code that read them is also
// compiled into tools that do not link the controller.
[[nodiscard]] constexpr std::string_view liveQualityLevelToken(LiveQualityLevel level) {
    switch (level) {
    case LiveQualityLevel::Ultra: return "ultra";
    case LiveQualityLevel::High: return "high";
    case LiveQualityLevel::Medium: return "medium";
    case LiveQualityLevel::Low: return "low";
    case LiveQualityLevel::Emergency: return "emergency";
    }
    return "ultra";
}
[[nodiscard]] constexpr std::optional<LiveQualityLevel> liveQualityLevelFromToken(std::string_view token) {
    for (std::size_t i = 0; i < kLiveQualityLevels; ++i) {
        if (token == liveQualityLevelToken(static_cast<LiveQualityLevel>(i))) {
            return static_cast<LiveQualityLevel>(i);
        }
    }
    return std::nullopt;
}

// Which lever a project gives up first. Project data (`live.qualityStrategy`), never inferred from a
// scene's name.
enum class LiveQualityStrategy : std::uint8_t {
    Balanced,        // resolution and the volume march together, then motion blur (the default)
    ResolutionFirst, // pixel-bound scenes (an SDF march, a volume): resolution carries the ladder
    EffectsFirst,    // scenes with a large fixed cost: effects go before the picture gets soft
};
[[nodiscard]] constexpr std::string_view liveQualityStrategyToken(LiveQualityStrategy strategy) {
    switch (strategy) {
    case LiveQualityStrategy::Balanced: return "balanced";
    case LiveQualityStrategy::ResolutionFirst: return "resolution_first";
    case LiveQualityStrategy::EffectsFirst: return "effects_first";
    }
    return "balanced";
}
[[nodiscard]] constexpr std::optional<LiveQualityStrategy> liveQualityStrategyFromToken(std::string_view token) {
    for (const auto s : {LiveQualityStrategy::Balanced, LiveQualityStrategy::ResolutionFirst,
                         LiveQualityStrategy::EffectsFirst}) {
        if (token == liveQualityStrategyToken(s)) {
            return s;
        }
    }
    return std::nullopt;
}
// Plain words for the Live panel ("resolution first").
[[nodiscard]] constexpr const char* liveQualityStrategyLabel(LiveQualityStrategy strategy) {
    switch (strategy) {
    case LiveQualityStrategy::Balanced: return "balanced";
    case LiveQualityStrategy::ResolutionFirst: return "resolution first";
    case LiveQualityStrategy::EffectsFirst: return "effects first";
    }
    return "balanced";
}

// One rung: the values it allows, as ceilings on the live tier's own. A rung never *raises* a
// setting above what the tier (and any quality arm) asked for -- `applyLiveRung` takes the minimum --
// so Ultra is exactly the tier, and a rung's reductions are the only difference a performer sees.
struct LiveQualityRung {
    LiveQualityLevel level = LiveQualityLevel::Ultra;
    float renderScale = 1.0f;            // QualitySettings::renderScale (clamped up to the floor)
    float volumeResolutionScale = 1.0f;  // ceiling on QualitySettings::volumeResolutionScale
    float volumeStepScale = 1.0f;        // ceiling on QualitySettings::volumeStepScale
    bool motionBlur = true;              // QualitySettings::motionBlur
    bool depthOfField = true;            // QualitySettings::depthOfField
    std::uint32_t cascadeCount = 4;      // ceiling on QualitySettings::cascadeCount
    std::uint32_t shadowResolution = 4096; // ceiling on QualitySettings::shadowResolution
    bool reducedShadowFiltering = false; // the Preview tier's filtering: no PCSS, 6 PCF taps
    // ADR-1094..1098 (Stage 2's levers). Ceilings like the rest: each only ever lowers the base's own value. Heroes are
    // exempt from all of them in the renderer (ADR-1097).
    float lodBias = 1.0f;               // floor on QualitySettings::lodBias (higher = coarser)
    float drawDistanceScale = 1.0f;     // ceiling on QualitySettings::drawDistanceScale
    float shadowCasterMinPixels = 0.0f; // floor on QualitySettings::shadowCasterMinPixels
    float postEffectQuality = 1.0f;   // ceiling on QualitySettings::postEffectQuality
    float particleCullDistance = 0.0f;  // QualitySettings::particleCullDistance (0 = none; the nearer limit wins)
    float particleSpawnScale = 1.0f;    // ceiling on QualitySettings::particleSpawnScale
};

using LiveQualityLadder = std::array<LiveQualityRung, kLiveQualityLevels>;
[[nodiscard]] const LiveQualityLadder& liveQualityLadder(LiveQualityStrategy strategy);

// The lowest render scale any ladder uses, and the choices the "lowest adaptive scale" setting
// offers (ADR-1024's floor, extended below 0.5 for pixel-bound scenes, ADR-1083).
inline constexpr float kLiveScaleFloorMin = 0.38f;
inline constexpr std::array<float, 6> kLiveScaleFloorChoices{1.0f, 0.85f, 0.71f, 0.5f, 0.42f, 0.38f};

// `base` with every field the ladder owns set from `rung`: the rung's value where it is lower, the
// base's otherwise; the scale at no less than `scaleFloor`. Fields the ladder does not own are
// `current`'s, untouched, so a setting written elsewhere (the live antialiasing floor) survives a
// rung change. Pure; the unit tests and the application share it.
[[nodiscard]] rendering::QualitySettings applyLiveRung(const rendering::QualitySettings& current,
                                                       const rendering::QualitySettings& base,
                                                       const LiveQualityRung& rung, float scaleFloor);

// The rung as the frame will see it: the scale after the floor.
[[nodiscard]] float effectiveRenderScale(const LiveQualityRung& rung, float scaleFloor);

// ---- ADR-1099: the named profiles, QUALITY / BALANCED / PERFORMANCE ---------------------------------------------------
//
// A profile is a row of the same table family as the ladder's rungs (a `LiveQualityRung` of ceilings), applied to the
// tier BEFORE the ladder: it is the ceiling the live controller works under, so Ultra under PERFORMANCE is the
// PERFORMANCE picture and the five levels go down from there. QUALITY is the tier exactly. Chosen per project
// (`live.profile`), from the Live panel, or with `--live-profile --quality <profile>`.
enum class QualityProfile : std::uint8_t { Quality, Balanced, Performance };
inline constexpr std::size_t kQualityProfiles = 3;
[[nodiscard]] std::string_view qualityProfileToken(QualityProfile profile);
[[nodiscard]] const char* qualityProfileLabel(QualityProfile profile); // "QUALITY", ...
[[nodiscard]] std::optional<QualityProfile> qualityProfileFromToken(std::string_view token);
[[nodiscard]] const LiveQualityRung& qualityProfileCeiling(QualityProfile profile);
// The profile's ceilings applied to `q` (the scale too, which no floor raises).
[[nodiscard]] rendering::QualitySettings applyQualityProfile(const rendering::QualitySettings& q, QualityProfile profile);

// ---- the controller --------------------------------------------------------------------------------------

struct InteractiveResolutionSettings {
    // Off is the honest default for the *type*; the editor turns it on (see `application.cpp`).
    // Every other consumer of a QualitySettings -- the render job, the benchmark harness, the GPU
    // tests -- gets a controller that does nothing unless somebody asked for one.
    bool enabled = false;
    // The GPU frame time the controller aims at (ADR-1080: `liveBudget(targetFps).qualityBudgetMs`).
    // It is the *GPU* budget, not the wall clock: the wall clock contains costs a quality level
    // cannot touch, and aiming the ladder at them would degrade the picture to punish the CPU (§8).
    double budgetMs = liveBudget(60.0).qualityBudgetMs;
    // The target the budget came from, kept for the status line.
    double targetFps = 60.0;
    // Which ladder (ADR-1084).
    LiveQualityStrategy strategy = LiveQualityStrategy::Balanced;
    // The lowest render scale (ADR-1024's setting). Rungs below it keep their other reductions at
    // this scale, so a raised floor still lets the effects go.
    float scaleFloor = kLiveScaleFloorMin;
    // Frames at a rung before another decision may be taken. A decision costs a render-target
    // reallocation and a screen-space history reset, so decisions have to be rare compared to
    // frames; 30 is half a second at 60 Hz.
    int dwellFrames = 30;
    // The decision is taken on the median of this many recent GPU samples, so one stalled frame
    // (a shader compile, another agent's process) cannot move the rung.
    int windowFrames = 20;
    // A higher rung must be predicted to fit inside `budgetMs * raiseMargin` ...
    double raiseMargin = 0.80;
    // ... for this many consecutive frames before the controller climbs (ADR-1085). Two seconds at
    // 60 fps: recovering is the cautious direction, degrading the quick one.
    int raiseHoldFrames = 120;
    // The GPU must be the binding constraint before quality is worth reducing: when the wall clock
    // is much longer than the GPU frame, the frame is waiting on the main thread and a smaller,
    // plainer world buys nothing. "The GPU must be at least this fraction of the wall clock".
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

    // The rung in force, its ladder entry, and the scale the frame renders at.
    [[nodiscard]] std::size_t rung() const { return rung_; }
    [[nodiscard]] const LiveQualityRung& current() const { return liveQualityLadder(settings_.strategy)[rung_]; }
    [[nodiscard]] LiveQualityLevel level() const { return current().level; }
    [[nodiscard]] float scale() const { return effectiveRenderScale(current(), settings_.scaleFloor); }
    // The GPU cost a decision now would see (-1 before the first sample): the median timestamp span,
    // capped by the mean frame interval (ADR-1085).
    [[nodiscard]] double gpuCostMs() const { return gpuCost(); }
    // Put the ladder back at the top and forget the history and the learned step costs. Used when
    // the controller is switched off, and when the thing being measured changes underneath it (a
    // new project, a new strategy).
    void reset();

    // The cost ratio between rung k-1 and rung k (cost(k-1) / cost(k)) the controller would use to
    // predict a climb from k: measured on the way down when it has been, the prior otherwise.
    [[nodiscard]] double stepRatio(std::size_t k) const;

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
    [[nodiscard]] double medianWall() const;
    [[nodiscard]] double meanWall() const;
    [[nodiscard]] double gpuCost() const;
    [[nodiscard]] double priorRatio(std::size_t k) const;
    void moveTo(std::size_t rung, double medianAtLeave);

    InteractiveResolutionSettings settings_{};
    std::size_t rung_ = 0;
    int sinceDecision_ = 0;
    int raiseStreak_ = 0;
    std::array<double, 64> gpu_{};
    std::array<double, 64> wall_{};
    std::size_t count_ = 0;
    std::size_t cursor_ = 0;
    // ADR-1085: measured cost ratio across step k (rung k-1 -> k), 0 = not measured. Index 0 unused.
    std::array<double, kLiveQualityLevels> measuredRatio_{};
    // The step just taken, waiting for the new rung's window to fill so its ratio can be measured.
    struct Pending {
        std::size_t from = 0;
        std::size_t to = 0;
        double fromMs = 0.0;
        bool active = false;
    };
    Pending pending_{};
    Stats stats_{};
};

} // namespace avgen::app
