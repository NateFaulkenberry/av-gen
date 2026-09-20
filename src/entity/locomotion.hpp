#pragma once

// The seam between a behaviour layer and an animation layer (ADR-088).
//
// The behaviour layer decides *what a character is doing*: where it is going, how fast, which way
// it is facing, whether it is reacting to something. The animation layer decides *what that looks
// like*: which clip, at what playback rate, blended how. Those are different jobs with different
// owners, and the surface between them is small enough to write down.
//
// Deliberately one-directional and dependency-free. This header includes nothing from the skinning
// system and the skinning system need include nothing from here beyond this file: a behaviour
// publishes a LocomotionState, and anything that can pose a skeleton consumes one. An entity with
// no pose sink runs its behaviours and moves its node exactly as it would with one -- which is
// what lets a craft, a rock and a character all be entities, and what lets this layer be built and
// tested before a skeleton exists.

#include "scene/scene_types.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace avgen::entity {

// What a character is doing, at the resolution an animation state machine cares about. Not a list
// of clip names: a clip name is the animation layer's business, and a behaviour that named one
// would break the moment an asset shipped with different clips.
enum class Activity : std::uint8_t {
    Idle,     // standing, breathing
    Walk,     // travelling at a comfortable pace
    Run,      // travelling fast
    Turn,     // turning on the spot
    Observe,  // stopped and looking at something
    React,    // a one-shot response to an event
    // ADR-194: off the ground. Not speed-derived like the three above them -- a body in the air is
    // airborne whatever its horizontal speed -- so the gait machine passes them through untouched
    // and remembers the gait underneath, which is what makes `Run -> Jump -> Fall -> Land -> Run`
    // come back as a run rather than as an idle.
    Jump,     // rising
    Fall,     // descending, and also the state of a body that walked off something
    Land,     // touched down, recovering; standing, but not yet walking
};
[[nodiscard]] const char* activityName(Activity activity);
// The inverse, for the one place an activity is named in data: a field's reaction arc says which
// activity it holds, and "react" in a scene file has to become Activity::React without the field
// layer owning a second copy of this list. False for a name that is not one.
[[nodiscard]] bool activityFromName(std::string_view name, Activity& out);

// Everything an animation layer needs from a behaviour layer, and nothing else.
struct LocomotionState {
    Activity activity = Activity::Idle;
    // The timeline second this decision was made at, never a wall clock. The animation layer
    // stores *when* a state was entered rather than how long it has run, so passing the render
    // time is what makes a scrubbed or offline frame reproduce exactly (ADR-086).
    double time = 0.0;
    glm::vec3 position{0.0f}; // world
    float yaw = 0.0f;         // radians about +Y; the facing the body should adopt
    float speed = 0.0f;       // horizontal m/s -- selects the gait and its playback rate
    float turnRate = 0.0f;    // rad/s, signed -- selects a turn-in-place clip and its direction
    // ---- what the body DID, beside what it meant to (ADR-545) ----------------------------------
    // `speed` and `yaw` above are intent, and they are a polar pair: a scalar along a heading. They
    // can describe a body walking where it looks and nothing else. These two are the measurement --
    // world-space metres per second, and the unit vector the body is facing -- and the difference
    // between them is the whole of strafing, backing up, circling a target while watching it, and
    // the future-trajectory features a motion matcher would query on.
    //
    // Zero on a body's first step and across a seek, because a backward difference has nothing to
    // difference against there.
    glm::vec3 velocity{0.0f}; // world
    glm::vec3 facing{0.0f, 0.0f, 1.0f};
    bool grounded = true;
    // 0..1, decaying. A reaction the animation layer may blend a one-shot over (a flinch, a
    // head snap). The behaviour layer says how strongly and when; the animation layer says what.
    //
    // ADR-300: read, at last. It is the weight of any `scene::PoseLayer` whose `drive` is
    // `Reaction` -- an additive clip masked to whatever part of the body a scene says, played *on*
    // the gait rather than instead of it, so the shoulders can flinch while the legs keep the
    // stride. It reaches nothing on a node that authors no such layer, which is the honest default:
    // there is no joint name this engine may assume across three rig families that share none.
    float reaction = 0.0f;
    // Where the character is attending, when it is attending to anything. An animation layer uses
    // this for head/eye look-at on top of whatever clip is playing.
    //
    // ADR-300: read by any `scene::PoseLayer` whose `drive` is `Look`. **World space here**, and
    // entity-local by the time it reaches a layer -- `Composition::AnimationSink::driveLayers` does
    // the conversion, through the node's own world transform, because a posed rig has no world
    // position (ADR-274) and the node is what carries the scale a joint offset is measured in.
    //
    // These three fields and the kinematics above them were written every frame and read by nobody
    // for as long as this struct existed. `LookAt` publishes a target and declines to turn the body
    // while travelling, on the stated grounds that the head is the animation layer's business; that
    // sentence was true about the intent and false about the engine until there was a layer.
    glm::vec3 lookTarget{0.0f};
    bool hasLookTarget = false;

    // The ground under this body, in WORLD space, and entity-local by the time it reaches a layer
    // -- `Composition::AnimationSink::driveLayers` does the conversion, the same one and for the
    // same reason as `lookTarget` (ADR-274). Read by any `scene::PoseLayer` whose `drive` is
    // `Ground`: a foot layer plants its hoof on this plane instead of on the flat the walk cycle
    // was authored over.
    //
    // ADR-359 draws the line here on purpose. Everything above this field is stateful and smoothed
    // and re-simulated on a seek; everything below it -- the solver, the layer -- is a pure
    // function of the pose and this plane. The engine already has a place where a filtered ground
    // normal lives, and duplicating the filter one level down would have been a second answer to
    // the same question with its own lag.
    glm::vec3 groundPoint{0.0f};
    glm::vec3 groundNormal{0.0f, 1.0f, 0.0f};
    bool hasGroundPlane = false;

    // ---- what an action asked for (ADR-096) ----
    // The activity an action named -- "sit", "sleep", "pickUp" -- or empty when the gait is in
    // charge and `activity` above is the whole story. Still an activity *name* and never a clip
    // name: EntityDesc::clips turns it into whatever the asset shipped, which is the rule that
    // lets one action drive an alien, a deer and a robot.
    //
    // A std::string rather than a view because this struct outlives the call it was built in: an
    // entity keeps its last locomotion for anything that asks. Assigned from the action's own
    // string, so in steady state it reuses its capacity and allocates nothing per frame.
    std::string action;
    // Clip seconds per timeline second. The gait's answer to a walk clip authored at 1.6 m/s being
    // played by a body moving at 2.0.
    float playbackRate = 1.0f;
    // Cross-fade seconds into this state; negative leaves it to the animation state's own blendIn.
    float blend = -1.0f;
};

// Implemented by the animation layer. An entity holds a pointer that stays null until something
// installs one.
class IPoseSink {
public:
    virtual ~IPoseSink() = default;
    virtual void setLocomotion(const LocomotionState& state) = 0;
};

// Implemented by the animation layer so sockets can follow joints. Until one exists, a socket
// resolves against the entity's own transform, which is correct for a craft and approximate for a
// character -- approximate being the right failure for a prop that has to be somewhere.
//
// **The frame this answers in is the rig's model space, which is the entity's own frame, and not
// the world** (ADR-274). The method used to be called `jointWorldTransform` and its only consumer,
// `Entity::socketTransform`, composed the answer *onto* the entity's frame -- so the name promised
// world space and the one use demanded entity space, and nothing implemented it, so nothing ever
// had to choose. Three facts decide it against the name:
//
//   1. **A rig is shared.** `scene::Scene::rigs` is a flat list and several `scene::Entity` records
//      may name the same `RigId` -- `scene::updateRigs` poses each rig once and picks the *nearest*
//      entity to rate it. A posed rig therefore cannot have a world position, because it stands in
//      as many places as there are bodies carrying it.
//   2. **glTF says so.** `scene/skeleton.hpp`: the file's whole chain from its scene root is baked
//      into the joints, so "the model space these matrices land in *is* the file's scene space, and
//      the entity that carries the skin contributes only its placement in the world". The placement
//      is the entity's, and the entity is the only thing that holds it.
//   3. **The GPU agrees.** The skinned vertex stage multiplies the entity's own model matrix by the
//      palette. If the palette were world-space the entity transform would be applied twice.
//
// So the name moved to meet the use, rather than the use moving to meet the name. The caller
// composes: `world = entityTransform * jointTransform(joint)`.
//
// And the matrix to read is the **model-space** one -- `scene::poseToModel`, joint by joint -- not
// the GPU palette, whose entry k is `model[palette[k]] * inverseBind[k]` and whose translation is
// therefore not where the joint is (ADR-260). That mistake is what a skeleton overlay exists to
// catch; `tests/unit/test_character_lab_sockets.cpp` measures the gap so nobody makes it silently.
// ---- root motion (ADR-337) ----------------------------------------------------------------------

// How far the clip an animation layer is playing has carried the body, and which run of which
// clip that number belongs to.
//
// A plain record rather than `scene::RootMotionSample`, which holds the same three values, because
// this header's whole point is that it includes nothing from the skinning system (see the top of
// this file). `Composition::AnimationSink` is the one object that sees both and it copies across:
// four lines at a seam that is deliberately one-directional, against a `#include` that would make
// `entity` depend on `scene::Skeleton`.
struct RootMotionSample {
    // **Entity-local** -- the rig's model space, in the *asset's* own units -- for the three
    // reasons ADR-274 gives. The entity composes its yaw and the node's scale on, exactly as
    // `Entity::socketTransform` does, because the entity is the only thing that knows where this
    // rig is standing and how big it is being drawn.
    glm::vec3 displacement{0.0f};
    // Which run of which clip. Two samples may only be subtracted from one another when these
    // agree; a cross-fade back out of a travelling clip changes it, and the step across the change
    // is zero rather than the whole displacement backwards.
    std::uint64_t generation = 0;
    bool active = false; // false = whatever is playing here was not opted in, which is the default
};

// Implemented by the animation layer. The **return half** of the animation seam, and the only one:
// everything else in this header goes behaviour -> animation, and this goes animation -> behaviour.
//
// ADR-300 §8 is the reason it is a separate interface rather than a field on `LocomotionState`.
// `LocomotionState` is written by the behaviour layer and read by the animation layer; a field on
// it that the animation layer wrote would be a second direction hidden inside a struct whose
// header says it has one. And `MotionAuthority::Simulation` is a big enough claim to be asked for
// by name: this is the only thing in the animation system that has it, and a reader of
// `entity.cpp` should be able to find out why the body moved by finding one word.
class IRootMotionSource {
public:
    virtual ~IRootMotionSource() = default;
    // What the clip playing at timeline second `now` has displaced the body by, measured from
    // that clip's own first key. Pure: asking twice at the same second gives the same answer, and
    // asking is free of side effects, which is what lets a seek replay it.
    [[nodiscard]] virtual RootMotionSample rootMotion(double now) const = 0;
};

class ISkeletonQuery {
public:
    virtual ~ISkeletonQuery() = default;
    // The joint's transform **in the entity's own frame** (the rig's model space): the pose this
    // rig is currently holding, including whatever the animation player last evaluated.
    //
    // False when this skeleton has no such joint, or has not been posed yet; `out` is then left
    // alone, and the caller is expected to say that it fell back rather than to pretend it did not.
    [[nodiscard]] virtual bool jointTransform(std::string_view joint,
                                              scene::Transform& out) const = 0;
};

} // namespace avgen::entity
