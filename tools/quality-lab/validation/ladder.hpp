#pragma once

// The distortion ladder (docs/quality-lab/metrics.md §4.1, artifact-detection.md §1) as a library,
// so `avgen_quality validate --ladder` and the unit suite run **the same arms** rather than two
// implementations that will eventually disagree about what was validated.
//
// ADR-182 is load-bearing for this whole project, because the product is measurement:
//
//     A probe must be shown capable of failing.
//
// Every arm below therefore asserts two halves -- the metric that must move, and the metrics that
// must *not*. The second half is what makes this a validation rather than a smoke test: a metric
// that rises for every distortion is detecting that something changed, which the file size already
// said.
//
// **And the ladder itself is a probe.** `runSpatialLadder` takes its metrics as bindings so a
// deliberately broken metric can be substituted and the ladder shown to fail. Without that, "the
// ladder passes" is a sentence with no information in it.

#include "capture/sequence.hpp"
#include "metrics/spatial.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace avgen::quality {

// ---- the arms, as images -----------------------------------------------------------------------
//
// One base image carrying the three things the ladder must distinguish: a smooth gradient (where
// banding shows and texture does not), fine high-contrast detail (where blur and aliasing show),
// and saturated colour (where a colour shift shows).
[[nodiscard]] Frame ladderBase();
[[nodiscard]] Frame ladderBlurred(const Frame& source, double sigma);
[[nodiscard]] Frame ladderQuantised(const Frame& source, int bits);
[[nodiscard]] Frame ladderExposed(const Frame& source, double scale);
[[nodiscard]] Frame ladderColourShifted(const Frame& source, int amount);
[[nodiscard]] Frame ladderSharpened(const Frame& source, double amount);

// A shallow diagonal, with coverage-based antialiasing or snapped to whole pixels. This is
// ADR-243's comparison -- "AA on" against "AA off" -- and not a downsample: bars one pixel wide sit
// exactly at Nyquist, so sampling every other column lands on the same phase and the region comes
// back FLAT, which is total signal loss rather than aliasing.
[[nodiscard]] Frame ladderDiagonalEdge(bool antialiased);
// The same edge rendered at `factor` times the resolution and box-averaged down: what
// `--supersample` does, so the metric's direction on ADR-243's third arm can be checked.
[[nodiscard]] Frame ladderSupersampledEdge(std::uint32_t factor);

// A smooth 8-bit luminance ramp, wide enough that consecutive codes are many pixels apart -- which
// is what banding *is*. And its dithered twin, which is the remedy.
[[nodiscard]] Frame ladderGradient();
[[nodiscard]] Frame ladderDithered(const Frame& source);

// ---- the arms, as sequences (Phase 3) ----------------------------------------------------------

// A synthetic render: frames plus the AOVs the engine would have written beside them. The velocity
// is EXACT by construction, which is the point -- an arm whose velocity is approximate cannot tell
// a broken warp from a broken motion vector.
struct SyntheticSequence {
    std::string name;
    std::vector<Frame> frames;
    std::vector<Plane> velocity; // velocity[i] is the velocity OF frame i, UV units
    std::vector<Plane> depth;
    std::vector<Plane> identifier;
    std::vector<Plane> normal;
    std::vector<Plane> emission;
};

// **The two controls.** Without these every other temporal arm is vacuous.
//
// `staticScene` -- nothing moves. Residual ~0, disocclusion ~0.
// `smoothMotion` -- a grating translating at exactly `pixelsPerFrame`, with the velocity that
//   describes it. The residual must be ~0 **and the temporal alternation must be large**, because
//   the second difference of a translating sinusoid is not zero. That single arm is ADR-243 in
//   miniature: the temporal detector calls authored motion instability, and the motion-compensated
//   residual does not.
[[nodiscard]] SyntheticSequence staticScene(std::size_t frames);
[[nodiscard]] SyntheticSequence smoothMotion(std::size_t frames, double pixelsPerFrame = 1.0);
// Two sub-pixel phases of the same grating, alternating, with the velocity saying nothing moved.
// Mean |Laplacian| is phase-independent for a sinusoid, so this arm changes the temporal measure
// and leaves the spatial one alone -- which is what makes it a shimmer arm and not a blur arm.
[[nodiscard]] SyntheticSequence shimmer(std::size_t frames);
// Each frame blended with its predecessor, on top of smooth motion.
[[nodiscard]] SyntheticSequence ghosting(std::size_t frames);
// Smooth motion past a static foreground occluder, with the identifier and depth AOVs that say so.
[[nodiscard]] SyntheticSequence disocclusion(std::size_t frames);
// Smooth motion where a band of pixels swaps identifier without moving and without changing depth:
// a geometric level swapping under a stationary surface.
[[nodiscard]] SyntheticSequence identifierPop(std::size_t frames);
// A static scene where one region's shading flickers while its normals hold still -- the arm the
// normal-unchanged mask exists to isolate.
[[nodiscard]] SyntheticSequence shadingFlicker(std::size_t frames);

// ---- results -----------------------------------------------------------------------------------

struct Check {
    std::string description; // what is being asserted, in words
    std::string expectation; // the comparison, e.g. "jagged > filtered"
    double observed = 0.0;
    double reference = 0.0;
    bool mustMove = true; // false marks the control half of an arm
    bool passed = false;
};

struct Arm {
    std::string name;
    std::string construction;
    std::vector<Check> checks;
    [[nodiscard]] bool passed() const;
};

struct LadderResult {
    std::vector<Arm> arms;
    [[nodiscard]] bool passed() const;
    [[nodiscard]] std::size_t failedChecks() const;
    [[nodiscard]] std::size_t totalChecks() const;
};

// The metrics the spatial ladder measures with. Substitutable so the ladder can be run against a
// broken metric and shown to fail -- ADR-182 applied to the validator itself.
struct MetricBindings {
    std::function<double(const Frame&)> spatialLaplacian;
    std::function<double(const Frame&)> quantisationSteps;
    std::function<double(const Frame&, const Frame&)> msSsim;
    std::function<double(const Frame&, const Frame&)> psnr;
    std::function<ColourDifference(const Frame&, const Frame&)> ciede2000;
};
[[nodiscard]] MetricBindings realMetrics();

[[nodiscard]] LadderResult runSpatialLadder(const MetricBindings& metrics = realMetrics());
[[nodiscard]] LadderResult runTemporalLadder();

// The arms for the metrics that come from ffmpeg's libvmaf. Separate because they need an external
// tool, and the Lab degrades rather than fails without one: with no ffmpeg this returns an empty
// result and the caller reports the arms as unavailable, not as passing.
//
// Three of these arms assert properties of libvmaf that are DEFECTS or blind spots rather than
// features (ADR-252). They are pinned deliberately: if a libvmaf upgrade changes them, this is
// where that is found, and the report's limitations then need rewriting.
[[nodiscard]] LadderResult runExternalLadder(const std::filesystem::path& workDirectory);

} // namespace avgen::quality
