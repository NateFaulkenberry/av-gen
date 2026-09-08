#include "audio/audio_player.hpp"

#include "core/log.hpp"

#include <miniaudio.h>

#include <algorithm>
#include <cmath>
#include <span>
#include <string>
#include <vector>

namespace avgen::audio {

// Threading model
// ---------------
// UI thread: setSource / play / pause / seek / volume / queries. Audio thread: dataCallback only.
// The device is created in setSource and keeps running until the source is released; when not
// playing the callback emits silence and pushes nothing into the analysis stream. Shared state
// is a handful of atomics. The callback never allocates, locks, logs or calls miniaudio.
//
// Seeks are handed to the callback through seekTarget_/seekPending_ so the discontinuity marker
// is placed by the producer side of the AnalysisStream at exactly the right ring position.
// The UI thread also stores playhead_ directly so positionFrames() is right immediately; the
// callback may overwrite it for at most one buffer before it consumes the pending seek.

struct AudioPlayer::Impl {
    static constexpr std::size_t kScratchFrames = 8192;
    static constexpr std::size_t kAnalysisCapacity = std::size_t{1} << 16;
    static constexpr std::uint32_t kOutputChannels = 2;

    std::shared_ptr<const AudioFile> file;
    const AudioFile* filePtr = nullptr; // raw view for the callback; set only while the device is down
    std::uint32_t channels = 0;
    std::uint64_t frameCount = 0;
    std::uint32_t sampleRate = 0;

    ma_device device{};
    bool deviceReady = false;
    std::string deviceNameCache;

    std::atomic<std::uint64_t> playhead{0};
    std::atomic<bool> playing{false};
    std::atomic<bool> atEnd{false};
    std::atomic<float> volume{1.0f};
    std::atomic<std::uint64_t> seekTarget{0};
    std::atomic<bool> seekPending{false};

    AnalysisStream stream{kAnalysisCapacity};
    std::vector<float> scratch;     // interleaved source frames, kScratchFrames * channels
    std::vector<float> monoScratch; // kScratchFrames

    static void dataCallback(ma_device* device, void* output, const void* /*input*/, ma_uint32 frames);
    void render(float* out, std::uint32_t frames);
    void releaseDevice();
};

void AudioPlayer::Impl::dataCallback(ma_device* device, void* output, const void* /*input*/,
                                     ma_uint32 frames) {
    auto* self = static_cast<Impl*>(device->pUserData);
    auto* out = static_cast<float*>(output);
    if (self == nullptr || out == nullptr) {
        return;
    }
    self->render(out, frames);
}

// REAL-TIME: no allocation, no locks, no logging, no exceptions, no miniaudio calls.
void AudioPlayer::Impl::render(float* out, std::uint32_t frames) {
    std::fill_n(out, static_cast<std::size_t>(frames) * kOutputChannels, 0.0f);

    if (seekPending.exchange(false, std::memory_order_acq_rel)) {
        const std::uint64_t target = seekTarget.load(std::memory_order_acquire);
        playhead.store(target, std::memory_order_release);
        atEnd.store(false, std::memory_order_release);
        stream.markDiscontinuity(target);
    }
    if (!playing.load(std::memory_order_acquire) || filePtr == nullptr) {
        return;
    }

    const float gain = volume.load(std::memory_order_relaxed);
    const float monoScale = 1.0f / static_cast<float>(channels);
    std::uint64_t local = playhead.load(std::memory_order_relaxed);
    std::uint32_t remaining = frames;
    float* dst = out;
    bool reachedEnd = false;

    while (remaining > 0 && !reachedEnd) {
        const std::size_t chunk = std::min<std::size_t>(remaining, kScratchFrames);
        const std::span<float> src(scratch.data(), chunk * channels);
        const std::uint64_t got = filePtr->readFrames(local, src); // zero-fills past the end
        const std::size_t n = static_cast<std::size_t>(got);

        for (std::size_t f = 0; f < n; ++f) {
            const float* frame = src.data() + f * channels;
            float left = frame[0];
            float right = channels >= 2 ? frame[1] : frame[0];
            float sum = 0.0f;
            for (std::uint32_t c = 0; c < channels; ++c) {
                sum += frame[c];
            }
            dst[f * kOutputChannels] = left * gain;
            dst[f * kOutputChannels + 1] = right * gain;
            // The analysis stream carries the unscaled downmix so live features match the offline
            // analysis of AudioFile::mono() regardless of monitor volume (ADR-012).
            monoScratch[f] = sum * monoScale;
        }
        if (n > 0) {
            stream.write(std::span<const float>(monoScratch.data(), n));
        }

        local += got;
        dst += n * kOutputChannels;
        remaining -= static_cast<std::uint32_t>(n);
        if (n < chunk) {
            reachedEnd = true;
        }
    }

    playhead.store(local, std::memory_order_release);
    if (reachedEnd) {
        playing.store(false, std::memory_order_release);
        atEnd.store(true, std::memory_order_release);
    }
}

void AudioPlayer::Impl::releaseDevice() {
    if (deviceReady) {
        ma_device_uninit(&device); // stops the device; the callback cannot be running afterwards
        deviceReady = false;
    }
    playing.store(false, std::memory_order_release);
    atEnd.store(false, std::memory_order_release);
    seekPending.store(false, std::memory_order_release);
    playhead.store(0, std::memory_order_release);
    seekTarget.store(0, std::memory_order_release);
    filePtr = nullptr;
    file.reset();
    channels = 0;
    frameCount = 0;
    sampleRate = 0;
    deviceNameCache.clear();
}

AudioPlayer::AudioPlayer()
    : impl_(std::make_unique<Impl>()) {}

AudioPlayer::~AudioPlayer() {
    impl_->releaseDevice();
}

Result<void> AudioPlayer::setSource(std::shared_ptr<const AudioFile> file) {
    Impl& s = *impl_;
    s.releaseDevice();
    if (!file) {
        return {};
    }
    if (file->channels() == 0 || file->sampleRate() == 0) {
        return fail("audio source has no channels or sample rate");
    }

    s.file = std::move(file);
    s.filePtr = s.file.get();
    s.channels = s.file->channels();
    s.frameCount = s.file->frameCount();
    s.sampleRate = s.file->sampleRate();
    s.scratch.assign(Impl::kScratchFrames * s.channels, 0.0f);
    s.monoScratch.assign(Impl::kScratchFrames, 0.0f);
    // The producer is idle (device down), so marking here is race-free: the next samples the
    // consumer sees start at frame 0 of the new source.
    s.stream.markDiscontinuity(0);

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = Impl::kOutputChannels;
    config.sampleRate = s.sampleRate; // miniaudio resamples to the device's native rate
    config.dataCallback = &Impl::dataCallback;
    config.pUserData = &s;

    const ma_result initResult = ma_device_init(nullptr, &config, &s.device);
    if (initResult != MA_SUCCESS) {
        const std::string reason = ma_result_description(initResult);
        s.releaseDevice();
        return fail("cannot open playback device: {}", reason);
    }
    s.deviceReady = true;
    s.deviceNameCache = s.device.playback.name;

    const ma_result startResult = ma_device_start(&s.device);
    if (startResult != MA_SUCCESS) {
        const std::string reason = ma_result_description(startResult);
        s.releaseDevice();
        return fail("cannot start playback device: {}", reason);
    }
    log::info("audio device '{}' opened: {} Hz requested, {} Hz native, {} ch out", s.deviceNameCache,
              s.device.sampleRate, s.device.playback.internalSampleRate, s.device.playback.channels);
    return {};
}

std::shared_ptr<const AudioFile> AudioPlayer::source() const {
    return impl_->file;
}

bool AudioPlayer::hasSource() const {
    return impl_->filePtr != nullptr;
}

Result<void> AudioPlayer::play() {
    Impl& s = *impl_;
    if (!s.deviceReady || s.filePtr == nullptr) {
        return fail("no audio source loaded");
    }
    if (s.atEnd.load(std::memory_order_acquire)) {
        seekFrames(0);
    }
    // Re-mark on every resume so the analysis stream knows where the next samples start.
    s.seekTarget.store(s.playhead.load(std::memory_order_acquire), std::memory_order_release);
    s.seekPending.store(true, std::memory_order_release);
    s.playing.store(true, std::memory_order_release);
    return {};
}

void AudioPlayer::pause() {
    impl_->playing.store(false, std::memory_order_release);
}

void AudioPlayer::stop() {
    pause();
    seekFrames(0);
}

bool AudioPlayer::isPlaying() const {
    return impl_->playing.load(std::memory_order_acquire);
}

bool AudioPlayer::atEnd() const {
    return impl_->atEnd.load(std::memory_order_acquire);
}

void AudioPlayer::seekFrames(std::uint64_t frame) {
    Impl& s = *impl_;
    const std::uint64_t target = std::min(frame, s.frameCount);
    s.playhead.store(target, std::memory_order_release);
    s.atEnd.store(false, std::memory_order_release);
    s.seekTarget.store(target, std::memory_order_release);
    s.seekPending.store(true, std::memory_order_release);
}

void AudioPlayer::seekSeconds(double seconds) {
    const Impl& s = *impl_;
    if (s.sampleRate == 0) {
        return;
    }
    const double maxFrames = static_cast<double>(s.frameCount);
    double frames = std::floor(seconds * static_cast<double>(s.sampleRate));
    if (!(frames > 0.0)) { // also catches NaN
        frames = 0.0;
    }
    seekFrames(static_cast<std::uint64_t>(std::min(frames, maxFrames)));
}

std::uint64_t AudioPlayer::positionFrames() const {
    return impl_->playhead.load(std::memory_order_acquire);
}

double AudioPlayer::positionSeconds() const {
    const Impl& s = *impl_;
    return s.sampleRate == 0 ? 0.0
                             : static_cast<double>(positionFrames()) / static_cast<double>(s.sampleRate);
}

double AudioPlayer::durationSeconds() const {
    return impl_->file ? impl_->file->durationSeconds() : 0.0;
}

std::uint32_t AudioPlayer::sampleRate() const {
    return impl_->sampleRate;
}

void AudioPlayer::setVolume(float volume) {
    impl_->volume.store(std::clamp(volume, 0.0f, 1.0f), std::memory_order_relaxed);
}

float AudioPlayer::volume() const {
    return impl_->volume.load(std::memory_order_relaxed);
}

AnalysisStream& AudioPlayer::analysisStream() {
    return impl_->stream;
}

std::uint32_t AudioPlayer::deviceSampleRate() const {
    return impl_->deviceReady ? impl_->device.sampleRate : 0;
}

std::string AudioPlayer::deviceName() const {
    return impl_->deviceNameCache;
}

} // namespace avgen::audio
