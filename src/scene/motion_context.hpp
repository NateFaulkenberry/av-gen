#pragma once

// MotionContext (Phase B §4): everything a procedural motion layer is allowed to know, in one
// place, in the one space a posed rig actually has.
//
// **Why a struct and not a pointer to the entity.** ADR-300 draws a line: a pose layer cannot reach
// the world. `pose_layers.hpp` includes nothing from `entity/` and says so in its first comment,
// and that is not tidiness -- it is what makes a layer a pure function of a pose and a context, so
// that a scrub replays it exactly and a test can construct one without a world. Handing a layer an
// `Entity&` would end that in one commit.
//
// So this is the keyhole. The entity tier fills it in at the seam (`Composition::AnimationSink::
// driveLayers`, which already depends on both tiers), and the layer reads it and nothing else.
//
// **Everything spatial here is ENTITY-LOCAL**, except where a field says otherwise. A posed rig has
// no world position (ADR-274); joints live in the rig's model space, and the node's transform is
// what carries the scale a joint offset is measured in. The seam does the conversion once, for the
// same reason and with the same covector care that ADR-359 already spelled out for the ground
// normal -- Glowmere draws these bodies at 3.3x to 3.6x, so getting it wrong is invisible on a
// uniform scale and wrong the moment a scene squashes one axis.
//
// **References and handles, not copies** (§4). The one thing here that is not a small POD is
// `ground`, and it is a borrowed pointer to a query the composition owns for the whole frame.

#include "scene/ground_query.hpp"

#include <cstdint>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace avgen::scene {

// What the body is doing, as much of it as a *pose* layer has any business knowing.
//
// **Deliberately a projection of `entity::Activity` and not that enum.** Nine activities matter to
// a gait machine -- Observe, React, Jump, Fall, Land all change which clip plays. To a layer that
// bends a knee or turns a head, the only question is whether the body is standing, travelling
// slowly, travelling fast, turning on the spot, or doing something this vocabulary has no opinion
// about. Importing the full enum would drag `entity/locomotion.hpp` into every pose layer and
// undo the paragraph above.
enum class LocomotionMode : std::uint8_t {
    Idle,
    Walk,
    Run,
    Turn,
    Other, // airborne, reacting, observing, or an action the gait is not driving
};
[[nodiscard]] const char* locomotionModeName(LocomotionMode mode);

struct MotionContext {
    // ---- time ----------------------------------------------------------------------------------
    // Seconds since the previous update. Zero on the first frame after a seek and on a paused
    // frame; a layer that divides by it must say what it does at zero (ADR-521).
    float dt = 0.0f;
    // The timeline second, never a wall clock. A layer that needs a phase derives it from this, so
    // that a scrubbed frame and a rendered frame agree (ADR-086, ADR-360).
    double time = 0.0;

    // ---- what the body DID (ADR-545: measured once, not authored twenty times) ------------------
    glm::vec3 velocity{0.0f};              // entity-local, m/s
    glm::vec3 facing{0.0f, 0.0f, 1.0f};    // entity-local, unit
    float groundSpeed = 0.0f;              // horizontal magnitude of `velocity`, m/s
    float turnRate = 0.0f;                 // rad/s, signed

    // ---- what it MEANT to do -------------------------------------------------------------------
    // The difference between these and the pair above is the whole of strafing, backing up and
    // circling a target while watching it, and it is what a stride warper needs in order to know
    // which way the stride should point.
    glm::vec3 desiredVelocity{0.0f};       // entity-local, m/s
    glm::vec3 desiredFacing{0.0f, 0.0f, 1.0f};

    // ---- what it is playing --------------------------------------------------------------------
    LocomotionMode mode = LocomotionMode::Idle;
    // Clip seconds per timeline second, as the gait resolved it. **Carried so a layer can see that
    // it saturated**: `Gait::playbackRate` clamps, and a clamped rate is the engine saying it could
    // not do the job with the one tool it had. Measured on the shipping Glowmere scene, the aliens
    // sit at the clamp floor permanently.
    float playbackRate = 1.0f;
    // The speed the playing clip was authored for, and what the body is actually doing against it.
    // `strideRatio` is 1 when they agree; below 1 the stride out-runs the body, which is 97% of the
    // real cases. Zero `authoredSpeed` means nothing to compare against -- not "fine".
    float authoredSpeed = 0.0f;
    float strideRatio = 1.0f;
    // Normalised 0..1 through the locomotion cycle, from Phase A's `PhaseTrack`, when the clip has
    // one. A stride warper and a foot planter both need it and neither may re-derive it.
    float phase = 0.0f;
    bool hasPhase = false;

    // ---- the world, through a keyhole ----------------------------------------------------------
    // ADR-551. Queries take and return **world** space; `worldFromLocal` and `localFromWorld` are
    // how a layer gets a posed joint there and the answer back. Null when no source is installed,
    // which a layer must handle -- see `IGroundQuery` on why an invalid sample is not flat ground.
    const IGroundQuery* ground = nullptr;
    glm::mat4 worldFromLocal{1.0f};
    glm::mat4 localFromWorld{1.0f};

    // The one ground plane the body as a whole is standing on, already smoothed upstream (ADR-359).
    // Per-FOOT ground is `ground` above; this is the body's, and the two are different questions.
    glm::vec3 groundPoint{0.0f};           // entity-local
    glm::vec3 groundNormal{0.0f, 1.0f, 0.0f};
    bool hasGroundPlane = false;

    // ---- targets -------------------------------------------------------------------------------
    glm::vec3 lookTarget{0.0f};            // entity-local
    bool hasLookTarget = false;
    float reaction = 0.0f;                 // 0..1, decaying

    // ---- configuration -------------------------------------------------------------------------
    // The body's own scale, so that a layer's thresholds are ratios rather than metres. ADR-552
    // paid for this rule on the other side of the pipeline: a number in metres judged a 1.66 m
    // alien and a 1.79 m human differently, and the Glowmere cast is drawn at 1.94x on top.
    float restHeight = 1.0f;

    // Convenience, so that every layer does not re-derive the same two lines.
    [[nodiscard]] glm::vec3 toWorld(const glm::vec3& local) const {
        return glm::vec3(worldFromLocal * glm::vec4(local, 1.0f));
    }
    [[nodiscard]] glm::vec3 toLocal(const glm::vec3& world) const {
        return glm::vec3(localFromWorld * glm::vec4(world, 1.0f));
    }
    // A direction, not a point: no translation, and **the inverse-transpose for a normal**, which
    // is not the same matrix unless the transform is a pure rotation (ADR-359).
    [[nodiscard]] glm::vec3 normalToLocal(const glm::vec3& worldNormal) const {
        const glm::vec3 n = glm::transpose(glm::mat3(worldFromLocal)) * worldNormal;
        const float len = glm::length(n);
        return len > 1e-6f ? n / len : glm::vec3(0.0f, 1.0f, 0.0f);
    }
};

} // namespace avgen::scene
