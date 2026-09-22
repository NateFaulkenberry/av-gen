#pragma once

// Phase C §64/§92: retargeting the legs through IK, for a rig a rotation retarget cannot move.
//
// ADR-553 measured the failure: 100STYLE retargeted onto the Glowmere alien by rotation held every
// leg at its rest reach (0.970 of the leg, on every frame of every clip), because the alien's foot is
// not downstream of the joints the rotations land on (ADR-543). The orientation and bone-length
// numbers were perfect, and the legs never moved. ADR-553 named the repair: "running FK on the
// source to find where the foot should be, then solving the target's leg to put it there".
//
// This is that repair. The rotation retarget still carries the body, the spine, the arms and the
// root's travel, where it is right. Then, per leg and per frame:
//   * the source ankle's offset from the source hip, in model space,
//   * turned from the source body's frame into the target body's (each body's forward is found
//     from its own hips at rest, so the two rigs need not face the same way),
//   * scaled by the ratio of the two legs' lengths,
//   * added to the target hip where the rotation retarget left it,
// and the target leg is solved with §21's `solveLegInPose` to put its foot there. The foot keeps the
// orientation the rotation retarget gave it.

#include "scene/animation.hpp"
#include "scene/motion_augment.hpp"
#include "scene/skeleton.hpp"

#include <string>
#include <vector>

namespace avgen::scene {

struct PositionalLeg {
    std::string sourceHip;
    std::string sourceKnee;
    std::string sourceAnkle;
    AugmentLeg target; // hip, knee, foot on the target rig
};

struct PositionalRetargetStats {
    std::uint32_t frames = 0;
    float worstShortfall = 0.0f; // the target foot's miss, as a fraction of its leg
    // The reach cap (owner ruling, 22 Sep; ADR-624): how many leg-frames it shortened, and by how
    // much the foot was pulled in, in the target's model units.
    std::uint32_t legFrames = 0;
    std::uint32_t cappedLegFrames = 0;
    float meanCapPull = 0.0f;  // over the capped leg-frames
    float worstCapPull = 0.0f;
    std::uint32_t relengthedLegFrames = 0; // leg-frames whose knee was off its rest distance from the hip
    std::uint32_t resolvedLegFrames = 0; // leg-frames whose solved foot still passed the cap, pulled in after
    std::uint32_t droppedFrames = 0; // frames the body was lowered to bring the feet within reach
    float meanDrop = 0.0f;           // over those frames, model units
    float worstDrop = 0.0f;
    std::vector<float> legScale; // target leg length over source leg length, per leg
    std::string problem;         // non-empty when a leg did not resolve
};

// **One scale for the body and its feet.** The rotation retarget scales the root's travel by its
// own ratio (`RetargetBinding::rootScale`, derived from the rest poses), and the feet here are scaled
// by the legs' length ratio. When the two differ, a planted foot slides: on the scout the root was
// scaled 0.826 and the legs 0.754, so a source foot standing still became a target foot sliding at
// 7% of the walking speed. So, given the target's travel joint and the root scale the rotation
// retarget used, the travel joint's deviation from its rest is rescaled to the legs' ratio.
struct PositionalRootRescale {
    std::string targetRoot;  // the target's travel joint; empty leaves the root as retargeted
    float rootScale = 0.0f;  // what the rotation retarget scaled it by
};

// The Glowmere alien's own longest leg: hip (`thigh_twist`) to foot over the leg's rest length, the
// most its authored clips ever reach (ADR-624). **Owner ruling, 22 Sep: borrowed human motion must
// never look hyperextended on the alien**, so a positional retarget onto it caps each leg here.
inline constexpr float kAlienMaxLegReach = 0.956f;

// The reach cap. `maxReach` 0 leaves the legs uncapped. `lowerBody` brings the hips down to the feet
// (a planted foot stays planted); off, the feet are pulled in toward the hips instead (a planted foot
// follows its hip and slides). See the .cpp and ADR-624 for the measurement that chose the default.
struct PositionalReachCap {
    float maxReach = 0.0f;
    bool lowerBody = true;
};

// Re-solve the legs of `retargeted` (the rotation retarget of `source` onto `target`) so each target
// foot follows its source ankle.
[[nodiscard]] AnimationClip retargetLegsPositional(const AnimationClip& source, const Skeleton& sourceSkeleton,
                                                   const AnimationClip& retargeted, const Skeleton& targetSkeleton,
                                                   const std::vector<PositionalLeg>& legs, float sampleRate,
                                                   PositionalRetargetStats* stats = nullptr,
                                                   const PositionalRootRescale& root = {},
                                                   const PositionalReachCap& cap = {});

} // namespace avgen::scene
