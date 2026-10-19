#pragma once

// The seam between a capture device and `AudioInput` (stems-multichannel, ADR-1521).
//
// A backend owns one opened input device and calls its sink from the device's IO thread with PLANAR blocks: one
// pointer per device channel, in the device's own channel order. Three implementations:
//   - Core Audio AUHAL (capture_coreaudio.*, macOS): every channel of every input stream, with the HAL's sample time;
//   - synthetic (synthetic_capture.*): test and benchmark only, never in a device list -- known signals per channel;
//   - (the existing miniaudio capture is not a backend: AudioInput keeps it as it was for the plain stereo path, which
//     is what the owner asked for, and splits its interleaved blocks itself.)
//
// REAL-TIME contract for the sink: `onBlock` runs on the IO thread and must not allocate, lock, log or block.

#include "core/error.hpp"

#include <cstdint>
#include <string>

namespace avgen::audio {

struct CaptureBlockTiming {
    // The device's own sample counter for the first frame of the block, when the backend has one (AUHAL's
    // AudioTimeStamp::mSampleTime). A jump from the previous block's end is a gap: input the device produced and
    // nobody received.
    bool hasSampleTime = false;
    double sampleTime = 0.0;
};

class CaptureSink {
public:
    virtual ~CaptureSink() = default;
    // `channels[c]` holds `frames` samples of device channel c, for c < channelCount.
    virtual void onBlock(const float* const* channels, std::uint32_t channelCount, std::uint32_t frames,
                         const CaptureBlockTiming& timing) = 0;
};

struct CaptureFormat {
    std::uint32_t sampleRate = 0;
    std::uint32_t channels = 0;
};

class CaptureBackend {
public:
    virtual ~CaptureBackend() = default;
    // Opens the device (not started). `sink` must outlive the backend's running state.
    [[nodiscard]] virtual Result<CaptureFormat> open(CaptureSink& sink) = 0;
    [[nodiscard]] virtual Result<void> start() = 0;
    // Stops IO; when it returns, `onBlock` is not running and will not run again until start().
    virtual void stop() = 0;
    [[nodiscard]] virtual std::string deviceName() const = 0;
    // A stable identity for the device (Core Audio's UID; "synthetic:<n>" for the test backend).
    [[nodiscard]] virtual std::string deviceId() const = 0;
    // False once the device has gone (unplugged, destroyed). Callbacks may continue after that and are ignored.
    [[nodiscard]] virtual bool connected() const { return true; }
    // HAL IO cycles that overran their deadline (kAudioDeviceProcessorOverload), when the backend can tell.
    [[nodiscard]] virtual std::uint64_t overloads() const { return 0; }
    // The device's format changed under the capture (rate or channel count); the owner reopens.
    [[nodiscard]] virtual bool formatChanged() const { return false; }
};

} // namespace avgen::audio
