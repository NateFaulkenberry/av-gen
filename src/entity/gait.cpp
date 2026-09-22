#include "entity/gait.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::entity {

bool gaitLocomotor(Activity activity) {
    switch (activity) {
    case Activity::Idle:
    case Activity::Walk:
    case Activity::Run:
    case Activity::Turn:
        return true;
    case Activity::Observe:
    case Activity::React:
    // ADR-194: airborne. Passed through for the same reason `React` is -- the gait underneath is
    // remembered, so a body that jumps mid-run comes back to a run.
    case Activity::Jump:
    case Activity::Fall:
    case Activity::Land:
        return false;
    }
    return false;
}

void Gait::reset() {
    gait_ = Activity::Idle;
    dwell_ = 0.0;
    changes_ = 0;
    moving_ = false;
    running_ = false;
}

Activity Gait::select(const GaitSettings& settings, Activity proposed, float speed, float turnRate,
                      double dt) {
    dwell_ += dt;

    // The bands, read in the direction the body is actually going. `moving_` and `running_` are
    // the memory that makes them bands rather than thresholds.
    const float moveThreshold = moving_ ? settings.moveExit : settings.moveEnter;
    moving_ = speed > moveThreshold;
    if (moving_) {
        const float runThreshold = running_ ? settings.runExit : settings.runEnter;
        running_ = speed > runThreshold;
    } else {
        running_ = false;
    }

    // Something else has the character's attention. Pass it through untouched and leave the
    // remembered gait alone, so `Walking -> React -> Walking` is what comes back rather than
    // `Walking -> React -> Idle` (ADR-091).
    if (!gaitLocomotor(proposed)) {
        return proposed;
    }

    Activity wanted = Activity::Idle;
    if (moving_) {
        wanted = running_ ? Activity::Run : Activity::Walk;
    } else if (std::abs(turnRate) > settings.turnEnter) {
        wanted = Activity::Turn;
    }

    if (wanted == gait_) {
        return gait_;
    }
    // The dwell. A change is refused while the current gait is younger than `minDwell`, which is
    // what stops a body accelerating across the band from switching twice in three frames. Not
    // applied to the first decision after a reset (dwell_ starts at 0 and the gait starts Idle,
    // so a character that begins walking begins walking).
    if (changes_ > 0 && dwell_ < static_cast<double>(settings.minDwell)) {
        return gait_;
    }
    gait_ = wanted;
    dwell_ = 0.0;
    ++changes_;
    return gait_;
}

float Gait::playbackRate(const GaitSettings& settings, Activity activity, float speed) {
    if (!settings.matchRate) {
        return 1.0f;
    }
    float authored = 0.0f;
    if (activity == Activity::Walk) {
        authored = settings.walkSpeed;
    } else if (activity == Activity::Run) {
        authored = settings.runSpeed;
    }
    if (authored <= 1e-4f) {
        // Not walking and not running: an idle, a turn, an observe. `idleRate` decides, because
        // whether that clip should be moving is a property of the *asset* -- an alien has an Idle
        // to play, a farm animal has only its Walk -- and this function cannot see clip names.
        //
        // **But only when the body is actually still.** A moving body's cycle must advance
        // whatever the gait has decided to call it: a clip frozen while the ground goes past is
        // foot skate, and eliminating that is what rate matching exists for. Keying the rate on
        // the gait's *activity* rather than on the body's *speed* is a coupling defect -- the two
        // agree while a body can only be at full speed or stopped, and disagree the moment it
        // decelerates through the `moveExit` band with real speed left.
        //
        // ADR-620 surfaced it: before authored acceleration, speed went 3.07 -> 0 in one frame, so
        // "the gait says Idle" and "the body has stopped" were the same instant and this branch
        // was only ever reached at a standstill. With a ramp they separate, and a farm animal --
        // `idleRate` 0, no idle clip -- froze its walk cycle mid-stride while still covering
        // ground, on up to 33 frames of one run.
        //
        // `speed > 0` rather than a threshold, deliberately: a behaviour that is not moving the
        // body assigns exactly zero, so the standing case is unchanged and nothing had to be
        // picked. `rateMin` still floors the result, so a crawl does not play a walk cycle at a
        // hundredth of its speed.
        if (speed > 0.0f && settings.walkSpeed > 1e-4f) {
            return std::clamp(speed / settings.walkSpeed, settings.rateMin, settings.rateMax);
        }
        return settings.idleRate;
    }
    return std::clamp(speed / authored, settings.rateMin, settings.rateMax);
}

float Gait::footSlip(const GaitSettings& settings, Activity activity, float speed) {
    float authored = 0.0f;
    if (activity == Activity::Walk) {
        authored = settings.walkSpeed;
    } else if (activity == Activity::Run) {
        authored = settings.runSpeed;
    } else {
        return 1.0f; // standing, turning, observing: no stride to be out of step with
    }
    if (authored <= 1e-4f || speed <= 1e-4f) {
        return 1.0f;
    }
    // What the clip is actually being played at, which is the authored rate only when matching is
    // on *and* the ratio is inside the clamp. A rate matcher that saturates is still slipping.
    const float rate = playbackRate(settings, activity, speed);
    return speed / (authored * std::max(rate, 1e-4f));
}

float Gait::approach(float current, float desired, float accel, float decel, double dt) {
    const auto step = static_cast<float>(dt);
    if (step <= 0.0f) {
        return current;
    }
    if (desired > current) {
        return std::min(desired, current + std::max(0.0f, accel) * step);
    }
    if (desired < current) {
        return std::max(desired, current - std::max(0.0f, decel) * step);
    }
    return current;
}

} // namespace avgen::entity
