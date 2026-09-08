#pragma once

// Decoded audio file held entirely in memory as float32 PCM (ADR-003). Seeking, analysis and
// offline evaluation all read from this buffer by frame index.

#include "core/error.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace avgen::audio {

class AudioFile {
public:
    // Decodes WAV/FLAC/MP3 (and anything else miniaudio's decoder handles) to float32.
    static Result<AudioFile> load(const std::filesystem::path& path);

    // Builds a file from interleaved samples; used by tests and generators. channels >= 1.
    static AudioFile fromInterleaved(std::vector<float> interleaved, std::uint32_t channels,
                                     std::uint32_t sampleRate);

    // Writes a 32-bit float WAV. Used by tests to produce fixtures on the fly.
    [[nodiscard]] Result<void> writeWav(const std::filesystem::path& path) const;

    [[nodiscard]] std::uint32_t sampleRate() const { return sampleRate_; }
    [[nodiscard]] std::uint32_t channels() const { return channels_; }
    [[nodiscard]] std::uint64_t frameCount() const { return frameCount_; }
    [[nodiscard]] double durationSeconds() const;
    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

    // Interleaved samples: frameCount() * channels().
    [[nodiscard]] std::span<const float> interleaved() const { return interleaved_; }
    // Mono downmix (average of channels): frameCount() samples. Computed once at load.
    [[nodiscard]] std::span<const float> mono() const { return mono_; }

    // Copies interleaved frames [start, start + n) into out (n = out.size() / channels()).
    // Frames past the end are zero-filled. Returns the number of real (non-padded) frames.
    std::uint64_t readFrames(std::uint64_t start, std::span<float> outInterleaved) const;

private:
    AudioFile() = default;
    void buildMono();

    std::filesystem::path path_;
    std::vector<float> interleaved_;
    std::vector<float> mono_;
    std::uint32_t sampleRate_ = 0;
    std::uint32_t channels_ = 0;
    std::uint64_t frameCount_ = 0;
};

} // namespace avgen::audio
