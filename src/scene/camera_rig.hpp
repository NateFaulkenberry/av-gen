#pragma once

// Multiple cameras, and the one that is on screen (ADR-245).
//
// Until this file there was exactly one camera in AV Gen: the `camera/*` parameter block that
// `Composition::applyParameters` reads into `Scene::camera` every frame. Everything that wanted a
// different viewpoint had to *become* that camera -- the Auto-director by baking keys onto it, the
// viewport by dragging it, a spline by riding it -- and so "which camera" and "where the camera is"
// were the same question. A piece that wants a wide establishing shot and a hero follow is a piece
// that wants them to be two different objects.
//
// Three concepts, deliberately separate, because conflating any two of them is what the single
// camera did:
//
//   * **Camera** (`CameraRig`) -- a persistent viewpoint. It has a name, a stable id, a placement,
//     an optical identity, and an opinion about when it should be used. It does not know when it is
//     on screen and it does not know what else exists.
//   * **Shot** (`CameraShot`) -- a span of time that names a camera. It says *when*, and how the
//     picture arrives (a cut or a blend). It does not know where the camera is.
//   * **Director** (`resolveActiveCamera`) -- a pure function from (cameras, shots, events, time)
//     to which camera is live. It does not move anything.
//
// **Everything here is a pure function of the clock.** That is not a stylistic preference: ADR-091
// makes an offline render reproduce a live one exactly, and a director that accumulates state
// between frames cannot be scrubbed. It is also why an authored camera may be Free or Spline and
// may *not* orbit -- the legacy orbit mode integrates `orbitSpeed * dt` into an angle, which is the
// one camera placement in this engine that depends on how the playhead got here rather than on
// where it is. The legacy camera keeps it, for the scenes that were authored against it; no new
// camera gets it.
//
// **The main camera is a camera.** `kMainCamera` names the legacy `camera/*` block. It appears in
// the camera list like any other, a shot may name it, and the Auto-director still bakes onto it
// exactly as it did before ADR-245 -- so a project that has never heard of this file resolves to
// the main camera at every instant and renders the identical image. What is new is that it is now
// *one of* the cameras rather than *the* camera.

#include "core/error.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::scene {

// A camera's identity, stable across renames, reorders, saves and loads. Minted by
// `CameraDirection::addCamera` and never reused within a composition, because a shot holds one and
// an id that comes back attached to a different camera is a shot that silently changes what it
// shows.
using CameraId = std::uint32_t;

inline constexpr CameraId kNoCamera = 0;
// The legacy `camera/*` block: the Auto-director's camera, the viewport's camera, and the camera
// every project written before ADR-245 has. Always exists; cannot be deleted.
inline constexpr CameraId kMainCamera = 1;

// How an authored camera decides where it is. Two, not three: see the header note on orbit.
enum class CameraPlacement : std::uint8_t {
    Free,   // explicit eye and aim (`cameras/<slug>/position`, `.../target`) -- keyframe these
    Spline, // rides a named scene spline at `.../splineT`, looking `.../lookAhead` further along
};
[[nodiscard]] const char* cameraPlacementName(CameraPlacement placement);
[[nodiscard]] std::optional<CameraPlacement> cameraPlacementFromName(std::string_view name);

// How the picture arrives when a shot becomes live.
enum class ShotTransition : std::uint8_t {
    Cut,   // instant
    Blend, // the eye, the aim and the lens cross-fade over `blendSeconds`
};
[[nodiscard]] const char* shotTransitionName(ShotTransition transition);
[[nodiscard]] std::optional<ShotTransition> shotTransitionFromName(std::string_view name);

// ---- a camera --------------------------------------------------------------------------------

// A persistent viewpoint.
//
// The transform fields here are the camera's *authored base* -- what its parameters are registered
// with. Once registered, the live values come from the parameters, which means a camera keyframes,
// modulates and audio-reacts through the systems that already exist. There is no camera animation
// system in this file and there is not going to be one: a camera is animated when the timeline has
// keys on its channels, and "static" is simply the absence of them (`CameraDirection::isAnimated`).
//
// For `kMainCamera` the transform fields are unused: its authored base is the composition's own
// `camera` block, which predates this file and still owns it. Its *metadata* -- name, eligibility,
// priority -- is read here like any other camera's.
struct CameraRig {
    CameraId id = kNoCamera;
    std::string name;   // what the user calls it
    // The path segment its parameters live under: `cameras/<slug>/position` and so on. Derived from
    // the name when the camera is created and **frozen from then on**, because a parameter path is
    // what a saved timeline track names and renaming a camera must not orphan its animation.
    std::string slug;

    CameraPlacement placement = CameraPlacement::Free;
    glm::vec3 position{0.0f, 12.0f, 40.0f};
    glm::vec3 target{0.0f, 2.0f, 0.0f};
    float fovDegrees = 50.0f;
    // The camera's optical identity, in millimetres. 0 means "no opinion, use `fovDegrees`".
    //
    // Here rather than on the director, because a lens is a property of a camera: a 24 mm
    // establishing camera and a 50 mm hero camera are two cameras, not one camera with a setting.
    // When it is non-zero the frame's physical lens takes this focal length and the field of view
    // follows from the sensor, so depth of field and the circle of confusion stay consistent with
    // the picture (ADR-037).
    float focalLength = 0.0f;

    // ---- watching something that moves -----------------------------------------------------------
    //
    // The two constraints a camera that covers an *event* cannot do without, and the reason the
    // Glowmere demo needs no Glowmere-specific code. Both name a composition node, both are empty by
    // default, and both are resolved against wherever that node is this frame -- so a camera can
    // ride a saucer across a valley and keep it in frame without anyone keyframing the saucer's path
    // into the camera.
    //
    // `aimNode` replaces the aim: the camera looks at that node instead of at its `target` channel.
    // `followNode` replaces the eye: the camera stands at that node plus `followOffset`, in world
    // axes, so the offset reads as a fixed camera position relative to the subject rather than as a
    // rotation that swings with it. A node that is not in the scene leaves the channel in charge,
    // which is what makes a camera authored against a world it has lost degrade to a static shot
    // rather than to the origin.
    std::string aimNode;
    // Where in the subject the camera looks, in world axes. Aiming at a saucer's centre puts the
    // saucer in the middle of frame and the thing it is doing off the bottom of it; -12 metres of Y
    // puts the saucer high and the beam and the ground under it in the picture. This is the
    // difference between a shot of the UFO and a shot of the abduction.
    glm::vec3 aimOffset{0.0f};
    std::string followNode;
    glm::vec3 followOffset{0.0f};

    // Spline placement only.
    std::string spline;
    float splineT = 0.0f;
    float lookAhead = 2.0f;
    glm::vec3 splineOffset{0.0f};

    // ---- what the director is allowed to do with it --------------------------------------------

    // Whether the Auto-director may choose this camera at all. Off by default: a camera somebody
    // composed for one moment appearing in the middle of an automatic cut is the failure mode this
    // flag exists to prevent (multicam spec section 27/30).
    bool autoDirectorEligible = false;
    // Tie-break when two claims land on the same instant; higher wins, then the lower id, so the
    // answer never depends on vector order.
    int priority = 0;

    // ---- event response (multicam-demo sections 5, 13) -----------------------------------------
    //
    // The name of a staging scenario (ADR-210). While that scenario is running, this camera claims
    // the frame -- which is how "when something important happens with the UFO, show the audience"
    // is expressed without a line of UFO-specific code anywhere in the engine. Empty (the default)
    // means the camera never responds to events.
    std::string eventScenario;
    // How long before the scenario starts the camera takes over, and how long after it ends it
    // keeps the frame. The lead is the difference between seeing an event and seeing its aftermath.
    double eventLeadSeconds = 0.25;
    double eventTailSeconds = 0.0;
    // How the event claim arrives. 0 -- the default -- is a hard cut, which is usually right for an
    // event: a dissolve onto something that has already started reads as a mistake.
    double eventBlendSeconds = 0.0;
    // Which phases of the scenario deserve the camera. Empty -- the default -- means all of them,
    // from the moment the scenario binds its subject.
    //
    // A scenario is not one moment: Glowmere's abduction spends seven seconds flying to its subject
    // before there is anything to see. Naming the beats is how an author says "cut to this when the
    // beam comes on, not when the saucer sets off", without the camera system learning what a beam
    // is. The names are the scenario's own beat names (ADR-210).
    std::vector<std::string> eventBeats;

    // The parameter prefix this camera's channels live under. `camera/` for the main camera (which
    // is why a legacy project is untouched), `cameras/<slug>/` for everything else.
    [[nodiscard]] std::string channelPrefix() const;

    friend bool operator==(const CameraRig&, const CameraRig&) = default;
};

// ---- a shot ----------------------------------------------------------------------------------

// A span of time that names a camera. The whole of "authored camera direction".
//
// A shot is not a camera and holds nothing a camera holds: no transform, no lens, no metadata. That
// is the point of it being a separate object -- the same camera can be shot three times in a piece,
// and the three shots differ in when they are and how they arrive, not in what they see.
struct CameraShot {
    CameraId camera = kNoCamera;
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    ShotTransition transition = ShotTransition::Cut;
    double blendSeconds = 0.0; // read only for Blend
    // "This moment is mine." A locked shot is not taken by an event camera.
    //
    // Without it the author has no way to protect an authored moment, and a world whose events run
    // continuously -- Glowmere's saucer abducts something every nineteen seconds -- can never show
    // an establishing shot at all. Every editor provides this; Unreal spells it `Lock Previous
    // Camera` on a cut section, an NLE spells it a locked track. One bool, and the priority rule
    // stays one sentence: an event takes the frame unless the shot the playhead is in is locked.
    bool locked = false;
    std::string label;         // optional, for the sequencer lane

    [[nodiscard]] bool contains(double seconds) const {
        return seconds >= startSeconds && seconds < endSeconds;
    }

    friend bool operator==(const CameraShot&, const CameraShot&) = default;
};

// ---- events the director can see ---------------------------------------------------------------

// "This named thing is happening between these two times."
//
// Deliberately a flat value rather than a pointer into the staging system: the director must not
// know what a scenario is, what an abduction is, or that either exists. Whatever can answer "what
// is running and when" fills these in; `Composition` does it from its staging scenarios.
struct CameraEventSpan {
    std::string name;
    double startSeconds = 0.0;
    double endSeconds = 0.0;
};

// ---- the active camera: the one published surface -----------------------------------------------
//
// **This is the contract.** Exactly one thing in the engine says which camera is on screen and it
// is this struct, published once per frame by `Composition` and mirrored by `Engine::activeCamera`.
// The pose itself is, as it has always been, `Scene::camera`: this says *which* camera that pose
// belongs to and *why*, which is what a preview, an overlay or a sequencer lane needs and what
// `Scene::camera` alone cannot answer.
//
// Nothing consumes it by mutating it. It is a reading of the frame.

enum class ActiveCameraReason : std::uint8_t {
    Default, // nothing claimed the frame, so the default camera has it
    Shot,    // an authored shot on the camera track
    Event,   // a camera whose scenario is running took it
};
[[nodiscard]] const char* activeCameraReasonName(ActiveCameraReason reason);

struct ActiveCameraState {
    CameraId camera = kMainCamera;   // what is on screen
    CameraId previous = kMainCamera; // what it is coming from, while `blend < 1`; else == camera
    float blend = 1.0f;              // 0 = entirely `previous`, 1 = entirely `camera`
    ActiveCameraReason reason = ActiveCameraReason::Default;
    std::string name;                // the active camera's display name, for an overlay or a label
    // When the current claim began and when it ends. `untilSeconds <= sinceSeconds` means it has no
    // end -- the default camera, which holds the frame until something takes it.
    double sinceSeconds = 0.0;
    double untilSeconds = 0.0;
    // The scenario that claimed the frame, when `reason == Event`. Empty otherwise.
    std::string eventName;
    // The focal length the active camera states, in millimetres, blended when blending. 0 means no
    // camera in force states one and the physical lens keeps whatever it had.
    float focalLength = 0.0f;
    // How far the active camera is focused, in metres. An authored camera focuses on what it is
    // aimed at -- which is what a camera operator does, and what stops a camera that was placed by
    // an author from inheriting a focus distance the Auto-director baked for a different shot
    // entirely. 0 means the main camera has the frame and `camera/lens/focusDistance` still rules.
    float focusDistance = 0.0f;

    [[nodiscard]] bool blending() const { return blend < 1.0f && previous != camera; }
    friend bool operator==(const ActiveCameraState&, const ActiveCameraState&) = default;
};

// ---- the collection ----------------------------------------------------------------------------

// Every camera a composition has, the shots that place them in time, and who gets the frame when
// nothing claims it.
//
// Owned by `Composition` and serialised in the scene document under `"cameraDirection"`. There
// rather than in the project, because a shot names a camera by id and a camera is part of the world
// it was composed for: splitting the two across two documents makes a project that loads a
// different scene a project full of dangling shots.
struct CameraDirection {
    // Always holds the main camera at index 0 (see `ensureMainCamera`), then the authored ones in
    // the order they were created. Order is presentation only; every reference is by id.
    std::vector<CameraRig> cameras;
    std::vector<CameraShot> shots;
    // Who has the frame when no shot and no event claims it. The main camera unless somebody says
    // otherwise, which is what makes an untouched project render exactly as it always has.
    CameraId defaultCamera = kMainCamera;
    // Next id to mint. Monotonic and serialised, so an id freed by a deletion never comes back.
    CameraId nextId = kMainCamera + 1;

    // Puts the main camera in the list if it is not there. Idempotent; called after every load and
    // by `addCamera`, so no caller has to remember that the list is never legitimately empty.
    void ensureMainCamera();

    // Adds a camera with a unique id and a unique slug derived from `name`. Returns its id.
    CameraId addCamera(CameraRig rig);
    // Removes a camera and every shot that named it. The main camera is refused: the Auto-director,
    // the viewport and every legacy project point at it. Returns false when nothing was removed.
    bool removeCamera(CameraId id);

    [[nodiscard]] CameraRig* find(CameraId id);
    [[nodiscard]] const CameraRig* find(CameraId id) const;
    [[nodiscard]] const CameraRig* findByName(std::string_view name) const;
    [[nodiscard]] std::string nameOf(CameraId id) const;

    // Whether anything here changes the picture. False means "one camera, exactly as before", and
    // is what `Composition` checks to take the untouched path.
    [[nodiscard]] bool directing() const { return cameras.size() > 1 || !shots.empty(); }

    // Refuses a collection that cannot be evaluated: a duplicate id or slug, a shot naming a camera
    // that does not exist, a shot that ends before it starts, a negative blend. Checked at load, so
    // a bad document fails loudly instead of resolving to something nobody authored.
    [[nodiscard]] Result<void> validate() const;

    [[nodiscard]] nlohmann::json toJson() const;
    [[nodiscard]] static Result<CameraDirection> fromJson(const nlohmann::json& doc);

    friend bool operator==(const CameraDirection&, const CameraDirection&) = default;
};

// A parameter-path segment from a display name: lowercased, non-alphanumerics folded to nothing,
// leading digits prefixed. Never empty (falls back to "camera"). Uniqueness is `addCamera`'s job.
[[nodiscard]] std::string cameraSlug(std::string_view name);

// ---- the director ------------------------------------------------------------------------------

// Which camera is live at `seconds`, and why.
//
// Pure: same inputs, same answer, at any playhead position, in any order, on any thread. That is
// what makes a camera cut scrubbable and an offline render identical to the live one (ADR-091), and
// it is the reason this takes `events` as a flat span rather than querying anything.
//
// Priority, highest first:
//   0. **A locked shot.** An authored shot marked `locked` is not taken by an event.
//   1. **Event.** A camera whose scenario is running (widened by its lead and tail). Highest
//      `priority` wins, then the lowest id.
//   2. **Shot.** The last shot in the list whose span contains `seconds` and whose camera exists.
//      Last rather than first, so an overlapping shot added later wins -- the sequencer convention,
//      and the only rule under which dragging a shot over another does what it looks like.
//   3. **Default.** `direction.defaultCamera`, unbounded.
//
// The outgoing camera is found by resolving again at the instant before this claim began, which is
// what makes a blend computable without remembering anything. A blend that was itself in progress
// when a new claim landed is simply superseded -- a three-way cross-fade is not a thing a cut does.
[[nodiscard]] ActiveCameraState resolveActiveCamera(const CameraDirection& direction,
                                                    std::span<const CameraEventSpan> events,
                                                    double seconds);

// ---- pose evaluation ---------------------------------------------------------------------------

// Where a camera is and what it sees. The blendable part of a camera: everything a cross-fade has
// to interpolate and nothing it does not.
struct CameraPose {
    glm::vec3 position{0.0f, 2.0f, 10.0f};
    glm::vec3 target{0.0f, 1.0f, 0.0f};
    float fovDegrees = 50.0f;
    float focalLength = 0.0f; // mm; 0 = no opinion
};

// Linear in position and aim, linear in the field of view, and linear in focal length only when
// both ends state one -- a camera with no opinion about its lens must not drag a camera that has
// one towards zero millimetres.
[[nodiscard]] CameraPose blendPoses(const CameraPose& from, const CameraPose& to, float t);

// Guards the degenerate aim: an eye and a target at the same point have no view matrix. Pushes the
// target one metre along `fallbackForward`.
void ensureDistinctAim(CameraPose& pose, glm::vec3 fallbackForward = glm::vec3(0.0f, 0.0f, -1.0f));

} // namespace avgen::scene
