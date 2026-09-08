#pragma once

// Plays an AudioFile through the default output device via miniaudio (ADR-003). The real-time
// callback only copies samples, applies volume, advances an atomic play-head and feeds the
// AnalysisStream. Position is derived from the play-head frame index, never wall time.

#include "audio/analysis_stream.hpp"
#include "audio/audio_file.hpp"
#include "core/error.hpp"

#include <atomic>
#include <cstdint>
#include <memory>

namespace avgen::audio {

class AudioPlayer {
public:
    AudioPlayer();
    ~AudioPlayer();
    AudioPlayer(const AudioPlayer&) = delete;
    AudioPlayer& operator=(const AudioPlayer&) = delete;

    // Installs a source and (re)creates the output device at the file's sample rate. Playback is
    // stopped and the position reset to 0. Passing nullptr releases the device.
    [[nodiscard]] Result<void> setSource(std::shared_ptr<const AudioFile> file);
    [[nodiscard]] std::shared_ptr<const AudioFile> source() const;
    [[nodiscard]] bool hasSource() const;

    [[nodiscard]] Result<void> play();
    void pause();
    void stop(); // pause + seek 0
    [[nodiscard]] bool isPlaying() const;
    [[nodiscard]] bool atEnd() const;

    void seekFrames(std::uint64_t frame);
    void seekSeconds(double seconds);
    [[nodiscard]] std::uint64_t positionFrames() const;
    [[nodiscard]] double positionSeconds() const;
    [[nodiscard]] double durationSeconds() const;
    [[nodiscard]] std::uint32_t sampleRate() const;

    void setVolume(float volume); // 0..1 linear, clamped
    [[nodiscard]] float volume() const;

    // Mono samples at the source sample rate, stamped with discontinuities on seek/setSource.
    [[nodiscard]] AnalysisStream& analysisStream();

    // Device-level information for the UI.
    [[nodiscard]] std::uint32_t deviceSampleRate() const;
    [[nodiscard]] std::string deviceName() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace avgen::audio
