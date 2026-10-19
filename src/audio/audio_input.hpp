#pragma once

// Live audio input (milestone 1.1, ADR-021): a miniaudio capture device whose callback downmixes
// to mono and feeds an AnalysisStream, so the analysis thread, signals and everything downstream
// work on a microphone or line input exactly as on a file. No playback, no position: the clock is
// the wall clock (RealtimeClock) and `framesCaptured()` is the sample-accurate stream position.

#include "audio/analysis_stream.hpp"
#include "audio/capture_backend.hpp"
#include "audio/input_source.hpp"
#include "core/error.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace avgen::audio {

struct AudioDeviceInfo {
    std::string name;
    std::string id;          // backend id string (Core Audio: the device UID, stable across replugs); "" = default
    bool isDefault = false;
    // ADR-1522, macOS: the device's input channels per Core Audio stream (one entry per stream) and nominal rate, from
    // the HAL -- miniaudio's own count is the first stream's only. Empty / 0 where unknown (other platforms).
    std::vector<std::uint32_t> streamChannels;
    double sampleRate = 0.0;
    [[nodiscard]] std::uint32_t channels() const;
};
// Capture devices known to miniaudio (may be empty on machines without inputs / permissions).
[[nodiscard]] std::vector<AudioDeviceInfo> listCaptureDevices();

class AudioInput {
public:
    AudioInput();
    ~AudioInput();
    AudioInput(const AudioInput&) = delete;
    AudioInput& operator=(const AudioInput&) = delete;

    // Opens the capture device whose id is `deviceName`, or whose name contains it (case-insensitive; "" = default)
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

    // ---- analysis sources on the device's channels (stems-multichannel, ADR-1521) ----------------------------------
    //
    // Each source gets its own stream, written by the same callback that writes the main mono stream above, so every
    // source shares the device's sample clock. The main stream is unchanged by any of this: the average of every
    // channel, as it always was.
    //
    // Sets the sources (UI thread). A running capture is stopped, reconfigured and restarted; nothing the callback
    // reads changes while it runs. A source whose channel (or pair partner) the device does not have gets no stream
    // and `sourceStream` returns null for it -- the engine publishes it inactive and the UI says why.
    void setSources(std::vector<InputSource> sources);
    [[nodiscard]] const std::vector<InputSource>& sources() const;
    // Which channels the main stream (what audio.* analyses) averages: empty = every channel, as it always was. A
    // mixer's main out on inputs 7/8 beside stems on 1-6 is {6, 7}. Channels the device lacks are ignored; if none
    // remain it is every channel again. Applies on the backend path (openBackend); the plain miniaudio path always
    // averages every channel. Like setSources, a running capture is stopped and restarted around it.
    void setMainChannels(std::vector<std::uint32_t> channels);
    [[nodiscard]] const std::vector<std::uint32_t>& mainChannels() const;
    // The source's stream: mono samples, or for a pair interleaved (mid, side) frames -- two floats per frame.
    [[nodiscard]] AnalysisStream* sourceStream(std::size_t index);
    [[nodiscard]] float sourcePeak(std::size_t index) const;  // the last block's peak, 0..1
    // Per device channel: the last block's peak (for the channel meters), 0 beyond channels().
    [[nodiscard]] float channelPeak(std::uint32_t channel) const;

    struct Stats {
        std::uint64_t blocks = 0;          // callbacks
        std::uint64_t droppedBlocks = 0;   // source blocks dropped whole because the analysis fell behind
        std::uint64_t gaps = 0;            // the device's sample time jumped (input nobody received)
        std::uint64_t overloads = 0;       // the HAL's IO cycle overran its deadline
        double callbackMeanMicros = 0.0;   // the callback's own time
        double callbackMaxMicros = 0.0;
    };
    [[nodiscard]] Stats stats() const;
    // The last block delivered: the stream frame just past it, and when (steady_clock nanoseconds). For latency
    // measurement: when a given frame of the input arrived.
    struct BlockStamp {
        std::uint64_t frameEnd = 0;
        std::int64_t steadyNs = 0;
    };
    [[nodiscard]] BlockStamp lastBlock() const;
    // Stops the device's IO and keeps everything else (the device name and id, the sources, the stats), so a
    // disconnected device stops costing anything while the owner waits for it to come back. open/openBackend restart.
    void stopCapture();
    // Whether the device is still there (false after an unplug), and whether its format changed under the capture.
    [[nodiscard]] bool connected() const;
    [[nodiscard]] bool formatChanged() const;
    // The open device's stable id ("" for the plain miniaudio path, which selects by name).
    [[nodiscard]] std::string deviceId() const;

    // Opens a capture through a backend instead of the plain miniaudio path: the Core Audio backend for multichannel
    // sources on macOS, or the synthetic backend in tests and benchmarks. Starts it.
    [[nodiscard]] Result<void> openBackend(std::unique_ptr<CaptureBackend> backend);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace avgen::audio
