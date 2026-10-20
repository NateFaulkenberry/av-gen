#pragma once

// A fixed-capacity single-producer single-consumer queue: no allocation after construction, no locks. The live Sonic
// timbre snapshots (ADR-1025) and the live sources' response feeds (ADR-1580) cross threads through it.

#include <array>
#include <atomic>
#include <cstddef>

namespace avgen {

template <class T, std::size_t Capacity>
class SpscQueue {
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0, "capacity must be a power of two");

public:
    // Producer. False (and the item is not queued) when full.
    bool push(const T& item) {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        if (head - tail_.load(std::memory_order_acquire) >= Capacity) {
            return false;
        }
        items_[head & (Capacity - 1)] = item;
        head_.store(head + 1, std::memory_order_release);
        return true;
    }
    // Consumer.
    bool pop(T& out) {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            return false;
        }
        out = items_[tail & (Capacity - 1)];
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }
    [[nodiscard]] std::size_t size() const {
        return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire);
    }

private:
    std::array<T, Capacity> items_{};
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
};

} // namespace avgen
