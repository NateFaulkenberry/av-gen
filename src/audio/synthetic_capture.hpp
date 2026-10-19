#pragma once

// The synthetic capture backend (stems-multichannel, ADR-1521): TEST AND BENCHMARK ONLY. It is never in a device list
// and says so in its name and id ("synthetic:<n>"); nothing in the application constructs it except `avgen
// --live-profile --input synthetic:<n>` (a benchmark flag).
//
// Each channel carries a known signal so a test can tell whether channel 1 reached the source it should, and only
// that source:
//   - channel c (0-based) is a sine at `toneHz(c)` -- a different frequency per channel, under the gain;
//   - plus, when `impulses` is set, a click (an 8-sample full-scale burst) every `impulsePeriodFrames` frames,
//     offset by c * `impulseStaggerFrames` so the channels' events never coincide.
// Driven either by the caller (`pump`, on the caller's thread standing in for the IO thread -- tests) or by its own
// thread in real time (`realtime`, for benchmarks and live use), in blocks of `blockFrames`.

#include "audio/capture_backend.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

namespace avgen::audio {

struct SyntheticCaptureConfig {
    std::uint32_t channels = 8;
    std::uint32_t sampleRate = 48000;
    std::uint32_t blockFrames = 128;
    float amplitude = 0.2f;
    bool impulses = true;
    std::uint32_t impulsePeriodFrames = 24000; // 0.5 s at 48 kHz
    std::uint32_t impulseStaggerFrames = 1500; // 31.25 ms between channels
    bool realtime = false;
    std::uint32_t silentChannelMask = 0;       // bit c set: channel c is digital silence (an unplugged input)
    [[nodiscard]] static float toneHz(std::uint32_t channel) { return 110.0f * static_cast<float>(channel + 1); }
};

class SyntheticCapture final : public CaptureBackend {
public:
    explicit SyntheticCapture(SyntheticCaptureConfig config);
    ~SyntheticCapture() override;

    [[nodiscard]] Result<CaptureFormat> open(CaptureSink& sink) override;
    [[nodiscard]] Result<void> start() override;
    void stop() override;
    [[nodiscard]] std::string deviceName() const override;
    [[nodiscard]] std::string deviceId() const override;
    [[nodiscard]] bool connected() const override { return connected_.load(); }
    [[nodiscard]] bool formatChanged() const override { return formatChanged_.load(); }

    // Delivers `blocks` blocks to the sink on the calling thread (only when not `realtime`).
    void pump(std::uint32_t blocks);
    // Pretend the device went away / changed format (the next pump or tick still delivers; the sink decides).
    void disconnect() { connected_.store(false); }
    void changeFormat() { formatChanged_.store(true); }
    // Skips `frames` of the device's sample time before the next block: a gap the sink must notice.
    void skip(std::uint32_t frames) { skip_ += frames; }
    // The first frame of the click on `channel` at or after `frame`, for tests.
    [[nodiscard]] std::uint64_t impulseFrame(std::uint32_t channel, std::uint64_t k) const;
    [[nodiscard]] const SyntheticCaptureConfig& config() const { return config_; }

private:
    void deliver();

    SyntheticCaptureConfig config_;
    CaptureSink* sink_ = nullptr;
    std::vector<std::vector<float>> buffers_;
    std::vector<const float*> pointers_;
    std::uint64_t frame_ = 0;
    std::uint64_t skip_ = 0;
    std::atomic<bool> connected_{true};
    std::atomic<bool> formatChanged_{false};
    std::atomic<bool> running_{false};
    std::jthread thread_;
};

} // namespace avgen::audio
