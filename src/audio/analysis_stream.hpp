#pragma once

// Single-producer single-consumer sample stream from the audio callback to the analysis thread.
// Besides samples it carries "discontinuity markers" (seek, new source) so the consumer can stamp
// every sample with its exact PCM frame index. Lock-free; no allocation after construction.

#include "core/ring_buffer.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <span>

namespace avgen::audio {

class AnalysisStream {
public:
    explicit AnalysisStream(std::size_t sampleCapacity);

    // ---- producer (audio callback) ----
    // Declares that the next samples written start at PCM frame `frameIndex`.
    void markDiscontinuity(std::uint64_t frameIndex);
    // Writes mono samples; drops samples if the consumer is behind.
    std::size_t write(std::span<const float> samples);

    // ---- consumer (analysis thread) ----
    struct ReadResult {
        std::size_t count = 0;         // samples copied into out
        bool discontinuity = false;    // true if the samples in out start at a new position
        std::uint64_t startFrame = 0;  // PCM frame index of out[0] when discontinuity is true
    };
    // Reads up to out.size() samples but never across a discontinuity marker.
    ReadResult read(std::span<float> out);
    void drain();
    [[nodiscard]] std::size_t available() const { return ring_.available(); }

private:
    struct Marker {
        std::uint64_t ringPosition = 0; // writtenTotal() at the time of the mark
        std::uint64_t frameIndex = 0;
    };
    static constexpr std::size_t kMaxMarkers = 32;

    SpscRingBuffer ring_;
    std::array<Marker, kMaxMarkers> markers_{};
    std::atomic<std::size_t> markerHead_{0};
    std::atomic<std::size_t> markerTail_{0};
};

} // namespace avgen::audio
