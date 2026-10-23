#pragma once

// ADR-1609: the display's vsync on the host clock, so the live pacer can place a frame's GPU end just before one.
//
// On macOS a CVDisplayLink for the display a window is on calls back once per refresh on its own thread; the callback
// stores the most recent vsync's host time and the measured refresh period, and the main thread reads them without
// waiting. Elsewhere (and when the link cannot be made) `available()` is false and nothing is aligned.

#include <atomic>
#include <cstdint>
#include <memory>

namespace avgen::platform {

class VsyncClock {
public:
    // The display holding this point (global desktop points, as DisplayInfo's bounds).
    static std::unique_ptr<VsyncClock> forDisplayAt(int x, int y);
    ~VsyncClock();
    VsyncClock(const VsyncClock&) = delete;
    VsyncClock& operator=(const VsyncClock&) = delete;

    [[nodiscard]] bool available() const { return lastVsyncNs_.load(std::memory_order_acquire) != 0; }
    // The most recent vsync on the host clock (sonic::hostNowNs's: mach_absolute_time in ns); 0 before the first.
    [[nodiscard]] std::uint64_t lastVsyncNs() const { return lastVsyncNs_.load(std::memory_order_acquire); }
    // The refresh period the link measured (ns); 0 when unknown.
    [[nodiscard]] std::uint64_t periodNs() const { return periodNs_.load(std::memory_order_acquire); }
    [[nodiscard]] std::uint32_t displayId() const { return displayId_; }

    // Called by the link's thread.
    void tick(std::uint64_t vsyncNs, std::uint64_t periodNs) {
        lastVsyncNs_.store(vsyncNs, std::memory_order_release);
        if (periodNs > 0) {
            periodNs_.store(periodNs, std::memory_order_release);
        }
    }

private:
    VsyncClock() = default;
    void* link_ = nullptr;
    std::uint32_t displayId_ = 0;
    std::atomic<std::uint64_t> lastVsyncNs_{0};
    std::atomic<std::uint64_t> periodNs_{0};
};

} // namespace avgen::platform
