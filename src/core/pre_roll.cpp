#include "core/pre_roll.hpp"

#include <algorithm>
#include <cmath>

namespace avgen {

const char* timelineStepName(TimelineStep step) {
    switch (step) {
    case TimelineStep::First:
        return "first";
    case TimelineStep::Repeat:
        return "repeat";
    case TimelineStep::Continuous:
        return "continuous";
    case TimelineStep::Jump:
        return "jump";
    }
    return "unknown";
}

TimelineStep classifyStep(bool havePrevious, const FrameTime& previous, const FrameTime& current) {
    if (!havePrevious) {
        return TimelineStep::First;
    }
    // Both clocks in this engine produce render times by exact accumulation or by exact
    // multiplication (`start + index / fps`), so the tolerance is float slop and not a window: a
    // window wide enough to absorb a real discontinuity would classify a small seek as motion,
    // which is the smear this whole enum exists to prevent.
    const double tolerance = 1e-9 + 1e-6 * std::abs(current.renderTime);
    const bool sameSecond = std::abs(current.renderTime - previous.renderTime) <= tolerance;
    if (current.frameIndex == previous.frameIndex) {
        return sameSecond ? TimelineStep::Repeat : TimelineStep::Jump;
    }
    if (current.frameIndex != previous.frameIndex + 1) {
        return TimelineStep::Jump;
    }
    const double expected = previous.renderTime + current.deltaTime;
    return std::abs(current.renderTime - expected) <= tolerance ? TimelineStep::Continuous : TimelineStep::Jump;
}

PreRollPlan planPreRoll(const PreRoll& roll, const FrameTime& time) {
    PreRollPlan plan;
    plan.arrivalFrameIndex = time.frameIndex;
    const std::uint32_t n = std::min(roll.frames, roll.cap);
    if (n == 0) {
        return plan;
    }
    // The step. The arriving frame's own delta when it has a usable one -- an offline render's
    // frames are 1/fps apart and the roll has to land on the same seconds the full render did, or
    // every time-keyed seed it re-runs is a seed that second never had. A first frame carries no
    // delta yet, and 60 is what every clock in this engine defaults to.
    double step = roll.stepSeconds;
    if (!(step > 0.0)) {
        step = time.deltaTime > 0.0 && time.deltaTime <= 0.1 ? time.deltaTime : 1.0 / 60.0;
    }
    const std::uint64_t base = std::max<std::uint64_t>(time.frameIndex, n);
    plan.arrivalFrameIndex = base;
    plan.frames.reserve(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        FrameTime f{};
        f.renderTime = time.renderTime - static_cast<double>(n - i) * step;
        f.deltaTime = step;
        f.frameIndex = base - n + i;
        plan.frames.push_back(f);
    }
    return plan;
}

} // namespace avgen
