#pragma once

// What a clip IS, read off the clip (ADR-821).
//
// A director that says "Rook jumps, and the Umbra pulse fires at the peak" needs to know, for each
// clip a character carries, whether it loops, how long it is, whether the body leaves the ground in
// it and when, where it is highest, when each foot plants and lifts, whether it travels and how
// fast, and when it may be cut away from. None of that is authored anywhere: glTF has no loop flag
// and no markers, and the alien pack's 26 clips carry neither (feasibility report §5.1).
//
// So it is measured, from the analysis this engine already has and nothing new:
//   * loop or one-shot -- `measureLoopClosure` (does the end join the start);
//   * contacts, plants and releases -- `analyseClip`'s contact tracks on the character's feet;
//   * root motion and horizontal speed -- `analyseClip`'s travel reading (ADR-546);
//   * off the ground -- the feet against ONE ground datum for the whole rig (the rest pose's lowest
//     foot), not against each clip's own lowest point. The contact detector's per-clip datum is right
//     for "which foot is planted" and wrong for "is anything touching the ground": a fall loop whose
//     feet never move reads as two 100% plants to it.
//
// An offline computation -- it samples every clip -- cached once per asset and shared by every
// node that instances the rig (`ClipSemanticsCache`), so a scene that never asks pays nothing.

#include "scene/skeleton.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace avgen::scene {

struct AnimationClip;

// A named instant in a clip, in seconds from the clip's own start (0 .. length). Map to the
// timeline as `cue time + seconds / cue speed`.
//   takeoff, peak, touchdown  -- the clip's principal flight: the last supported sample before it,
//                                the highest point of the body in it, the first supported sample
//                                after it. Absent when the body never leaves the ground.
//   plant.<joint>, release.<joint> -- each contact span's start and end, per foot.
struct ClipEvent {
    std::string name;
    float seconds = 0.0f;
};

enum class ClipGround : std::uint8_t {
    Grounded, // the body is supported throughout (a flight shorter than `minFlightSeconds` is a stride)
    Leaves,   // supported, then off the ground, then (usually) supported again: a jump
    Airborne, // off the ground for most of the clip: a fall, a float
};
[[nodiscard]] const char* clipGroundName(ClipGround ground);

struct ClipSemantics {
    std::string clip;        // the state name the player knows it by (the short name)
    float length = 0.0f;     // playable seconds
    bool loops = false;      // the end joins the start (`measureLoopClosure`)
    float loopGapSteps = 0.0f; // how far the end is from the start, in the clip's own median steps
    // Root motion: whether the body went anywhere, how far, and how fast horizontally. Every clip in
    // the alien pack is authored in place, so `travels` is false and the speed is the body's sway.
    bool travels = false;
    glm::vec3 rootTravel{0.0f};
    float groundSpeed = 0.0f;
    // The ground.
    ClipGround ground = ClipGround::Grounded;
    float supportedFraction = 1.0f; // share of the clip with a foot on the ground datum
    float flightStart = 0.0f;       // the principal flight, when `ground != Grounded`
    float flightEnd = 0.0f;
    float peakHeight = 0.0f;        // the body's rise at `peak` over its rest height, model units
    bool startsSupported = true;
    bool endsSupported = true;
    std::vector<ClipEvent> events;  // sorted by time
    // When cutting away looks wrong: a body cut out of the air lands nowhere. The principal flight,
    // for a one-shot; nothing for a loop, which is built to be left at any frame.
    std::vector<std::pair<float, float>> committed;

    [[nodiscard]] const ClipEvent* event(std::string_view name) const;
    // Interruptible at `seconds` (clip time): outside every committed window.
    [[nodiscard]] bool interruptibleAt(float seconds) const;
};

struct ClipSemanticsSettings {
    float sampleRate = 30.0f;
    // A foot within this fraction of the rest height above the rig's ground datum is on the ground.
    float supportFraction = 0.04f;
    // Shorter unsupported runs are a stride's float, not a flight.
    float minFlightSeconds = 0.12f;
    // Below this share of supported samples the clip is `Airborne` rather than `Leaves`.
    float airborneBelow = 0.35f;
};

struct ClipSemanticsTable {
    std::vector<ClipSemantics> clips;      // parallel to the rig's clips
    std::vector<std::string> feet;         // the joints read as feet, and why they were chosen
    float groundDatum = 0.0f;              // model-space height of the rest pose's lowest foot
    float restHeight = 0.0f;
    std::vector<std::string> warnings;     // e.g. no feet found: nothing about the ground is known
    [[nodiscard]] const ClipSemantics* find(std::string_view clip) const;
};

// The feet: joints whose names read as a left or right foot (`roleForJointName`), one per side, the
// lowest in the rest pose where several match (a rig with foot controllers carries more than one).
[[nodiscard]] std::vector<int> footJoints(const Skeleton& skeleton);

[[nodiscard]] ClipSemanticsTable clipSemantics(const Skeleton& skeleton, const std::vector<AnimationClip>& clips,
                                               const ClipSemanticsSettings& settings = {});

// The table as a document: what `avgen_motion semantics --json` prints and what a capability card
// is built from. Times in clip seconds; heights and speeds in model units, as measured.
[[nodiscard]] nlohmann::json clipSemanticsJson(const ClipSemanticsTable& table);

// Computed once, on first request, and shared by every copy of a rig (a rig is copied per node).
// Thread-safe; the answer never changes, because the clips it is computed from never do.
class ClipSemanticsCache {
public:
    [[nodiscard]] const ClipSemanticsTable& get(const Skeleton& skeleton, const std::vector<AnimationClip>& clips);

private:
    std::once_flag once_;
    ClipSemanticsTable table_;
};

} // namespace avgen::scene
