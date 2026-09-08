#pragma once

// Time model (ADR-012). The engine never reads a system clock outside RealtimeClock. Every
// subsystem update receives a FrameTime, so the same scene evaluates identically in real time
// (delta-driven) and offline (frameIndex / fps).

#include <chrono>
#include <cstdint>

namespace avgen {

struct FrameTime {
    double renderTime = 0.0; // seconds on the engine timeline
    double deltaTime = 0.0;  // seconds since the previous frame (0 on the first frame)
    std::uint64_t frameIndex = 0;
};

class FrameClock {
public:
    virtual ~FrameClock() = default;
    // Advances the clock and returns the time for the frame about to be evaluated.
    virtual FrameTime tick() = 0;
    // Returns the last FrameTime produced by tick() without advancing.
    [[nodiscard]] virtual FrameTime current() const = 0;
    // Moves the timeline to an absolute time without producing a delta on the next tick.
    virtual void seek(double renderTime) = 0;
};

// Wall-clock driven. Delta is clamped to maxDelta so a stall (debugger, window drag) does not
// produce a giant simulation step.
class RealtimeClock final : public FrameClock {
public:
    explicit RealtimeClock(double maxDelta = 0.1);
    FrameTime tick() override;
    [[nodiscard]] FrameTime current() const override { return current_; }
    void seek(double renderTime) override;

private:
    using clock = std::chrono::steady_clock;
    FrameTime current_{};
    clock::time_point last_{};
    bool started_ = false;
    double maxDelta_;
};

// Deterministic: renderTime = startTime + frameIndex / fps.
class FixedStepClock final : public FrameClock {
public:
    explicit FixedStepClock(double fps, double startTime = 0.0);
    FrameTime tick() override;
    [[nodiscard]] FrameTime current() const override { return current_; }
    void seek(double renderTime) override;
    [[nodiscard]] double fps() const { return fps_; }

private:
    FrameTime current_{};
    double fps_;
    double startTime_;
    bool started_ = false;
};

} // namespace avgen
