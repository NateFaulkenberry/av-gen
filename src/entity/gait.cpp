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
    //
    // **Unless the body has stopped underneath it (ADR-907).** A flinch while walking keeps the
    // feet going and the walk is what comes back. A two-second hold in a beam, or an observe pose
    // at the end of an errand, stands the body still -- and a body that is standing is standing,
    // whatever it is attending to. Remembering a walk through that is how the first frame of the
    // next walk read "Idle" (the speed ramps up from nothing, below the move band) and changed the
    // gait, whose `minDwell` then refused the walk for a quarter of a second while the body
    // accelerated to walking pace: legs idle, ground moving. Before ADR-907 every behaviour
    // restarted at full speed in one frame, so the stop underneath never showed.
    if (!gaitLocomotor(proposed)) {
        if (!moving_ && (gait_ == Activity::Walk || gait_ == Activity::Run)) {
            gait_ = Activity::Idle;
            dwell_ = 0.0;
            ++changes_;
        }
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

float Gait::playbackRate(const GaitSettings& settings, Activity activity, float speed, float turnRate) {
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
        // **A turn on the spot, on an asset with nothing to turn with (ADR-908).**
        //
        // `idleRate` below `kVisibleClipRate` is the labelled compensation above: an asset whose
        // only clip is a locomotion cycle, which freezes it rather than run it on the spot. That is
        // the right answer for a body standing still and the wrong one for a body *turning*: the
        // GV3 audit found the farm animals rotating a frozen mid-stride pose about their own axis
        // on every pivot -- 27 to 44% of all the turning they did -- which reads as a statue on a
        // turntable. A body turning at w rad/s moves its feet round the pivot at w * `pivotRadius`,
        // so its cycle plays at that speed over the speed the cycle was authored at, floored where
        // a cycle becomes visible and capped by the gait's own ceiling. An asset with a real idle
        // (`idleRate` >= visible) has a turn clip of its own and is left alone.
        if (activity == Activity::Turn && settings.idleRate < kVisibleClipRate &&
            settings.walkSpeed > 1e-4f && settings.pivotRadius > 0.0f && turnRate != 0.0f) {
            const float feet = std::abs(turnRate) * settings.pivotRadius;
            return std::clamp(feet / settings.walkSpeed, std::max(settings.rateMin, kVisibleClipRate),
                              std::max(settings.rateMax, kVisibleClipRate));
        }
        return settings.idleRate;
    }
    return std::clamp(speed / authored, settings.rateMin, settings.rateMax);
}

float turnPace(const TurnSettings& turning, float pace, float error) {
    const float facing = std::max(0.0f, std::cos(error));
    if (turning.radius <= 0.0f || pace <= kTurnRestSpeed) {
        return facing;
    }
    return kTurnKeep + (1.0f - kTurnKeep) * facing;
}

bool insideTurn(const TurnSettings& turning, float pace, float distance, float error) {
    if (turning.radius <= 0.0f || pace <= kTurnRestSpeed) {
        return false;
    }
    return distance < 2.0f * turning.radius * std::abs(std::sin(error));
}

float turnCap(const TurnSettings& turning, float before, float after) {
    if (turning.radius <= 0.0f || after <= kTurnRestSpeed) {
        return turning.rate;
    }
    return std::min(turning.rate, std::min(before, after) / turning.radius);
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
