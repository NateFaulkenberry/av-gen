#include "audio/analysis_stream.hpp"

#include <algorithm>

namespace avgen::audio {

// Threading model
// ---------------
// The sample ring is an SPSC ring; the marker queue is a second SPSC ring of fixed capacity
// (kMaxMarkers, a power of two) indexed by the monotonic counters markerHead_/markerTail_.
//
// The producer publishes a marker (release on markerHead_) BEFORE it writes the samples that
// follow the mark (release on the sample ring's head). The consumer therefore snapshots the
// sample ring first (acquire) and the marker queue second (acquire): any marker whose position
// lies inside the snapshot of readable samples was published before those samples became
// visible, so it is guaranteed to be seen. Reading in the other order could let the consumer
// pull samples across a marker it has not observed yet.
//
// If a marker's ringPosition is behind readTotal() (only possible after drain()), the implied
// frame index of the next sample is frameIndex + (readTotal() - ringPosition), since the samples
// after a mark are contiguous in the source.

AnalysisStream::AnalysisStream(std::size_t sampleCapacity)
    : ring_(sampleCapacity) {}

void AnalysisStream::markDiscontinuity(std::uint64_t frameIndex) {
    const std::size_t head = markerHead_.load(std::memory_order_relaxed);
    const std::size_t tail = markerTail_.load(std::memory_order_acquire);
    if (head - tail >= kMaxMarkers) {
        // Queue full: the consumer is far behind (32 unconsumed seeks). Dropping the mark is the
        // only allocation-free option; the consumer resyncs on the next mark that fits.
        return;
    }
    markers_[head % kMaxMarkers] = Marker{ring_.writtenTotal(), frameIndex};
    markerHead_.store(head + 1, std::memory_order_release);
}

std::size_t AnalysisStream::write(std::span<const float> samples) {
    return ring_.write(samples);
}

AnalysisStream::ReadResult AnalysisStream::read(std::span<float> out) {
    ReadResult result;

    // Snapshot the samples first, then the markers (see the ordering note above).
    const std::size_t readable = ring_.available();
    if (readable == 0 || out.empty()) {
        // Markers are consumed only together with samples, so a discontinuity is never reported
        // on an empty read and can never be lost by a consumer that ignores empty results.
        return result;
    }
    const std::size_t readPos = ring_.readTotal();
    std::size_t tail = markerTail_.load(std::memory_order_relaxed);
    const std::size_t head = markerHead_.load(std::memory_order_acquire);

    // Pop every marker at or behind the read position; the most recent one wins.
    while (tail != head) {
        const Marker& marker = markers_[tail % kMaxMarkers];
        if (marker.ringPosition > readPos) {
            break;
        }
        result.discontinuity = true;
        result.startFrame = marker.frameIndex + (readPos - marker.ringPosition);
        ++tail;
    }
    if (tail != markerTail_.load(std::memory_order_relaxed)) {
        markerTail_.store(tail, std::memory_order_release);
    }

    // Never read across the next marker (which now lies strictly ahead, so limit >= 1).
    std::size_t limit = std::min(out.size(), readable);
    if (tail != head) {
        const std::size_t untilMarker = markers_[tail % kMaxMarkers].ringPosition - readPos;
        limit = std::min(limit, untilMarker);
    }
    result.count = ring_.read(out.first(limit));
    return result;
}

void AnalysisStream::drain() {
    // Consumer side only. Discard buffered samples and every marker that refers to a discarded
    // position, except the most recent such marker: it stays queued (behind the read position)
    // so the next read() still reports a discontinuity with the correct implied frame index for
    // whatever the producer writes next. Markers at or past the new read position are pending
    // and untouched.
    ring_.drain();
    const std::size_t readPos = ring_.readTotal();
    std::size_t tail = markerTail_.load(std::memory_order_relaxed);
    const std::size_t head = markerHead_.load(std::memory_order_acquire);
    while (tail != head && tail + 1 != head && markers_[(tail + 1) % kMaxMarkers].ringPosition <= readPos) {
        ++tail;
    }
    if (tail != markerTail_.load(std::memory_order_relaxed)) {
        markerTail_.store(tail, std::memory_order_release);
    }
}

} // namespace avgen::audio
