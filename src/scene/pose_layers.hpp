#pragma once

// The animation layer stack (ADR-300): what goes on top of the clip the state machine is playing.
//
// Before this, one cross-fade between two clip slots was the entire blending model of this engine,
// and three-way fades were explicitly refused. That is enough to say "walk, then run" and not
// enough to say anything at all about a *part* of a body: a head that turns while the legs keep
// walking, a flinch in the shoulders that does not interrupt the stride, an aim. Those are not
// enhancements on top of locomotion -- they are the only way the expressive half of a character
// gets expressed, and `entity::LocomotionState` has been publishing `reaction`, `lookTarget` and
// `hasLookTarget` every frame, into nothing, for as long as it has existed (ADR-225 is the rule
// that makes that a defect rather than a spare field).
//
// **Every layer here is ADR-260 `PoseOnly`.** A layer writes `SkinnedRig::pose` and nothing else.
// It cannot write `state().position()`, it cannot write a `MotionOffset`, and it cannot touch the
// node's position parameter -- not by policy but structurally, because this module includes nothing
// from `entity/` or from the composition and has no way to reach any of them. The stride-bob defect
// (ADR-260, defect 1) is exactly what happens when that is a convention instead of a boundary.
//
// **Determinism.** `apply()` is a pure function of (the layers' current intent, the skeleton, the
// clips, and the sample second it is given). Nothing here is integrated, smoothed or remembered
// across frames, which is deliberate: ADR-091 asks the baked tier for scrub == play, and a layer
// that carried state would be the thing that broke it. The smoothing a look-at needs already exists
// one layer up, in the behaviour that decides *where* to look, and that layer is re-simulated on a
// seek.
//
// **The frame.** A layer's target is **entity-local** -- the rig's model space -- for the same
// three reasons ADR-274 gives for `ISkeletonQuery::jointTransform`: a rig is shared, glTF bakes the
// file's chain into the joints, and the GPU multiplies the entity transform in afterwards. Whoever
// sets the intent does the world-to-entity conversion, because the entity is the only thing that
// knows where this rig is standing.

#include "scene/ik.hpp"
#include "scene/skeleton.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace avgen::scene {

struct AnimationClip;

// What a layer does to the joints its mask covers.
enum class PoseLayerKind : std::uint8_t {
    // Turn a group of joints, as a rigid group, about one joint's pivot, so that the body's forward
    // axis points at a target. A head turn, a look, an aim.
    Aim,
    // Add a clip's displacement from its own first frame on top of whatever is already posed. The
    // clip is not played *instead of* the gait, it is played *on* it: the shoulders can perform a
    // reaction while the legs keep the stride they were in.
    Additive,
    // Two-bone inverse kinematics on a named hip->knee->foot chain, so a foot lands on the ground
    // that is actually under it rather than on the ground the walk cycle was authored over
    // (ADR-344). The one kind here that reads a *chain* rather than a mask, for the reason ADR-344
    // gives: a two-bone solve is not maskable per joint, because half a knee does not reach half a
    // target. `weight` still blends the whole correction towards the animated pose.
    Foot,
};
[[nodiscard]] const char* poseLayerKindName(PoseLayerKind kind);
[[nodiscard]] bool poseLayerKindFromName(std::string_view name, PoseLayerKind& out);

// Which field of the animation-intent seam moves this layer.
//
// A layer with `Manual` drive is one a timeline, a tool or a test sets by hand. The other two are
// the point of the exercise: they name a field of `entity::LocomotionState` that was published
// every frame and read by nobody. The binding is by *role* and never by layer name, so an author
// may call a layer whatever they like and a second look layer on the same body still works.
enum class PoseLayerDrive : std::uint8_t {
    Manual,
    Look,     // weight from `hasLookTarget`, target from `lookTarget`
    Reaction, // weight from `reaction`
    // Weight 1 while the body is standing on something, and the ground plane under it -- the one
    // the entity's own `GroundFollower` already smoothed -- handed down as `groundPoint` and
    // `groundNormal`. ADR-344 §4: the smoothing lives up there, where it is re-simulated on a seek,
    // and never down here, where it would be memory a pose layer is not allowed to have.
    Ground,
};
[[nodiscard]] const char* poseLayerDriveName(PoseLayerDrive drive);
[[nodiscard]] bool poseLayerDriveFromName(std::string_view name, PoseLayerDrive& out);

struct PoseLayer {
    std::string name;
    PoseLayerKind kind = PoseLayerKind::Aim;
    PoseLayerDrive drive = PoseLayerDrive::Manual;
    JointMaskSpec mask;

    // ---- Aim ----------------------------------------------------------------------------------
    // The joint the group turns about. Empty means the first joint of the mask this rig actually
    // carries. Naming it matters on a flat rig: the eyes and the mouth of `alien-scout.glb` are
    // siblings of the head rather than children of it, so "turn the head" is five joints turning
    // about the head's pivot, and which of the five holds the pivot is not a detail.
    std::string pivot;
    // The direction, **in the rig's own model space**, that counts as the body's forward. The
    // default is not a guess about glTF: it is this engine's own yaw convention, which sets a
    // body's facing as `yaw = atan2(direction.x, direction.z)` (`entity/behaviors.cpp`), so yaw
    // zero is +Z and an asset that did not face +Z would already be walking backwards everywhere.
    // Authorable because the day an asset arrives that does not, the alternative is a silent 180.
    glm::vec3 forward{0.0f, 0.0f, 1.0f};
    float maxYawDegrees = 70.0f;   // horizontal swing away from `forward`
    float maxPitchDegrees = 35.0f; // vertical

    // ---- Additive -----------------------------------------------------------------------------
    // The clip whose displacement from its own first frame is added. Resolved through
    // `SkinnedRig::findClip`, so a scene may name "React" and get "Alien_Low_Green|React".
    std::string clip;
    float clipRate = 1.0f;
    // The phase is the timeline second, wrapped, and not the time this layer's weight started
    // rising. That is a limitation and it is a deliberate one: a start time is state carried across
    // frames, and the only place it could be kept correctly across a seek is inside the simulation
    // that a seek replays -- `entity::LocomotionState`, whose publish site is `entity.cpp`. ADR-300
    // §6 says what that costs and who owns the field.

    // ---- Foot (ADR-344) -------------------------------------------------------------------------
    // The chain, named outright and in order: the hip, the knee, and the joint that touches the
    // ground. Three names rather than "the leg and its descendants", because the nine farm rigs
    // spell the same anatomy three different ways -- the bull ends at `HoofB.L`, the goat runs
    // `LowerLegB.L -> AnkleB.L -> FootB.L`, the chick stops at `Foot.L` -- and there is no rule
    // that turns one into the others. Any joints *between* the named three ride along unchanged,
    // which is what makes the goat's ankle a non-problem.
    //
    // `chainMid` must be a descendant of `chainRoot` and `chainTip` a descendant of `chainMid`;
    // `bind` checks it and says so when it is not, because "these three names are a leg" is an
    // assumption the alien pack disproves (its foot, thigh and leg are three separate branches).
    std::string chainRoot;
    std::string chainMid;
    std::string chainTip;

    // Which way the knee points, **as a direction in the rig's model space**, not a position: a
    // position would have to be rewritten every frame by whoever moved the body, and this is a fact
    // about the animal. Zero means "keep the bend plane the animation is already in", which is the
    // right default whenever the clip bends the knee at all -- the solver has no better opinion
    // than the animator. It is only needed for a chain the clip leaves straight, and there it is
    // required: `IkStatus::DegenerateBend` refuses rather than picking a side.
    glm::vec3 poleDirection{0.0f};
    // How straight the limb may go, as a fraction of its own length. 1 by default, and ADR-344 has
    // the measurement that says so: the farm pack binds its legs at 97.9% to 100.0% of their own
    // span, so anything less clamps a hoof standing exactly where the artist put it.
    float extension = 1.0f;

    // ---- Foot: the ground, as a plane ----------------------------------------------------------
    // Foot planting takes a plane rather than a point so that one piece of intent serves all four
    // of a bull's feet: each foot drops onto the plane *underneath itself*, along model-space -Y,
    // rather than all of them converging on one spot. An explicit `target` overrides it, which is
    // how a test or a timeline drives one foot by hand.
    //
    // Entity-local, like every other target here (ADR-274).
    glm::vec3 groundPoint{0.0f};
    glm::vec3 groundNormal{0.0f, 1.0f, 0.0f};
    bool hasGround = false;
    // How far above the plane the *tip joint* rests, in the rig's own model units. A hoof joint is
    // not the sole of the hoof, and the difference is the gap or the sink.
    float groundOffset = 0.0f;
    // 0 keeps the foot's animated orientation; 1 lays its sole flat on the plane. Deliberately the
    // same shape and the same argument as `entity::GroundSettings::slopeAlign`, one level down: a
    // hoof leans into a slope, it does not become part of it. The two are complementary --
    // `slopeAlign` tilts the body, this tilts one foot -- and neither replaces the other.
    float footAlign = 0.0f;
    // Which way is *out of the sole*, as a direction in the tip joint's own bind frame.
    //
    // **Zero means "whatever was up when the animal was standing"**, resolved once by `bind` from
    // the rig's rest pose, and that default is a measurement rather than a convenience. The obvious
    // convention is a cardinal axis -- the tip joint's +Y -- and on this pack it is wrong on all
    // nine: these joints are aligned along the *bone*, not along the ground. Not one of them has an
    // axis within 26 degrees of vertical, and a bull's hind hoof is 42.8 degrees off, so a +Y
    // convention lays the sole at 43 degrees to the slope and calls it aligned. Taking the rest
    // pose's up needs no authoring and cannot be wrong about a rig it has read.
    glm::vec3 soleUp{0.0f};

    // ---- intent, written per frame by whatever drives the layer --------------------------------
    float weight = 0.0f;         // 0 = this layer does nothing at all this frame
    glm::vec3 target{0.0f};      // ENTITY-LOCAL (the rig's model space), never world
    bool hasTarget = false;
};

// What one layer did this frame, for the same reason `entity::SocketResolution` exists: "it did
// nothing" has four different causes and three of them are mistakes.
enum class LayerResolution : std::uint8_t {
    Inactive, // weight 0: nothing was asked of it
    NoJoints, // its mask named joints and this rig carries none of them
    NoPivot,  // an aim layer whose pivot joint is not in this rig
    NoSource, // an additive layer whose clip this rig does not have
    NoTarget, // an aim layer with no target this frame, or a target on top of its own pivot
    // A foot layer whose three chain names this rig does not carry, or carries in something that is
    // not a chain: the named knee is not below the named hip, or the named foot is not below the
    // knee. The alien pack is the reason this is a first-class answer rather than an assertion --
    // `foot.l` is a direct child of `root.x` and `leg_stretch.l` is not even under it, so the three
    // names an author would reach for name three branches and not a limb.
    NoChain,
    // A foot layer that solved, but on a target its leg cannot reach: the limb is extended towards
    // it and stopped at its own limit. Separate from `Applied` because it is the honest report of
    // a foot that is *not* where it was asked to be, and a scene in which every foot says this is
    // a scene whose ground intent is wrong by more than a leg length.
    Clamped,
    // A foot layer the solver refused: a zero-length bone, a target on top of the hip, or a
    // straight chain with no pole to say which way the knee folds. `scene::ikStatusName` says
    // which; this says that it happened.
    Degenerate,
    Applied,  // it wrote at least one joint
};
[[nodiscard]] const char* layerResolutionName(LayerResolution r);

struct PoseLayerStats {
    std::uint32_t layers = 0;  // layers in the stack
    std::uint32_t applied = 0; // layers that wrote a joint
    std::uint32_t joints = 0;  // joint writes, summed over the layers that applied
};

// An ordered stack of layers over one skeleton. Lives on `SkinnedRig`; copied with it, because
// ADR-086 copies a rig per node instance and two characters must be able to be looking at
// different things.
class PoseLayerStack {
public:
    [[nodiscard]] bool empty() const { return layers_.empty(); }
    [[nodiscard]] std::size_t size() const { return layers_.size(); }
    // Per-frame intent is written through here; masks are resolved by `bind`.
    [[nodiscard]] std::vector<PoseLayer>& layers() { return layers_; }
    [[nodiscard]] const std::vector<PoseLayer>& layers() const { return layers_; }
    [[nodiscard]] const std::vector<JointMask>& masks() const { return masks_; }
    [[nodiscard]] const std::vector<LayerResolution>& results() const { return results_; }
    // Per layer, what the two-bone solver said the last time a `Foot` layer ran. `Solved` on every
    // layer that is not one, which is a lie a caller has to read alongside `results()` -- the point
    // is to say *which* degeneracy, and `LayerResolution::Degenerate` only says that there was one.
    [[nodiscard]] const std::vector<IkStatus>& ikStatuses() const { return ikStatus_; }
    // The layer with this role, or nullptr. Roles are how the animation-intent seam reaches a
    // layer; a name is how a person does.
    [[nodiscard]] PoseLayer* find(PoseLayerDrive drive);
    [[nodiscard]] const PoseLayer* find(std::string_view name) const;

    void clear();
    // Installs `layers` and resolves every mask against `skeleton`, reporting in prose what it
    // could not resolve: a joint name this rig does not carry, a pivot it does not carry, a clip it
    // does not have, a mask that resolved to nothing, and a mask whose joints nest inside one
    // another. The caller logs them. An unreported no-op is the failure this whole unit exists to
    // stop repeating.
    [[nodiscard]] std::vector<std::string> bind(std::vector<PoseLayer> layers, const Skeleton& skeleton,
                                                const std::vector<AnimationClip>& clips);
    // Re-resolves the installed layers against a (possibly different) skeleton, keeping the intent.
    [[nodiscard]] std::vector<std::string> rebind(const Skeleton& skeleton,
                                                  const std::vector<AnimationClip>& clips);

    // Applies every layer in order to `pose`, which must already hold the base pose the state
    // machine produced. `now` is the rig's sample second, not the frame's -- so a rate-limited rig's
    // layers move on the same fixed grid its clips do (ADR-086) rather than on the frame rate.
    PoseLayerStats apply(const Skeleton& skeleton, const std::vector<AnimationClip>& clips, double now,
                         Pose& pose);

private:
    std::vector<PoseLayer> layers_;
    std::vector<JointMask> masks_;
    std::vector<LayerResolution> results_;
    std::vector<int> clipIndex_;  // per layer, resolved once by bind
    std::vector<int> pivotIndex_; // per layer, resolved once by bind
    // Per layer, the three joint indices of a `Foot` chain, or -1. Resolved once by bind, including
    // the check that they are an ancestor chain rather than three names that happen to exist.
    std::vector<glm::ivec3> chain_;
    // Per layer, `PoseLayer::soleUp` resolved against this rig's rest pose. Held here rather than
    // recomputed per frame because it is a fact about the asset, and read out of the *rest* pose
    // rather than the current one because "up" has to mean the same thing on every frame of a walk.
    std::vector<glm::vec3> soleUp_;
    std::vector<IkStatus> ikStatus_;
    // Scratch, kept so a per-frame apply allocates nothing after the first.
    std::vector<glm::mat4> model_;
    std::vector<glm::mat4> updated_;
    Pose reference_;
    Pose sampled_;
};

// The rotation that takes `from` to `to`, limited to `maxYaw` radians of azimuth and `maxPitch`
// radians of elevation away from `from`, about model-space up (+Y). Exposed because it is the whole
// of what an aim layer decides and a test that could only reach it through a posed rig would be
// testing four things at once.
[[nodiscard]] glm::quat aimRotation(const glm::vec3& from, const glm::vec3& to, float maxYaw, float maxPitch);

// Where a foot standing at `tip` should stand, given the plane through `planePoint` with normal
// `planeNormal` and a joint that rests `offset` above it. The drop is along model-space **-Y**
// rather than along the plane normal, because the foot is over the ground it is over: sliding it
// down the normal would move it sideways across the terrain, and on a 30-degree bank that is half a
// hoof's width of drift for nothing. Exposed because it is the whole of what "plant" means and a
// test that could only reach it through a posed rig would be testing four things at once.
[[nodiscard]] glm::vec3 plantOnPlane(const glm::vec3& tip, const glm::vec3& planePoint,
                                     const glm::vec3& planeNormal, float offset);

} // namespace avgen::scene
