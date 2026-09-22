#pragma once

// Phase C §21: motion augmentation. New clips made from a source clip by moving where its feet
// land and re-solving the legs.
//
// §21 names seven kinds: mirroring, speed, stride, directional warping, turn variation,
// start/stop variants and root-motion adaptation. Phase B shipped speed (`motion_variants`) and
// root-motion adaptation (`adaptRootMotion`). This is the other five, built on one step.
//
// **The one step: foot targets, then IK.** Every kind except mirroring is a map from where the body
// and its planted feet were to where they should be. The pelvis follows the map, each leg is solved
// with `solveTwoBone` to put its foot on the mapped target, and everything else rides along. A
// warp that rescaled joint rotations directly would skate every planted foot. A warp that moves
// the targets and solves for them keeps a planted foot planted, **by construction**, and where the
// leg cannot reach, the solver says so and the variant is refused rather than shipped (§21: "do not
// generate redundant samples", and do not generate broken ones either).
//
// **In-place content is planted first.** Every Glowmere cycle is authored in place (ADR-540): the
// root stays put and the planted foot slides. A map over positions means nothing for a clip whose
// feet are on a treadmill, so every variant first plants it (`AugmentKind::Plant`): the root moves by
// the velocity the planted feet imply, which leaves them still in model space. The warps then act on
// a clip in which "planted" means "not moving", which is the case every one of them is derived for.
//
// **Each output carries its heading.** A turn variant's body turns because the generator turned
// it, and a pelvis's yaw is posture, not heading (the scout's crouch holds it at -43 degrees).
// So the generator records the heading it applied, per frame, in `PackClip::heading`, and the
// database reads that instead of guessing from the pelvis.

#include "scene/animation.hpp"
#include "scene/motion_analysis.hpp"
#include "scene/motion_coverage.hpp"
#include "scene/motion_database.hpp"
#include "scene/ik.hpp"
#include "scene/motion_pack.hpp"
#include "scene/skeleton.hpp"

#include <string>
#include <vector>

namespace avgen::scene {

// A leg, as the three joints `solveTwoBone` takes. They need not be a parent chain: the alien's leg
// is three siblings under the pelvis (ADR-543), and the solve treats them as rigid segments either
// way.
struct AugmentLeg {
    std::string root;
    std::string mid;
    std::string tip;
};

enum class AugmentKind : std::uint8_t {
    Plant,      // in place -> travelling, nothing else: the base every other kind is applied to
    Stride,     // the step lengthened or shortened, at the same cadence (parameter: scale)
    Direction,  // travel turned against a fixed facing: a diagonal walk (parameter: radians)
    Turn,       // travel and facing turned together, progressively (parameter: rad/s)
    Start,      // stride eased up from a small fraction over the first part of the clip
    Stop,       // stride eased down to a small fraction over the last part of the clip
    Mirror,     // left and right exchanged
};
[[nodiscard]] const char* augmentKindName(AugmentKind kind);

struct AugmentOptions {
    float sampleRate = 30.0f;
    std::vector<AugmentLeg> legs;
    // The contacts of the source clip, one track per leg tip (by joint name). They say which frames
    // a foot is planted, which is what anchors it.
    std::vector<ContactTrack> contacts;
    // A foot the leg cannot reach by more than this fraction of the leg's length refuses the
    // variant. 2%: at the scout's 0.8 m leg, 16 mm, under the §32 bar (0.0510 m) by a factor of 3.
    float maxShortfall = 0.02f;
    // Start/stop: the fraction of full stride the ramp begins or ends at, and the share of the
    // clip it takes. **Not zero**: a stride of zero is both feet in one place, the fade Phase B
    // §9 forbids, so a start begins at a short step and a stop ends at one.
    float rampFloor = 0.2f;
    float rampShare = 0.6f;
    // Mirror: how a left joint's name maps to its right twin. Tried in order, both ways.
    std::vector<std::pair<std::string, std::string>> mirrorSuffixes{
        {".l", ".r"}, {".L", ".R"}, {"_l", "_r"}, {"_L", "_R"}, {"Left", "Right"}, {"left", "right"}};
};

struct AugmentResult {
    AnimationClip clip;
    AugmentKind kind = AugmentKind::Plant;
    float parameter = 0.0f;
    // Per frame (the output's frames, at `sampleRate`), the heading the generator applied, as
    // yaw in radians about +Y. The database reads it as the body's facing.
    std::vector<float> heading;
    // Worst foot-to-target miss over every leg and frame, as a fraction of that leg's length.
    float worstShortfall = 0.0f;
    // Worst horizontal drift of a planted foot within one stance, model units. Zero up to
    // floating point when the solve reached every target.
    float worstPlantedDrift = 0.0f;
    bool accepted = false;
    std::string refusal; // why not, when refused
};

// Solve one leg of `pose` so its tip lands on `target` (model space), with the tip's model rotation
// set to `footRotation`. `root`, `mid` and `tip` need not be a parent chain. This is the one leg
// solve augmentation and the positional retarget share.
struct LegSolve {
    IkStatus status = IkStatus::Solved;
    float shortfall = 0.0f; // target-to-tip distance, as a fraction of the leg's length
};
[[nodiscard]] LegSolve solveLegInPose(const Skeleton& skeleton, Pose& pose, int root, int mid, int tip,
                                      const glm::vec3& target, const glm::mat3& footRotation,
                                      const glm::vec3& forward);

// The body velocity a clip's planted feet imply at each frame, model space, horizontal. A frame
// with no planted foot takes the value interpolated from its neighbours (wrapping on a loop).
[[nodiscard]] std::vector<glm::vec3> plantedVelocity(const Skeleton& skeleton, const AnimationClip& clip,
                                                     const std::vector<ContactTrack>& contacts,
                                                     float sampleRate, bool loop);

// Make one variant of `source`. A source authored in place is planted first. `loop` is the
// source's own loop flag.
[[nodiscard]] AugmentResult augmentClip(const Skeleton& skeleton, const AnimationClip& source, bool loop,
                                        AugmentKind kind, float parameter, const AugmentOptions& options);

// ---- augmenting a pack, kept only where it adds coverage ----------------------------------------
//
// §21: "Do not generate redundant samples merely to increase the number. The goal is coverage, not
// frame count." So every planned variant is generated, gated on its own quality (the legs reach),
// and then **kept only if it improves the pack's coverage**, measured by §58's categories on a
// database built from the pack with and without it. A variant that lands where the pack already has
// good coverage is refused as redundant, and the report says which category it would have added
// to and why that was not enough.

struct AugmentPlanItem {
    std::string clip;        // the source clip, by name
    AugmentKind kind = AugmentKind::Stride;
    float parameter = 0.0f;
};

struct AugmentPackOptions {
    AugmentOptions augment;             // legs, rate, limits; `contacts` is filled per source clip
    MotionDatabaseOptions database;     // the database coverage is measured on
    MotionCategoryOptions categories;
};

struct AugmentDecision {
    AugmentPlanItem item;
    std::string name;       // the variant's clip name, when one was generated
    bool generated = false; // passed its own quality gate
    bool kept = false;      // and improved coverage
    std::string reason;
    // The categories whose grade or window count moved, "walk: moderate 7 -> good 18".
    std::vector<std::string> gains;
};

struct AugmentPackResult {
    MotionPack pack;                       // the source pack plus every kept variant
    std::vector<AugmentDecision> decisions;
    MotionCategoryReport before;
    MotionCategoryReport after;
    [[nodiscard]] std::string report() const;
};

[[nodiscard]] Result<AugmentPackResult> augmentPack(const MotionPack& pack,
                                                    const std::vector<AugmentPlanItem>& plan,
                                                    const AugmentPackOptions& options);

} // namespace avgen::scene
