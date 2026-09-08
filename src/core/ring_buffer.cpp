#include "core/ring_buffer.hpp"

#include <algorithm>
#include <bit>

namespace avgen {

SpscRingBuffer::SpscRingBuffer(std::size_t minCapacity)
    : capacity_(std::bit_ceil(std::max<std::size_t>(minCapacity, 2))), mask_(capacity_ - 1) {
    data_.assign(capacity_, 0.0f);
}

std::size_t SpscRingBuffer::write(std::span<const float> samples) {
    const std::size_t head = head_.load(std::memory_order_relaxed);
    const std::size_t tail = tail_.load(std::memory_order_acquire);
    const std::size_t free = capacity_ - (head - tail);
    const std::size_t n = std::min(free, samples.size());
    for (std::size_t i = 0; i < n; ++i) {
        data_[(head + i) & mask_] = samples[i];
    }
    head_.store(head + n, std::memory_order_release);
    return n;
}

std::size_t SpscRingBuffer::read(std::span<float> out) {
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    const std::size_t head = head_.load(std::memory_order_acquire);
    const std::size_t avail = head - tail;
    const std::size_t n = std::min(avail, out.size());
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = data_[(tail + i) & mask_];
    }
    tail_.store(tail + n, std::memory_order_release);
    return n;
}

void SpscRingBuffer::drain() {
    const std::size_t head = head_.load(std::memory_order_acquire);
    tail_.store(head, std::memory_order_release);
}

std::size_t SpscRingBuffer::available() const {
    return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_relaxed);
}

std::size_t SpscRingBuffer::freeSpace() const {
    return capacity_ - (head_.load(std::memory_order_relaxed) - tail_.load(std::memory_order_acquire));
}

} // namespace avgen
