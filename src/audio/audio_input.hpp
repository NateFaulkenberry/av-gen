#pragma once

// Live audio input (milestone 1.1, ADR-021): a miniaudio capture device whose callback downmixes
// to mono and feeds an AnalysisStream, so the analysis thread, signals and everything downstream
// work on a microphone or line input exactly as on a file. No playback, no position: the clock is
// the wall clock (RealtimeClock) and `framesCaptured()` is the sample-accurate stream position.

#include "audio/analysis_stream.hpp"
#include "core/error.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace avgen::audio {

struct AudioDeviceInfo {
    std::string name;
    std::string id;          // backend id string; "" = default
    bool isDefault = false;
};
// Capture devices known to miniaudio (may be empty on machines without inputs / permissions).
[[nodiscard]] std::vector<AudioDeviceInfo> listCaptureDevices();

class AudioInput {
public:
    AudioInput();
    ~AudioInput();
    AudioInput(const AudioInput&) = delete;
    AudioInput& operator=(const AudioInput&) = delete;

    // Opens the capture device whose name contains `deviceName` (case-insensitive; "" = default)
    // at `sampleRate` (0 = the device's native rate) and starts capturing. Errors: no device,
    // permission denied, backend failure.
    [[nodiscard]] Result<void> open(const std::string& deviceName = "", std::uint32_t sampleRate = 0);
    void close();
    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] std::string deviceName() const;
    [[nodiscard]] std::uint32_t sampleRate() const;
    [[nodiscard]] std::uint32_t channels() const;
    // Mono frames delivered to the stream so far (the analysis position).
    [[nodiscard]] std::uint64_t framesCaptured() const;
    // Peak of the last callback block (0..1), for a level meter.
    [[nodiscard]] float lastPeak() const;
    [[nodiscard]] AnalysisStream& analysisStream();
    // Software gain applied before the stream (1 = unity).
    void setGain(float gain);
    [[nodiscard]] float gain() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace avgen::audio
