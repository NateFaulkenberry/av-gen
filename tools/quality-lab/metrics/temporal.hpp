#pragma once

// Phase 3 of the Quality Lab: the temporal detectors (docs/quality-lab/artifact-detection.md §1,
// §4).
//
// **Read ADR-243 before reading this file.** A correctly implemented temporal detector in this
// repository was measured *anti-correlated with human judgement* on the artifact viewers actually
// report: it scored removing anti-aliasing as a 42% improvement while the reviewer called it "more
// distracting". The arithmetic was right, the arms were non-vacuous, and the conclusion was
// backwards, because the thing it measured (temporal alternation of luma) was not the thing being
// objected to (spatial aliasing on moving geometry).
//
// Three consequences are encoded here rather than left to discipline:
//
// 1. **`temporalAlternation` is named for its arithmetic**, not for "stability". The artifact class
//    it is licensed to speak about is narrower than what it computes and is stated in the report,
//    not in the name.
// 2. **`motionCompensatedResidual` is the instrument ADR-243 did not have** -- the only measure here
//    that separates authored motion from unwanted change, because it asks the engine's own velocity
//    buffer where every pixel came from before differencing.
// 3. **The disocclusion mask is the whole validity of that number.** A translating camera disoccludes
//    a large fraction of the frame; unmasked, the residual reports camera motion as instability,
//    which is the exact shape of ADR-243's mistake. `disocclusionFraction` is therefore reported
//    beside the residual always, and it is a validity gate rather than a quality number: a residual
//    computed over 4% of the frame is not a statement about the frame.

#include "capture/sequence.hpp"
#include "metrics/spatial.hpp"

#include <cstdint>
#include <vector>

namespace avgen::quality {

// ---- temporal alternation (artifact-detection.md §4) -------------------------------------------

// Mean and peak of |x(t+1) - 2x(t) + x(t-1)| over luma, per pixel then pooled. Units are 0..255
// luma steps, the same scale `tools/temporal_stats.py` reports, so a number from this and a number
// from that script are comparable.
//
// The **peak** is not decoration: a single-frame pop in a long sequence is invisible in a mean, and
// popping is an artifact class the Lab is specifically hunting (metrics.md §2.1).
struct AlternationStats {
    double mean = 0.0;
    double peak = 0.0;
    // The fraction of pixels whose second difference exceeds `threshold` -- the count form, which
    // localises a finding that a mean dilutes.
    double exceedingFraction = 0.0;
};
[[nodiscard]] AlternationStats temporalAlternation(const Frame& previous, const Frame& current,
                                                   const Frame& next, double threshold = 8.0);

// ---- the motion-compensated residual (artifact-detection.md §1) ---------------------------------

// What the detector was given. Beauty frames are required; every AOV is optional and its absence
// *widens* the mask rather than silently narrowing it -- see `DisocclusionPolicy` below.
struct MotionInputs {
    const Frame* previous = nullptr; // frame t
    const Frame* current = nullptr;  // frame t+1
    const Plane* velocity = nullptr; // velocity AOV of frame t+1, UV units
    const Plane* depthPrevious = nullptr;
    const Plane* depthCurrent = nullptr;
    const Plane* idPrevious = nullptr;
    const Plane* idCurrent = nullptr;
};

struct DisocclusionPolicy {
    // A pixel is disoccluded when the identifier it warps back to is not the identifier it has now.
    // Exact where identifiers are exact, which they are: the target is R32Uint.
    bool useIdentifier = true;
    // ...or when the depth it warps back to differs from its own by more than this fraction of its
    // own depth. Catches the same-object-different-surface case a silhouette produces, which the
    // identifier test cannot see.
    bool useDepth = true;
    double depthRelativeThreshold = 0.05;
    // A pixel whose source lies outside the frame has no prediction at all and is always excluded.
    // Not a policy -- there is nothing to read.
};

struct MotionResidual {
    // Mean |luma(t+1) - luma(warp(t))| over VALID pixels only, in 0..255 luma steps.
    double residual = 0.0;
    // 95th percentile over valid pixels. The tail is usually the finding.
    double residualP95 = 0.0;
    // The fraction of pixels the mask excluded, split by cause so a number that turns out to be
    // dominated by one test can be seen to be.
    double disocclusionFraction = 0.0;
    double offFrameFraction = 0.0;
    double identifierFraction = 0.0;
    double depthFraction = 0.0;
    // 1 - disocclusionFraction. Reported because metrics.md §2.2 requires the residual and the
    // coverage it was computed over to appear together or not at all.
    double validFraction = 0.0;
    // Per-pixel, for the diagnostic image. 0 where invalid.
    std::vector<float> residualMap;
    std::vector<std::uint8_t> valid; // 1 = counted, 0 = masked out
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    // Mean over the pixels of `mask` that are also valid -- the AOV-gated per-class residual of
    // artifact-detection.md §3. One residual, four masks, because four algorithms measuring the
    // same thing under four names is how a metric suite becomes unfalsifiable.
    struct Gated {
        double residual = 0.0;
        double coverage = 0.0; // fraction of the frame in the mask AND valid
        std::size_t pixels = 0;
    };
    [[nodiscard]] Gated over(const std::vector<std::uint8_t>& mask) const;
};

// The residual. Returns an empty result (width 0) when the inputs disagree about shape or the
// beauty frames are missing -- a caller that ignores that gets zeros, which is why the CLI checks
// `width` and the report records `available: false` rather than printing a zero.
[[nodiscard]] MotionResidual motionCompensatedResidual(const MotionInputs& inputs,
                                                       const DisocclusionPolicy& policy = {});

// **The floor, and why it is a function rather than a constant.**
//
// Bilinear prediction is itself a resample, so warping blurs, so the residual has a floor above zero
// that depends on the content and on the sub-pixel phase of the motion. artifact-detection.md §1
// says to measure that floor on the smooth-motion control and report the residual relative to it.
// This computes the floor directly: it warps frame t forward and back by the same velocity and
// measures what the round trip alone cost, with no renderer in the loop.
//
// A residual below its own floor is not a better renderer; it is a warp that did nothing.
//
// The floor is not zero even for a whole-pixel motion, and the reason is the velocity's precision
// rather than the warp's: the engine's target is **RG16Float**, so a UV of 1/1920 is stored with
// about three decimal digits and the warp lands a fraction of a pixel away from where the geometry
// actually was. Measured on a float32 synthetic at 96 pixels wide, a whole-pixel warp costs 1.2e-6
// luma steps; a half-pixel warp costs 7.8. The first number is the encoding and the second is the
// resample, and neither is the renderer.
[[nodiscard]] double warpFloor(const Frame& frame, const Plane& velocity);

} // namespace avgen::quality
