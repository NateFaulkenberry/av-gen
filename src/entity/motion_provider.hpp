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

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>

#include <glm/glm.hpp>

namespace avgen::entity {

// ---- inertialization at the seam (ADR-613, Phase C §32) ----------------------------------------
//
// Shared by every provider rather than owned by one, because it is a property of the seam: a
// provider swaps what the body is playing, and what it swapped away from has to decay rather than
// vanish. `MatchMotionProvider` and `ClipMotionProvider` both use these; they must not grow second
// versions.

// The interval a per-frame distance budget is stated over. **A distance budget carries the frame
// length it was measured at** -- ADR-612's 0.0510 m is a 1/30 s step -- and pairing it with the
// product's frame rate instead would halve or double it for free.
inline constexpr float kBlendBudgetFrameSeconds = 1.0f / 30.0f;

// **The halflife that minimises the worst thing a transition can do to a foot.** Derived, not
// chosen: see ADR-613 for the two failures it balances and for why the size of the jump cancels
// out of the answer. `soonestRepeatSeconds` is the shortest interval in which one transition can
// follow another -- the matcher's continuation lock, the gait tier's minimum dwell.
[[nodiscard]] float derivedInertializeHalflife(float soonestRepeatSeconds, float frameSeconds);

// A critically damped decay released from rest: exactly 1 at `elapsed` 0 and with zero slope
// there, so the pose at the transition instant is exactly the outgoing pose and there is no kink.
[[nodiscard]] float inertializationDecay(float halflife, float elapsed);

// The floor at which a transition is let go. **Not an epsilon**: dropping a slot is itself a
// discontinuity of `decay` times its offset, so this decides how big that last teleport is. At 1%
// the worst offset the Glowmere corpus produces (0.81 m) leaves 0.008 m, 16% of ADR-612's bar, and
// it ends the decay's tail at 0.43 s rather than 0.81 s -- which is what stops every slot being
// sampled on every frame. See ADR-613's cost table.
inline constexpr float kInertializationFloor = 0.01f;

// Add `decay` times the pose difference (`was` - `became`) to `out`. The two ends are the outgoing
// and incoming poses **at the instant of the transition**, sampled by the caller from whatever its
// content is addressed by; the offset is therefore recomputed every frame rather than remembered,
// which is what lets a scrub landing mid-transition reconstruct it (ADR-360) and what lets
// `MotionMemory` hold two integers where an engine would hold a pose.
void applyInertializedOffset(const scene::Pose& was, const scene::Pose& became, float decay,
                             scene::Pose& out);

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
    // World space, unit: the way the body faces **now**. This is not an intent but the frame the
    // intents above are read in. A motion database stores its features relative to the body's
    // facing, so a world-space velocity means nothing to it until it is expressed relative to this.
    // The default is +Z, the frame every clip is authored in.
    glm::vec3 bodyFacing{0.0f, 0.0f, 1.0f};
    MovementMode mode = MovementMode::Ground;

    // §38. Where the body will want to be heading shortly, so a turn can begin before the corner
    // rather than at it. One sample, not a trajectory: Phase C's motion matcher will want a real
    // future trajectory and this is the lightweight stand-in that does not pretend to be one.
    // Zero length means "no opinion", which is not the same as "straight ahead".
    // **Both are still written by nobody** -- `futureSeconds` has exactly one reference in the
    // tree, this line. `entity/trajectory.hpp` was built to produce them and is itself uncalled;
    // ADR-615 has the list and the ruling. A matcher reading these gets zero, which it correctly
    // treats as "no opinion".
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
    // The timeline second this provider last decided what to play. A *time*, never an elapsed
    // count, for the reason ADR-086 gives.
    //
    // **It was called `transitionStart`, and that name was true for one of its two writers.**
    // `ClipMotionProvider` writes it when the clip actually changes, which is a transition;
    // `MatchMotionProvider` writes it on **every search**, including the ones whose winner was the
    // continuation and which therefore changed nothing at all. The search interval and the
    // continuation lock read it under the second convention and need exactly that. So the field
    // was never wrong -- the *name* was, for the reader most likely to be hunting for it, since
    // "when did the current transition begin" is the first thing an inertializer asks and this
    // would have answered it with the time of the last search.
    //
    // Renamed to what both writers actually mean. **A deciding is not a changing**, and the blend
    // slots above keep their own clock rather than deriving one from here (ADR-613).
    double decisionTime = 0.0;

    // ---- the inertialized transition (ADR-613, Phase C §32) ------------------------------------
    //
    // **Two sample indices and a clock, because the offset itself may not live here.** ADR-547's
    // inertialization on `AnimationPlayer` decays the pose difference captured at the instant of a
    // transition, and recomputes that difference every frame rather than storing it, so that a
    // scrub landing mid-transition reconstructs it instead of inheriting it. The same rule binds
    // harder at this seam: `MotionMemory` holds no containers (above), and `advance` may not touch
    // a skeleton (ADR-556), so the difference can be neither stored nor computed on the simulation
    // half. What *can* be recorded is the pair of samples it is the difference between, which is
    // two integers, and `pose` -- which already has a skeleton and runs once per drawn frame --
    // recomputes the rest.
    //
    // Both are in the settling provider's own index space, exactly as `selection` is, and mean
    // nothing to anyone else.
    static constexpr std::uint32_t kNoBlend = 0xFFFFFFFFu;

    // **More than one, because a transition can interrupt a transition, and dropping the one in
    // flight is the same teleport in miniature.** Measured: with one slot the shipping loop leaves
    // 18.8% of the outgoing offset undecayed when the next switch arrives 0.2 s later, and throws
    // it away in a single frame -- which is most of what the fix has left to give (ADR-613's
    // table). Slots are the whole cost of not doing that: three `std::uint32_t` and a `float` per
    // level of nesting, still a plain value, still bounded, still reconstructed by a replay.
    //
    // **Three is a measured depth, not a guess** -- see ADR-613 for what each level buys.
    static constexpr std::size_t kBlendSlots = 3;
    struct Blend {
        // What was playing at the instant of the switch.
        std::uint32_t from = kNoBlend;
        // What it switched to, at that same instant. Recorded separately from `selection` because
        // `selection` moves on with the motion and the offset is anchored to where it started.
        std::uint32_t to = kNoBlend;
        // Where each end was in **the provider's own time coordinate** at that instant -- a clip
        // second for the clip player, a database sample's time for the matcher. Recorded because
        // the two ends are frozen poses and a provider whose content is addressed by an index plus
        // a time cannot reconstruct them from the index alone.
        float fromTime = 0.0f;
        float toTime = 0.0f;
        // Seconds since that instant. **Its own clock**, for the reason `decisionTime` gives
        // above.
        float elapsed = 0.0f;
        [[nodiscard]] bool live() const { return from != kNoBlend && to != kNoBlend; }
        friend bool operator==(const Blend&, const Blend&) = default;
    };
    // Newest first. A new transition shifts the array down and drops the oldest.
    Blend blends[kBlendSlots]{};

    [[nodiscard]] bool blending() const { return blends[0].live(); }
    // Advance every live blend's clock. Called on every step, including the ones that do not
    // search: `pose` is handed a memory and a skeleton and no clock at all (ADR-556).
    void tickBlends(float dt) {
        for (Blend& b : blends) {
            if (b.live()) {
                b.elapsed += dt;
            }
        }
    }
    // Record a transition, keeping the ones still in flight underneath it.
    void pushBlend(std::uint32_t from, float fromTime, std::uint32_t to, float toTime,
                   std::size_t slots) {
        for (std::size_t i = kBlendSlots - 1; i > 0; --i) {
            blends[i] = blends[i - 1];
        }
        blends[0] = Blend{from, to, fromTime, toTime, 0.0f};
        for (std::size_t i = std::max<std::size_t>(slots, 1); i < kBlendSlots; ++i) {
            blends[i] = Blend{};
        }
    }
    void clearBlends() {
        for (Blend& b : blends) {
            b = Blend{};
        }
    }
    // **Which provider settled this memory**, as an index into the chain, or -1 for none. Recorded
    // because `advance` and `pose` are separate calls and the second must go to the provider that
    // won the first: a chain that re-selected at pose time could hand a motion matcher's database
    // frame to the clip player, which would read it as a clip index and pose a different animation
    // entirely. It is part of the memory, so a replay reconstructs it like everything else.
    int provider = -1;
    // **Which database `selection` and the blends index**, as that database's `identity`, or 0 for
    // content that is not a database. Phase C §40/§76: a database can be replaced while characters
    // are mid-motion, and a sample index from the old one is a valid-looking index into the new one
    // that names a different frame. A provider that finds a stamp other than its database's treats
    // the memory as a first selection -- search afresh, nothing to blend from -- rather than posing
    // whatever frame the stale index now happens to hit. An identity, not a load counter, so a
    // replay against the same database reproduces the same memory (ADR-360).
    std::uint64_t database = 0;

    void reset() { *this = MotionMemory{}; }
};

// **The two halves, and why the seam has two methods rather than ADR-541's one.**
//
// ADR-541 specified a single `evaluate` that advanced the memory and posed the skeleton together.
// Wiring it found the reason that cannot work here, and the reason is measured rather than
// aesthetic: `EntityWorld::seek` reproduces a scrubbed frame by **replaying the simulation at a
// fixed 1/60 step, for up to ninety seconds** -- 5,400 steps per body -- and then poses the rigs
// **once**, at the target time. Scrub latency is already this repository's worst interactive cost.
// A seam that posed an 89-joint skeleton on every replay step would multiply that by 5,400.
//
// So the seam splits where the work splits:
//
//   * `advance` is the **simulation** half. It runs on every step, including every replay step, and
//     it is what makes a scrubbed frame reproduce a played one. It may not touch a skeleton.
//   * `pose` is the **presentation** half. It runs once per drawn frame, from memory `advance`
//     already settled, and it is a pure function of that memory.
//
// This is not a compromise; it is the shape these algorithms already have. Learned motion matching
// is a Stepper that advances a latent and a Decompressor that turns it into a pose (Phase 0 §3);
// classical motion matching is a search that picks a database frame and a lookup that reads it.
// The single-call version was hiding that seam, not simplifying it.

// Why a provider declined.
//
// **A provider that returns false must say why, and the reason must be visible** -- the same rule
// `LayerResolution`, `IkStatus`, `PathStatus`, `SocketResolution` and `ActionResult` already follow.
// A silent false is indistinguishable from a provider that is not installed, and the fallback chain
// below would then hide a broken provider behind a working one forever.
// **As built, nothing in the product distinguishes these (ADR-615).** The only consumers in
// `src/` are `MotionResult::ok()` (`== Produced`) and the chain's passthrough, so the six
// diagnoses collapse to a boolean on every live path; `motionStatusName` has no caller outside
// tests. And two values are produced by nobody at all: **`SkeletonMismatch`** -- the digest it
// cites exists on both `MotionPack` and `MotionDatabase`, and no provider compares them -- and
// **`Failed`**. The rationale above is therefore still an aspiration: a silent false and a typed
// one are equally invisible until something reads the type.
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

    // **The simulation half.** Advance `in` to `next` for `request`. Runs on every step, including
    // every step of a seek's replay, and touches no skeleton.
    //
    // A provider must write `next` whether it succeeds or not, because a chain that falls through
    // must not leave the memory holding a failed provider's state.
    [[nodiscard]] virtual MotionResult advance(const MotionRequest& request, const MotionMemory& in,
                                               double time, float dt, MotionMemory& next) const = 0;

    // **The presentation half.** Write the pose `memory` describes into `out`. Runs once per drawn
    // frame and must be a pure function of (`memory`, `skeleton`) -- if it needed anything else,
    // that thing belongs in `MotionMemory` and therefore in the replay.
    [[nodiscard]] virtual MotionResult pose(const MotionMemory& memory, const scene::Skeleton& skeleton,
                                            scene::Pose& out) const = 0;
};

} // namespace avgen::entity
