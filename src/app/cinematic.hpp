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
#include "signals/musical_events.hpp"

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
    // Milestone 9 added five more, because "zoom in, zoom out, rotate" is not a vocabulary and the
    // five below are the moves the first nine could only approximate.
    Discovery,  // travel towards something partly hidden, arcing so it emerges rather than appears
    HeroReveal, // move around a hero *while* opening out: the silhouette turns and the scale lands
    Flyby,      // pass close at speed, holding the subject in frame as it goes by
    Drift,      // lateral travel through the world with the aim held: parallax, not a pan
    Transition, // leave one subject and find another
};
[[nodiscard]] const char* shotKindName(ShotKind k);
// Accepts the canonical names and the words a cinematographer would use for the same move --
// "follow" for Track, "establishing" for Establish. The alias table is one-way: `shotKindName`
// always returns the canonical name, so a round trip through JSON is stable.
[[nodiscard]] std::optional<ShotKind> shotKindFromName(std::string_view name);

// Where the camera points, which is a separate decision from where it is. Before this existed the
// rule was hard-coded ("everything aims at its subject except Passage"), which is why there was no
// way to express a lateral drift: the aim has to stay put while the camera does not.
enum class LookMode : std::uint8_t {
    Subject,  // hold the subject
    Ahead,    // look where the move is going
    Fixed,    // hold an explicit point, whatever the camera does
    Parallel, // hold the *direction* set at the start; only parallax moves
    Handoff,  // start on the subject, end on the handoff subject
};
[[nodiscard]] const char* lookModeName(LookMode m);
[[nodiscard]] std::optional<LookMode> lookModeFromName(std::string_view name);

// The shape of the path between the two ends, as distinct from the shape of the *timing*, which is
// what easeIn/easeOut control. A straight line between two points has no parallax and so reads as
// flat however well it is eased; a bow of a tenth of the chord is the difference between a move
// through a world and a move across a photograph.
enum class MovementCurve : std::uint8_t {
    Straight,
    Arc,  // bows horizontally, perpendicular to the chord
    Rise, // bows upward through the middle
    Dip,  // bows downward through the middle
};
[[nodiscard]] const char* movementCurveName(MovementCurve c);
[[nodiscard]] std::optional<MovementCurve> movementCurveFromName(std::string_view name);

// "This object is the point of this shot." The camera side of hero spotlighting and nothing more:
// it says who and how strongly, and leaves framing, exposure and rim light to whatever reads it.
// Kept on the shot rather than on the subject because the same object is a hero in one shot and
// scenery in the next, which is the whole idea.
struct Spotlight {
    bool active = false;
    float emphasis = 0.0f; // 0..1: how much of the frame's attention the subject should own
};

// What the shot is about. A shot with no subject is a camera move; a shot with a subject is a shot.
struct FocalTarget {
    glm::vec3 position{0.0f};
    float radius = 8.0f;         // how big the subject is, which sets how far "close" is
    std::string name;            // for the UI and for error messages
    // How far this subject wants to be seen from, in metres. 0 means "no opinion, use the radii".
    //
    // Shot distances are in radii so a shot is reusable against a subject of any size, and that is
    // right — but it is not the whole story. An establishing shot at 14 radii on a 16-metre subject
    // stands 115 metres away, which in this engine is past the distance foreground vegetation is
    // drawn to: the camera ends up staring across culled ground at a small object, moving 7 metres
    // over 31 seconds, and reads as broken rather than as wide. A hero that states a stand-off is
    // stating the range in which it actually reads, and the director honours it as a *bound* on the
    // radii rather than replacing them, so the difference between a wide shot and a close one
    // survives.
    float preferredDistance = 0.0f;
    // The elevation this subject reads best from, in **degrees** above the horizontal. A 16 m
    // mushroom wants to be looked up at; a pool wants to be looked down into.
    //
    // Note the unit, because it is a trap that was waiting inside this field the whole time it was
    // dead: `Shot::startElevation` is **not an angle**, it is a height as a multiple of the orbit
    // radius (`orbitPoint` computes `elevation * |r|`). Wiring the hero's degrees straight into the
    // shot's ratio put the camera thirty-four radii in the air and the sequence failed its own
    // "the hero is a speck" check, which is the check doing exactly its job.
    //
    // Honoured the way `preferredDistance` is -- as a *bias* on the kind's own elevations rather than
    // as a replacement -- so a reveal that rises through its shot still rises, it just does so around
    // the angle the subject asked for. 0 means the subject has no opinion, which is the same
    // convention `preferredDistance` uses.
    float preferredElevationDegrees = 0.0f;
    // The bearing this subject is best approached from, in radians, derived from the ground it stands
    // on (`world::preferredApproachAzimuth`). The golden-angle spread that stops a film being nine
    // views down one axis is applied *around* this rather than around zero -- a global zero suits
    // whichever hero happens to have open ground to its north and puts every other one's establishing
    // shot into a hillside.
    float preferredAzimuth = 0.0f;
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

    // ---- authored overrides --------------------------------------------------------------------
    // Everything above derives the path from the subject, which is what makes a shot reusable. But
    // the reference move this project is measured against (the audit, 1.4) is a valley traverse
    // authored as two absolute points -- it is a shot about a *place*, and a place has no radius to
    // count in. Any of these, when set, replace the derived value; unset, nothing changes.
    std::optional<glm::vec3> startPosition;
    std::optional<glm::vec3> endPosition;
    // Where the aim begins, for a shot continuing another (ADR-185). The mirror of `startPosition`,
    // and needed for the same reason: shots are contiguous, so the last key of one and the first key
    // of the next sit at the *same second*. A continuous take pins the position so those coincident
    // keys agree; nothing pinned the aim, so the look target stepped from one subject to the other
    // in a single frame -- measured at 278 m on the shipped project, while the eye moved 0.58 m.
    //
    // Set, the aim eases from here to whatever the look mode wants across the first part of the
    // shot, which is what a camera operator does: the body keeps moving and the head *pans*. Unset,
    // nothing changes, so a cut is still a cut.
    std::optional<glm::vec3> startTarget;
    std::optional<glm::vec2> heightRange; // absolute world Y at each end, as a pair or not at all

    // Unset means "whatever this kind does", which is what keeps `kind` a kind rather than a label:
    // setting `kind` alone still changes the move. A shot that names one overrides its kind.
    std::optional<LookMode> look;
    std::optional<MovementCurve> curve;
    std::optional<float> curveBow;   // bow at the midpoint, as a fraction of the chord
    glm::vec3 lookAt{0.0f};          // LookMode::Fixed
    std::optional<FocalTarget> handoff; // LookMode::Handoff and ShotKind::Transition

    // Metres per second. Zero means the duration is authored and the speed falls out of it; above
    // zero it is the other way round and `Sequence::retime()` derives the duration from the path.
    // A flyby is defined by its speed, not by how long somebody wanted it on screen.
    float speed = 0.0f;

    Spotlight spotlight;

    [[nodiscard]] double endSeconds() const { return startSeconds + durationSeconds; }
    // The camera's position and aim at a normalised time through the shot, 0..1.
    [[nodiscard]] glm::vec3 cameraAt(float t) const;
    [[nodiscard]] glm::vec3 targetAt(float t) const;
    // The aim the look mode alone asks for, before `startTarget`'s pan is applied.
    [[nodiscard]] glm::vec3 targetWithoutStart(float t) const;
    [[nodiscard]] LookMode lookMode() const;
    [[nodiscard]] MovementCurve movementCurve() const;
    [[nodiscard]] float bowAmount() const;

    // What the lens should be focused on. ADR-062 recorded `focusOnSubject` and never wired it;
    // this is the wire, and it is a distance because that is what a lens takes.
    [[nodiscard]] float focusDistanceAt(float t) const;
    // The fraction of the frame's height the subject spans, from its radius, the distance and the
    // focal length. A "hero" shot in which the hero is forty pixels tall is not a hero shot, and
    // this is the number that says so before anyone renders it.
    [[nodiscard]] float subjectCoverageAt(float t) const;

    // Path length in metres and the fastest the camera ever moves along it, both sampled. `samples`
    // is a polyline resolution: a bowed or swept path is not its chord.
    [[nodiscard]] float pathLength(int samples = 24) const;
    [[nodiscard]] float peakSpeed(int samples = 24) const;
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

    // Cuts per minute. The one number that says whether a sequence is cut or chopped; a director
    // that produces twenty of these over ninety seconds has failed however good each shot is.
    [[nodiscard]] double cutsPerMinute() const;
    // Separate from `validate()` because cadence is a judgement and geometry is not: a hand-authored
    // sequence may legitimately want a two-second shot, and a generated one may not.
    [[nodiscard]] Result<void> validateCadence(double minShotSeconds,
                                               double maxCutsPerMinute) const;

    // Rewrites the duration of every shot that named a speed, then repacks the whole cut list
    // back-to-back from the first shot's start. Repacking is all-or-nothing on purpose: a sequence
    // where some shots are timed and some are paced cannot have both be authoritative.
    Result<void> retime();

    // Who the film is about at this moment, or nullptr if nothing is spotlit.
    [[nodiscard]] const FocalTarget* spotlightAt(double seconds) const;
    // The spotlight as spans, for the systems that will frame, expose and rim-light the subject.
    // Spans rather than keys because a lighting change wants to know when it starts and ends, not
    // what it is halfway through.
    [[nodiscard]] nlohmann::json spotlightSpans() const;

    // Bakes the sequence into timeline keyframes for `camera/position`, `camera/target` and, when
    // any shot asks for it, the lens. `samplesPerShot` sets how finely a curved move is sampled;
    // straight moves need two keys and curves need enough that the eye cannot see the segments.
    [[nodiscard]] nlohmann::json toTimelineTracks(int samplesPerShot = 8) const;

    [[nodiscard]] nlohmann::json toJson() const;
    [[nodiscard]] static Result<Sequence> fromJson(const nlohmann::json& j);
};

// ---- direction ---------------------------------------------------------------------------------
//
// Milestone 10: a sequence built from the music rather than from a list of times somebody typed.
//
// The rules are almost entirely about what *not* to do. The camera does not move on every beat, it
// does not cut on every phrase, and it never shakes -- there is no shake in this file and there is
// not going to be one. What it does is read the structure (ADR-063) and let the shape of the piece
// choose the shape of the move: a build starts the camera going somewhere, the drop it feeds lands
// on the reveal, and a breakdown gets a slow close shot that a loud section could not hold.

// What the world offers the director. It is a list of things worth pointing at plus one thing the
// How the Auto-director cuts.
//
// **Continuous shot** -- one uninterrupted take. The camera travels through the world and around its
// subjects without editorial cuts: each move starts where the last one ended *and at the speed it
// ended with*, so the sequence reads as one operator's move whose intent changes rather than as a cut
// list played without fades.
//
// **Edited sequence** -- a cut list. Each shot is composed independently and the camera cuts between
// them, which is what a piece with distinct sections and distinct subjects wants.
//
// The distinction is not cosmetic and it is not new: the flag existed and defaulted to continuous
// from the start, but it was invisible, and what it did was pin positions without doing anything
// about velocity. A camera that arrives at a section boundary, stops dead, and accelerates away
// again has cut -- it has just done it without a frame of black.
enum class DirectorMode : std::uint8_t { ContinuousShot, EditedSequence };
[[nodiscard]] const char* directorModeName(DirectorMode mode);
[[nodiscard]] std::optional<DirectorMode> directorModeFromName(std::string_view name);

// film is about; everything else is a preference with a defensible default.
struct DirectionBrief {
    FocalTarget hero;                     // the one object the film is for
    std::vector<FocalTarget> supporting;  // everything else worth a shot, most interesting first
    double minShotSeconds = 5.0;          // below this a sequence is chopped rather than cut
    double minBuildShotSeconds = 2.0;     // ...except a build, which exists to end
    // Above this a section is more than one shot. A fold can hand back a thirty-second verse, and
    // holding one move on one subject for a third of a piece is not restraint -- it is the film
    // being about whatever the longest section happened to land on. Splitting it keeps the cut on
    // the music (the pieces are inside one section) while letting the cast change. Drops are never
    // split: the whole point of reading the structure is that the reveal lands on the beat.
    double maxShotSeconds = 12.0;
    float wideFocalLength = 24.0f;
    float heroFocalLength = 50.0f;
    // How the film is cut. The reference camera "never orbits, never zooms, and holds its final
    // pose" (audit 1.4), and section 8 lists that restraint among the things not to change, so a
    // continuous take is the default.
    //
    // `ContinuousShot` is one uninterrupted move whose *intent* changes at the section boundaries:
    // each shot starts where the last one ended, and -- since this became a user-facing mode -- the
    // camera no longer decelerates to a stop at each of those boundaries either. See `DirectorMode`.
    DirectorMode mode = DirectorMode::ContinuousShot;
    [[nodiscard]] bool continuous() const { return mode == DirectorMode::ContinuousShot; }
    std::uint32_t seed = 1; // picks which supporting subject a section gets; nothing else is random
};

// Bakes a structure into a sequence. Fails rather than guesses when the structure is empty or the
// hero has no size, for the same reason `validate()` does.
[[nodiscard]] Result<Sequence> directFromStructure(const signals::MusicalStructure& structure,
                                                   const DirectionBrief& brief);

// Which move a section of music wants. Exposed because it is the single most arguable table in the
// file and a caller disagreeing with it should be able to see it rather than reverse-engineer it.
[[nodiscard]] ShotKind shotKindForSection(signals::MusicalSection section);

} // namespace avgen::app
