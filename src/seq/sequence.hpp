#pragma once

// The cinematic sequence (ADR-089): the whole timed audiovisual performance, and the one thing in
// this engine that says *when*.
//
// ## What this is not
//
// It is not a second animation system. The engine already has one coherent property-animation
// architecture -- `params::Timeline` with `Track`s of `Key`s against a `ParameterSet` -- and every
// scene already exposes its camera, its nodes and its environment through that set:
// `camera/position`, `nodes/<name>/rotation`, `scene/fogDensity`, `env/sky/sunIntensity`. There was
// never a missing animation system. What was missing was *choreography*: the idea that a piece is a
// list of shots, that a shot is about a subject over a span of time, and that a character walking
// down a street is a path plus a gait plus a place in a song.
//
// So a Sequence **bakes**. `bake()` turns shots, actors, scene slots, transitions and overlay cues
// into ordinary timeline tracks, and from then on the timeline the engine already has does the
// work. This is the same decision ADR-075 made for the camera director, for the same three reasons:
//
//  - **Determinism** (spec 33). Frame 1000 is identical whether playback walked there or an offline
//    renderer jumped to it, because evaluation is `Track::evaluate(t)` and nothing else.
//  - **Scrubbing** (spec 30). There is no accumulated state to rewind, so 10s -> 45s -> 3s -> 30s
//    is four pure evaluations.
//  - **Cost** (spec 35). Nothing in this file runs per frame. The per-frame cost of a sequence is
//    the cost of the tracks it produced, which is the cost the timeline already had.
//
// The price is that a bake is a moment: editing a shot means re-baking. That is stated rather than
// hidden, and it is why `bake()` is cheap enough to run on every edit (see the numbers in
// docs/sequencer.md).
//
// ## Naming
//
// `app::Sequence` (ADR-062) already exists and is a *camera* shot list -- the output of the
// director. `seq::Sequence` is the whole piece, and it holds `app::Shot` by value as one of the
// ways a shot's camera can be described. Where both are in scope, always qualify. `seq::Director`
// (seq/director.hpp) is the thing that installs a sequence into a running engine; `app::Sequence`
// is a value, `seq::Director` is a verb.
//
// ## The seam to the 2D composition system
//
// Text, lyrics and graphic overlays are `OverlayCue`s here: timed, ordered, styled *intentions*.
// Turning one into a layer is `seq::LayerSink`'s job (seq/layers.hpp); `seq::CompositionLayerSink`
// is the real one, over `comp::LayerStack` (ADR-083), and `NullLayerSink` still exists so the
// timing half can be tested without a font engine. Nothing in this file knows what a font is.

#include "analysis/structure.hpp"
#include "app/cinematic.hpp"
#include "core/error.hpp"
#include "params/timeline.hpp"
#include "seq/events.hpp"
#include "seq/layers.hpp"
#include "signals/musical_events.hpp"
#include "spatial/spline.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::seq {

// ---- markers (spec 19, 20) --------------------------------------------------------------------

// Markers are authoring context, not executable events. A `Section` is a label somebody (or the
// structure fold) put on a passage; a `Beat` is a beat time copied from the analysis so the editor
// can draw it and snap to it; a `Cue` is a point an author wants to find again.
enum class MarkerKind : std::uint8_t { Section, Cue, Beat };
[[nodiscard]] const char* markerKindName(MarkerKind kind);
[[nodiscard]] std::optional<MarkerKind> markerKindFromName(std::string_view name);

struct Marker {
    double timeSeconds = 0.0;
    std::string name;
    MarkerKind kind = MarkerKind::Cue;
};

// ---- scene slots (spec 6, 36) -----------------------------------------------------------------

// A scene the sequence can cut to.
//
// Switching is visibility on a composition node, not a file load. A `kind: "scene"` node already
// loads a child scene file and already propagates its own visibility through the whole subtree
// (scene/composition.cpp), and `nodes/<name>/visible` is already a parameter -- so a cut is a Step
// key on a bool, which costs nothing, needs no streaming, and is exactly as scrub-safe as every
// other track. The scenes are all resident, which is the v1 trade spec 36 permits.
//
// `file` is not read by the bake. It is recorded so a future preloader knows what a slot *is*
// without having to walk the composition, which is the hook spec 36 asks for rather than the
// streaming system it says not to build.
struct SceneSlot {
    std::string id;    // how shots name it
    std::string node;  // composition node to show; empty means the node is named `id`
    std::string file;  // informational: the scene file that node loads
    [[nodiscard]] const std::string& nodeName() const { return node.empty() ? id : node; }
};

// ---- camera (spec 8-11) -----------------------------------------------------------------------

enum class CameraKind : std::uint8_t {
    Inherit, // emit nothing; the previous shot's last key holds (the timeline holds after the end)
    Move,    // a subject-relative move in the ADR-062 vocabulary
    Keys,    // hand-authored position/target keys
};
[[nodiscard]] const char* cameraKindName(CameraKind kind);
[[nodiscard]] std::optional<CameraKind> cameraKindFromName(std::string_view name);

struct CameraKey {
    double timeSeconds = 0.0; // relative to the shot start
    glm::vec3 position{0.0f};
    glm::vec3 target{0.0f};
    float focalLength = 0.0f; // 0 = do not key the lens at this key
    params::KeyInterp interp = params::KeyInterp::Smooth;
};

// What the camera does during a shot.
//
// `move` is an `app::Shot` because that type is already the right one: it knows fourteen kinds of
// move, five ways to aim, four path shapes, easing at both ends, and how to derive all of it from a
// subject's position and radius so a shot is reusable against a subject of any size (ADR-062,
// ADR-071). Nothing here re-derives any of that.
//
// `lookAtActor` is the one thing `app::Shot` cannot express, and it is spec 10: a subject that
// *moves*. `app::Shot::targetAt` takes a static focal target, so a shot that tracks a walking
// character would have to have its aim hand-authored. Instead the bake samples the actor's
// position at each camera sample and blends it into the aim -- which is a look-at target, done as a
// bake, and therefore as deterministic as everything else here.
struct ShotCamera {
    CameraKind kind = CameraKind::Inherit;
    app::Shot move;                 // kind == Move
    std::vector<CameraKey> keys;    // kind == Keys
    // spec 10: aim at an actor rather than at a fixed point.
    std::string lookAtActor;
    float lookAtHeight = 1.6f;      // aim this far above the actor's origin (eye height, metres)
    float lookAtWeight = 1.0f;      // 0 = ignore the actor, 1 = aim entirely at it
    int samples = 24;               // keys per shot for a curved move; 2 is enough for a straight one
};

// ---- camera presets (spec 11) -----------------------------------------------------------------

// Useful initial configurations, not a system. Each one fills a `ShotCamera` with a move that reads
// as the named shot against a subject of the given radius; the author then edits it like any other.
enum class CameraPreset : std::uint8_t { Isometric, Follow, Wide, Close, TopDown, Tracking, Reveal };
[[nodiscard]] const char* cameraPresetName(CameraPreset preset);
[[nodiscard]] std::optional<CameraPreset> cameraPresetFromName(std::string_view name);
[[nodiscard]] ShotCamera cameraFromPreset(CameraPreset preset, const app::FocalTarget& subject);

// ---- transitions (spec 7) ---------------------------------------------------------------------

// Hard cuts, a dip to or from black, and a match cut. A crossfade between two 3D scenes would need
// both drawn into separate targets and blended, which is a renderer change for one transition; a
// dip is the transition a cutter actually reaches for and it is two keys on `scene/brightness`,
// which the engine already maps onto the tonemap exposure. Named honestly rather than called a
// crossfade, and still not implemented -- see ADR-092 for the second evaluation and what it cost.
//
// `MatchCut` is the one that turned out to be cheap. A match cut is a hard cut whose two frames
// *rhyme*: the outgoing subject and the incoming subject sit in the same place in frame at the same
// apparent size, so the eye reads continuity across a change of everything else. In an engine where
// a shot is a subject, a radius, a distance in radii and a framing offset, that is arithmetic --
// `app::Shot::subjectCoverageAt` already computes the apparent size, so matching it is inverting
// one expression. It costs no renderer change, no second target and no frame time; it is a
// different opening distance for the incoming shot, decided at bake.
enum class TransitionKind : std::uint8_t { Cut, FadeIn, FadeOut, MatchCut };
[[nodiscard]] const char* transitionKindName(TransitionKind kind);
[[nodiscard]] std::optional<TransitionKind> transitionKindFromName(std::string_view name);

struct Transition {
    TransitionKind kind = TransitionKind::Cut;
    double seconds = 0.5;
};

// ---- shots (spec 5) ---------------------------------------------------------------------------

// A span of the piece with one visual setup. A shot is not a scene: several shots may name the same
// slot and differ only in camera, lighting or which of the scene's parameters they animate, which
// is what makes reusing an expensive environment cheap.
struct Shot {
    std::string name;
    double startSeconds = 0.0;
    double durationSeconds = 8.0;
    std::string scene;   // slot id; empty inherits the previous shot's slot
    ShotCamera camera;
    Transition in{TransitionKind::Cut, 0.0};
    Transition out{TransitionKind::Cut, 0.0};
    // spec 17: arbitrary scene parameters animated within this shot. Key times are relative to the
    // shot start, so a shot can be moved without rewriting its automation.
    std::vector<params::Track> tracks;

    [[nodiscard]] double endSeconds() const { return startSeconds + durationSeconds; }
};

// ---- actors (spec 12-16) ----------------------------------------------------------------------

// An animation state entered at a time (spec 13). A cue names a state on the skinned rig the
// actor's node carries (ADR-086) -- "Idle", "Walk", "Run" -- and never a transform. That separation
// is the point: the same walk plays wherever the actor is.
//
// `timeSeconds` is the clip's phase origin as well as its start. `AnimationPlayer` is a pure
// function of (state, the second it was entered, speed, now), so driving it from the cue's own
// absolute time -- rather than from the moment a scrub happened to notice the cue -- is what makes
// a character's feet land in the same place whether the frame was played to or jumped to.
struct ClipCue {
    double timeSeconds = 0.0;
    std::string clip;          // animation state name; empty = leave the rig alone from here
    float speed = 1.0f;        // clip seconds per timeline second
    float blendSeconds = -1.0f; // cross-fade in; < 0 = the rig's own transition time
};

struct ActorKey {
    double timeSeconds = 0.0;
    glm::vec3 position{0.0f};
    std::optional<glm::vec3> rotationDegrees; // unset: derived from the direction of travel
    std::optional<glm::vec3> scale;
    params::KeyInterp interp = params::KeyInterp::Smooth;
};

// spec 15: a path the actor walks, rather than a hundred hand-placed keys. The engine already has
// `spatial::Spline` -- pure, deterministic, arc-length parameterised, with a tangent at every
// sample -- so "walk this street between 0:12 and 0:28, facing the way you are going" is a spline,
// two times and a flag.
struct ActorPath {
    bool active = false;
    spatial::Spline spline;
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    int samples = 48;          // keys emitted along the path
    bool faceTangent = true;   // heading follows the tangent
    // Arc-length fraction at each end, so a path can be shared and walked in pieces.
    float startU = 0.0f;
    float endU = 1.0f;
};

// Anything the sequence moves: a character, a vehicle, a prop, a light, a camera rig. Nothing here
// is character-specific (spec 16) -- a "character" is an actor that happens to name a rig.
struct Actor {
    std::string id;
    std::string node;   // composition node it drives; empty means the node is named `id`
    std::vector<ActorKey> keys;
    ActorPath path;
    std::vector<ClipCue> clips;
    bool visible = true;

    [[nodiscard]] const std::string& nodeName() const { return node.empty() ? id : node; }
    // Pure evaluation, used by the bake and by a camera that looks at this actor. The path wins
    // inside its time range; outside it, the keys; with neither, the origin.
    [[nodiscard]] glm::vec3 positionAt(double seconds) const;
    // Heading in radians about +Y, derived from the path tangent or from the direction between the
    // surrounding keys. Zero when the actor is not moving.
    [[nodiscard]] float headingAt(double seconds) const;
    [[nodiscard]] const ClipCue* clipAt(double seconds) const;
    // Latest time this actor has anything to say.
    [[nodiscard]] double endSeconds() const;
};

// What one actor's rig is doing at an instant: the state, and the second its phase started from.
struct AnimationCue {
    std::string node;           // composition node carrying the rig
    std::string clip;           // animation state name
    double startSeconds = 0.0;  // the cue's own time, not the time it was noticed
    float speed = 1.0f;
    float blendSeconds = -1.0f;
};

// ---- the sequence -----------------------------------------------------------------------------

struct BakeOptions {
    // Keys per shot for a camera move whose `samples` is zero.
    int cameraSamplesPerShot = 24;
    // The value `scene/brightness` takes at full black.
    float fadeFloor = 0.0f;
    // Emit `camera/mode = 1` (free) at t = 0. A composition ignores `camera/position` unless the
    // mode is free (scene/composition.cpp), so without this the whole camera track does nothing and
    // says nothing -- exactly the failure ADR-075 is about. Off only for tests that check it.
    bool emitCameraMode = true;
    // Section 34: let a shot's `Spotlight` raise its subject's level-of-detail floor for the length
    // of the shot. Emitted as Multiply tracks so the bake does not have to know -- or restore --
    // the values the author chose; see `bake()`.
    bool spotlightQuality = true;
    // How many beats are in a bar, for `TriggerKind::Bar`. The analysis publishes a beat list and a
    // bar counter but a sequence only carries the beats, so the fold back into bars is stated here
    // rather than assumed to be four everywhere.
    int beatsPerBar = 4;
};

struct OverlayBinding {
    std::string cueId;
    std::string layerId;   // empty = the sink declined (deferred until the layer system lands)
};

struct BakeResult {
    // `{"enabled": true, "tracks": [...], "cues": [...]}` -- the exact shape
    // `params::Timeline::fromJson` reads, so a baked track and a hand-authored one are the same
    // kind of object and cannot drift apart in what they support.
    nlohmann::json timeline;
    std::vector<std::string> targets;   // every parameter path written, sorted, deduplicated
    std::vector<std::string> warnings;  // things an author should be told, not failures
    std::vector<OverlayBinding> overlays;
    // What the events resolved to, split by tier (seq/events.hpp). The baked half is already in
    // `timeline`; the dispatched and live halves are handed to a `seq::EventDispatcher`, and the
    // clip half to `animationAt`.
    EventSchedule events;
    int trackCount = 0;
    int keyCount = 0;
};

// The complete timed performance.
struct Sequence {
    std::string name = "sequence";
    // 0 = derive from the shots, the actors and the overlays.
    double durationSeconds = 0.0;
    std::vector<SceneSlot> scenes;
    std::vector<Shot> shots;
    std::vector<Actor> actors;
    std::vector<OverlayCue> overlays;
    std::vector<Marker> markers;
    // The editable song structure (ADR-215). Saved with the piece, because it is authored data the
    // moment anybody touches it: detected sections are the analyzer's first pass, and a boundary a
    // person dragged or a section they named is work that a re-analysis must not destroy. The
    // markers above are still *derived* from this -- `setSectionMarkers` -- so the strip and the
    // event triggers see one answer.
    analysis::SongStructure structure;
    // spec 17 of the cinematic world brief: "when X happens, do Y". Most of these stop being
    // events at bake and become keys; the rest are dispatched. seq/events.hpp is the argument.
    std::vector<SequenceEvent> events;
    // spec 16/17: animation that belongs to the piece rather than to one shot. Times are absolute.
    std::vector<params::Track> tracks;

    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] double duration() const;
    [[nodiscard]] const Shot* shotAt(double seconds) const;
    [[nodiscard]] const Shot* shotNamed(std::string_view name) const;
    [[nodiscard]] const Actor* actorNamed(std::string_view id) const;
    [[nodiscard]] const SceneSlot* slotNamed(std::string_view id) const;
    // The slot in force at a time, following the inheritance rule ("" = keep the previous shot's).
    [[nodiscard]] const SceneSlot* sceneAt(double seconds) const;

    // Replaces the marker list's Section entries with the fold's sections (spec 19). Beat and Cue
    // markers are kept: a section label is derived and an author's cue is not.
    void setSectionMarkers(const signals::MusicalStructure& folded);
    // The same, from the editable structure (ADR-215). `TriggerKind::Section` matches on a marker's
    // *name*, so the name written here is the section's display name -- the label a person gave it
    // where there is one, and the function's name otherwise. That is the string an event names, and
    // it is why renaming a section is an edit with consequences rather than a decoration.
    void setSectionMarkers(const analysis::SongStructure& structure);
    // Re-derives the Section markers from `this->structure`. Called after any structural edit.
    void refreshSectionMarkers();
    // Adds `Beat` markers for every beat time (spec 20). Existing beat markers are replaced.
    void setBeatMarkers(std::span<const double> beatTimes);
    [[nodiscard]] std::vector<double> markerTimes(MarkerKind kind) const;

    // What every actor's rig should be playing at `seconds`, and since when (spec 13, 33). The one
    // part of a sequence that is not a baked track, because a clip's phase origin is a *time* and
    // a track carries values. Pure, cheap, and evaluated per frame by seq::Director.
    [[nodiscard]] std::vector<AnimationCue> animationAt(double seconds) const;
    // ...including the clips events scheduled. A scheduled clip and an authored one are the same
    // object, so the later of the two simply wins: this is still one pure function of the second.
    [[nodiscard]] std::vector<AnimationCue> animationAt(double seconds,
                                                        std::span<const ScheduledClip> scheduled) const;

    // Everything the event resolver can know before the piece runs: the shot edges, the markers the
    // analysis put here, the cues an author placed and the clip spans the actors state. Pure, and
    // public because the editor wants to show what an event would resolve to without baking.
    [[nodiscard]] TriggerContext triggerContext(int beatsPerBar = 4) const;

    // Turns the whole thing into timeline tracks. Pure: the same sequence bakes to the same JSON.
    [[nodiscard]] Result<BakeResult> bake(LayerSink& sink, const BakeOptions& options = {}) const;

    [[nodiscard]] nlohmann::json toJson() const;
    [[nodiscard]] static Result<Sequence> fromJson(const nlohmann::json& j);
};

// ---- snapping (spec 21) -----------------------------------------------------------------------

enum class SnapMode : std::uint8_t { Off, Frames, Beats, Markers };
[[nodiscard]] const char* snapModeName(SnapMode mode);
[[nodiscard]] std::optional<SnapMode> snapModeFromName(std::string_view name);

// Nearest snap point to `seconds`, or `seconds` when nothing is within `toleranceSeconds`
// (0 = always snap to the nearest). `grid` is the frame rate for Frames and is ignored otherwise.
// `points` must be ascending. Pure, so the editor and a test agree about where a cut lands.
[[nodiscard]] double snapTime(double seconds, SnapMode mode, std::span<const double> points,
                              double fps = 60.0, double toleranceSeconds = 0.0);

} // namespace avgen::seq
