#include "app/interactive_resolution.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace avgen::app {

double liveFrameCapPeriodMs(bool enabled, double targetFps, double refreshHz) {
    if (!enabled || !(targetFps > 0.0)) {
        return 0.0;
    }
    if (!(refreshHz > 0.0)) {
        return 1000.0 / targetFps;
    }
    // 1% tolerance: a 59.94 Hz display with a 60 target is "equal", not "above".
    if (targetFps > refreshHz * 1.01) {
        return 0.0;
    }
    const double vsyncs = std::max(1.0, std::floor(refreshHz / targetFps + 0.01));
    return vsyncs * 1000.0 / refreshHz;
}

LivePaceStep livePaceStep(double nowMs, double deadlineMs, double periodMs) {
    if (!(periodMs > 0.0)) {
        return {0.0, 0.0};
    }
    if (!(deadlineMs > 0.0)) {
        return {0.0, nowMs + periodMs};
    }
    if (nowMs < deadlineMs) {
        return {deadlineMs - nowMs, deadlineMs + periodMs};
    }
    if (nowMs - deadlineMs > periodMs * 0.5) {
        return {0.0, nowMs + periodMs};
    }
    return {0.0, deadlineMs + periodMs};
}

// ---- the ladders (ADR-1083, ADR-1084) -------------------------------------------------------------
//
// Every value is a ceiling on the live tier's own (see `applyLiveRung`). The realtime tier marches
// volumes at half the scene's resolution with the authored step count, three cascades in a 2048
// atlas; the reductions below are the Preview tier's own values for the same fields (quarter-
// resolution volumes, half the steps, two cascades, a 1024 atlas, no PCSS) -- existing settings at
// existing values, not new ones -- plus a quarter of the steps and a scale below 0.5 at Emergency.
//
// The orders come from the measured costs (docs/live-quality/evidence-live-projection-2026-10-02.md
// §D): Liminal's frame is 97% per-pixel, so resolution carries its ladder; Glowmere's is 86% per-
// pixel with a volume march and motion blur that are large at every size, so they go alongside the
// scale; Sonic's is about half fixed (vertex work and shadows) and its thin geometry beads below
// 0.71 (ADR-1024), so its effects go before its sharpness.
namespace {
using L = LiveQualityLevel;
constexpr LiveQualityRung rung(L level, float scale) {
    LiveQualityRung r;
    r.level = level;
    r.renderScale = scale;
    return r;
}
constexpr LiveQualityRung volumes(LiveQualityRung r, float resolution, float steps = 1.0f) {
    r.volumeResolutionScale = resolution;
    r.volumeStepScale = steps;
    return r;
}
constexpr LiveQualityRung noMotionBlur(LiveQualityRung r) {
    r.motionBlur = false;
    return r;
}
constexpr LiveQualityRung noDepthOfField(LiveQualityRung r) {
    r.depthOfField = false;
    return r;
}
constexpr LiveQualityRung cascades(LiveQualityRung r, std::uint32_t n) {
    r.cascadeCount = n;
    return r;
}
constexpr LiveQualityRung plainShadows(LiveQualityRung r) {
    r.shadowResolution = 1024;
    r.reducedShadowFiltering = true;
    return r;
}
// ADR-1094..1098: the background levers -- small casters stop casting, far emitters stop, LOD comes sooner. The brief's
// "first" group (background shadows, background particles); heroes are exempt in the renderer. Only on the two lowest
// levels, where the picture is already being traded, so the levels a 60 target settles on are unchanged.
constexpr LiveQualityRung background(LiveQualityRung r, float casterPixels, float particleMetres, float lod) {
    r.shadowCasterMinPixels = casterPixels;
    r.particleCullDistance = particleMetres;
    r.lodBias = lod;
    return r;
}

constexpr LiveQualityLadder kResolutionFirst{
    rung(L::Ultra, 1.0f),
    rung(L::High, 0.85f),
    volumes(rung(L::Medium, 0.71f), 0.25f),
    background(cascades(noDepthOfField(noMotionBlur(volumes(rung(L::Low, 0.5f), 0.25f, 0.5f))), 2), 12.0f, 80.0f, 1.5f),
    background(plainShadows(cascades(noDepthOfField(noMotionBlur(volumes(rung(L::Emergency, 0.38f), 0.25f, 0.25f))), 2)),
               24.0f, 50.0f, 2.0f),
};

constexpr LiveQualityLadder kBalanced{
    rung(L::Ultra, 1.0f),
    volumes(rung(L::High, 0.85f), 0.25f),
    noMotionBlur(volumes(rung(L::Medium, 0.71f), 0.25f)),
    background(cascades(noDepthOfField(noMotionBlur(volumes(rung(L::Low, 0.5f), 0.25f, 0.5f))), 2), 12.0f, 80.0f, 1.5f),
    background(plainShadows(cascades(noDepthOfField(noMotionBlur(volumes(rung(L::Emergency, 0.42f), 0.25f, 0.25f))), 2)),
               24.0f, 50.0f, 2.0f),
};

constexpr LiveQualityLadder kEffectsFirst{
    rung(L::Ultra, 1.0f),
    cascades(noDepthOfField(volumes(rung(L::High, 1.0f), 0.25f)), 2),
    plainShadows(cascades(noDepthOfField(noMotionBlur(volumes(rung(L::Medium, 0.85f), 0.25f, 0.5f))), 2)),
    background(plainShadows(cascades(noDepthOfField(noMotionBlur(volumes(rung(L::Low, 0.71f), 0.25f, 0.5f))), 2)), 12.0f,
               80.0f, 1.5f),
    background(plainShadows(cascades(noDepthOfField(noMotionBlur(volumes(rung(L::Emergency, 0.5f), 0.25f, 0.25f))), 2)),
               24.0f, 50.0f, 2.0f),
};

// ADR-1099: the profiles, rows of the same family. QUALITY is the tier. BALANCED spends nothing on what is small or far
// (the background levers, and a quarter-resolution fog). PERFORMANCE adds the half-resolution post effects, fewer
// particles, two cascades in a 1024 atlas and an 85% scale.
constexpr LiveQualityRung kProfileQuality = rung(L::Ultra, 1.0f);
constexpr LiveQualityRung kProfileBalanced = background(volumes(rung(L::Ultra, 1.0f), 0.5f), 8.0f, 120.0f, 1.25f);
constexpr LiveQualityRung profilePerformance() {
    LiveQualityRung r = background(plainShadows(cascades(volumes(rung(L::Ultra, 0.85f), 0.25f), 2)), 24.0f, 60.0f, 2.0f);
    r.postEffectQuality = 0.5f;
    r.particleSpawnScale = 0.7f;
    r.drawDistanceScale = 0.8f;
    return r;
}
constexpr LiveQualityRung kProfilePerformance = profilePerformance();

// The number of effect levers (everything but the scale) that differ between two rungs.
int effectChanges(const LiveQualityRung& a, const LiveQualityRung& b) {
    return (a.volumeResolutionScale != b.volumeResolutionScale ? 1 : 0) +
           (a.volumeStepScale != b.volumeStepScale ? 1 : 0) + (a.motionBlur != b.motionBlur ? 1 : 0) +
           (a.depthOfField != b.depthOfField ? 1 : 0) + (a.cascadeCount != b.cascadeCount ? 1 : 0) +
           (a.shadowResolution != b.shadowResolution ? 1 : 0) +
           (a.reducedShadowFiltering != b.reducedShadowFiltering ? 1 : 0) + (a.lodBias != b.lodBias ? 1 : 0) +
           (a.drawDistanceScale != b.drawDistanceScale ? 1 : 0) +
           (a.shadowCasterMinPixels != b.shadowCasterMinPixels ? 1 : 0) +
           (a.postEffectQuality != b.postEffectQuality ? 1 : 0) +
           (a.particleCullDistance != b.particleCullDistance ? 1 : 0) +
           (a.particleSpawnScale != b.particleSpawnScale ? 1 : 0);
}
} // namespace

const LiveQualityLadder& liveQualityLadder(LiveQualityStrategy strategy) {
    switch (strategy) {
    case LiveQualityStrategy::ResolutionFirst: return kResolutionFirst;
    case LiveQualityStrategy::EffectsFirst: return kEffectsFirst;
    case LiveQualityStrategy::Balanced: break;
    }
    return kBalanced;
}

float effectiveRenderScale(const LiveQualityRung& rung, float scaleFloor) {
    return std::clamp(std::max(rung.renderScale, scaleFloor), kLiveScaleFloorMin, 1.0f);
}

namespace {
// The Stage 2 levers as ceilings over `base`: never a better value than the base asked for.
void applyStageTwoCeilings(rendering::QualitySettings& q, const rendering::QualitySettings& base,
                           const LiveQualityRung& rung) {
    q.lodBias = std::max(base.lodBias, rung.lodBias);
    q.drawDistanceScale = std::min(base.drawDistanceScale, rung.drawDistanceScale);
    q.shadowCasterMinPixels = std::max(base.shadowCasterMinPixels, rung.shadowCasterMinPixels);
    q.postEffectQuality = std::min(base.postEffectQuality, rung.postEffectQuality);
    q.particleCullDistance = base.particleCullDistance <= 0.0f   ? rung.particleCullDistance
                             : rung.particleCullDistance <= 0.0f ? base.particleCullDistance
                                                                 : std::min(base.particleCullDistance,
                                                                            rung.particleCullDistance);
    q.particleSpawnScale = std::min(base.particleSpawnScale, rung.particleSpawnScale);
}
} // namespace

rendering::QualitySettings applyLiveRung(const rendering::QualitySettings& current,
                                         const rendering::QualitySettings& base, const LiveQualityRung& rung,
                                         float scaleFloor) {
    rendering::QualitySettings q = current;
    // The scale too is a ceiling: a quality arm that asked for less (`--quality-arm scale71`) keeps it.
    q.renderScale = std::min(base.renderScale > 0.0f ? base.renderScale : 1.0f, effectiveRenderScale(rung, scaleFloor));
    q.volumeResolutionScale = std::min(base.volumeResolutionScale, rung.volumeResolutionScale);
    q.volumeStepScale = std::min(base.volumeStepScale, rung.volumeStepScale);
    q.motionBlur = base.motionBlur && rung.motionBlur;
    q.depthOfField = base.depthOfField && rung.depthOfField;
    q.cascadeCount = std::min(base.cascadeCount, rung.cascadeCount);
    q.shadowResolution = std::min(base.shadowResolution, rung.shadowResolution);
    if (rung.reducedShadowFiltering) {
        // The Preview tier's filtering (render_quality.hpp), never more than the base asked for.
        q.softShadows = false;
        q.shadowPcfTaps = std::min(base.shadowPcfTaps, 6u);
        q.pcssBlockerTaps = std::min(base.pcssBlockerTaps, 6u);
    } else {
        q.softShadows = base.softShadows;
        q.shadowPcfTaps = base.shadowPcfTaps;
        q.pcssBlockerTaps = base.pcssBlockerTaps;
    }
    applyStageTwoCeilings(q, base, rung);
    return q;
}

std::string_view qualityProfileToken(QualityProfile profile) {
    switch (profile) {
    case QualityProfile::Quality: return "quality";
    case QualityProfile::Balanced: return "balanced";
    case QualityProfile::Performance: return "performance";
    }
    return "quality";
}

const char* qualityProfileLabel(QualityProfile profile) {
    switch (profile) {
    case QualityProfile::Quality: return "QUALITY";
    case QualityProfile::Balanced: return "BALANCED";
    case QualityProfile::Performance: return "PERFORMANCE";
    }
    return "QUALITY";
}

std::optional<QualityProfile> qualityProfileFromToken(std::string_view token) {
    for (const auto p : {QualityProfile::Quality, QualityProfile::Balanced, QualityProfile::Performance}) {
        if (token == qualityProfileToken(p)) {
            return p;
        }
    }
    return std::nullopt;
}

const LiveQualityRung& qualityProfileCeiling(QualityProfile profile) {
    switch (profile) {
    case QualityProfile::Quality: return kProfileQuality;
    case QualityProfile::Balanced: return kProfileBalanced;
    case QualityProfile::Performance: return kProfilePerformance;
    }
    return kProfileQuality;
}

rendering::QualitySettings applyQualityProfile(const rendering::QualitySettings& q, QualityProfile profile) {
    return applyCeiling(q, qualityProfileCeiling(profile));
}

rendering::QualitySettings applyCeiling(const rendering::QualitySettings& q, const LiveQualityRung& c) {
    rendering::QualitySettings out = q;
    out.renderScale = std::min(q.renderScale > 0.0f ? q.renderScale : 1.0f, c.renderScale);
    out.volumeResolutionScale = std::min(q.volumeResolutionScale, c.volumeResolutionScale);
    out.volumeStepScale = std::min(q.volumeStepScale, c.volumeStepScale);
    out.motionBlur = q.motionBlur && c.motionBlur;
    out.depthOfField = q.depthOfField && c.depthOfField;
    out.cascadeCount = std::min(q.cascadeCount, c.cascadeCount);
    out.shadowResolution = std::min(q.shadowResolution, c.shadowResolution);
    if (c.reducedShadowFiltering) {
        out.softShadows = false;
        out.shadowPcfTaps = std::min(q.shadowPcfTaps, 6u);
        out.pcssBlockerTaps = std::min(q.pcssBlockerTaps, 6u);
    }
    applyStageTwoCeilings(out, q, c);
    return out;
}

// ---- the controller ------------------------------------------------------------------------------

void InteractiveResolution::configure(const InteractiveResolutionSettings& s) {
    const InteractiveResolutionSettings before = settings_;
    settings_ = s;
    settings_.windowFrames = std::clamp(settings_.windowFrames, 1, static_cast<int>(gpu_.size()));
    // The dwell must cover the window, and this is load-bearing rather than tidiness. After a rung
    // change the ring still holds the *previous* rung's costs; if a decision could be taken before
    // the window had refilled, the controller would read the old rung's cost, conclude it was still
    // over budget and drop again -- a double step on stale evidence, every time, all the way to the
    // floor. Clamped here so that a settings file or a test cannot express the configuration in
    // which that happens.
    settings_.dwellFrames = std::max({1, settings_.dwellFrames, settings_.windowFrames});
    settings_.raiseHoldFrames = std::max(1, settings_.raiseHoldFrames);
    settings_.scaleFloor = std::clamp(settings_.scaleFloor, kLiveScaleFloorMin, 1.0f);
    settings_.lowestLevel = std::min(settings_.lowestLevel, kLiveQualityLevels - 1);
    if (!settings_.enabled || settings_.strategy != before.strategy || settings_.ladder != before.ladder) {
        // A different ladder: rung k means something else, and what was learned about it does not
        // carry over.
        reset();
    } else if (settings_.scaleFloor != before.scaleFloor) {
        // The same ladder at different scales: the measured step costs are stale.
        measuredRatio_.fill(0.0);
        pending_ = {};
    }
}

void InteractiveResolution::reset() {
    rung_ = 0;
    sinceDecision_ = 0;
    raiseStreak_ = 0;
    count_ = 0;
    cursor_ = 0;
    measuredRatio_.fill(0.0);
    pending_ = {};
    probation_ = false;
    holdScale_.fill(1);
    unsustainable_ = false;
}

double InteractiveResolution::medianGpu() const {
    // The median of the window, not the mean: one 200 ms frame where a shader compiled or another
    // agent's process landed is not evidence that the world is too big, and a mean would let it
    // move the rung on its own.
    const std::size_t n = std::min(count_, static_cast<std::size_t>(settings_.windowFrames));
    if (n == 0) {
        return -1.0;
    }
    std::vector<double> v;
    v.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        v.push_back(gpu_[(cursor_ + gpu_.size() - 1 - i) % gpu_.size()]);
    }
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(n / 2), v.end());
    return v[n / 2];
}

double InteractiveResolution::meanWall() const {
    const std::size_t n = std::min(count_, static_cast<std::size_t>(settings_.windowFrames));
    double sum = 0.0;
    std::size_t used = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const double w = wall_[(cursor_ + wall_.size() - 1 - i) % wall_.size()];
        if (w > 0.0) {
            sum += w;
            ++used;
        }
    }
    return used > 0 ? sum / static_cast<double>(used) : -1.0;
}

double InteractiveResolution::gpuCost() const {
    // ADR-1085: the GPU timestamp span (first pass begins to last pass ends) over-states a frame's
    // cost when consecutive frames overlap on the GPU -- measured on Liminal at Low: an 11-12 ms span
    // median while frames arrived at about 110 fps, which walked the ladder to Emergency. Frames
    // that arrive at a mean interval of T cannot be costing the GPU more than T each, so the cost is
    // the smaller of the two. The *mean* interval, not the median: under Fifo the intervals are
    // vsync multiples, and a GPU-bound 14 ms frame on a 120 Hz display alternates 8.3 and 25 ms --
    // a median of 8.3 would under-read it by half. A stall only raises the mean, so it can never
    // make this smaller than the span says.
    const double span = medianGpu();
    const double interval = meanWall();
    return interval > 0.0 && interval < span ? interval : span;
}

double InteractiveResolution::medianWall() const {
    const std::size_t n = std::min(count_, static_cast<std::size_t>(settings_.windowFrames));
    if (n == 0) {
        return -1.0;
    }
    std::vector<double> w;
    w.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        w.push_back(wall_[(cursor_ + wall_.size() - 1 - i) % wall_.size()]);
    }
    std::nth_element(w.begin(), w.begin() + static_cast<std::ptrdiff_t>(n / 2), w.end());
    return w[n / 2];
}

double InteractiveResolution::priorRatio(std::size_t k) const {
    // The pixel ratio between two rungs. `renderScale` is linear, so the ratio of pixels is the
    // ratio of the squares; `SceneRenderer::resize` rounds each axis to even, which moves this by
    // well under a percent and is not worth modelling here.
    const LiveQualityLadder& ladder = this->ladder();
    const double a = effectiveRenderScale(ladder[k - 1], settings_.scaleFloor);
    const double b = effectiveRenderScale(ladder[k], settings_.scaleFloor);
    return (a * a) / (b * b);
}

double InteractiveResolution::stepRatio(std::size_t k) const {
    if (k == 0 || k >= kLiveQualityLevels) {
        return 1.0;
    }
    if (measuredRatio_[k] > 0.0) {
        return measuredRatio_[k];
    }
    // Not measured: the climb is predicted with the pixel ratio, which the affine law says
    // over-states the higher rung's cost (its fixed term does not grow) -- conservative in the right
    // direction -- and a quarter more for each effect the climb turns back on, since a rung that only
    // switches effects on has no pixel ratio to be careful with. In practice every rung above the
    // current one was left on the way down, so this is the case only after a reset or a 2-rung drop.
    const LiveQualityLadder& ladder = this->ladder();
    return priorRatio(k) * (1.0 + 0.25 * effectChanges(ladder[k - 1], ladder[k]));
}

void InteractiveResolution::moveTo(std::size_t rung, double medianAtLeave) {
    pending_ = {rung_, rung, medianAtLeave, true};
    rung_ = rung;
    sinceDecision_ = 0;
    raiseStreak_ = 0;
}

InteractiveResolution::Decision InteractiveResolution::note(double gpuMs, double wallMs) {
    Decision d{rung_, false};
    if (!settings_.enabled) {
        return d;
    }
    ++stats_.framesSeen;
    if (rung_ > 0) {
        ++stats_.framesReduced;
    }
    // A frame the GPU timeline could not report is not a free frame. FrameTimeline has no completed
    // frame for the first two or three of a session and none at all on a build without timestamp
    // queries, and counting those as 0 ms would walk the ladder straight back to the top.
    if (!(gpuMs >= 0.0)) {
        return d;
    }
    gpu_[cursor_] = gpuMs;
    wall_[cursor_] = wallMs;
    cursor_ = (cursor_ + 1) % gpu_.size();
    ++count_;
    ++sinceDecision_;

    const auto window = static_cast<std::size_t>(settings_.windowFrames);
    if (count_ < window) {
        return d;
    }
    const double gpu = gpuCost();
    if (!(gpu > 0.0)) {
        return d;
    }
    // ADR-1085: the step just taken, measured, once the new rung's window holds only its own frames.
    // The ratio is cost(higher-quality rung) / cost(lower-quality rung) across each step crossed;
    // a two-rung move is split evenly in log space. Clamped to [1, 4]: content moving under a
    // measurement can make a step look free or ruinous, and neither should steer the climb.
    if (pending_.active && sinceDecision_ >= settings_.windowFrames) {
        const std::size_t hi = std::min(pending_.from, pending_.to);
        const std::size_t lo = std::max(pending_.from, pending_.to);
        const double higherCost = pending_.from < pending_.to ? pending_.fromMs : gpu;
        const double lowerCost = pending_.from < pending_.to ? gpu : pending_.fromMs;
        if (lo > hi && higherCost > 0.0 && lowerCost > 0.0) {
            const double perStep = std::pow(higherCost / lowerCost, 1.0 / static_cast<double>(lo - hi));
            for (std::size_t k = hi + 1; k <= lo; ++k) {
                measuredRatio_[k] = std::clamp(perStep, 1.0, 4.0);
            }
        }
        pending_.active = false;
    }
    if (sinceDecision_ < settings_.dwellFrames) {
        return d;
    }
    const double budget = settings_.budgetMs;
    // ADR-1103: the project's minimum is the bottom of the ladder.
    const std::size_t last = settings_.lowestLevel;
    // ADR-1104: the first decision after a raise is the raise's measurement.
    const bool onProbation = probation_;
    probation_ = false;

    if (gpu > budget) {
        raiseStreak_ = 0;
        if (rung_ >= last) {
            unsustainable_ = true; // at the minimum and still over: said, never pushed past (ADR-1103)
            if (rung_ > last) {
                moveTo(last, gpu); // the minimum was raised under us
                return {rung_, true};
            }
            // The bottom of the ladder: nothing left to give. Judged once per dwell like every other decision --
            // re-judged every frame, a median on the budget line flipped the verdict frame to frame and the Live
            // panel's "LIVE TARGET UNSUSTAINABLE" flickered with it.
            sinceDecision_ = 0;
            return d;
        }
        unsustainable_ = false;
        if (onProbation) {
            // The raise did not hold: back down one level, and wait twice as long before trying it again.
            holdScale_[rung_] = std::min(8, holdScale_[rung_] * 2);
            moveTo(rung_ + 1, gpu);
            ++stats_.reverts;
            ++stats_.drops;
            return {rung_, true};
        }
        // The GPU has to be what the frame is waiting for (§8: a CPU-bound frame is a warning, not
        // a reason to degrade the picture).
        const double wall = medianWall();
        if (wall > 0.0 && gpu < wall * settings_.gpuShareToAct) {
            ++stats_.heldByCpu;
            sinceDecision_ = 0;
            return d;
        }
        // How far down to go. Unmeasured steps are predicted by their pixel ratio alone, which
        // under-states the saving of a step that also drops effects -- so the step chosen is never
        // larger than the evidence supports, and a frame that needs more takes another step at the
        // next decision. Capped at two rungs so a heavy scene does not watch the ladder walk down.
        std::size_t want = rung_;
        double predicted = gpu;
        for (std::size_t step = 1; step <= 2 && rung_ + step <= last; ++step) {
            want = rung_ + step;
            const bool measured = measuredRatio_[want] > 0.0;
            predicted /= measured ? measuredRatio_[want] : priorRatio(want);
            // An unmeasured step that changes effects and not the scale has no prediction at all
            // (its pixel ratio is 1): take it alone and measure it, rather than skip past it to a
            // softer picture on the strength of a zero that only means "unknown".
            if (predicted <= budget || (!measured && priorRatio(want) <= 1.0)) {
                break;
            }
        }
        moveTo(want, gpu);
        ++stats_.drops;
        return {rung_, true};
    }

    unsustainable_ = false;
    if (rung_ > last) {
        moveTo(last, gpu); // a minimum raised above the level in force: go up to it
        return {rung_, true};
    }
    if (rung_ > 0) {
        // One rung at a time on the way up, only when the higher rung is predicted to fit with
        // margin, and only once that has been true for `raiseHoldFrames` frames in a row (ADR-1085),
        // longer for a raise that has already failed (ADR-1104).
        if (gpu * stepRatio(rung_) <= budget * settings_.raiseMargin) {
            ++raiseStreak_;
        } else {
            raiseStreak_ = 0;
        }
        if (raiseStreak_ >= settings_.raiseHoldFrames * holdScale_[rung_ - 1]) {
            moveTo(rung_ - 1, gpu);
            ++stats_.raises;
            probation_ = true;
            return {rung_, true};
        }
    }
    return d;
}

} // namespace avgen::app

namespace avgen::app {

// ---- ADR-1105 ----------------------------------------------------------------------------------------------------

namespace {
void giveUp(LiveQualityRung& r, std::string_view group, int depth) {
    if (depth <= 0) {
        return;
    }
    const bool full = depth >= 2;
    if (group == "particles") {
        r.particleCullDistance = full ? 50.0f : 80.0f;
        r.particleSpawnScale = full ? 0.6f : 0.85f;
    } else if (group == "shadows") {
        r.shadowCasterMinPixels = full ? 24.0f : 12.0f;
        r.cascadeCount = 2;
        if (full) {
            r.shadowResolution = 1024;
            r.reducedShadowFiltering = true;
        }
    } else if (group == "volumes") {
        r.volumeResolutionScale = 0.25f;
        r.volumeStepScale = full ? 0.5f : 1.0f;
    } else if (group == "post") {
        r.postEffectQuality = 0.5f;
        if (full) {
            r.motionBlur = false;
            r.depthOfField = false;
        }
    } else if (group == "lod") {
        r.lodBias = full ? 2.0f : 1.5f;
        r.drawDistanceScale = full ? 0.75f : 0.9f;
    } else if (group == "resolution") {
        r.renderScale = full ? 0.5f : 0.71f;
    }
}
} // namespace

std::optional<LiveQualityLadder> ladderFromPriority(const std::vector<std::string>& groups, std::string& error) {
    error.clear();
    std::vector<std::string> order;
    for (const std::string& g : groups) {
        if (std::find(kLeverGroups.begin(), kLeverGroups.end(), g) == kLeverGroups.end()) {
            error = "'" + g + "' is not a lever group (particles, shadows, volumes, post, lod, resolution)";
            return std::nullopt;
        }
        if (std::find(order.begin(), order.end(), g) != order.end()) {
            error = "'" + g + "' is named twice";
            return std::nullopt;
        }
        order.push_back(g);
    }
    if (order.empty()) {
        error = "the priority names no group";
        return std::nullopt;
    }
    // depth[level][i]: how far group i (in priority order) is given up at that level.
    const auto depthAt = [&](std::size_t level, std::size_t i) -> int {
        switch (level) {
        case 1: return i == 0 ? 1 : 0;
        case 2: return i == 0 ? 2 : i == 1 ? 1 : 0;
        case 3: return i <= 1 ? 2 : i <= 3 ? 1 : 0;
        case 4: return 2;
        default: return 0;
        }
    };
    LiveQualityLadder ladder{};
    for (std::size_t level = 0; level < kLiveQualityLevels; ++level) {
        LiveQualityRung& r = ladder[level];
        r.level = static_cast<LiveQualityLevel>(level);
        for (std::size_t i = 0; i < order.size(); ++i) {
            giveUp(r, order[i], depthAt(level, i));
        }
        if (level + 1 == kLiveQualityLevels) {
            for (const std::string_view g : kLeverGroups) {
                if (std::find(order.begin(), order.end(), g) == order.end()) {
                    giveUp(r, g, 1); // unnamed: a little, and only at the last level
                }
            }
            if (std::find(order.begin(), order.end(), "resolution") != order.end()) {
                r.renderScale = 0.38f; // named: the full ladder's last step
            }
        }
    }
    return ladder;
}

// ---- ADR-1101 ----------------------------------------------------------------------------------------------------

bool applyLeverToCeiling(LiveQualityRung& c, std::string_view lever) {
    if (lever == "volumequarter") c.volumeResolutionScale = std::min(c.volumeResolutionScale, 0.25f);
    else if (lever == "volumesteps") c.volumeStepScale = std::min(c.volumeStepScale, 0.5f);
    else if (lever == "volumepreview") { c.volumeResolutionScale = std::min(c.volumeResolutionScale, 0.25f); c.volumeStepScale = std::min(c.volumeStepScale, 0.5f); }
    else if (lever == "posttaps") c.postEffectQuality = std::min(c.postEffectQuality, 0.5f);
    else if (lever == "nomotionblur") c.motionBlur = false;
    else if (lever == "nodof") c.depthOfField = false;
    else if (lever == "castercull") c.shadowCasterMinPixels = std::max(c.shadowCasterMinPixels, 24.0f);
    else if (lever == "shadowatlas1k") c.shadowResolution = std::min<std::uint32_t>(c.shadowResolution, 1024u);
    else if (lever == "lodbias2") c.lodBias = std::max(c.lodBias, 2.0f);
    else if (lever == "drawdist75") c.drawDistanceScale = std::min(c.drawDistanceScale, 0.75f);
    else if (lever == "particlelod") {
        c.particleSpawnScale = std::min(c.particleSpawnScale, 0.7f);
        c.particleCullDistance = c.particleCullDistance > 0.0f ? std::min(c.particleCullDistance, 60.0f) : 60.0f;
    } else if (lever == "scale85") c.renderScale = std::min(c.renderScale, 0.85f);
    else if (lever == "scale71") c.renderScale = std::min(c.renderScale, 0.71f);
    else if (lever == "pcss") c.reducedShadowFiltering = true;
    else return false;
    return true;
}

std::string ceilingJsonText(const LiveQualityRung& c) {
    const LiveQualityRung n{};
    nlohmann::json j = nlohmann::json::object();
    if (c.renderScale != n.renderScale) j["renderScale"] = c.renderScale;
    if (c.volumeResolutionScale != n.volumeResolutionScale) j["volumeResolutionScale"] = c.volumeResolutionScale;
    if (c.volumeStepScale != n.volumeStepScale) j["volumeStepScale"] = c.volumeStepScale;
    if (c.motionBlur != n.motionBlur) j["motionBlur"] = c.motionBlur;
    if (c.depthOfField != n.depthOfField) j["depthOfField"] = c.depthOfField;
    if (c.cascadeCount != n.cascadeCount) j["cascadeCount"] = c.cascadeCount;
    if (c.shadowResolution != n.shadowResolution) j["shadowResolution"] = c.shadowResolution;
    if (c.reducedShadowFiltering != n.reducedShadowFiltering) j["reducedShadowFiltering"] = c.reducedShadowFiltering;
    if (c.lodBias != n.lodBias) j["lodBias"] = c.lodBias;
    if (c.drawDistanceScale != n.drawDistanceScale) j["drawDistanceScale"] = c.drawDistanceScale;
    if (c.shadowCasterMinPixels != n.shadowCasterMinPixels) j["shadowCasterMinPixels"] = c.shadowCasterMinPixels;
    if (c.postEffectQuality != n.postEffectQuality) j["postEffectQuality"] = c.postEffectQuality;
    if (c.particleCullDistance != n.particleCullDistance) j["particleCullDistance"] = c.particleCullDistance;
    if (c.particleSpawnScale != n.particleSpawnScale) j["particleSpawnScale"] = c.particleSpawnScale;
    return j.dump();
}

std::optional<LiveQualityRung> ceilingFromJsonText(const std::string& text, std::string& error) {
    error.clear();
    const nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
    if (!j.is_object()) {
        error = "must be an object";
        return std::nullopt;
    }
    LiveQualityRung c{};
    for (const auto& [key, v] : j.items()) {
        const bool num = v.is_number();
        const bool flag = v.is_boolean();
        if (key == "renderScale" && num) c.renderScale = std::clamp(v.get<float>(), kLiveScaleFloorMin, 1.0f);
        else if (key == "volumeResolutionScale" && num) c.volumeResolutionScale = std::clamp(v.get<float>(), 0.1f, 1.0f);
        else if (key == "volumeStepScale" && num) c.volumeStepScale = std::clamp(v.get<float>(), 0.1f, 1.0f);
        else if (key == "motionBlur" && flag) c.motionBlur = v.get<bool>();
        else if (key == "depthOfField" && flag) c.depthOfField = v.get<bool>();
        else if (key == "cascadeCount" && num) c.cascadeCount = std::clamp(v.get<std::uint32_t>(), 1u, 4u);
        else if (key == "shadowResolution" && num) c.shadowResolution = std::clamp(v.get<std::uint32_t>(), 256u, 4096u);
        else if (key == "reducedShadowFiltering" && flag) c.reducedShadowFiltering = v.get<bool>();
        else if (key == "lodBias" && num) c.lodBias = std::clamp(v.get<float>(), 1.0f, 8.0f);
        else if (key == "drawDistanceScale" && num) c.drawDistanceScale = std::clamp(v.get<float>(), 0.05f, 1.0f);
        else if (key == "shadowCasterMinPixels" && num) c.shadowCasterMinPixels = std::clamp(v.get<float>(), 0.0f, 512.0f);
        else if (key == "postEffectQuality" && num) c.postEffectQuality = std::clamp(v.get<float>(), 0.125f, 1.0f);
        else if (key == "particleCullDistance" && num) c.particleCullDistance = std::max(v.get<float>(), 0.0f);
        else if (key == "particleSpawnScale" && num) c.particleSpawnScale = std::clamp(v.get<float>(), 0.0f, 1.0f);
        else {
            error = "'" + key + "' is not a quality ceiling (or has the wrong type)";
            return std::nullopt;
        }
    }
    return c;
}

} // namespace avgen::app
