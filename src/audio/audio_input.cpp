#include "audio/audio_input.hpp"

#include "core/log.hpp"

#include <miniaudio.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstring>
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

std::vector<AudioDeviceInfo> listCaptureDevices() {
    std::vector<AudioDeviceInfo> devices;
    forEachCaptureDevice([&](const ma_device_info& info, ma_backend backend) {
        AudioDeviceInfo d;
        d.name = info.name;
        d.id = deviceIdString(backend, info.id, info.name);
        d.isDefault = info.isDefault != MA_FALSE;
        devices.push_back(std::move(d));
    });
    return devices;
}

struct AudioInput::Impl {
    static constexpr std::size_t kScratchFrames = 8192;
    static constexpr std::size_t kAnalysisCapacity = std::size_t{1} << 16;

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

    static void dataCallback(ma_device* device, void* /*output*/, const void* input, ma_uint32 frameCount);
    void capture(const float* in, std::uint32_t frameCount);
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

    frames.fetch_add(frameCount, std::memory_order_acq_rel);
    peak.store(std::min(blockPeak, 1.0f), std::memory_order_relaxed);
}

void AudioInput::Impl::release() {
    if (deviceReady) {
        ma_device_uninit(&device); // stops the device; the callback cannot be running afterwards
        deviceReady = false;
    }
    deviceNameCache.clear();
    sampleRate = 0;
    channels = 0;
    peak.store(0.0f, std::memory_order_relaxed);
}

AudioInput::AudioInput()
    : impl_(std::make_unique<Impl>()) {}

AudioInput::~AudioInput() {
    impl_->release();
}

Result<void> AudioInput::open(const std::string& deviceName, std::uint32_t sampleRate) {
    Impl& s = *impl_;
    s.release();

    std::optional<ma_device_id> selected;
    std::string selectedName;
    if (!deviceName.empty()) {
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

    const ma_result startResult = ma_device_start(&s.device);
    if (startResult != MA_SUCCESS) {
        const std::string reason = ma_result_description(startResult);
        s.release();
        return fail("cannot start capture device: {}", reason);
    }
    log::info("audio input '{}' opened: {} Hz ({} Hz native), {} ch in", s.deviceNameCache, s.sampleRate,
              s.device.capture.internalSampleRate, s.channels);
    return {};
}

void AudioInput::close() {
    impl_->release();
}

bool AudioInput::isOpen() const {
    return impl_->deviceReady;
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

} // namespace avgen::audio
