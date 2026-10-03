#include "app/interactive_resolution.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace avgen::app {

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

constexpr LiveQualityLadder kResolutionFirst{
    rung(L::Ultra, 1.0f),
    rung(L::High, 0.85f),
    volumes(rung(L::Medium, 0.71f), 0.25f),
    cascades(noDepthOfField(noMotionBlur(volumes(rung(L::Low, 0.5f), 0.25f, 0.5f))), 2),
    plainShadows(cascades(noDepthOfField(noMotionBlur(volumes(rung(L::Emergency, 0.38f), 0.25f, 0.25f))), 2)),
};

constexpr LiveQualityLadder kBalanced{
    rung(L::Ultra, 1.0f),
    volumes(rung(L::High, 0.85f), 0.25f),
    noMotionBlur(volumes(rung(L::Medium, 0.71f), 0.25f)),
    cascades(noDepthOfField(noMotionBlur(volumes(rung(L::Low, 0.5f), 0.25f, 0.5f))), 2),
    plainShadows(cascades(noDepthOfField(noMotionBlur(volumes(rung(L::Emergency, 0.42f), 0.25f, 0.25f))), 2)),
};

constexpr LiveQualityLadder kEffectsFirst{
    rung(L::Ultra, 1.0f),
    cascades(noDepthOfField(volumes(rung(L::High, 1.0f), 0.25f)), 2),
    plainShadows(cascades(noDepthOfField(noMotionBlur(volumes(rung(L::Medium, 0.85f), 0.25f, 0.5f))), 2)),
    plainShadows(cascades(noDepthOfField(noMotionBlur(volumes(rung(L::Low, 0.71f), 0.25f, 0.5f))), 2)),
    plainShadows(cascades(noDepthOfField(noMotionBlur(volumes(rung(L::Emergency, 0.5f), 0.25f, 0.25f))), 2)),
};

// The number of effect levers (everything but the scale) that differ between two rungs.
int effectChanges(const LiveQualityRung& a, const LiveQualityRung& b) {
    return (a.volumeResolutionScale != b.volumeResolutionScale ? 1 : 0) +
           (a.volumeStepScale != b.volumeStepScale ? 1 : 0) + (a.motionBlur != b.motionBlur ? 1 : 0) +
           (a.depthOfField != b.depthOfField ? 1 : 0) + (a.cascadeCount != b.cascadeCount ? 1 : 0) +
           (a.shadowResolution != b.shadowResolution ? 1 : 0) +
           (a.reducedShadowFiltering != b.reducedShadowFiltering ? 1 : 0);
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
    return q;
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
    if (!settings_.enabled || settings_.strategy != before.strategy) {
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
    const LiveQualityLadder& ladder = liveQualityLadder(settings_.strategy);
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
    const LiveQualityLadder& ladder = liveQualityLadder(settings_.strategy);
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
    const std::size_t last = kLiveQualityLevels - 1;

    if (gpu > budget) {
        raiseStreak_ = 0;
        if (rung_ >= last) {
            return d; // the bottom of the ladder: nothing left to give
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

    if (rung_ > 0) {
        // One rung at a time on the way up, only when the higher rung is predicted to fit with
        // margin, and only once that has been true for `raiseHoldFrames` frames in a row (ADR-1085).
        if (gpu * stepRatio(rung_) <= budget * settings_.raiseMargin) {
            ++raiseStreak_;
        } else {
            raiseStreak_ = 0;
        }
        if (raiseStreak_ >= settings_.raiseHoldFrames) {
            moveTo(rung_ - 1, gpu);
            ++stats_.raises;
            return {rung_, true};
        }
    }
    return d;
}

} // namespace avgen::app
