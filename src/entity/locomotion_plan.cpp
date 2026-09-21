#include "entity/locomotion_plan.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::entity {

const char* locomotionPhaseName(LocomotionPhase phase) {
    switch (phase) {
    case LocomotionPhase::Idle: return "idle";
    case LocomotionPhase::Starting: return "starting";
    case LocomotionPhase::Moving: return "moving";
    case LocomotionPhase::Stopping: return "stopping";
    case LocomotionPhase::Turning: return "turning";
    case LocomotionPhase::Strafing: return "strafing";
    }
    return "idle";
}

LocomotionPlan planLocomotion(const LocomotionPlanSettings& settings,
                              const LocomotionPlanState& in, Activity gait, float speed,
                              float desiredSpeed, float turnRate, float strafeAngle, double time,
                              LocomotionPlanState& next) {
    next = in;
    LocomotionPlan out;
    out.strafeAngle = strafeAngle;

    const double held = in.started ? time - in.entered : 0.0;
    const bool mayChange = !in.started || held >= static_cast<double>(settings.minDwell);
    const float strafeDegrees = std::abs(glm::degrees(strafeAngle));
    const bool standing = speed < settings.standSpeed;
    const bool wantsToStand = desiredSpeed < settings.standSpeed;
    // Airborne and action states pass straight through, the same way `Gait::select` lets them:
    // a body in the air is not starting, stopping or strafing, it is falling.
    const bool locomotor = gait == Activity::Idle || gait == Activity::Walk ||
                           gait == Activity::Run || gait == Activity::Turn;

    LocomotionPhase phase = in.phase;
    if (!in.started) {
        phase = standing ? LocomotionPhase::Idle : LocomotionPhase::Moving;
    } else if (!locomotor) {
        // Not our business this frame. Hold the phase rather than reset it, so a body that jumps
        // mid-stride comes back to the stride it left -- the rule `Gait::select` already follows
        // for the gait underneath an airborne state (ADR-194).
        phase = in.phase;
    } else if (mayChange) {
        switch (in.phase) {
        case LocomotionPhase::Idle:
            if (!wantsToStand) {
                phase = LocomotionPhase::Starting;
            } else if (std::abs(turnRate) > settings.turnEnterRate) {
                phase = LocomotionPhase::Turning;
            }
            break;
        case LocomotionPhase::Turning:
            if (!wantsToStand) {
                phase = LocomotionPhase::Starting;
            } else if (std::abs(turnRate) < settings.turnExitRate) {
                phase = LocomotionPhase::Idle;
            }
            break;
        case LocomotionPhase::Starting:
            // Two ways out, and both are needed. The body reached its pace, or it ran out of
            // start. Without the second a body asked for a speed it can never reach -- a walker
            // on a steep slope, a body held by a tractor beam -- would be starting forever.
            if (wantsToStand) {
                phase = LocomotionPhase::Stopping;
                next.stopFromSpeed = speed;
            } else if (speed >= desiredSpeed * settings.startCompleteFraction ||
                       held >= static_cast<double>(settings.startSeconds)) {
                phase = strafeDegrees > settings.strafeEnterDegrees ? LocomotionPhase::Strafing
                                                                    : LocomotionPhase::Moving;
            }
            break;
        case LocomotionPhase::Moving:
        case LocomotionPhase::Strafing:
            // **A braking test, not a slow test.** `desiredSpeed < speed * fraction` asks whether
            // the body is being *asked to shed* speed. A plain "is it slow" test would put a
            // character that only ever creeps into a permanent stop, and this repository's cast
            // creeps: 97 of 100 of them travel below a quarter of their authored stride (B.A).
            if (wantsToStand || desiredSpeed < speed * settings.stopTriggerFraction) {
                phase = LocomotionPhase::Stopping;
                next.stopFromSpeed = std::max(speed, 1e-3f);
            } else if (in.phase == LocomotionPhase::Moving &&
                       strafeDegrees > settings.strafeEnterDegrees) {
                phase = LocomotionPhase::Strafing;
            } else if (in.phase == LocomotionPhase::Strafing &&
                       strafeDegrees < settings.strafeExitDegrees) {
                phase = LocomotionPhase::Moving;
            }
            break;
        case LocomotionPhase::Stopping:
            if (!wantsToStand && desiredSpeed > speed) {
                phase = LocomotionPhase::Starting; // asked to go again mid-stop
            } else if (standing || held >= static_cast<double>(settings.stopSeconds)) {
                phase = LocomotionPhase::Idle;
            }
            break;
        }
    }

    out.changed = !in.started || phase != in.phase;
    if (out.changed) {
        next.phase = phase;
        next.entered = time;
    }
    next.started = true;
    out.phase = phase;

    // ---- progress and the stride ramp ----------------------------------------------------------
    //
    // §8's `strideRamp` and §9's brake are the same quantity seen from two ends, so they are one
    // number. A start eases the stride in from a stand; a stop eases it out. The curve is
    // `smoothstep`, which leaves and arrives with zero slope -- a linear ramp changes stride
    // length discontinuously at both ends, and the discontinuity is visible as a hitch.
    const double sinceEntered = out.changed ? 0.0 : time - next.entered;
    const auto smooth = [](float t) {
        t = std::clamp(t, 0.0f, 1.0f);
        return t * t * (3.0f - (2.0f * t));
    };
    if (phase == LocomotionPhase::Starting && settings.startSeconds > 1e-4f) {
        out.progress = std::clamp(static_cast<float>(sinceEntered) / settings.startSeconds, 0.0f, 1.0f);
        // From a short first step up to the authored stride.
        out.strideScale = 0.35f + (0.65f * smooth(out.progress));
    } else if (phase == LocomotionPhase::Stopping && settings.stopSeconds > 1e-4f) {
        out.progress = std::clamp(static_cast<float>(sinceEntered) / settings.stopSeconds, 0.0f, 1.0f);
        // Down to a short final step, never to zero: a stride of zero is both feet in one place,
        // and §9 asks for a final *step*, not a fade.
        out.strideScale = 1.0f - (0.6f * smooth(out.progress));
    } else {
        out.progress = 0.0f;
        out.strideScale = 1.0f;
    }
    return out;
}

} // namespace avgen::entity
