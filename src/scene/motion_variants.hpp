#pragma once

// Offline procedural generation (Phase B §42) and augmentation (§43).
//
// **Built after the gate, deliberately.** §44's `gateMotionQuality` was written and fixed before
// any of this existed. A generator built first gets tuned until its output looks acceptable, and
// the gate then codifies whatever it happened to produce; with the gate fixed independently, this
// has something it cannot argue with.
//
// **"Do not generate thousands of pointless clips" is the constraint, and a count cannot enforce
// it.** A count rewards generation. What rewards *useful* generation is **coverage**: how much of
// the range a character actually moves through has a clip near it. So this reports coverage, and
// `generateVariants` stops when adding another variant would not improve it.
//
// **Coverage is monotone too, and that is ADR-559's trap.** More variants can only cover more, so
// coverage alone recommends generating forever. Its opposing quantity is not pack size -- disk is
// cheap -- but **search cost**: every variant is samples in the motion database, and Phase C
// measured a linear scan at 5.9 ns per sample. A hundred variants of one walk is a hundred times
// the scan for a range the character never uses. Both numbers are reported and
// `test_motion_variants.cpp` finds the knee rather than asserting a threshold.
//
// **Every variant carries its provenance** (§43): the source clip, the transformation, its
// parameters and the tool version. A generated clip that cannot say what it came from is one
// nobody can regenerate or reject later.

#include "scene/animation.hpp"
#include "scene/motion_analysis.hpp"
#include "scene/motion_quality.hpp"
#include "scene/skeleton.hpp"

#include <string>
#include <vector>

namespace avgen::scene {

// What was done to a source clip to produce this one. §43's list, as far as it is implemented.
enum class VariantKind : std::uint8_t {
    Source,       // not a variant: the clip as authored
    SpeedWarp,    // time-warped: the same motion at a different pace
    StrideWarp,   // the step lengthened or shortened in space, at the same pace
    Mirror,       // left and right exchanged
};
[[nodiscard]] const char* variantKindName(VariantKind kind);

struct VariantProvenance {
    std::string source;      // the clip this came from
    VariantKind kind = VariantKind::Source;
    float parameter = 1.0f;  // the warp factor, or 0 for a mirror
    std::string tool;        // tool and version, so a pack says what produced it
    // The speed this variant is *for*, in the source's own units. What coverage is measured over.
    float targetSpeed = 0.0f;
    [[nodiscard]] std::string describe() const;
};

struct MotionVariant {
    AnimationClip clip;
    VariantProvenance provenance;
    // Kept beside the clip so a caller can see WHY a variant was accepted or dropped,
    // rather than being handed a surviving set and no account of the rest.
    MotionQualityReport quality;
    MotionQualityVerdict verdict;
};

struct VariantOptions {
    // The speeds the character actually uses. **Authored, not swept**: generating for a range
    // nobody moves through is exactly the "thousands of pointless clips" the spec forbids.
    std::vector<float> targetSpeeds;
    // The speed the source clip's stride was authored for. Zero means unknown, and then no speed
    // variant can be generated -- ADR-540 established this cannot be derived from a clip whose
    // root nets zero.
    float sourceSpeed = 0.0f;
    // How close a clip must be to a target speed to count as covering it, as a fraction.
    float coverageTolerance = 0.18f;
    // Bounds on the warp. Past these the honest answer is a different source clip.
    float minWarp = 0.6f;
    float maxWarp = 1.7f;
    bool mirror = false;
    std::string tool = "avgen-motion/1";
    // **What the gate is allowed to measure, and without it the gate always passes.** An
    // unmeasured metric never fails (§44, deliberately), so a generator that gates with no
    // contacts and no limbs configured refuses nothing -- the dead-knob family wearing a
    // validator's clothes, which this generator shipped with until its own test caught it.
    MotionQualityOptions quality;
    MotionQualityLimits limits;
};

struct CoverageReport {
    // 0..1: the fraction of `targetSpeeds` with a clip inside the tolerance.
    float coverage = 0.0f;
    std::uint32_t covered = 0;
    std::uint32_t total = 0;
    // The opposing quantity (ADR-559): how many database samples this set costs, and what that is
    // in scan time at Phase C's measured 5.9 ns per sample.
    std::uint32_t samples = 0;
    float scanMicroseconds = 0.0f;
    [[nodiscard]] std::string report() const;
};

// How much of `targets` the clips cover, and what they cost.
[[nodiscard]] CoverageReport measureCoverage(const std::vector<MotionVariant>& variants,
                                             const std::vector<float>& targets, float tolerance,
                                             float sampleRate);

// Generate variants of `source` toward the speeds `options` names, stopping when another variant
// would not improve coverage.
[[nodiscard]] std::vector<MotionVariant> generateVariants(const Skeleton& skeleton,
                                                          const AnimationClip& source,
                                                          const VariantOptions& options);

} // namespace avgen::scene
