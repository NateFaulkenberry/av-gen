#include "audio/waveform.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::audio {

std::pair<float, float> WaveformSummary::peak(double fromSeconds, double toSeconds) const {
    if (minValues.empty() || !(toSeconds > fromSeconds)) {
        return {0.0f, 0.0f};
    }
    const double perSecond = static_cast<double>(bucketsPerSecond);
    const auto count = static_cast<double>(minValues.size());
    // Half-open in buckets as well as in seconds, and the end is rounded *up* so a range narrower
    // than a bucket still covers the one it starts in. Without that, zooming in far enough would
    // make every column empty and the waveform would disappear exactly when it is most useful.
    double first = std::floor(fromSeconds * perSecond);
    double last = std::ceil(toSeconds * perSecond);
    if (last <= 0.0 || first >= count) {
        return {0.0f, 0.0f}; // entirely before or after the file
    }
    first = std::max(first, 0.0);
    last = std::min(last, count);
    if (last <= first) {
        return {0.0f, 0.0f};
    }
    const auto begin = static_cast<std::size_t>(first);
    const auto end = static_cast<std::size_t>(last);
    const auto lo = std::min_element(minValues.begin() + static_cast<std::ptrdiff_t>(begin),
                                     minValues.begin() + static_cast<std::ptrdiff_t>(end));
    const auto hi = std::max_element(maxValues.begin() + static_cast<std::ptrdiff_t>(begin),
                                     maxValues.begin() + static_cast<std::ptrdiff_t>(end));
    return {*lo, *hi};
}

WaveformSummary summarise(const AudioFile& file, std::uint32_t bucketsPerSecond) {
    WaveformSummary out;
    out.bucketsPerSecond = std::clamp(bucketsPerSecond, 1u, 2000u);
    out.sampleRate = file.sampleRate();
    out.durationSeconds = file.durationSeconds();

    const std::span<const float> mono = file.mono();
    if (mono.empty() || file.sampleRate() == 0) {
        return out;
    }
    const double samplesPerBucket =
        static_cast<double>(file.sampleRate()) / static_cast<double>(out.bucketsPerSecond);
    // Round up, so the last partial bucket exists rather than the tail being dropped. A file that
    // ends mid-bucket is the normal case, not an edge one.
    const auto buckets = static_cast<std::size_t>(
        std::ceil(static_cast<double>(mono.size()) / samplesPerBucket));
    out.minValues.assign(buckets, 0.0f);
    out.maxValues.assign(buckets, 0.0f);

    for (std::size_t b = 0; b < buckets; ++b) {
        // Bucket edges are computed from the bucket index rather than accumulated, so rounding does
        // not drift across a long file and the last bucket ends exactly at the last sample.
        const auto begin = static_cast<std::size_t>(static_cast<double>(b) * samplesPerBucket);
        auto end = static_cast<std::size_t>(static_cast<double>(b + 1) * samplesPerBucket);
        end = std::min(end, mono.size());
        if (begin >= end) {
            continue; // a bucket finer than one sample; leave it flat rather than read backwards
        }
        const auto [lo, hi] = std::minmax_element(mono.begin() + static_cast<std::ptrdiff_t>(begin),
                                                  mono.begin() + static_cast<std::ptrdiff_t>(end));
        out.minValues[b] = *lo;
        out.maxValues[b] = *hi;
    }
    return out;
}

} // namespace avgen::audio
