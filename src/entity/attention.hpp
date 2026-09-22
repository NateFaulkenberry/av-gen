#pragma once

// Attention (Phase B §22): which of the things a character could look at, it is looking at.
//
// **What §22 is careful to say, and what this therefore is not.** *"Do not make LookAt itself
// responsible for choosing targets."* Phase D decides that a mushroom is interesting; Phase B turns
// that into body motion. So this does not score the world, does not know what a mushroom is, and
// has no opinion about what matters. It takes candidates that already carry a salience somebody
// else computed, and answers one question: **which one is the body attending to right now, and for
// how long has it been.**
//
// That question needs an answer of its own because the naive one thrashes. Two candidates of
// near-equal salience make a character snap between them every frame, and a viewer reads that as a
// malfunction rather than as indecision.
//
// **The hysteresis shape is `Gait::select`'s and `planLocomotion`'s, deliberately.** Not for
// consistency's sake: that shape has now been *measured* twice under a contested boundary -- 180
// frames of +/-1 degree wobble on a strafe threshold giving at most two switches -- so its
// behaviour is characterised rather than assumed. A third user makes it a family with one
// characterisation instead of three mechanisms with three.
//
// **Two numbers, not one.** Switch count alone is the wrong measure. A selector that switches
// rarely and holds each target for a fifth of a second reads worse than one that switches slightly
// more often and commits, because what a viewer notices is the *snap away and back*. So the state
// carries when the current target was acquired, and the tests assert on dwell as well as on count.

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace avgen::entity {

// Something a character could attend to. §22's list, and nothing beyond it: position, orientation,
// priority, weight, duration.
struct AttentionCandidate {
    // Stable across frames. How a candidate is recognised as "the same one I was already looking
    // at" -- an identity, never an index into a list that may be rebuilt in a different order.
    std::uint64_t id = 0;
    glm::vec3 position{0.0f};      // world
    glm::vec3 orientation{0.0f};   // world, unit; zero when the thing has no facing of its own
    // How much this deserves attention, computed by whoever knows what it is. Phase B never
    // writes this and has no rule for producing it.
    float salience = 0.0f;
    // How long attention should be held once acquired, in seconds. Zero means "no opinion", and
    // the settings' floor applies instead.
    float duration = 0.0f;
};

struct AttentionSettings {
    // A challenger must beat the incumbent's salience by this **fraction** before it may take over.
    // A fraction rather than an absolute, so the same setting works for a scene whose saliences run
    // 0..1 and one whose run 0..100.
    float switchMargin = 0.25f;
    // The floor under how long a target is held once acquired, whatever the candidates do. This is
    // the number that decides whether a character reads as committing or as twitching, and it is
    // separate from the margin because they catch different failures: the margin stops a
    // *near-equal* challenger, and the dwell stops a *briefly-stronger* one.
    float minDwellSeconds = 0.6f;
    // After this long, attention decays even if the target is still the most salient thing --
    // §22's `duration`. A character that stares at one mushroom forever is as wrong as one that
    // cannot choose.
    float maxHoldSeconds = 4.0f;
    // How long a target that has just been examined is left alone for. Attention is not only a
    // question of what is loudest: a character that has just looked at something should look
    // somewhere else before looking back, and that is the difference between attending and staring.
    float refractorySeconds = 0.8f;
    // Below this salience nothing is worth attending to and the body looks where it is going.
    float acquireThreshold = 0.05f;
    // ...and attention is not dropped until it falls below this, which is the other half of a band.
    float releaseThreshold = 0.02f;

    friend bool operator==(const AttentionSettings&, const AttentionSettings&) = default;
};

// What the character remembers. A plain value the entity owns, replayed by `EntityWorld::seek`
// (ADR-554): a `double` acquisition **time**, never an accumulator, for the reason ADR-086 gives.
struct AttentionState {
    std::uint64_t target = 0;   // 0 = attending to nothing
    double acquired = 0.0;
    // The target a hold just expired on, and the instant it may be looked at again.
    //
    // **Without this, `maxHoldSeconds` is a no-op for a lone candidate.** The first version
    // dropped the target when the hold expired and then re-acquired the same one on the same
    // frame, because it was still the most salient thing in the world -- so the character never
    // looked away, the dwell never reset, and the weight decayed to zero and stayed there. A
    // refractory period is what makes "I have looked at that" a fact with consequences.
    std::uint64_t cooling = 0;
    double coolUntil = 0.0;
    bool started = false;

    void reset() { *this = AttentionState{}; }
};

struct AttentionResult {
    std::uint64_t target = 0;
    glm::vec3 position{0.0f};
    bool hasTarget = false;
    // 0..1. Eases in on acquisition and out as the hold expires, so the head turns rather than
    // snapping -- and so a layer can multiply by it without knowing anything about attention.
    float weight = 0.0f;
    // Seconds the current target has been held. Reported because "how often does it switch" and
    // "how long does it commit" are different questions and only the second one is what a viewer
    // is actually reading.
    float dwell = 0.0f;
    bool changed = false;
};

// Choose. Pure: everything remembered arrives in `in` and leaves in `next`.
[[nodiscard]] AttentionResult chooseAttention(const AttentionSettings& settings,
                                              const AttentionState& in,
                                              const std::vector<AttentionCandidate>& candidates,
                                              double time, AttentionState& next);

} // namespace avgen::entity
