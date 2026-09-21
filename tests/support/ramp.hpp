#pragma once

// **What "stalled" means for a body that has an authored acceleration** (ADR-620, ADR-621).
//
// Two independent detectors in this suite ask the same question and got the same answer wrong:
// `test_abduction_poc`'s `stalledPercent` ("commanded to move and did not move") and
// `test_farm_locomotion`'s `frozenWhileMoving` ("the clip is frozen while the body covers
// ground"). Both were written for a world in which `state.speed` was assigned outright, so a body
// was either at full speed or stopped and the two could never disagree. With an authored ramp a
// body spends real frames between the two, and both detectors counted those as faults.
//
// **This is a definition, not a tolerance, and the difference matters.** The ramp is authored,
// bounded and known: `Gait::approach` moves the speed by exactly `accel * dt` while it is climbing
// and by less once it has arrived. So "is this body inside its own acceleration budget" has an
// exact answer rather than a threshold someone picked, and a body inside it is **accelerating,
// which is not stalled**. Nothing here is loosened -- a body that is genuinely stuck is not
// changing speed at its authored rate, and still counts.
//
// It lives in one place so the two detectors cannot drift apart, which is the same reasoning that
// put `MatchSettings::kDefaultContinuation` beside the halflife derived from it.

#include "entity/gait.hpp"

#include <cmath>

namespace avgen::testsupport {

// True when the body's speed moved by (as near as floating point allows) the most its authored
// gait permits in one step -- i.e. it is still on its ramp rather than settled at a target.
//
// Returns false when nothing was authored, because then there is no ramp and the old, strict
// reading of "did not move" is the correct one.
[[nodiscard]] inline bool withinAuthoredRamp(const entity::GaitSettings& gait, float previousSpeed,
                                             float speed, double dt) {
    if (!gait.accelAuthored || dt <= 0.0) {
        return false;
    }
    const auto step = static_cast<float>(dt);
    const float delta = speed - previousSpeed;
    constexpr float kFloatSlack = 1e-4f; // float accumulation only; not a tuning surface
    if (delta > 0.0f) {
        return delta >= (gait.accel * step) - kFloatSlack;
    }
    if (delta < 0.0f) {
        return -delta >= (gait.decel * step) - kFloatSlack;
    }
    return false;
}

} // namespace avgen::testsupport
