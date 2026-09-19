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
#include "seq/section_actions.hpp"
#include "seq/layers.hpp"
#include "signals/musical_events.hpp"
#include "song/section_timeline.hpp"
#include "song/shot_language.hpp"
#include "spatial/spline.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
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
    Inherit,  // emit nothing; the previous shot's last key holds (the timeline holds after the end)
    Move,     // a subject-relative move in the ADR-062 vocabulary
    Keys,     // hand-authored position/target keys
    Behavior, // a relationship to a performer, resolved per camera sample: chase, orbit, POV
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

// ---- camera behaviours: a relationship to a performer rather than a path ----------------------
//
// `CameraKind::Move` composes a camera around a *point*: a subject, a radius, a distance in radii.
// That is the right model for a shot about a place, and the wrong one for a shot about somebody who
// walks -- the point is fixed at the cut and the world moves past it.
//
// A behaviour is the other model. It states a *relationship* -- four metres behind, two above; an
// arc of ninety degrees; at the eyes -- and the relationship is resolved against wherever the
// performer is at each camera sample.
//
// ## Why this can be baked, which is the whole design
//
// The mandate asks whether "live" must mean "nondeterministic". Here it does not, and the reason is
// specific rather than general: a `seq::Actor`'s position is `positionAt(t)`, a **pure function of
// time** over its keys or its spline path. So the bake can ask where the performer *will be* at each
// sample and emit ordinary `camera/position` keys -- and every determinism requirement in the
// mandate's section 14 is then satisfied by construction, exactly as it already is for `lookAtActor`.
// Scrubbing to a frame and playing to it read the same keys. There is no state to reset at a cut,
// because there is no state.
//
// The boundary is worth stating plainly, because it is the thing that decides what a behaviour can
// target: **a performer, not an arbitrary object.** A node moved by modulation, by an entity
// behaviour's walk cycle or by a staging scenario is not a function of time the bake can evaluate,
// and a behaviour aimed at one would have to be evaluated per frame -- which is the authored camera
// rig's job (`scene::CameraRig::followNode`), not the sequencer's. See
// docs/investigations/camera-preset-architecture.md.

// Where a behaviour camera points. Separate from where it *is*, because the two are independent
// decisions: a chase can look at the performer, or ahead down their line of travel, or at a fixed
// place they are walking towards.
enum class CameraAim : std::uint8_t {
    Subject, // at the performer, plus `aimOffset`
    Travel,  // along the performer's direction of travel -- what POV wants, and a chase often does
    Custom,  // at a fixed world point, whatever the performer does
};
[[nodiscard]] const char* cameraAimName(CameraAim aim);
[[nodiscard]] std::optional<CameraAim> cameraAimFromName(std::string_view name);

enum class CameraBehaviorKind : std::uint8_t {
    Chase, // hold a spatial relationship as the performer travels
    Orbit, // circle the performer, who may be standing still
    Pov,   // occupy the performer's viewpoint
};
[[nodiscard]] const char* cameraBehaviorName(CameraBehaviorKind kind);
[[nodiscard]] std::optional<CameraBehaviorKind> cameraBehaviorFromName(std::string_view name);

struct CameraBehavior {
    CameraBehaviorKind kind = CameraBehaviorKind::Chase;
    // Which performer this is about. Empty means the behaviour cannot resolve, and the bake says so
    // rather than silently placing the camera at the origin.
    std::string actor;

    // ---- chase ---------------------------------------------------------------------------------
    //
    // **Performer-local axes, and the convention is load-bearing**: x is lateral (+ is the
    // performer's right), y is vertical (+ up), z is forward/back (+ is ahead of them, so a chase
    // sits at negative z). That matches the engine's own convention -- `headingDegrees` builds to
    // +Z forward, rotation about +Y -- so "four metres behind and two above" is (0, 2, -4) and stays
    // behind when they turn.
    glm::vec3 offset{0.0f, 2.0f, -4.0f};
    // False interprets `offset` in world axes instead, which is a camera that holds a compass
    // bearing while the performer turns under it. Occasionally what you want; rarely.
    bool actorSpace = true;
    // Stand where the performer *was*, not where they are. A time lag rather than a spring: a spring
    // integrates, so its position at t depends on the path taken to reach t, and a scrub and a
    // play-through would disagree. Sampling `positionAt(t - lag)` is a fact about the performer and
    // two runs that agree about them agree about the camera. What it cannot do is overshoot and
    // settle, which is what integration buys.
    double lagSeconds = 0.0;

    // ---- orbit ---------------------------------------------------------------------------------
    //
    // Angles in degrees about +Y, from the performer's position, evaluated from **shot-local time**
    // rather than accumulated -- `angle(t) = mix(start, end, t)` -- so the same frame gives the same
    // pose however you arrived at it. A full circle is a 360 degree span; direction is the sign.
    float radius = 8.0f;
    float height = 2.0f;
    float startDegrees = 0.0f;
    float endDegrees = 90.0f;
    // Ease the *angle*, not the timing, so an orbit starts and ends without a jerk.
    bool easeInOut = true;

    // ---- pov -----------------------------------------------------------------------------------
    //
    // Performer-local, same axes as `offset`. 1.7 m is roughly eye height on a human-scaled rig.
    glm::vec3 eyeOffset{0.0f, 1.7f, 0.0f};

    // ---- aim, for all three --------------------------------------------------------------------
    CameraAim aim = CameraAim::Subject;
    glm::vec3 aimOffset{0.0f, 1.0f, 0.0f}; // where in the performer to look; chest rather than feet
    glm::vec3 aimPoint{0.0f};              // `aim == Custom`
    // Sample the performer this far *ahead* for the aim, so the camera leads them into a turn.
    double lookAheadSeconds = 0.0;

    // ---- clearance -------------------------------------------------------------------------------
    //
    // Metres the eye is kept above the ground, 0 to leave it alone. Applied at bake, which is the
    // only place the whole camera path is known at once -- so a chase that would have gone through a
    // hill is lifted over it before a single frame is rendered, rather than corrected while running.
    //
    // The ground only. That scope is deliberate and is the honest limit: the ground is what a chase
    // camera actually hits, it is exactly queryable, and it cannot jitter. Trunks and rocks are not
    // covered -- a camera squeezing between scattered instances pops, and a popping camera is worse
    // than one that clips a tree.
    float clearance = 0.0f;

    friend bool operator==(const CameraBehavior&, const CameraBehavior&) = default;
};

// Where a performer is and which way they face, at one instant. The behaviour evaluator takes these
// rather than an `Actor` and a time, so it is a pure function of its arguments and a test can hand it
// whatever pose it wants to ask about.
struct ActorPose {
    glm::vec3 position{0.0f};
    float headingDegrees = 0.0f; // about +Y; 0 faces +Z, matching `Actor::headingAt`
    [[nodiscard]] glm::vec3 forward() const;
    [[nodiscard]] glm::vec3 right() const;
};

struct BehaviorPose {
    glm::vec3 eye{0.0f};
    glm::vec3 target{0.0f};
};

// **The evaluator.** Pure: everything time-varying has already been resolved into the two poses.
//
// `t01` is the normalised position within the shot, which only Orbit reads. `eyeRef` is the
// performer at the time the *eye* is composed against -- t for orbit and POV, t - lag for a chase.
// `aimRef` is the performer at the time the *aim* is composed against, which is t + lookAhead.
// Splitting them is what lets a chase trail the performer while still looking where they are going.
[[nodiscard]] BehaviorPose cameraPoseFor(const CameraBehavior& behavior, float t01,
                                         const ActorPose& eyeRef, const ActorPose& aimRef);

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
    CameraBehavior behavior;        // kind == Behavior
    // spec 10: aim at an actor rather than at a fixed point.
    std::string lookAtActor;
    float lookAtHeight = 1.6f;      // aim this far above the actor's origin (eye height, metres)
    float lookAtWeight = 1.0f;      // 0 = ignore the actor, 1 = aim entirely at it
    int samples = 24;               // keys per shot for a curved move; 2 is enough for a straight one
};

// ---- camera presets (spec 11) -----------------------------------------------------------------

// Useful initial configurations, not a system. Each one fills a `ShotCamera` with a move that reads
// as the named shot against a subject of the given radius; the author then edits it like any other.
// The three at the end are behaviours rather than moves: they stamp a `CameraBehavior` and set
// `kind = CameraKind::Behavior`, where the first seven stamp an `app::Shot`. The picker does not
// distinguish them, because to an author they are all "how should this shot's camera work".
enum class CameraPreset : std::uint8_t {
    Isometric, Follow, Wide, Close, TopDown, Tracking, Reveal, Chase, Orbit, Pov
};
[[nodiscard]] std::span<const CameraPreset> allCameraPresets();
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
// A span of the piece with one visual setup.
//
// **Every shot here is a person's.** A shot briefly carried an `Origin` so that re-running Song Mode
// could replace the shots it had made; Song Mode no longer makes any, so nothing writes `Directed`
// and the field is gone rather than left as one that is always `Authored`. The director's own
// shots live on the camera track, where `scene::CameraShot::Origin` still draws that distinction
// because there it is still real (ADR-245).
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
    // How high the ground is at a world point, for a camera behaviour's `clearance`. Null -- the
    // default -- means no clearance is applied and the bake *says so* rather than leaving a setting
    // that silently does nothing (ADR-225).
    //
    // A callback rather than a `world::TerrainQuery`, so `seq/` does not learn what terrain is: the
    // sequencer's job is to ask how high the ground is, and whose ground it is belongs to whoever
    // installs the sequence.
    std::function<float(float x, float z)> groundHeightAt;
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
// ---- editing shots (the Shots lane's gestures, as functions) ------------------------------------
//
// The section lane's edits have lived in the model since ADR-247 (`song::moveBoundary`,
// `song::splitSection`); the shot lane's lived inline in `SequencePanel` and could not be tested
// without ImGui. These are those gestures, moved out unchanged, so a test drives the same arithmetic
// the pointer does instead of a copy of it that can drift.
//
// `minSeconds` is the shortest a shot may become. The panel passes its own `kMinBlockSeconds`; the
// default matches it.
inline constexpr double kMinShotSeconds = 0.25;

// Slides a shot, keeping its length. Never before zero.
void moveShot(Shot& shot, double newStart);

// ---- the same three, aware of the neighbours -----------------------------------------------------
//
// `Sequence::validate` refuses overlapping shots outright -- one camera cannot be in two places --
// so a drag that produces one is not a state to resolve later, it is an edit that will be rejected
// when it goes in. These clamp at the gesture instead, which is the difference between a control
// that will not let you do the wrong thing and one that lets you do it and then complains.
//
// They also SNAP: released within `snapSeconds` of a neighbour's edge, a shot lands flush against it.
// Butted edges are how a cut is made, and hitting one to the millisecond by hand is not a skill
// worth requiring.
//
// `index` is into `shots`, which `validate` requires to be in time order; the neighbours are
// therefore `index - 1` and `index + 1`.
inline constexpr double kShotSnapSeconds = 0.15;

void moveShot(std::vector<Shot>& shots, std::size_t index, double newStart,
              double snapSeconds = kShotSnapSeconds);
void trimShotEnd(std::vector<Shot>& shots, std::size_t index, double newEnd,
                 double minSeconds = kMinShotSeconds, double snapSeconds = kShotSnapSeconds);
void trimShotStart(std::vector<Shot>& shots, std::size_t index, double newStart, double fixedEnd,
                   double minSeconds = kMinShotSeconds, double snapSeconds = kShotSnapSeconds);

// Moves the END, keeping the start. Refuses to go shorter than `minSeconds`.
void trimShotEnd(Shot& shot, double newEnd, double minSeconds = kMinShotSeconds);

// Moves the START, keeping the END where it is.
//
// The end is passed in rather than read from the shot, and that is the whole subtlety: during a drag
// the duration is changing under the gesture, so recomputing the end from it would move both edges
// at once -- trimming a start would become a move. The caller remembers the end when the drag begins.
void trimShotStart(Shot& shot, double newStart, double fixedEnd, double minSeconds = kMinShotSeconds);

// Splits the shot at `index` at `seconds`. The earlier half keeps everything; the later half is a
// copy named "<name> b", starting at the cut, and both sides get a hard cut at the new seam.
//
// Refuses where either half would be shorter than `minSeconds` -- a split that makes a shot nobody
// can see is not a split. Returns the index of the new later half.
[[nodiscard]] std::optional<std::size_t> splitShot(std::vector<Shot>& shots, std::size_t index,
                                                   double seconds,
                                                   double minSeconds = kMinShotSeconds);

// Copies the shot at `index`, named "<name> copy", and places it in the list directly after the
// original and in TIME directly after it too. Both matter: a copy on the same span would never play,
// because `shotAt` takes the first match. Returns the new index.
[[nodiscard]] std::optional<std::size_t> duplicateShot(std::vector<Shot>& shots, std::size_t index);

// Removes the shot at `index`. Unlike a section, a timeline of no shots is legal: the piece simply
// has no authored camera, which is what an untouched project already looks like.
bool removeShot(std::vector<Shot>& shots, std::size_t index);

// ---- the same cut, on the lanes that are not shots (ADR-356) -------------------------------------
//
// The slice tool cuts every lane that holds a span, and two of those lanes had no split operation at
// all: a lyric was a block you could only move and trim, and an actor's clip cue was a block you
// could only add or delete. They are here, beside `splitShot`, for the reason `splitShot` is here --
// so the gesture and a test drive the same arithmetic -- and they take the same shape as it:
// `minSeconds`, a refusal rather than a degenerate result, and the new index on success.

// Splits the overlay at `index` at `seconds`. The later half is a copy with the same text, an id
// suffixed "-b", and the same style: a slice is the start of two lines, not a guess at what they
// say, and a lyric cut in half that arrived with half the words would have thrown away the text a
// person was about to retype in one of the two halves anyway.
[[nodiscard]] std::optional<std::size_t> splitOverlay(std::vector<OverlayCue>& overlays,
                                                      std::size_t index, double seconds,
                                                      double minSeconds = kMinShotSeconds);

// Splits the clip-cue span starting at `index` on one actor.
//
// A cue is an instant, and the block the lane draws for it runs to the NEXT cue -- or to the end of
// the piece for the last one, which is why `endOfPiece` is passed rather than derived: the panel
// already knows the piece's duration and `Actor` alone does not. Splitting inserts a second cue for
// the same clip at the cut, which is what turns one span into two that can then be given different
// clips, speeds or blends. Cues are kept in time order.
[[nodiscard]] std::optional<std::size_t> splitActorClip(Actor& actor, std::size_t index,
                                                        double seconds, double endOfPiece,
                                                        double minSeconds = kMinShotSeconds);

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
    // The authored section timeline and the vocabulary it is written in (ADR-247).
    //
    // `structure` above is the **analyzer's report**: what the audio was found to contain, with
    // confidences, recomputed whenever somebody presses Analyze. `sectionTimeline` is the **film**:
    // what the person says each passage is and how it should be treated. The brief that asked for
    // this is explicit that they are not one object, and the reason is visible the moment somebody
    // types "Ocean Ambience" -- that is not a claim about the music, and a detector must never be
    // in a position to overwrite it.
    //
    // `shotLanguage` carries only the person's own section types and shot intents. The built-in
    // vocabulary is code and is never written, so a piece that defined nothing serializes nothing.
    song::SectionTimeline sectionTimeline;
    song::ShotLanguage shotLanguage;
    // ADR-216's table: what the world should DO when a section of a given kind begins -- as opposed
    // to what the camera should do, which is the shot language above. Two different questions about
    // the same boundary, and keeping them apart is why neither had to learn the other's vocabulary.
    //
    // Empty by default and empty in most projects. `generatePerformanceEvents` declines every kind the
    // set does not name, so an unauthored project generates nothing -- which is the honest report
    // that nobody has said what a Drop should make happen, rather than a guess dressed as one.
    SectionPerformanceSet sectionPerformance;
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
