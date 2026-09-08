#pragma once

// Deterministic synthetic test signals (ADR-009). Everything here is seeded and pure so golden
// values are stable across platforms.

#include "core/rng.hpp"

#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

namespace avgen::testsupport {

inline std::vector<float> sine(float frequencyHz, std::uint32_t sampleRate, std::size_t frames,
                               float amplitude = 1.0f, float phase = 0.0f) {
    std::vector<float> out(frames);
    const double w = 2.0 * std::numbers::pi * static_cast<double>(frequencyHz) / static_cast<double>(sampleRate);
    for (std::size_t i = 0; i < frames; ++i) {
        out[i] = amplitude * static_cast<float>(std::sin(w * static_cast<double>(i) + phase));
    }
    return out;
}

inline std::vector<float> silence(std::size_t frames) { return std::vector<float>(frames, 0.0f); }

inline std::vector<float> whiteNoise(std::size_t frames, float amplitude = 1.0f, std::uint64_t seed = 1) {
    Rng rng(seed);
    std::vector<float> out(frames);
    for (auto& s : out) {
        s = amplitude * (rng.nextFloat() * 2.0f - 1.0f);
    }
    return out;
}

// Unit impulses every `periodFrames` frames starting at `offsetFrames`, else zero.
inline std::vector<float> impulseTrain(std::size_t frames, std::size_t periodFrames, std::size_t offsetFrames = 0,
                                       float amplitude = 1.0f) {
    std::vector<float> out(frames, 0.0f);
    for (std::size_t i = offsetFrames; i < frames; i += periodFrames) {
        out[i] = amplitude;
    }
    return out;
}

// Click track: short decaying noise bursts at the given tempo. Bursts are `clickFrames` long.
inline std::vector<float> clickTrack(float bpm, std::uint32_t sampleRate, std::size_t frames,
                                     std::size_t clickFrames = 256, float amplitude = 0.9f, std::uint64_t seed = 7) {
    std::vector<float> out(frames, 0.0f);
    const double period = 60.0 / static_cast<double>(bpm) * static_cast<double>(sampleRate);
    Rng rng(seed);
    for (double t = 0.0; t < static_cast<double>(frames); t += period) {
        const auto start = static_cast<std::size_t>(t);
        for (std::size_t i = 0; i < clickFrames && start + i < frames; ++i) {
            const float env = std::exp(-static_cast<float>(i) / (static_cast<float>(clickFrames) * 0.25f));
            out[start + i] = amplitude * env * (rng.nextFloat() * 2.0f - 1.0f);
        }
    }
    return out;
}

// Sums signals of equal length.
inline std::vector<float> mix(const std::vector<float>& a, const std::vector<float>& b) {
    std::vector<float> out(a.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        out[i] = a[i] + (i < b.size() ? b[i] : 0.0f);
    }
    return out;
}

// Interleaves a mono signal into N identical channels.
inline std::vector<float> interleave(const std::vector<float>& mono, std::uint32_t channels) {
    std::vector<float> out(mono.size() * channels);
    for (std::size_t i = 0; i < mono.size(); ++i) {
        for (std::uint32_t c = 0; c < channels; ++c) {
            out[i * channels + c] = mono[i];
        }
    }
    return out;
}

} // namespace avgen::testsupport
