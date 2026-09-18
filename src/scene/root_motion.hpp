#pragma once

// Root motion extraction (ADR-335), behind a per-clip opt-in.
//
// ADR-161 decided root motion was not implemented because the content had none. It measured three
// clips of one Mixamo file and it was right about those three; two asset packs arrived afterwards
// and the check was never re-run. ADR-260 re-ran it across all 168 clips and found five per alien
// that genuinely translate their root, `Landing`'s -0.567 m among them, and recorded the inventory
// rather than a fix. This is the fix, and it is opt-in because **163 of the 168 clips must not
// change at all**: a clip that is not named here is read, sampled and posed by exactly the code
// that read, sampled and posed it before, and the extraction path is not entered.
//
// ---- what root motion is, in this engine's vocabulary -------------------------------------------
//
// A transfer of authority, not a change to the picture. A clip that carries displacement draws a
// body moving whether or not anything extracts it; what nothing knows, today, is that the body
// moved. `state().position()` -- ADR-260's simulation position, the one navigation, the crowd
// field, path validity and every trigger read -- stays exactly where it was while the drawn body
// walks away from it.
//
// So extraction does two things in the same frame and they have to cancel:
//
//   1. the displacement from the clip's first key to the sampled second is handed to the entity,
//      which adds it to `EntityState::travel` -- `MotionAuthority::Simulation`, the one authority
//      `scene::PoseLayerStack` is deliberately built to be unable to take (ADR-300 §8);
//   2. the same displacement is **subtracted from the pose**, so the body is drawn in exactly the
//      place it was drawn before.
//
// If only (1) happened the body would travel twice. If only (2) happened it would not travel at
// all. The invariant worth testing is that the composition of the two is the identity, and
// `tests/unit/test_root_motion.cpp` tests it that way, with the uncompensated arm as the control
// that says the measurement can see 2.045 m when there is 2.045 m to see.
//
// ---- the carrier, which is the whole of why this file is not four lines ------------------------
//
// The textbook move is "zero the root joint's translation channel". On this project's primary
// character content that is wrong, and the probe that ran before any of this was written is the
// reason it is known to be wrong. `assets/aliens/alien-scout.glb` has 90 joints and is nearly
// flat:
//
//     [0] rig                    <- the armature wrapper; no clip animates it
//       [1] root.x               <- foot.l, foot.r and the two thigh twists, and nothing else
//       [13] spine_05.x  [24..27] spine_04..01  [28] hand.l  [48] hand.r  [82] head.x
//       [74] Antenna  [78] Eye_L  [80] Eye_R  [83] Mouth  [84] leg_stretch.l  ...
//
// The spine, both hands, the head, the antenna, the eyes and the mouth are **siblings** of
// `root.x` under the armature, not descendants of it. Nearly every joint carries its own baked
// translation channel. Zeroing `root.x` would pin the legs and leave the torso descending: the
// character would come apart. Measured in `test_root_motion.cpp`'s "carrier" arm.
//
// So the displacement is **read** from one joint and **compensated at another**: the topmost
// ancestor of the joint it was read from -- joint 0 here, whose parent is -1. Subtracting a
// model-space vector from a parentless joint's local translation is exactly a rigid model-space
// translation of everything beneath it, because the translation component of `T * R * S` is `T`
// and nothing else touches it. On a properly nested rig (the farm pack: `Head01` under `Neck01`
// under a spine) read-joint and carrier coincide in effect, so the rule is not a special case for
// one asset.
//
// Which joint is read is the rule ADR-260's inventory already established and paid for: **the
// lowest-indexed joint the clip gives a translation channel to**. Import order is topological, so
// that is the highest joint in the hierarchy this clip moves. It is not `joints[0]` -- that is the
// armature wrapper no clip animates, and taking it cost ADR-260's first attempt 168 clips of
// silent zero. An opt-in may name a joint instead, and says so when the name misses.
//
// ---- determinism (ADR-091, ADR-267) -------------------------------------------------------------
//
// Nothing here accumulates. `displacementAt` is a pure function of (clip, joint, the axis mask,
// the clip second) and is always measured from the clip's own first key, never from the previous
// frame. The entity subtracts consecutive samples to get a step's worth, so a scrub that replays
// the same steps sums the same numbers, and a frame rendered at a wobbling rate agrees with one
// rendered offline. A running total on the rig would have been the obvious implementation and it
// is the one thing that could not have survived a seek.

#include "scene/skeleton.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace avgen::scene {

struct AnimationClip;

// Which components of the extracted displacement are handed to the simulation. The rest stay in
// the pose, untouched, exactly as if the clip had not been opted in at all.
//
// **Y defaults on and is the one an author will want off.** Grounding assigns rather than adds --
// `state.travel.y = ground.height - anchor.y`, in two behaviours -- so a grounded body's vertical
// root motion is overwritten inside the same update that produced it. That is not a bug in either
// layer: a body standing on terrain has its height decided by the terrain. It does mean that for
// `Landing`, whose displacement is 96% vertical, the axis mask is the difference between a setting
// that does something and one that is silently erased. ADR-335 §5 has the measurement.
struct RootMotionAxes {
    bool x = true;
    bool y = true;
    bool z = true;
    [[nodiscard]] glm::vec3 mask() const {
        return {x ? 1.0f : 0.0f, y ? 1.0f : 0.0f, z ? 1.0f : 0.0f};
    }
    [[nodiscard]] bool none() const { return !x && !y && !z; }
};
// "xyz", "xz", "y", "" -- the authored spelling. False for a string holding anything else.
[[nodiscard]] bool rootMotionAxesFromName(std::string_view name, RootMotionAxes& out);
// The canonical spelling of `axes`; "none" when it masks everything out.
[[nodiscard]] std::string rootMotionAxesName(const RootMotionAxes& axes);

// What one rig's current clip has displaced by, at one second.
//
// `generation` changes whenever the clip being played changes *or* is restarted at a new phase
// origin. It exists because `displacement` is always measured from the clip's first key, so two
// consecutive samples can only be subtracted when they belong to the same run of the same clip:
// without it, a cross-fade from `Landing` back to `Idle` would hand the entity the whole of
// `Landing`'s displacement backwards, in one step, as a teleport.
struct RootMotionSample {
    glm::vec3 displacement{0.0f}; // model space = the entity's own frame (ADR-274)
    std::uint64_t generation = 0;
    bool active = false;          // false = the clip playing here is not opted in
};

// One clip opted in, as a scene authors it.
struct RootMotionSpec {
    std::string clip;  // resolved through `scene::findClip`, so "Landing" finds "Rig|Landing"
    std::string joint; // "" = the lowest-indexed joint this clip translates
    RootMotionAxes axes;
};

// One clip opted in, resolved against the skeleton and clip list that will actually carry it.
struct RootMotionBinding {
    int clip = -1;    // index into the rig's clips
    int joint = -1;   // the joint whose translation is read
    int carrier = -1; // the parentless ancestor of `joint`; where the compensation is written
    RootMotionAxes axes;
    std::string clipName;
    std::string jointName;
};

// The opted-in clips of one rig. Empty on every rig in this repository that does not author one,
// which is every rig in this repository except the Character Intelligence Lab's `watcher`.
class RootMotionSet {
public:
    [[nodiscard]] bool empty() const { return bindings_.empty(); }
    [[nodiscard]] std::size_t size() const { return bindings_.size(); }
    [[nodiscard]] const std::vector<RootMotionBinding>& bindings() const { return bindings_; }

    // Resolves every spec against `skeleton` and `clips`, returning in prose what it could not:
    // a clip this rig does not have, a joint it does not carry, a clip that translates nothing,
    // an axis mask that masks everything out. The caller logs them with the node's name on. An
    // opt-in that silently did nothing would be the same failure as a socket that returned `true`
    // on its fallback (ADR-274), and this unit exists downstream of that lesson.
    [[nodiscard]] std::vector<std::string> bind(const std::vector<RootMotionSpec>& specs,
                                                const Skeleton& skeleton,
                                                const std::vector<AnimationClip>& clips);
    void clear() { bindings_.clear(); }

    // The binding for `clipIndex`, or nullptr -- which is the answer for 163 of the 168 clips and
    // the reason they are untouched.
    [[nodiscard]] const RootMotionBinding* find(int clipIndex) const;

private:
    std::vector<RootMotionBinding> bindings_;
};

// The model-space displacement of `binding.joint` from the clip's first key to `clipSeconds`,
// masked to the opted-in axes. A pure function; `scratchPose` is a caller-owned buffer so a
// per-frame call allocates nothing after the first.
//
// **Model space, not the palette** (ADR-274 §2). The palette entry is `model * inverseBind` and
// its translation is not where the joint is; on this asset the gap is 0.91 of a 1.662-unit
// character. Only the joint's own ancestor chain is composed, which on `alien-scout.glb` is two
// matrices rather than ninety.
[[nodiscard]] glm::vec3 rootMotionDisplacement(const Skeleton& skeleton,
                                               const std::vector<AnimationClip>& clips,
                                               const RootMotionBinding& binding, float clipSeconds,
                                               Pose& scratchPose);

// Subtracts `displacement` from the carrier joint's local translation, which -- because the
// carrier is parentless -- is exactly a rigid model-space translation of the whole skeleton by
// `-displacement`. Half (2) of the cancellation described at the top of this file.
void applyRootMotionCompensation(const RootMotionBinding& binding, const glm::vec3& displacement,
                                 Pose& pose);

// The model-space position of `joint` under `pose`, composing only its own ancestor chain.
// Exposed because every test of the above has to be able to ask the same question independently
// of the code under test, and one that could only ask through the code under test would be
// asserting that the implementation equals itself.
[[nodiscard]] glm::vec3 jointModelPosition(const Skeleton& skeleton, const Pose& pose, int joint);

} // namespace avgen::scene
