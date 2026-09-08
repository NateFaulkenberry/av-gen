// TEMPORARY STUB — the audio module owns the real implementation; discarded on merge.
//
// Minimal but correct single-producer single-consumer implementation so the analysis module and
// its tests link and run in this worktree.

#include "audio/analysis_stream.hpp"

#include <algorithm>

namespace avgen::audio {

AnalysisStream::AnalysisStream(std::size_t sampleCapacity)
    : ring_(sampleCapacity) {}

void AnalysisStream::markDiscontinuity(std::uint64_t frameIndex) {
    const std::size_t head = markerHead_.load(std::memory_order_relaxed);
    const std::size_t tail = markerTail_.load(std::memory_order_acquire);
    if (head - tail >= kMaxMarkers) {
        return; // marker queue full: drop (the consumer is far behind anyway)
    }
    markers_[head % kMaxMarkers] = Marker{ring_.writtenTotal(), frameIndex};
    markerHead_.store(head + 1, std::memory_order_release);
}

std::size_t AnalysisStream::write(std::span<const float> samples) {
    return ring_.write(samples);
}

AnalysisStream::ReadResult AnalysisStream::read(std::span<float> out) {
    ReadResult result;
    std::size_t tail = markerTail_.load(std::memory_order_relaxed);
    const std::size_t head = markerHead_.load(std::memory_order_acquire);
    const std::uint64_t position = ring_.readTotal();

    std::size_t limit = out.size();
    bool pending = false;
    std::uint64_t pendingStart = 0;
    while (tail != head) {
        const Marker& marker = markers_[tail % kMaxMarkers];
        if (marker.ringPosition > position) {
            limit = std::min<std::size_t>(limit, marker.ringPosition - position);
            break;
        }
        // Marker at or before the read position (before: samples were drained past it).
        pendingStart = marker.frameIndex + (position - marker.ringPosition);
        pending = true;
        ++tail;
    }
    if (pending) {
        if (ring_.available() == 0) {
            return result; // keep the marker until there are samples to attach it to
        }
        markerTail_.store(tail, std::memory_order_release);
        result.discontinuity = true;
        result.startFrame = pendingStart;
    }
    result.count = ring_.read(out.first(limit));
    return result;
}

void AnalysisStream::drain() {
    ring_.drain();
}

} // namespace avgen::audio
