#pragma once

// The motion provider seam (ADR-541, Phase B §32/§33), and the one rule that makes it worth having:
// **a provider is a pure function whose memory is owned by the entity and handed to it.**
//
// Phase A declined to build this, on the grounds that a seam with one implementation behind it and
// no second caller is a guess about the future. It is built now because Phase B gives it a second
// caller -- the controller -- and because Phase C's motion matcher and Phase E's neural provider
// are the same shape, and retrofitting a seam around a shipping clip player later is how a
// conditional becomes permanent.
//
// **Why the memory is not in the provider.** `EntityWorld::seek` reproduces a frame by replaying
// the simulation at a fixed step. Anything a provider remembers across frames -- the database frame
// a motion matcher settled on, the local clip time, the latent state of a network, the instant an
// inertialization began -- has to be reconstructible by that replay, or a scrubbed frame differs
// from a played one and ADR-360 is broken. A provider that owned its own memory would be the one
// object in the character pipeline a seek could not rewind.
//
// So the provider reads `MotionMemory`, writes the next one, and owns nothing. `Entity` holds the
// value, `Entity::reset` clears it with everything else, and the replay rebuilds it. This is the
// same split ADR-359 drew for the ground plane and ADR-337 drew for root motion, one layer out, and
// obligation D4 in `character_ai.hpp` already states it as a rule.

#include "scene/animation.hpp"
#include "scene/skeleton.hpp"

#include <cstdint>
#include <string>
#include <string_view>

#include <glm/glm.hpp>

namespace avgen::entity {

// How the body is getting about. Not a clip name and not a gait: a *mode* the request can ask for,
// which the provider turns into content through the motion pack.
enum class MovementMode : std::uint8_t {
    Ground,   // walking, running, standing -- feet on something
    Airborne, // jumping, falling; no stride to match and no foot to plant
    Rooted,   // performing an action in place; the body does not travel
};
[[nodiscard]] const char* movementModeName(MovementMode mode);

// What the character wants to be doing. **Locomotion intent only.**
//
// §32 warns against a giant bag of every possible behaviour, and the line this draws is: a
// `MotionRequest` says how the BODY should travel. Where the character is looking and what it is
// reaching for are *pose* concerns -- they already cross the seam as `LocomotionState::lookTarget`
// and are consumed by `PoseLayerDrive::Look`, and duplicating them here would make two answers to
// one question (ADR-260). A look does not change which clip plays.
//
// Rule R4 stands: this names an intent and never a clip. `style` is an authored label like "limp"
// or "sneak" that the motion pack resolves, exactly as `Activity` already resolves through
// `EntityDesc::clips`.
struct MotionRequest {
    // World space, metres per second. The magnitude is the speed and the direction is the heading,
    // so a strafe is expressible and a polar `speed`+`yaw` pair would not be (ADR-545).
    glm::vec3 desiredVelocity{0.0f};
    // World space, unit. Separate from the velocity's direction on purpose: a body circling a
    // target while watching it has one of each and they disagree.
    glm::vec3 desiredFacing{0.0f, 0.0f, 1.0f};
    float desiredTurnRate = 0.0f; // rad/s, signed; for turning on the spot
    MovementMode mode = MovementMode::Ground;

    // §38. Where the body will want to be heading shortly, so a turn can begin before the corner
    // rather than at it. One sample, not a trajectory: Phase C's motion matcher will want a real
    // future trajectory and this is the lightweight stand-in that does not pretend to be one.
    // Zero length means "no opinion", which is not the same as "straight ahead".
    glm::vec3 futureDirection{0.0f};
    float futureSeconds = 0.0f;

    // §39. An avoidance correction from outside -- Phase D supplies it, Phase B only executes it.
    // Added to `desiredVelocity`, not blended, because the caller has already decided how hard to
    // steer and a second weight here would be a second opinion.
    glm::vec3 steering{0.0f};

    // An authored style label, resolved through the motion pack. Empty means the default.
    std::string_view style;
};

// Everything a provider is allowed to remember, as a plain value.
//
// Bounded by construction: no containers, no handles into anything that could outlive a frame. A
// provider that needed more than this needs a decision, not a bigger struct.
struct MotionMemory {
    // Seconds into whatever the provider is playing. The clip player's `time`, a motion matcher's
    // position in the database frame it settled on.
    float localTime = 0.0f;
    // Which content it settled on. An index into the provider's own space -- a clip index, a
    // database frame -- meaningless to anyone else and never serialized.
    std::uint32_t selection = 0;
    // Bumped whenever `selection` changes, so a consumer can tell "still playing the same thing"
    // from "playing the same thing again". This is what an inertializer needs and what a naive
    // equality test on `selection` gets wrong on a loop.
    std::uint32_t generation = 0;
    // 0..1 through the locomotion cycle, when the content has a phase (Phase A's `PhaseTrack`).
    float phase = 0.0f;
    bool hasPhase = false;
    // The timeline second the current transition began, for inertialization. A *time*, never an
    // elapsed count, for the reason ADR-086 gives.
    double transitionStart = 0.0;

    void reset() { *this = MotionMemory{}; }
};

// Why a provider declined.
//
// **A provider that returns false must say why, and the reason must be visible** -- the same rule
// `LayerResolution`, `IkStatus`, `PathStatus`, `SocketResolution` and `ActionResult` already follow.
// A silent false is indistinguishable from a provider that is not installed, and the fallback chain
// below would then hide a broken provider behind a working one forever.
enum class MotionStatus : std::uint8_t {
    Produced,        // it posed the skeleton
    NoContent,       // nothing in its pack matches this request
    SkeletonMismatch,// the content is not for this rig (ADR-550's digest)
    NotReady,        // a model or a database is still loading
    Unsupported,     // it does not handle this movement mode or style
    Failed,          // it tried and could not
};
[[nodiscard]] const char* motionStatusName(MotionStatus status);

struct MotionResult {
    MotionStatus status = MotionStatus::NoContent;
    [[nodiscard]] bool ok() const { return status == MotionStatus::Produced; }
    // What it decided to play, for diagnostics and for the editor view §50 wants. A view into the
    // provider's own storage, valid until the next call.
    std::string_view content;
    // Clip seconds per timeline second the provider chose, if it chose one.
    float playbackRate = 1.0f;
};

// The seam.
//
// Const, because a provider holds no per-character state: one instance may serve every character in
// a scene, and the memory that distinguishes them arrives as an argument.
class IMotionProvider {
public:
    virtual ~IMotionProvider() = default;
    // A name for logs and for the debug view. Stable across frames.
    [[nodiscard]] virtual std::string_view name() const = 0;
    // Pose `out` for `request`. `in` is what this character remembered last frame and `next` is
    // what it will remember; a provider must write `next` whether it succeeds or not, because a
    // chain that falls through must not leave the memory holding a failed provider's state.
    [[nodiscard]] virtual MotionResult evaluate(const MotionRequest& request, const MotionMemory& in,
                                                double time, float dt,
                                                const scene::Skeleton& skeleton, scene::Pose& out,
                                                MotionMemory& next) const = 0;
};

} // namespace avgen::entity
