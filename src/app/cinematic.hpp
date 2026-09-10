#pragma once

// The cinematic director (ADR-062, milestone 3 of the cinematic upgrade).
//
// A sequence of shots, each with a reason to move. The engine already has a camera, a timeline that
// can drive `camera/position` and `camera/target`, and splines that can be camera rails; what it
// has never had is the vocabulary above them -- the idea that a camera move is a *kind* of move
// with a subject, and that a film is a list of them.
//
// The output of this file is keyframes on the existing timeline tracks. It does not drive the
// camera itself and it does not run per frame: a sequence is evaluated once into keys, and from
// then on the timeline the engine already has does the work. That keeps deterministic playback,
// scrubbing, offline rendering and everything else that already works on timelines working here
// too, and it means a director'd sequence can be inspected as ordinary keys.
//
// The shot kinds are the ones a cinematographer would name. `Orbit` is in the list because
// sometimes a changing silhouette is the point -- but it is one of eight, not the default.

#include "core/error.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::app {

// Why the camera is moving. Each kind determines how a shot's start and end are derived from its
// subject, so a shot is described by intent rather than by two positions somebody had to guess.
enum class ShotKind : std::uint8_t {
    Establish,  // hold wide; the world, not the subject
    Approach,   // move towards the subject; its scale becomes apparent
    Reveal,     // start on something ambiguous, pull back until it reads
    Entry,      // travel into the interior of something
    Passage,    // move through, elements passing the camera on both sides
    Descent,    // travel downward through layers
    Ascent,     // rise towards a luminous opening
    Orbit,      // move around, when the changing silhouette is the point
    Track,      // follow the subject, holding it at a constant place in frame
};
[[nodiscard]] const char* shotKindName(ShotKind k);
[[nodiscard]] std::optional<ShotKind> shotKindFromName(std::string_view name);

// What the shot is about. A shot with no subject is a camera move; a shot with a subject is a shot.
struct FocalTarget {
    glm::vec3 position{0.0f};
    float radius = 8.0f;         // how big the subject is, which sets how far "close" is
    std::string name;            // for the UI and for error messages
};

// Where the subject sits in the frame and how the lens treats it. Thirds are the default because
// dead centre is the composition nobody chose; a shot may still ask for it.
struct CompositionProfile {
    glm::vec2 framing{-0.22f, 0.10f};  // subject's normalised offset from centre
    float headroom = 0.12f;            // extra space above the subject, as a fraction of frame
    float focalLength = 35.0f;         // millimetres
    float aperture = 2.8f;
    bool focusOnSubject = true;        // depth of field follows the focal target
};

struct Shot {
    std::string name;
    ShotKind kind = ShotKind::Establish;
    double startSeconds = 0.0;
    double durationSeconds = 6.0;
    FocalTarget subject;
    CompositionProfile composition;

    // How far the camera is from the subject at the start and end, in multiples of the subject's
    // radius. Distance rather than absolute position is what makes a shot reusable against a
    // subject of any size, which is the whole reason the subject carries a radius.
    float startDistance = 6.0f;
    float endDistance = 6.0f;
    // Where the camera sits around the subject, in radians and in height above it.
    float startAzimuth = 0.0f;
    float endAzimuth = 0.0f;
    float startElevation = 0.15f;
    float endElevation = 0.15f;
    // Eases the whole move. Motion that starts and stops abruptly reads as a machine.
    bool easeIn = true;
    bool easeOut = true;

    [[nodiscard]] double endSeconds() const { return startSeconds + durationSeconds; }
    // The camera's position and aim at a normalised time through the shot, 0..1.
    [[nodiscard]] glm::vec3 cameraAt(float t) const;
    [[nodiscard]] glm::vec3 targetAt(float t) const;
};

// A film. Shots are held in start order and may not overlap: two cameras at once is not a thing a
// single-camera engine can honour, and silently picking one is worse than refusing.
struct Sequence {
    std::string name;
    std::vector<Shot> shots;

    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] double durationSeconds() const;
    // The shot covering a time, or nullptr in a gap.
    [[nodiscard]] const Shot* shotAt(double seconds) const;

    // Bakes the sequence into timeline keyframes for `camera/position`, `camera/target` and, when
    // any shot asks for it, the lens. `samplesPerShot` sets how finely a curved move is sampled;
    // straight moves need two keys and curves need enough that the eye cannot see the segments.
    [[nodiscard]] nlohmann::json toTimelineTracks(int samplesPerShot = 8) const;

    [[nodiscard]] nlohmann::json toJson() const;
    [[nodiscard]] static Result<Sequence> fromJson(const nlohmann::json& j);
};

} // namespace avgen::app
