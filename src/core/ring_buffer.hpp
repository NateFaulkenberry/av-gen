#pragma once

// Single-producer single-consumer lock-free ring buffer for float samples. The audio callback
// is the producer; the analysis thread is the consumer. Capacity is rounded up to a power of two.
// No allocation after construction.

#include <atomic>
#include <cstddef>
#include <span>
#include <vector>

namespace avgen {

class SpscRingBuffer {
public:
    explicit SpscRingBuffer(std::size_t minCapacity);

    // Producer. Returns the number of samples actually written (drops the rest if full).
    std::size_t write(std::span<const float> samples);

    // Consumer. Returns the number of samples actually read.
    std::size_t read(std::span<float> out);

    // Consumer. Discards everything currently buffered.
    void drain();

    [[nodiscard]] std::size_t available() const;   // samples readable (consumer view)
    [[nodiscard]] std::size_t freeSpace() const;   // samples writable (producer view)
    [[nodiscard]] std::size_t capacity() const { return capacity_; }

    // Monotonic counters (never wrap in practice): total samples ever written / read. They let a
    // producer describe positions in the stream ("a discontinuity starts at sample N").
    [[nodiscard]] std::size_t writtenTotal() const { return head_.load(std::memory_order_acquire); }
    [[nodiscard]] std::size_t readTotal() const { return tail_.load(std::memory_order_acquire); }

private:
    std::vector<float> data_;
    std::size_t capacity_;
    std::size_t mask_;
    alignas(64) std::atomic<std::size_t> head_{0}; // write index (producer)
    alignas(64) std::atomic<std::size_t> tail_{0}; // read index (consumer)
};

} // namespace avgen
