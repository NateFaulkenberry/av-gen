#pragma once

// The Core Audio capture backend (stems-multichannel, ADR-1522): a narrow AUHAL input unit, macOS only.
//
// Why not miniaudio here (measured, docs/prototypes/stems-multichannel/02-research-report.md §3.2): on a device whose
// inputs span several Core Audio streams -- an aggregate device, and possibly the Volt 876's analog and ADAT banks --
// miniaudio 0.11.25 opens only the first stream's channels, and asked for more it fills them with copies. This backend
// asks AUHAL for every channel of the device (the sum over `kAudioDevicePropertyStreamConfiguration`) as float32,
// non-interleaved, at the device's own rate (TN2091: AUHAL does not convert rates), and hands each block on with the
// HAL's sample time, so a gap is visible. The plain stereo path (no sources) stays on miniaudio, as the owner asked.
//
// Lifecycle: listeners on the device (`DeviceIsAlive`, `NominalSampleRate`, input `StreamConfiguration`,
// `ProcessorOverload`) and on the system's device list set atomics only; the engine reads them each frame
// (`connected()`, `formatChanged()`, `overloads()`) and reopens on the main thread. Measured on a private aggregate
// destroyed mid-capture: callbacks continued after the device was gone, so the callbacks are never the signal.

#include "audio/capture_backend.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace avgen::audio {

// Every input-capable Core Audio device: its UID, name, channels per input stream and rates. Empty off macOS.
struct CoreAudioInputDevice {
    std::string uid;
    std::string name;
    std::vector<std::uint32_t> streamChannels; // one entry per input stream, in channel order
    double sampleRate = 0.0;                   // nominal
    [[nodiscard]] std::uint32_t channels() const;
};
[[nodiscard]] std::vector<CoreAudioInputDevice> coreAudioInputDevices();
// Whether a device with this UID exists now (cheap: one HAL translation).
[[nodiscard]] bool coreAudioDevicePresent(const std::string& uid);

class CoreAudioCapture final : public CaptureBackend {
public:
    explicit CoreAudioCapture(std::string uid, std::uint32_t periodFrames = 128);
    ~CoreAudioCapture() override;
    CoreAudioCapture(const CoreAudioCapture&) = delete;
    CoreAudioCapture& operator=(const CoreAudioCapture&) = delete;

    [[nodiscard]] Result<CaptureFormat> open(CaptureSink& sink) override;
    [[nodiscard]] Result<void> start() override;
    void stop() override;
    [[nodiscard]] std::string deviceName() const override { return name_; }
    [[nodiscard]] std::string deviceId() const override { return uid_; }
    [[nodiscard]] bool connected() const override;
    [[nodiscard]] std::uint64_t overloads() const override;
    [[nodiscard]] bool formatChanged() const override;

    struct State; // the IO and listener state (capture_coreaudio.cpp)

private:
    std::string uid_;
    std::string name_;
    std::uint32_t periodFrames_;
    std::unique_ptr<State> state_;
};

} // namespace avgen::audio
