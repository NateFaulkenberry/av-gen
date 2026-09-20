#pragma once

// Offline motion analysis (ADR-546): what a clip's joints are doing with the ground, and where in
// a cycle any given instant sits.
//
// **Offline, and pure.** Nothing here runs per frame. It takes a skeleton and a clip, samples them
// on a fixed grid, and returns metadata that an offline tool bakes into a motion pack and a runtime
// then reads. It allocates, which is why it is not in the per-frame path; it reads no clock, draws
// no randomness, and two runs over the same clip return the same bytes.
//
// **Contacts are not feet.** A contact is a joint that has stopped moving relative to the ground
// for long enough to count. That definition covers a hoof, a hand on a table, a knee, a tail tip
// and a chicken's toe, and the caller says which joints to watch. Nothing here knows how many legs
// a character has -- the farm pack alone has two, four and "the chick's, which is two but tiny",
// and the alien's are detached branches (ADR-543).
//
// **Phase is not a walk cycle.** It is a monotone [0,1) coordinate through whatever the clip
// repeats, anchored on a reference joint's contacts when the clip has any and on time when it does
// not. A one-shot take gets a phase too, and says it is not cyclic, because a system that only
// understood cycles would have nothing to say about `Landing` -- which is exactly the clip a
// transition most wants a phase for.

#include "scene/skeleton.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace avgen::scene {

struct AnimationClip;

// ---- contacts ------------------------------------------------------------------------------------

enum class ContactKind : std::uint8_t { Foot, Hand, Body, Custom };
[[nodiscard]] const char* contactKindName(ContactKind kind);
[[nodiscard]] bool contactKindFromName(std::string_view name, ContactKind& out);

// A joint to watch, and what to call what it touches.
struct ContactJoint {
    std::string joint;
    ContactKind kind = ContactKind::Foot;
};

struct ContactSettings {
    // Sampling grid for the analysis, in hertz. 30 matches the alien pack's authored rate; a clip
    // sampled far off its own key rate invents motion between keys and then measures it.
    float sampleRate = 30.0f;
    // Below this speed, in model units per second, a joint counts as not moving. A speed and not a
    // displacement, so the answer does not change with the sample rate.
    //
    // **Applied to the VERTICAL component, and to the horizontal one only when the clip actually
    // travels.** "Planted means stationary" is a fact about the ground and not about a clip: every
    // locomotion clip in this repository is authored in place (ADR-540), and in an in-place cycle
    // the stance foot is precisely the thing that moves -- it sweeps backwards under the hips at
    // the authored stride speed while the swing foot comes forward. A horizontal test on that
    // content finds the swing and calls it the plant. Measured: it gave `Walking` zero left
    // contacts and `Idle` a 100% duty cycle on both feet, which is exactly backwards.
    float speedThreshold = 0.35f;
    // And within this fraction of the joint's own vertical RANGE in this clip, above the lowest it
    // gets. The pair of tests is needed: a foot at the top of its swing can be instantaneously
    // stationary, and a foot dragged along the ground is low but moving; either test alone finds
    // one of those and calls it a plant.
    //
    // **A fraction of the range rather than an absolute distance**, because an absolute one is a
    // magic number with an asset's scale baked into it -- it means one thing on a 1.66 m alien, a
    // different thing on a 0.11 m chick, and a third on a rig authored in centimetres. It was
    // absolute (0.06) in the first version and it found a false plant: `Walking`'s left foot dips
    // to 0.173 and hesitates at the bottom of its swing, 0.035 above the true plant at 0.138, and
    // a 0.06 band swallowed both and reported two contacts where there is one.
    float heightFraction = 0.15f;
    // A floor under that band, in model units, so a joint that does not move vertically at all --
    // a standing idle, where the range is zero -- still counts as in contact rather than failing a
    // zero-width test on floating-point noise.
    float heightFloor = 1e-4f;
    // Runs shorter than this are noise rather than contact. Two samples at 30 Hz is 1/15 s.
    std::uint32_t minSamples = 2;
    // Gaps shorter than this are closed before short runs are dropped, so a single bad sample in
    // the middle of a plant does not split it into two.
    std::uint32_t bridgeSamples = 1;
    // Whether the clip's end joins its start. Locomotion clips loop, and a looping clip's contacts
    // WRAP: `Running`'s right foot is planted from 0.633 s through 0.033 s of the next lap, which a
    // linear reading reports as two separate contacts at opposite ends of the clip. Measured on the
    // real pack, and it is not a rounding artifact -- it is half the stance.
    //
    // With this set, a span touching the start and a span touching the end are merged into one
    // wrapped span (recorded with `start > end`), and a reference joint with exactly one wrapped or
    // unwrapped plant is cyclic with a cycle equal to the clip's own length.
    bool looping = true;
};

// One span of contact, in the clip's own seconds.
// One span of contact, in the clip's own seconds. `start > end` means the span WRAPS the loop
// point -- the contact began near the end of the clip and continues past its start.
struct ContactSpan {
    float start = 0.0f;
    float end = 0.0f;
    float clipLength = 0.0f; // so a wrapped span can report its own duration
    [[nodiscard]] bool wraps() const { return start > end; }
    [[nodiscard]] float duration() const {
        return wraps() ? (clipLength - start) + end : (end > start ? end - start : 0.0f);
    }
};

struct ContactTrack {
    std::string joint;
    ContactKind kind = ContactKind::Foot;
    int jointIndex = -1;         // resolved against the skeleton, or -1 when the rig lacks it
    std::vector<ContactSpan> spans;
    // What fraction of the clip this joint spends in contact. A locomotion cycle sits near 0.4-0.6
    // per foot; a value near 0 or near 1 is the tell that the thresholds are wrong for this asset,
    // and it is reported rather than left for a human to notice.
    float dutyCycle = 0.0f;
    float lowest = 0.0f;         // the lowest this joint got, model units -- the height datum used
};

// Detect contacts for `joints` over `clip`.
//
// Ground frame: a joint's position with the root's *horizontal* displacement from the clip's first
// sample removed. For an in-place clip that is the identity, and every locomotion clip in this
// repository is in place (ADR-540); for a travelling clip it is the difference between "the foot is
// planted" and "the foot is moving with the body", which are the same thing in model space and
// opposite things on the ground.
//
// Joints the skeleton does not carry come back as a track with `jointIndex == -1` and no spans,
// rather than being dropped: a caller has to be able to tell "this rig has no left hand" from
// "this rig's left hand never touched anything".
[[nodiscard]] std::vector<ContactTrack> detectContacts(const Skeleton& skeleton, const AnimationClip& clip,
                                                       std::span<const ContactJoint> joints,
                                                       const ContactSettings& settings = {});

// ---- phase ---------------------------------------------------------------------------------------

enum class PhaseLandmarkKind : std::uint8_t { Plant, Release };
[[nodiscard]] const char* phaseLandmarkKindName(PhaseLandmarkKind kind);

struct PhaseLandmark {
    float time = 0.0f;       // clip seconds
    float phase = 0.0f;      // the phase value this landmark sits at
    int contactTrack = -1;   // index into the tracks it came from
    PhaseLandmarkKind kind = PhaseLandmarkKind::Plant;
};

struct PhaseTrack {
    // Phase per sample on the same grid the contacts were found on, each in [0, 1).
    std::vector<float> phase;
    float sampleRate = 30.0f;
    std::vector<PhaseLandmark> landmarks;
    // True when the reference joint planted at least twice, so there is a repeat to anchor on.
    bool cyclic = false;
    // Seconds per cycle, averaged over the plants that were found. Zero when acyclic.
    float cycleSeconds = 0.0f;
    // How much the individual cycle lengths differ from that average, as a fraction of it. Near
    // zero for a clean loop; large for a take that speeds up, or for thresholds that found a
    // spurious plant. Reported because "this clip has a phase" and "this clip's phase is worth
    // matching on" are different claims.
    float cycleVariance = 0.0f;

    // The phase at a clip second, interpolated on the grid. Clamped outside the clip.
    [[nodiscard]] float at(float clipSeconds) const;
    // The inverse: the first clip second at which this track reaches `phase`. This is what a
    // phase-matched transition asks for -- "where in the run cycle is the walk's left foot about
    // to land" -- and it is a search rather than a formula because the phase grid is data.
    //
    // Returns 0 for an empty track. For a cyclic track the answer is always within one cycle of
    // the start, which is the useful one: a caller rebasing a clip's clock wants the *earliest*
    // time with that phase, not the last.
    [[nodiscard]] float timeAt(float phase) const;
    [[nodiscard]] bool empty() const { return phase.empty(); }
};

// Build a phase track from contacts.
//
// `referenceTrack` is the index of the track whose plants anchor phase 0 -- the left foot, by
// convention, but the caller chooses because the convention is not universal and a quadruped has
// four candidates. Out of range, or a track with fewer than two plants, gives an **acyclic** track
// whose phase is simply the normalised time: still a phase, still usable by a transition, and
// honest about having no cycle to offer.
[[nodiscard]] PhaseTrack extractPhase(std::span<const ContactTrack> tracks, int referenceTrack,
                                      float clipLength, const ContactSettings& settings = {});

// ---- the two together ----------------------------------------------------------------------------

// What an offline pass records about one clip. Grouped because the phase is derived from the
// contacts and shipping one without the other invites them to disagree.
struct ClipAnalysis {
    std::string clip;
    float length = 0.0f;
    std::vector<ContactTrack> contacts;
    PhaseTrack phase;
    // Ground-frame displacement of the root over the clip, and the straight-line speed it implies.
    // This is the number ADR-540 measured as ~0 for every locomotion clip in this repository, and
    // it is recorded per clip so that a motion pack can say which of its clips actually travel.
    glm::vec3 rootTravel{0.0f};
    float groundSpeed = 0.0f;
};

[[nodiscard]] ClipAnalysis analyseClip(const Skeleton& skeleton, const AnimationClip& clip,
                                       std::span<const ContactJoint> joints, int referenceJoint = 0,
                                       const ContactSettings& settings = {});

} // namespace avgen::scene
