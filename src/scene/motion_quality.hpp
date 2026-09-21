#pragma once

// Motion quality metrics (Phase B §41), and the gate that uses them (§44).
//
// **Offline, and the spec says so twice**: *"build offline validation metrics"* and *"these metrics
// should eventually feed MotionPack validation"*. So this measures a clip on a skeleton, not a
// character on a frame. My first framing of this stage was a per-character runtime sample, which is
// a different and more expensive thing that §41 does not ask for.
//
// **Each metric reports whether it was measurable, separately from its value.** A clip with no
// contact track has a foot slide of zero and a clip with perfect contacts has a foot slide of zero,
// and those are different facts. ADR-551 already paid for that confusion once, when a terrain
// reject was read as "no ground"; §40 nearly paid for it again with slope.
//
// **What §44 does with a failure is a refusal, not a log.** A gate that only reports is the
// dead-knob family in a new place: the number would be right, the message would appear, and the
// bad variant would go into the pack anyway. `gateMotionQuality` returns a verdict and
// `buildMotionPack`'s caller is expected to drop what fails.

#include "scene/motion_analysis.hpp"
#include "scene/skeleton.hpp"

#include <string>
#include <vector>

namespace avgen::scene {

// One measurement, with its own answer to "was this measurable at all".
struct QualityMetric {
    float value = 0.0f;
    bool measured = false;
    [[nodiscard]] bool exceeds(float limit) const { return measured && value > limit; }
};

struct MotionQualityReport {
    std::string clip;

    // §41's list. The four a clip on a skeleton can answer are filled; the two that need a running
    // character (reach error against a live target, velocity error against a controller's desire)
    // are left unmeasured rather than filled with zero.
    QualityMetric footSlide;        // displacement while a contact says planted, model units
    QualityMetric contactHeight;    // how far a planted foot's height wanders within its stance
    QualityMetric limbExtension;    // worst root-to-tip distance as a fraction of limb length
    QualityMetric bodyCorrection;   // pelvis displacement a compensation had to apply
    QualityMetric reachError;       // end-effector to target -- needs a target
    QualityMetric velocityError;    // actual minus desired -- needs a controller

    [[nodiscard]] std::string report() const;
};

// A limb, for the extension metric. Named rather than derived, for ADR-543's reason: on this
// project's primary character the three joints of a leg are not an ancestor chain.
struct QualityLimb {
    std::string root;
    std::string mid;
    std::string tip;
};

struct MotionQualityOptions {
    float sampleRate = 30.0f;
    std::vector<ContactJoint> contacts;
    std::vector<QualityLimb> limbs;
};

// Measure `clip` on `skeleton`.
[[nodiscard]] MotionQualityReport measureMotionQuality(const Skeleton& skeleton,
                                                       const AnimationClip& clip,
                                                       const MotionQualityOptions& options);

// ---- §44: the gate -----------------------------------------------------------------------------

struct MotionQualityLimits {
    // Model units of slide within a stance. A foot that moves a fifth of a body height while it is
    // supposed to be planted is the artefact every viewer notices.
    float maxFootSlide = 0.12f;
    // How far a planted foot's height may wander inside its own stance.
    float maxContactHeight = 0.06f;
    // A limb at 1.0 is straight. Past it the pose is asking for a length the bones do not have,
    // which a solver can only answer by clamping -- so a *generated* variant that does it is one
    // that should not have been generated.
    float maxLimbExtension = 1.0f;
    float maxBodyCorrection = 0.35f;

    friend bool operator==(const MotionQualityLimits&, const MotionQualityLimits&) = default;
};

struct MotionQualityVerdict {
    bool pass = true;
    // Why, in the order they were checked. Empty on a pass.
    std::vector<std::string> failures;
    [[nodiscard]] std::string report() const;
};

// **A refusal, not a warning.** §44: *"do not flood MotionPacks with bad procedural output."* A
// verdict that does not pass means the variant is dropped by whoever asked for it.
//
// An **unmeasured** metric never fails a gate: refusing a clip because nobody gave it a contact
// track would reject every hand-authored clip in the repository. It is reported as unmeasured and
// the gate says so, which is the difference between "this is bad" and "I cannot tell".
[[nodiscard]] MotionQualityVerdict gateMotionQuality(const MotionQualityReport& report,
                                                     const MotionQualityLimits& limits);

} // namespace avgen::scene
