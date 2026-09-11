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

    // A per-frame number for anything that wants to *decorrelate* successive frames -- a dither
    // seed, a ray-march offset -- as opposed to anything that *accumulates* across them.
    //
    // It is derived from the time on the timeline, not from how many frames have been drawn, and
    // that distinction is the whole point. `frameIndex` counts from wherever the render started, so
    // a given second is frame 104 in a full render and frame 4 in a render of the last five
    // seconds, and a jitter keyed to it draws that second two different ways. Measured on
    // night-shift before this existed: three renders covering t=104s agreed on nothing, differing
    // over 83% of the frame by about one value in 255 -- small, and exactly large enough to make a
    // re-rendered section not splice cleanly into a full render.
    //
    // Quantised to 1/240 s so it still changes every frame at any sane frame rate, and wraps at
    // 2^24 to stay exactly representable as a float on the way to a shader.
    [[nodiscard]] std::uint32_t frameNonce() const {
        const double ticks = renderTime * 240.0;
        // A long double round would be exact but the domain here is bounded: a timeline is seconds,
        // not centuries, and the wrap makes the far end harmless anyway.
        const auto whole = static_cast<std::int64_t>(ticks < 0.0 ? ticks - 0.5 : ticks + 0.5);
        return static_cast<std::uint32_t>(static_cast<std::uint64_t>(whole) & 0xFFFFFFu);
    }
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
    // Like seek, but the *next* tick returns `renderTime` itself with frame index 0 (offline
    // renders: frame f is at start + f / fps).
    void restartAt(double renderTime);
    [[nodiscard]] double fps() const { return fps_; }

private:
    FrameTime current_{};
    double fps_;
    double startTime_;
    bool started_ = false;
};

} // namespace avgen
