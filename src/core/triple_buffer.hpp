#pragma once

// Lock-free single-writer single-reader triple buffer. The writer always has a free slot; the
// reader always sees the most recently published value. Used to hand AnalysisFrames from the
// analysis thread to the render thread without blocking either.

#include <array>
#include <atomic>
#include <cstdint>

namespace avgen {

template <typename T>
class TripleBuffer {
public:
    TripleBuffer() = default;

    // Writer side: fill the back buffer, then publish it.
    T& back() { return slots_[backIndex_]; }
    void publish() {
        // Pack: bits 0-1 = index of the newest slot, bit 2 = "new data" flag.
        const std::uint8_t packed = static_cast<std::uint8_t>(backIndex_ | kFresh);
        const std::uint8_t previous = middle_.exchange(packed, std::memory_order_acq_rel);
        backIndex_ = previous & kIndexMask;
    }

    // Reader side: returns true if a newer value was swapped into front().
    bool acquire() {
        std::uint8_t middle = middle_.load(std::memory_order_acquire);
        if ((middle & kFresh) == 0) {
            return false;
        }
        const std::uint8_t packed = static_cast<std::uint8_t>(frontIndex_);
        middle = middle_.exchange(packed, std::memory_order_acq_rel);
        frontIndex_ = middle & kIndexMask;
        return true;
    }
    const T& front() const { return slots_[frontIndex_]; }
    T& front() { return slots_[frontIndex_]; }

private:
    static constexpr std::uint8_t kIndexMask = 0x3;
    static constexpr std::uint8_t kFresh = 0x4;
    std::array<T, 3> slots_{};
    std::uint8_t backIndex_ = 0;
    std::uint8_t frontIndex_ = 1;
    std::atomic<std::uint8_t> middle_{2};
};

} // namespace avgen
