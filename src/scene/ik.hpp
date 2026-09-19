#pragma once

// Two-bone analytic inverse kinematics (ADR-344): the arithmetic of "put that joint there", with
// no rig, no pose and no skeleton anywhere in it.
//
// **Why analytic and not iterative.** A two-bone chain has a closed form -- the law of cosines
// gives the knee angle directly from the three lengths, and the rest is two rotations -- so CCD or
// FABRIK would buy nothing but iteration counts and a convergence threshold to get wrong. It is
// also the only form that is *exactly* the same on a scrub as on a play: an iterative solver seeded
// from the previous frame's answer is state carried across frames, which is precisely what ADR-091
// forbids the baked tier, and one seeded from rest converges to a slightly different place
// depending on how far it had to go. A closed form has no seed.
//
// **Pure, and deliberately ignorant.** Everything here is a function of (three points, a target, a
// pole) in one space. It does not know what a joint is, it cannot read a clock, and it allocates
// nothing. The caller decides which space that is -- `scene::PoseLayerStack` uses the rig's model
// space, per ADR-274 -- and the caller owns any smoothing, because a solver that smoothed would be
// a solver that remembered.
//
// **What it refuses.** A bone of zero length, a target sitting on the root, and a chain whose bend
// plane cannot be decided are all reported rather than approximated: each one has a direction that
// does not exist, and inventing one is how a leg ends up folded through a body. An out-of-reach
// target is not an error -- it is the ordinary case of a foot planting on ground further away than
// the leg is long -- so it clamps to the limb's own extension limit, lands short, and *says* it
// landed short.

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>

namespace avgen::scene {

// The three joint positions of a two-bone chain, in one space. `mid` need not be `root`'s direct
// child and `tip` need not be `mid`'s: the solve treats root->mid and mid->tip as rigid segments,
// which is exactly true for any intermediate joint the solve does not itself write. The farm pack
// needs that -- a sheep's hind leg is `UpperLegB.L -> LowerLegB.L -> AnkleB.L -> FootB.L`, and the
// ankle rides along.
struct TwoBoneChain {
    glm::vec3 root{0.0f};
    glm::vec3 mid{0.0f};
    glm::vec3 tip{0.0f};
};

// Why a solve did or did not put the tip on the target. "It did nothing" has four causes here and
// three of them are mistakes, which is the same reason `scene::LayerResolution` exists.
enum class IkStatus : std::uint8_t {
    // The tip is on the target, to floating-point.
    Solved,
    // The target was outside [minReach, maxReach]: too far for the limb to straighten to, or so
    // close the limb cannot fold small enough. The limb aims at it and stops at its own limit, so
    // the tip is on the *ray* from root to target and short of (or beyond) the target itself.
    Clamped,
    // A bone of zero length. There is no chain to solve and no rotation that would make one.
    DegenerateBone,
    // The target is on top of the root. "Which way" has no answer.
    DegenerateTarget,
    // The chain is already straight to within `kStraightEpsilon` and no pole was given, so the
    // plane the knee should bend in is undetermined. Refused rather than guessed: a guessed plane
    // is a knee that bends sideways, and on a quadruped it is a hock that bends the wrong way down
    // the whole herd. Give a pole.
    DegenerateBend,
};
[[nodiscard]] const char* ikStatusName(IkStatus status);

// The answer, as two **model-space pre-rotations** rather than as two local transforms, because
// the caller is the only thing that knows what a local transform is here.
//
//   new model orientation of the root joint = rootDelta * old
//   new model orientation of the mid joint  = rootDelta * midBend * old
//
// They are reported separately rather than pre-composed so a partial-weight caller can slerp each
// increment towards identity on its own. Slerping the *composed* mid rotation instead makes the
// knee's share of a half-weight solve depend on the hip's, which reads as the knee lagging.
struct TwoBoneSolution {
    IkStatus status = IkStatus::Solved;
    glm::quat rootDelta{1.0f, 0.0f, 0.0f, 0.0f};
    glm::quat midBend{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 mid{0.0f};  // where those rotations put the mid joint
    glm::vec3 tip{0.0f};  // and the tip -- equal to the target exactly when status is Solved
    float upperLength = 0.0f; // |mid - root|
    float lowerLength = 0.0f; // |tip - mid|
    float requested = 0.0f;   // |target - root|, what was asked for
    float achieved = 0.0f;    // |tip - root|, what the limb could do
    float minReach = 0.0f;    // |upper - lower|
    float maxReach = 0.0f;    // (upper + lower) * extension
    // The interior angle at the mid joint after the solve, radians: 0 folded flat, pi straight.
    float kneeAngle = 0.0f;
};

// Below this sine of the interior knee angle the chain counts as straight and its own bend plane
// is not usable. 1e-3 is about 0.057 degrees -- small enough that no posed leg trips it and large
// enough that the cross product it guards has not lost its direction to cancellation.
inline constexpr float kStraightEpsilon = 1e-3f;

// Solve `chain` so its tip reaches `target`.
//
// `pole` is a **position**, not a direction: the knee is swung about the root->target axis until it
// lies on the same side as this point. It is optional (`hasPole`), and when it is absent the knee
// keeps whatever plane the incoming chain already bent in, carried round by the aim -- which is the
// right default, because the animation is usually already bending the knee the correct way and the
// solver has no business having a second opinion. The exception is a straight chain, which has no
// plane to keep; that is `DegenerateBend`.
//
// `extension` is the fraction of (upper + lower) the limb may straighten to, 0..1. Short of 1 on
// purpose: at exactly 1 the knee angle is pi, the bend plane vanishes, and the next frame's solve
// has nothing to keep. 0.99 leaves a visible-to-nobody bend and a well-defined axis.
[[nodiscard]] TwoBoneSolution solveTwoBone(const TwoBoneChain& chain, const glm::vec3& target,
                                           const glm::vec3& pole, bool hasPole, float extension = 0.99f);

// The shortest-arc rotation taking unit `from` to unit `to`, with the antiparallel case resolved
// about `fallbackAxis` rather than left to a zero-length cross product. Exposed because both the
// solver and the foot-plant that sits on top of it need exactly this and a second copy would be a
// second convention.
[[nodiscard]] glm::quat shortestArc(const glm::vec3& from, const glm::vec3& to,
                                    const glm::vec3& fallbackAxis);

} // namespace avgen::scene
