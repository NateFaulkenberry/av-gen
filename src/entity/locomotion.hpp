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
    bool grounded = true;
    // 0..1, decaying. A reaction the animation layer may blend a one-shot over (a flinch, a
    // head snap). The behaviour layer says how strongly and when; the animation layer says what.
    float reaction = 0.0f;
    // Where the character is attending, when it is attending to anything. An animation layer uses
    // this for head/eye look-at on top of whatever clip is playing.
    glm::vec3 lookTarget{0.0f};
    bool hasLookTarget = false;

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
// the world** (ADR-272). The method used to be called `jointWorldTransform` and its only consumer,
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
