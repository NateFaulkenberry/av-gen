#include "audio/audio_input.hpp"

#include "audio/capture_coreaudio.hpp"
#include "core/log.hpp"

#include <miniaudio.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <numeric>
#include <optional>
#include <string>
#include <vector>

namespace avgen::audio {

// Threading model
// ---------------
// UI thread: open / close / gain / queries. Audio thread: dataCallback only. The callback
// downmixes the interleaved capture block to mono, applies the gain, tracks the block peak,
// writes the AnalysisStream and advances the frame counter. It never allocates, locks, logs
// or calls miniaudio. The discontinuity marker (frame 0) is placed in open() while the device
// is still down, so it is race-free; there are no seeks on a live input.

namespace {

constexpr float kMaxGain = 16.0f; // +24 dB; enough for quiet line inputs without runaway

std::string lowercase(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool nameMatches(const std::string& filter, const char* name) {
    return filter.empty() || lowercase(name).find(lowercase(filter)) != std::string::npos;
}

std::string deviceIdString(ma_backend backend, const ma_device_id& id, const char* name) {
#if defined(__APPLE__)
    if (backend == ma_backend_coreaudio) {
        return id.coreaudio;
    }
#endif
    (void)backend;
    (void)id;
    return name;
}

// Runs `fn(info, backend)` for every capture device known to a throw-away context.
template <typename Fn>
void forEachCaptureDevice(Fn&& fn) {
    ma_context context;
    if (ma_context_init(nullptr, 0, nullptr, &context) != MA_SUCCESS) {
        return;
    }
    ma_device_info* playback = nullptr;
    ma_uint32 playbackCount = 0;
    ma_device_info* capture = nullptr;
    ma_uint32 captureCount = 0;
    if (ma_context_get_devices(&context, &playback, &playbackCount, &capture, &captureCount) == MA_SUCCESS) {
        for (ma_uint32 i = 0; i < captureCount; ++i) {
            fn(capture[i], context.backend);
        }
    }
    ma_context_uninit(&context);
}

} // namespace

std::uint32_t AudioDeviceInfo::channels() const {
    return std::accumulate(streamChannels.begin(), streamChannels.end(), 0u);
}

std::vector<AudioDeviceInfo> listCaptureDevices() {
    std::vector<AudioDeviceInfo> devices;
    forEachCaptureDevice([&](const ma_device_info& info, ma_backend backend) {
        AudioDeviceInfo d;
        d.name = info.name;
        d.id = deviceIdString(backend, info.id, info.name);
        d.isDefault = info.isDefault != MA_FALSE;
        devices.push_back(std::move(d));
    });
    // ADR-1522: the HAL's own stream layout and rate, by UID (empty off macOS).
    for (const CoreAudioInputDevice& hal : coreAudioInputDevices()) {
        for (AudioDeviceInfo& d : devices) {
            if (d.id == hal.uid) {
                d.streamChannels = hal.streamChannels;
                d.sampleRate = hal.sampleRate;
            }
        }
    }
    // At most one capture device is the default, and the backend does not always agree. Observed on
    // a machine with a USB interface attached: CoreAudio reported **both** "Volt 876" and "MacBook
    // Pro Microphone" as default, from a capture-only enumeration where two defaults cannot be
    // meaningful.
    //
    // This matters beyond tidiness, because "open the default device" resolves by scanning this list
    // for the flag: with two set, which microphone the engine opens depends on enumeration order,
    // which is not a decision anybody made. The first is kept -- it is the one the backend offers
    // first and, on the machine above, the one the system actually defaults to -- and the rest are
    // cleared, so the list means what the rest of the engine assumes it means.
    bool seen = false;
    for (AudioDeviceInfo& d : devices) {
        if (!d.isDefault) {
            continue;
        }
        if (seen) {
            log::warn("audio input: '{}' also claims to be the default capture device; keeping the "
                      "first one the backend offered",
                      d.name);
            d.isDefault = false;
        }
        seen = true;
    }
    return devices;
}

struct AudioInput::Impl {
    static constexpr std::size_t kScratchFrames = 8192;
    static constexpr std::size_t kAnalysisCapacity = std::size_t{1} << 16;
    // ADR-1521: a source's ring. 16384 floats is 341 ms of mono at 48 kHz (170 ms of a pair's two floats per frame):
    // 32 hops of headroom for an analysis thread that measured ~1 ms per hop for eight sources.
    static constexpr std::size_t kSourceCapacity = std::size_t{1} << 14;
    static constexpr std::size_t kMaxMeteredChannels = 64;

    ma_device device{};
    bool deviceReady = false;
    std::string deviceNameCache;
    std::uint32_t sampleRate = 0;
    std::uint32_t channels = 0;

    std::atomic<std::uint64_t> frames{0};
    std::atomic<float> peak{0.0f};
    std::atomic<float> gain{1.0f};

    AnalysisStream stream{kAnalysisCapacity};
    std::vector<float> monoScratch; // kScratchFrames

    // ---- ADR-1521: the sources and the backend path ----
    struct Tap {
        InputSource source;
        bool present = false;                    // the device has its channel(s)
        std::unique_ptr<AnalysisStream> stream;  // present only
        std::atomic<float> peak{0.0f};
        bool pending = false;                    // IO thread only: a block was dropped; mark before the next one
    };
    std::vector<InputSource> sources;
    std::vector<std::unique_ptr<Tap>> taps;      // replaced only while no callback runs
    std::vector<float> tapScratch;               // 2 * kScratchFrames
    std::array<std::atomic<float>, kMaxMeteredChannels> channelPeaks{};
    std::atomic<std::uint64_t> blocks{0}, droppedBlocks{0}, gaps{0};
    std::atomic<double> callbackMean{0.0}, callbackMax{0.0};
    double expectedSampleTime = -1.0;            // IO thread only

    struct Sink final : CaptureSink {
        Impl* self = nullptr;
        void onBlock(const float* const* channelData, std::uint32_t channelCount, std::uint32_t frameCount,
                     const CaptureBlockTiming& timing) override {
            self->capturePlanar(channelData, channelCount, frameCount, timing);
        }
    } sink;
    std::unique_ptr<CaptureBackend> backend;
    std::string deviceIdCache;

    static void dataCallback(ma_device* device, void* /*output*/, const void* input, ma_uint32 frameCount);
    void capture(const float* in, std::uint32_t frameCount);
    void capturePlanar(const float* const* in, std::uint32_t channelCount, std::uint32_t frameCount,
                       const CaptureBlockTiming& timing);
    void feedTap(Tap& tap, const float* a, const float* b, std::size_t stride, std::uint32_t frameCount, float g,
                 std::uint64_t firstFrame);
    void noteCallbackTime(std::chrono::steady_clock::time_point started);
    void rebuildTaps();
    void stopIo();
    void startIo();
    void release();
};

void AudioInput::Impl::dataCallback(ma_device* device, void* /*output*/, const void* input,
                                    ma_uint32 frameCount) {
    auto* self = static_cast<Impl*>(device->pUserData);
    const auto* in = static_cast<const float*>(input);
    if (self == nullptr || in == nullptr) {
        return;
    }
    self->capture(in, frameCount);
}

// REAL-TIME: no allocation, no locks, no logging, no exceptions, no miniaudio calls.
void AudioInput::Impl::capture(const float* in, std::uint32_t frameCount) {
    const auto started = std::chrono::steady_clock::now();
    const std::uint64_t firstFrame = frames.load(std::memory_order_relaxed);
    const float g = gain.load(std::memory_order_relaxed);
    const float scale = g / static_cast<float>(channels);
    float blockPeak = 0.0f;
    std::uint32_t remaining = frameCount;
    const float* src = in;

    while (remaining > 0) {
        const std::size_t chunk = std::min<std::size_t>(remaining, kScratchFrames);
        for (std::size_t f = 0; f < chunk; ++f) {
            const float* frame = src + f * channels;
            float sum = 0.0f;
            for (std::uint32_t c = 0; c < channels; ++c) {
                sum += frame[c];
            }
            const float v = sum * scale;
            monoScratch[f] = v;
            blockPeak = std::max(blockPeak, std::fabs(v));
        }
        stream.write(std::span<const float>(monoScratch.data(), chunk));
        src += chunk * channels;
        remaining -= static_cast<std::uint32_t>(chunk);
    }

    // ADR-1521: the per-channel meters and the sources, from the same interleaved block.
    const std::uint32_t metered = std::min<std::uint32_t>(channels, kMaxMeteredChannels);
    for (std::uint32_t c = 0; c < metered; ++c) {
        float p = 0.0f;
        for (std::uint32_t f = 0; f < frameCount; ++f) {
            p = std::max(p, std::fabs(in[static_cast<std::size_t>(f) * channels + c]));
        }
        channelPeaks[c].store(std::min(p * g, 1.0f), std::memory_order_relaxed);
    }
    for (const auto& tap : taps) {
        if (tap->present) {
            const float* a = in + tap->source.channel;
            feedTap(*tap, a, tap->source.pair ? a + 1 : nullptr, channels, frameCount, g, firstFrame);
        }
    }

    frames.fetch_add(frameCount, std::memory_order_acq_rel);
    peak.store(std::min(blockPeak, 1.0f), std::memory_order_relaxed);
    noteCallbackTime(started);
}

// REAL-TIME, as capture(). The backend path: planar blocks. The main mono stream is the same average of every
// channel the interleaved path writes.
void AudioInput::Impl::capturePlanar(const float* const* in, std::uint32_t channelCount, std::uint32_t frameCount,
                                     const CaptureBlockTiming& timing) {
    const auto started = std::chrono::steady_clock::now();
    if (in == nullptr || channelCount == 0 || channelCount != channels) {
        return; // a format the capture was not opened with: the owner reopens on formatChanged()
    }
    const std::uint64_t firstFrame = frames.load(std::memory_order_relaxed);
    if (timing.hasSampleTime) {
        if (expectedSampleTime >= 0.0 && timing.sampleTime != expectedSampleTime) {
            gaps.fetch_add(1, std::memory_order_relaxed);
            for (const auto& tap : taps) {
                tap->pending = true; // the next block starts somewhere else: the analysis must not splice across
            }
        }
        expectedSampleTime = timing.sampleTime + static_cast<double>(frameCount);
    }
    const float g = gain.load(std::memory_order_relaxed);
    const float scale = g / static_cast<float>(channelCount);
    float blockPeak = 0.0f;
    std::uint32_t done = 0;
    while (done < frameCount) {
        const std::size_t chunk = std::min<std::size_t>(frameCount - done, kScratchFrames);
        for (std::size_t f = 0; f < chunk; ++f) {
            float sum = 0.0f;
            for (std::uint32_t c = 0; c < channelCount; ++c) {
                sum += in[c][done + f];
            }
            const float v = sum * scale;
            monoScratch[f] = v;
            blockPeak = std::max(blockPeak, std::fabs(v));
        }
        stream.write(std::span<const float>(monoScratch.data(), chunk));
        done += static_cast<std::uint32_t>(chunk);
    }
    const std::uint32_t metered = std::min<std::uint32_t>(channelCount, kMaxMeteredChannels);
    for (std::uint32_t c = 0; c < metered; ++c) {
        float p = 0.0f;
        for (std::uint32_t f = 0; f < frameCount; ++f) {
            p = std::max(p, std::fabs(in[c][f]));
        }
        channelPeaks[c].store(std::min(p * g, 1.0f), std::memory_order_relaxed);
    }
    for (const auto& tap : taps) {
        if (tap->present) {
            const std::uint32_t c = tap->source.channel;
            feedTap(*tap, in[c], tap->source.pair ? in[c + 1] : nullptr, 1, frameCount, g, firstFrame);
        }
    }
    frames.fetch_add(frameCount, std::memory_order_acq_rel);
    peak.store(std::min(blockPeak, 1.0f), std::memory_order_relaxed);
    noteCallbackTime(started);
}

// REAL-TIME. A block goes into a source's ring whole or not at all: a dropped block marks a discontinuity at the
// next block's true frame, so the analysis resets there instead of splicing two separated pieces of audio into one
// window. A pair is written as interleaved (mid, side) frames.
void AudioInput::Impl::feedTap(Tap& tap, const float* a, const float* b, std::size_t stride, std::uint32_t frameCount,
                               float g, std::uint64_t firstFrame) {
    const std::size_t per = b != nullptr ? 2 : 1;
    if (tap.stream->freeSpace() < static_cast<std::size_t>(frameCount) * per) {
        tap.pending = true;
        droppedBlocks.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (tap.pending) {
        tap.stream->markDiscontinuity(firstFrame);
        tap.pending = false;
    }
    float p = 0.0f;
    std::uint32_t done = 0;
    while (done < frameCount) {
        const std::size_t chunk = std::min<std::size_t>(frameCount - done, kScratchFrames);
        if (b != nullptr) {
            for (std::size_t f = 0; f < chunk; ++f) {
                const float x = a[(done + f) * stride] * g;
                const float y = b[(done + f) * stride] * g;
                const float mid = 0.5f * (x + y);
                tapScratch[2 * f] = mid;
                tapScratch[2 * f + 1] = 0.5f * (x - y);
                p = std::max(p, std::fabs(mid));
            }
        } else {
            for (std::size_t f = 0; f < chunk; ++f) {
                const float x = a[(done + f) * stride] * g;
                tapScratch[f] = x;
                p = std::max(p, std::fabs(x));
            }
        }
        tap.stream->write(std::span<const float>(tapScratch.data(), chunk * per));
        done += static_cast<std::uint32_t>(chunk);
    }
    tap.peak.store(std::min(p, 1.0f), std::memory_order_relaxed);
}

void AudioInput::Impl::noteCallbackTime(std::chrono::steady_clock::time_point started) {
    const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - started).count();
    const std::uint64_t n = blocks.fetch_add(1, std::memory_order_relaxed) + 1;
    const double mean = callbackMean.load(std::memory_order_relaxed);
    callbackMean.store(mean + (us - mean) / static_cast<double>(std::min<std::uint64_t>(n, 4096)),
                       std::memory_order_relaxed);
    if (us > callbackMax.load(std::memory_order_relaxed)) {
        callbackMax.store(us, std::memory_order_relaxed);
    }
}

// Not real-time: called only while no callback can run (before start, or between stopIo and startIo). A source that is
// unchanged keeps its tap and its stream, so taking one source away or adding one does not disturb the others: their
// samples stay contiguous and their analysis carries on.
void AudioInput::Impl::rebuildTaps() {
    std::vector<std::unique_ptr<Tap>> previous = std::move(taps);
    taps.clear();
    const std::uint64_t at = frames.load(std::memory_order_acquire);
    for (const InputSource& source : sources) {
        const bool present =
            channels > 0 && source.channel < channels && (!source.pair || source.channel + 1 < channels);
        const auto kept = std::find_if(previous.begin(), previous.end(), [&](const std::unique_ptr<Tap>& t) {
            return t && t->present && present && t->source.channel == source.channel && t->source.pair == source.pair;
        });
        if (kept != previous.end()) {
            (*kept)->source = source; // a new label or key; the same samples
            taps.push_back(std::move(*kept));
            continue;
        }
        auto tap = std::make_unique<Tap>();
        tap->source = source;
        tap->present = present;
        if (tap->present) {
            tap->stream = std::make_unique<AnalysisStream>(source.pair ? 2 * kSourceCapacity : kSourceCapacity);
            tap->stream->markDiscontinuity(at);
        }
        taps.push_back(std::move(tap));
    }
    tapScratch.assign(2 * kScratchFrames, 0.0f);
}

void AudioInput::Impl::stopIo() {
    if (backend) {
        backend->stop();
    } else if (deviceReady) {
        ma_device_stop(&device);
    }
}

void AudioInput::Impl::startIo() {
    if (backend) {
        static_cast<void>(backend->start());
    } else if (deviceReady) {
        ma_device_start(&device);
    }
}

void AudioInput::Impl::release() {
    if (backend) {
        backend->stop(); // the callback cannot be running afterwards
        backend.reset();
    }
    if (deviceReady) {
        ma_device_uninit(&device); // stops the device; the callback cannot be running afterwards
        deviceReady = false;
    }
    taps.clear();
    deviceIdCache.clear();
    for (auto& p : channelPeaks) {
        p.store(0.0f, std::memory_order_relaxed);
    }
    deviceNameCache.clear();
    sampleRate = 0;
    channels = 0;
    peak.store(0.0f, std::memory_order_relaxed);
}

AudioInput::AudioInput()
    : impl_(std::make_unique<Impl>()) {
    impl_->sink.self = impl_.get();
}

AudioInput::~AudioInput() {
    impl_->release();
}

Result<void> AudioInput::open(const std::string& deviceName, std::uint32_t sampleRate) {
    Impl& s = *impl_;
    s.release();

    std::optional<ma_device_id> selected;
    std::string selectedName;
    if (!deviceName.empty()) {
        // An exact id (a saved Core Audio UID) first, then a name substring, as it always matched.
        forEachCaptureDevice([&](const ma_device_info& info, ma_backend backend) {
            if (!selected && deviceIdString(backend, info.id, info.name) == deviceName) {
                selected = info.id;
                selectedName = info.name;
            }
        });
        forEachCaptureDevice([&](const ma_device_info& info, ma_backend /*backend*/) {
            if (!selected && nameMatches(deviceName, info.name)) {
                selected = info.id;
                selectedName = info.name;
            }
        });
        if (!selected) {
            return fail("no capture device matches '{}'", deviceName);
        }
    }

    ma_device_config config = ma_device_config_init(ma_device_type_capture);
    config.capture.format = ma_format_f32;
    config.capture.channels = 0; // native channel count; downmixed in the callback
    config.capture.pDeviceID = selected ? &*selected : nullptr;
    config.sampleRate = sampleRate; // 0 = native; otherwise miniaudio resamples
    // ADR-1025: a short period. The analysis runs a hop (512 samples) as soon as its last sample lands, and at
    // miniaudio's default 10 ms period that sample waited for the rest of its 480-frame block: measured on a
    // BlackHole loopback, 128 frames takes several milliseconds off audio -> bus for a live instrument. The callback
    // only downmixes and copies, so 375 calls a second cost nothing measurable.
    config.periodSizeInFrames = 128;
    config.dataCallback = &Impl::dataCallback;
    config.pUserData = &s;

    const ma_result initResult = ma_device_init(nullptr, &config, &s.device);
    if (initResult != MA_SUCCESS) {
        const std::string reason = ma_result_description(initResult);
        s.release();
        return fail("cannot open capture device{}: {}", selectedName.empty() ? "" : " '" + selectedName + "'",
                    reason);
    }
    s.deviceReady = true;
    s.deviceNameCache = s.device.capture.name;
    s.sampleRate = s.device.sampleRate;
    s.channels = std::max<std::uint32_t>(s.device.capture.channels, 1);
    s.monoScratch.assign(Impl::kScratchFrames, 0.0f);
    s.frames.store(0, std::memory_order_release);
    s.peak.store(0.0f, std::memory_order_relaxed);
    // The producer is idle (device not started): the next samples start at frame 0.
    s.stream.markDiscontinuity(0);
    s.expectedSampleTime = -1.0;
    s.rebuildTaps(); // ADR-1521: the sources set before the open (release() cleared the last device's taps)
    s.blocks.store(0);
    s.droppedBlocks.store(0);
    s.gaps.store(0);
    s.callbackMean.store(0.0);
    s.callbackMax.store(0.0);

    const ma_result startResult = ma_device_start(&s.device);
    if (startResult != MA_SUCCESS) {
        const std::string reason = ma_result_description(startResult);
        s.release();
        return fail("cannot start capture device: {}", reason);
    }
    log::info("audio input '{}' opened: {} Hz ({} Hz native), {} ch in, {}-frame period", s.deviceNameCache,
              s.sampleRate, s.device.capture.internalSampleRate, s.channels,
              s.device.capture.internalPeriodSizeInFrames);
    return {};
}

void AudioInput::close() {
    impl_->release();
}

bool AudioInput::isOpen() const {
    return impl_->deviceReady || impl_->backend != nullptr;
}

std::string AudioInput::deviceName() const {
    return impl_->deviceNameCache;
}

std::uint32_t AudioInput::sampleRate() const {
    return impl_->sampleRate;
}

std::uint32_t AudioInput::channels() const {
    return impl_->channels;
}

std::uint64_t AudioInput::framesCaptured() const {
    return impl_->frames.load(std::memory_order_acquire);
}

float AudioInput::lastPeak() const {
    return impl_->peak.load(std::memory_order_relaxed);
}

AnalysisStream& AudioInput::analysisStream() {
    return impl_->stream;
}

void AudioInput::setGain(float gain) {
    if (!(gain >= 0.0f)) { // also catches NaN
        gain = 0.0f;
    }
    impl_->gain.store(std::min(gain, kMaxGain), std::memory_order_relaxed);
}

float AudioInput::gain() const {
    return impl_->gain.load(std::memory_order_relaxed);
}

Result<void> AudioInput::openBackend(std::unique_ptr<CaptureBackend> backend) {
    Impl& s = *impl_;
    s.release();
    if (!backend) {
        return fail("no capture backend");
    }
    auto format = backend->open(s.sink);
    if (!format) {
        return std::unexpected(format.error());
    }
    s.backend = std::move(backend);
    s.deviceNameCache = s.backend->deviceName();
    s.deviceIdCache = s.backend->deviceId();
    s.sampleRate = format->sampleRate;
    s.channels = std::max<std::uint32_t>(format->channels, 1);
    s.monoScratch.assign(Impl::kScratchFrames, 0.0f);
    s.frames.store(0, std::memory_order_release);
    s.peak.store(0.0f, std::memory_order_relaxed);
    s.blocks.store(0);
    s.droppedBlocks.store(0);
    s.gaps.store(0);
    s.callbackMean.store(0.0);
    s.callbackMax.store(0.0);
    s.stream.markDiscontinuity(0);
    s.expectedSampleTime = -1.0;
    s.rebuildTaps();
    if (auto r = s.backend->start(); !r) {
        const std::string reason = r.error().message;
        s.release();
        return fail("cannot start capture device: {}", reason);
    }
    log::info("audio input '{}' opened through its backend: {} Hz, {} ch, {} source(s)", s.deviceNameCache,
              s.sampleRate, s.channels, s.sources.size());
    return {};
}

void AudioInput::setSources(std::vector<InputSource> sources) {
    Impl& s = *impl_;
    if (sources == s.sources) {
        return;
    }
    const bool running = s.backend != nullptr || s.deviceReady;
    if (running) {
        s.stopIo(); // after this no callback runs until startIo, so the taps can be replaced
    }
    s.sources = std::move(sources);
    if (running) {
        s.rebuildTaps();
        s.startIo();
    }
}

const std::vector<InputSource>& AudioInput::sources() const {
    return impl_->sources;
}

AnalysisStream* AudioInput::sourceStream(std::size_t index) {
    const auto& taps = impl_->taps;
    return index < taps.size() && taps[index]->present ? taps[index]->stream.get() : nullptr;
}

float AudioInput::sourcePeak(std::size_t index) const {
    const auto& taps = impl_->taps;
    return index < taps.size() ? taps[index]->peak.load(std::memory_order_relaxed) : 0.0f;
}

float AudioInput::channelPeak(std::uint32_t channel) const {
    return channel < impl_->channels && channel < Impl::kMaxMeteredChannels
               ? impl_->channelPeaks[channel].load(std::memory_order_relaxed)
               : 0.0f;
}

AudioInput::Stats AudioInput::stats() const {
    const Impl& s = *impl_;
    Stats out;
    out.blocks = s.blocks.load(std::memory_order_relaxed);
    out.droppedBlocks = s.droppedBlocks.load(std::memory_order_relaxed);
    out.gaps = s.gaps.load(std::memory_order_relaxed);
    out.overloads = s.backend ? s.backend->overloads() : 0;
    out.callbackMeanMicros = s.callbackMean.load(std::memory_order_relaxed);
    out.callbackMaxMicros = s.callbackMax.load(std::memory_order_relaxed);
    return out;
}

bool AudioInput::connected() const {
    return impl_->backend ? impl_->backend->connected() : impl_->deviceReady;
}

bool AudioInput::formatChanged() const {
    return impl_->backend && impl_->backend->formatChanged();
}

std::string AudioInput::deviceId() const {
    return impl_->deviceIdCache;
}

} // namespace avgen::audio
