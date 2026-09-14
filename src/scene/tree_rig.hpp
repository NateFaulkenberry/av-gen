#pragma once

// Hierarchical branch animation: a skeleton over the tree's own graph, and a spring-damped pose.
//
// WHY A SKELETON AND NOT A PER-TIER WIND UNIFORM. `wind::VegetationMotion` is the engine's existing
// answer to "make this plant move", it is analytic, it costs nothing, and it was the obvious choice.
// It cannot be used here, for a reason that is a property of this tree rather than of that system:
// the tree is six meshes, one per tier, and `bendDisplacement` bends a mesh about ITS OWN base. Two
// tiers bent about different origins separate at every joint between them -- a tertiary branch's
// base pulls away from the secondary that carries it -- and no setting of the parameters fixes it,
// because the discontinuity is in the decomposition, not in the amounts.
//
// Making the displacement continuous instead would mean giving every tier the same base and extent,
// at which point every tier moves identically and there is no hierarchy left to have.
//
// A skeleton has neither problem. The mesh is bound to joints, so a joint shared across a fork
// carries both sides of it, and `poseToModel` composes a child's local transform onto its parent's
// model matrix -- which IS transform inheritance, the thing section 40 of the brief asks for, rather
// than an amplitude ladder that resembles it. A primary limb moving carries its secondaries and
// their foliage with it because that is what a parent matrix does.
//
// THE BUDGET IS THE REASON THIS IS COARSE. `kMaxPaletteJoints` is 256 and a tree has thousands of
// axes, so joints go on the trunk, the primary limbs and as many secondaries as fit, and the twigs
// ride whichever joint carries them. That is the right place to spend it: the motion a viewer reads
// as "the tree is alive" is the slow travel of big limbs, and a twig that moves exactly with its
// parent is not a defect anyone can see at this scale.
//
// DETERMINISM. The integrator is state, so a pose is NOT a pure function of time -- it depends on
// the frames before it. That is the whole point of inertia and it is the one place in this work
// where the engine's usual "pure function of (parameters, time)" rule is deliberately not met.
// `TreeAnimator::settle` exists so an offline render can reach a reproducible state before frame
// zero, and the substep count is derived from the stiffest joint rather than from the frame rate,
// so the same wall-clock second integrates identically at 24 and at 120 fps.

#include "core/error.hpp"
#include "scene/animation.hpp"
#include "scene/skeleton.hpp"
#include "scene/tree.hpp"
#include "scene/tree_mesh.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace avgen::scene {

struct TreeRigSettings {
    // How far apart joints sit along an axis, in metres. Smaller means a smoother whip and fewer
    // limbs that fit in the palette.
    float jointSpacing = 2.2f;
    // Tiers that get their own joints, in order of priority. Tertiary axes never do: there are
    // over a thousand of them and they are below the scale anyone reads motion at.
    bool jointsOnSecondary = true;
    // Leaves room under kMaxPaletteJoints; the palette is a hard cap and overflowing it is a
    // validation failure rather than a degradation.
    int maxJoints = 240;

    // Physical response, per tier. omega is the resonance in radians per second and zeta the damping
    // ratio: a massive limb is slow and well damped, a twig is fast and rings.
    float trunkOmega = 0.55f, trunkZeta = 0.85f, trunkCompliance = 0.05f;
    float primaryOmega = 1.1f, primaryZeta = 0.55f, primaryCompliance = 0.35f;
    float secondaryOmega = 2.2f, secondaryZeta = 0.38f, secondaryCompliance = 1.0f;
    // Per JOINT. A chain of a dozen joints each bent by this compounds, so the crown's total travel
    // is much larger than any one number here suggests -- 0.30 produced a tree leaning like a palm.
    float maxBendRadians = 0.055f;
};

// The skeleton, plus what the animator needs that a Skeleton does not carry.
struct TreeRig {
    Skeleton skeleton;
    std::vector<std::uint32_t> jointNode;    // the graph node each joint sits on
    std::vector<std::uint32_t> jointAxis;    // the axis each joint belongs to
    std::vector<BranchTier> jointTier;
    std::vector<float> compliance;           // how freely this joint bends, 0 at the base
    std::vector<float> phase;                // decorrelates joints so nothing moves in lockstep
    std::vector<glm::vec3> restWorld;        // joint positions in the rest pose
    [[nodiscard]] std::size_t size() const { return skeleton.joints.size(); }
};

[[nodiscard]] Result<TreeRig> buildTreeRig(const TreeGraph& graph, const TreeRigSettings& settings = {});

// Binds every vertex of every tier to the rig, filling `MeshData::skin`. `meshes` must have been
// built from the same graph: the per-vertex axis recorded during the sweep is what makes the bind
// robust, because a twig hanging next to an unrelated limb is nearer that limb's joints than its
// own and a purely positional bind attaches it to the wrong branch.
[[nodiscard]] Result<void> skinTreeMeshes(const TreeGraph& graph, const TreeRig& rig, TreeMeshes& meshes);

// What drives the motion. Filled from the wind alone, or from the wind plus audio (phase 9); the
// animator does not know or care which.
struct TreeMotionInputs {
    double time = 0.0;
    glm::vec3 windDirection{1.0f, 0.0f, 0.25f};
    float windSpeed = 0.6f;   // steady lean
    float gust = 0.0f;        // 0..1, slow travelling swell
    float flutter = 0.0f;     // 0..1, fast small-scale
    float impulse = 0.0f;     // a one-frame shove, for an onset
};

class TreeAnimator {
public:
    void reset(const TreeRig& rig);
    // Integrates `dt` seconds. Substepped internally from the stiffest joint, so the result is a
    // function of elapsed time rather than of how it was divided into frames.
    void step(const TreeRig& rig, const TreeMotionInputs& inputs, float dt, float limitRadians = 0.055f);
    // Runs `seconds` of simulation before anything is drawn, so an offline render starts from the
    // wind's steady state instead of from a tree standing perfectly still.
    void settle(const TreeRig& rig, const TreeMotionInputs& inputs, float seconds);
    // The local pose, ready for `skinningPalette`.
    void pose(const TreeRig& rig, Pose& out) const;
    [[nodiscard]] std::span<const glm::vec2> bend() const { return bend_; }

private:
    std::vector<glm::vec2> bend_;     // radians, about world X and Z
    std::vector<glm::vec2> velocity_;
};

// Builds the `SkinnedRig` a Scene carries. `enabled` is false, deliberately: `updateRigs` would
// otherwise evaluate an animation player this rig does not have and overwrite the pose the animator
// just computed. The header says a disabled rig's palette "is left exactly as it is", which is
// precisely the contract wanted here.
[[nodiscard]] SkinnedRig makeSkinnedRig(const TreeRig& rig);

// Poses a rig and writes its palette. Call once per frame after `TreeAnimator::step`.
void applyTreePose(const TreeRig& rig, const TreeAnimator& animator, SkinnedRig& out);

} // namespace avgen::scene
