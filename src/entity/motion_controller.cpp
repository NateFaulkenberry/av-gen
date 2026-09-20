#include "entity/motion_controller.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::entity {
namespace {

glm::vec3 flatten(const glm::vec3& v) { return glm::vec3(v.x, 0.0f, v.z); }

float horizontalLength(const glm::vec3& v) {
    return std::sqrt((v.x * v.x) + (v.z * v.z));
}

// Signed angle from `from` to `to` about +Y, radians, in (-pi, pi]. Both are expected flat and
// non-degenerate; the caller checks.
float signedYaw(const glm::vec3& from, const glm::vec3& to) {
    const float dot = std::clamp((from.x * to.x) + (from.z * to.z), -1.0f, 1.0f);
    const float cross = (from.z * to.x) - (from.x * to.z);
    return std::atan2(cross, dot);
}

// Rotate `v` about +Y by `radians`.
glm::vec3 rotateY(const glm::vec3& v, float radians) {
    const float s = std::sin(radians);
    const float c = std::cos(radians);
    return glm::vec3((v.x * c) + (v.z * s), v.y, (v.z * c) - (v.x * s));
}

} // namespace

MotionSolution stepMotion(const MotionRequest& request, const MotionState& in,
                          const MotionLimits& limits, float dt, MotionState& next) {
    next = in;
    MotionSolution out;
    out.velocity = in.velocity;
    out.facing = in.facing;
    out.speed = horizontalLength(in.velocity);

    if (dt <= 0.0f) {
        // ADR-521: the first tick of every render has dt 0. Nothing changes and nothing is divided.
        next.started = true;
        return out;
    }

    // §39: the steering correction is added, not blended. The caller has already decided how hard
    // to avoid; a second weight here would be a second opinion about one thing.
    const glm::vec3 desired = flatten(request.desiredVelocity + request.steering);
    const float desiredSpeed = horizontalLength(desired);
    const glm::vec3 current = flatten(in.velocity);
    const float currentSpeed = horizontalLength(current);

    // ---- direction, rate-limited as a turn -------------------------------------------------
    //
    // Below `headingFloor` the body has no meaningful heading: a standing character asked to walk
    // east must not have to rotate a velocity vector it does not have. Above it, the velocity's
    // direction turns at `maxTurnRate` and no faster, which is why a fast body and a slow body take
    // the same time to come round -- the thing that collapsing this into one acceleration limit
    // gets backwards.
    glm::vec3 heading = currentSpeed > limits.headingFloor ? current / currentSpeed : glm::vec3(0.0f);
    if (desiredSpeed > 1e-5f) {
        const glm::vec3 wanted = desired / desiredSpeed;
        if (glm::length(heading) < 1e-5f) {
            heading = wanted; // from rest, any direction is free
        } else {
            const float want = signedYaw(heading, wanted);
            const float most = limits.maxTurnRate * dt;
            const float applied = std::clamp(want, -most, most);
            out.turnLimited = std::abs(want) > most + 1e-6f;
            heading = rotateY(heading, applied);
        }
    }

    // ---- speed, limited differently up and down ---------------------------------------------
    //
    // Braking and accelerating are separate limits because stopping is not the reverse of starting
    // -- the same split `Gait::approach` already draws for the scalar case, reused here in the
    // vector one rather than re-decided.
    const float rising = desiredSpeed > currentSpeed;
    const float limit = rising ? limits.maxAcceleration : limits.maxDeceleration;
    const float most = limit * dt;
    const float want = desiredSpeed - currentSpeed;
    const float applied = std::clamp(want, -most, most);
    out.accelerationLimited = std::abs(want) > most + 1e-6f;
    const float speed = std::max(0.0f, currentSpeed + applied);

    out.velocity = glm::length(heading) > 1e-5f ? heading * speed : glm::vec3(0.0f);
    out.speed = horizontalLength(out.velocity);

    // ---- facing, which is not the heading ----------------------------------------------------
    //
    // A body circling a target while watching it has one of each and they disagree; that is the
    // whole reason `MotionRequest` carries both (ADR-545). The facing turns at its own rate, which
    // is faster than the velocity's, because turning the head and shoulders costs less than
    // turning the momentum.
    glm::vec3 wantFacing = flatten(request.desiredFacing);
    const float facingLen = horizontalLength(wantFacing);
    if (facingLen > 1e-5f) {
        wantFacing /= facingLen;
        const glm::vec3 have = horizontalLength(in.facing) > 1e-5f
                                   ? glm::normalize(flatten(in.facing))
                                   : wantFacing;
        const float delta = signedYaw(have, wantFacing);
        const float mostFacing = limits.maxFacingRate * dt;
        const float appliedFacing = std::clamp(delta, -mostFacing, mostFacing);
        out.facing = glm::normalize(rotateY(have, appliedFacing));
        out.turnRate = appliedFacing / dt;
    }

    // §35: acceleration measured once, here, rather than re-derived by lean, stride and balance
    // independently. The same rule ADR-545 established for velocity one tier down.
    out.acceleration = (out.velocity - in.velocity) / dt;

    next.previousVelocity = in.velocity;
    next.velocity = out.velocity;
    next.facing = out.facing;
    next.started = true;
    return out;
}

} // namespace avgen::entity
