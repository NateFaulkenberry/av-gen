#pragma once

// The seam between a behaviour layer and an animation layer (ADR-087).
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
};
[[nodiscard]] const char* activityName(Activity activity);

// Everything an animation layer needs from a behaviour layer, and nothing else.
struct LocomotionState {
    Activity activity = Activity::Idle;
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
class ISkeletonQuery {
public:
    virtual ~ISkeletonQuery() = default;
    // False when this skeleton has no such joint; `out` is then left alone.
    [[nodiscard]] virtual bool jointWorldTransform(std::string_view joint,
                                                   scene::Transform& out) const = 0;
};

} // namespace avgen::entity
