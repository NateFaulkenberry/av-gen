#pragma once

// **Staged and dark (ADR-615).** `adaptRootMotion` has no caller in `src/` or `tools/` -- tests
// only. So `RootMotionAdaptSettings` is a tuning surface for something that never runs, and
// `RootMotionAdaptResult::clamped` is a saturation report that nothing reads, including the
// `clamped` flag that exists precisely so this engine does not repeat the silent-clamp failure
// B.A found in `Gait::playbackRate`. Left built and documented rather than wired or deleted.

// Procedural root motion (Phase B §37): adapting an authored clip's displacement to the velocity
// the controller actually wants.
//
// **What §36 already settled and this must not reopen.** ADR-337 established one authoritative
// movement result: a clip's displacement is extracted, handed to the entity as
// `MotionAuthority::Simulation`, and subtracted from the pose so the two cancel. §36 is explicit
// that multiple systems must not fight over character translation. This does not add a second
// mover; it scales the one that exists, before it is applied, and the cancellation still holds
// because both halves see the same scaled number.
//
// **§37 is explicit that this is not blind X/Z scaling**, and the four constraints it names are
// the whole design:
//
//   * **orientation** -- the scale is applied in the body's own frame, so the forward component
//     and the lateral one are scaled separately. A body asked to halve its pace should take
//     shorter steps, not narrower ones.
//   * **turning** -- a turn is angular and a stride is linear, so the yaw a clip carries is left
//     alone. Scaling it would make a body asked to walk slower also turn slower, which is a
//     different instruction.
//   * **foot contacts** -- the scale is clamped, because a root that advances less than the feet
//     do is the foot slide this whole phase exists to remove. `strideRatio` and this are the same
//     quantity from opposite ends and the clamp is where they meet.
//   * **terrain** -- vertical displacement is never scaled. A clip that steps down a kerb steps
//     down the same kerb at any pace, and scaling Y would sink the body into a slope.
//
// **The free control.** Root displacement per stride against distance actually travelled is
// `Gait::footSlip` seen from the other end: one measures how far the body goes for a stride's
// worth of animation, the other how much animation is played for a metre of travel. They are
// reciprocals, so they must agree -- and a pair of measurements that must agree is a control that
// costs nothing. `test_root_motion_adapt.cpp` asserts the product is 1.

#include "entity/gait.hpp"

#include <glm/glm.hpp>

namespace avgen::entity {

struct RootMotionAdaptSettings {
    // How far the authored displacement may be scaled. Not unbounded, for the reason §37's
    // "respect foot contacts" implies: a root advancing at a tenth of the feet is a body
    // moonwalking, and past some point the honest answer is a different clip (§12's modes) rather
    // than a more extreme scale.
    float minScale = 0.25f;
    float maxScale = 2.5f;
    // Below this desired speed the body is stopping rather than travelling slowly, and the scale
    // is left to the stop's own ramp instead of being driven toward the floor.
    float standSpeed = 0.1f;
    // Whether the lateral component may be scaled independently of the forward one. On by
    // default: a body asked to move forward at half pace and sideways at full pace is strafing,
    // and one scale for both would be a different motion.
    bool separateLateral = true;

    friend bool operator==(const RootMotionAdaptSettings&, const RootMotionAdaptSettings&) = default;
};

struct RootMotionAdaptResult {
    // The displacement to apply this step, in the same frame the authored one arrived in.
    glm::vec3 displacement{0.0f};
    float forwardScale = 1.0f;
    float lateralScale = 1.0f;
    // **Reported, not hidden.** A scale sitting on its limit means the clip cannot express what
    // was asked and the feet will slide by the remainder -- which is exactly the failure
    // `Gait::playbackRate` hid across the entire shipping cast until B.A went looking.
    bool clamped = false;
};

// Scale `authored` so the body covers `desired` this step.
//
// `authored` and `desired` are both in the **body's own frame**: +Z forward, +X lateral. The
// caller converts, because the entity knows its own yaw and this function should not have to.
//
// `dt <= 0` returns the authored displacement unchanged rather than dividing (ADR-521).
[[nodiscard]] RootMotionAdaptResult adaptRootMotion(const RootMotionAdaptSettings& settings,
                                                    const glm::vec3& authored,
                                                    const glm::vec3& desired, float dt);

} // namespace avgen::entity
