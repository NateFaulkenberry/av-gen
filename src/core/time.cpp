#include "core/time.hpp"

#include <algorithm>

namespace avgen {

RealtimeClock::RealtimeClock(double maxDelta) : maxDelta_(maxDelta) {}

FrameTime RealtimeClock::tick() {
    const auto now = clock::now();
    if (!started_) {
        started_ = true;
        last_ = now;
        current_.deltaTime = 0.0;
        return current_;
    }
    const double dt = std::chrono::duration<double>(now - last_).count();
    last_ = now;
    current_.deltaTime = std::clamp(dt, 0.0, maxDelta_);
    current_.renderTime += current_.deltaTime;
    current_.frameIndex += 1;
    return current_;
}

void RealtimeClock::seek(double renderTime) {
    current_.renderTime = renderTime;
    current_.deltaTime = 0.0;
    last_ = clock::now();
}

FixedStepClock::FixedStepClock(double fps, double startTime) : fps_(fps), startTime_(startTime) {
    current_.renderTime = startTime;
}

FrameTime FixedStepClock::tick() {
    if (!started_) {
        started_ = true;
        current_.deltaTime = 0.0;
        current_.renderTime = startTime_;
        return current_;
    }
    current_.frameIndex += 1;
    const double next = startTime_ + static_cast<double>(current_.frameIndex) / fps_;
    current_.deltaTime = next - current_.renderTime;
    current_.renderTime = next;
    return current_;
}

void FixedStepClock::restartAt(double renderTime) {
    seek(renderTime);
    started_ = false;
}

void FixedStepClock::seek(double renderTime) {
    startTime_ = renderTime;
    current_.frameIndex = 0;
    current_.renderTime = renderTime;
    current_.deltaTime = 0.0;
    started_ = true;
}

} // namespace avgen
