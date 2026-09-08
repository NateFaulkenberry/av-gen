#include "audio/audio_file.hpp"

#include "core/log.hpp"

#include <miniaudio.h>

#include <algorithm>
#include <cstring>

namespace avgen::audio {

Result<AudioFile> AudioFile::load(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        return fail("audio file not found: {}", path.string());
    }

    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
    ma_decoder decoder{};
    const ma_result initResult = ma_decoder_init_file(path.string().c_str(), &config, &decoder);
    if (initResult != MA_SUCCESS) {
        return fail("cannot decode '{}': {}", path.string(), ma_result_description(initResult));
    }

    AudioFile file;
    file.path_ = path;
    file.sampleRate_ = decoder.outputSampleRate;
    file.channels_ = decoder.outputChannels;
    if (file.channels_ == 0 || file.sampleRate_ == 0) {
        ma_decoder_uninit(&decoder);
        return fail("decoder reported no channels or sample rate for '{}'", path.string());
    }

    // Length may be unknown (some MP3 streams); grow as we read.
    ma_uint64 declaredFrames = 0;
    if (ma_decoder_get_length_in_pcm_frames(&decoder, &declaredFrames) == MA_SUCCESS && declaredFrames > 0) {
        file.interleaved_.reserve(static_cast<std::size_t>(declaredFrames * file.channels_));
    }

    constexpr ma_uint64 kChunkFrames = 16384;
    std::vector<float> chunk(static_cast<std::size_t>(kChunkFrames * file.channels_));
    ma_uint64 totalFrames = 0;
    for (;;) {
        ma_uint64 framesRead = 0;
        const ma_result r = ma_decoder_read_pcm_frames(&decoder, chunk.data(), kChunkFrames, &framesRead);
        if (framesRead > 0) {
            file.interleaved_.insert(file.interleaved_.end(), chunk.begin(),
                                     chunk.begin() + static_cast<std::ptrdiff_t>(framesRead * file.channels_));
            totalFrames += framesRead;
        }
        if (r == MA_AT_END || framesRead == 0) {
            break;
        }
        if (r != MA_SUCCESS) {
            ma_decoder_uninit(&decoder);
            return fail("decode error in '{}' after {} frames: {}", path.string(), totalFrames,
                        ma_result_description(r));
        }
    }
    ma_decoder_uninit(&decoder);

    if (totalFrames == 0) {
        return fail("audio file '{}' contains no samples", path.string());
    }
    file.frameCount_ = totalFrames;
    file.buildMono();
    log::info("loaded '{}': {} Hz, {} ch, {} frames ({:.2f} s)", path.filename().string(), file.sampleRate_,
              file.channels_, file.frameCount_, file.durationSeconds());
    return file;
}

AudioFile AudioFile::fromInterleaved(std::vector<float> interleaved, std::uint32_t channels,
                                     std::uint32_t sampleRate) {
    AudioFile file;
    file.channels_ = std::max<std::uint32_t>(channels, 1);
    file.sampleRate_ = std::max<std::uint32_t>(sampleRate, 1);
    file.frameCount_ = interleaved.size() / file.channels_;
    interleaved.resize(static_cast<std::size_t>(file.frameCount_ * file.channels_));
    file.interleaved_ = std::move(interleaved);
    file.buildMono();
    return file;
}

Result<void> AudioFile::writeWav(const std::filesystem::path& path) const {
    ma_encoder_config config = ma_encoder_config_init(ma_encoding_format_wav, ma_format_f32, channels_, sampleRate_);
    ma_encoder encoder{};
    const ma_result initResult = ma_encoder_init_file(path.string().c_str(), &config, &encoder);
    if (initResult != MA_SUCCESS) {
        return fail("cannot create '{}': {}", path.string(), ma_result_description(initResult));
    }
    ma_uint64 written = 0;
    const ma_result r = ma_encoder_write_pcm_frames(&encoder, interleaved_.data(), frameCount_, &written);
    ma_encoder_uninit(&encoder);
    if (r != MA_SUCCESS || written != frameCount_) {
        return fail("failed writing '{}': {} ({} of {} frames)", path.string(), ma_result_description(r), written,
                    frameCount_);
    }
    return {};
}

double AudioFile::durationSeconds() const {
    return sampleRate_ == 0 ? 0.0 : static_cast<double>(frameCount_) / static_cast<double>(sampleRate_);
}

std::uint64_t AudioFile::readFrames(std::uint64_t start, std::span<float> outInterleaved) const {
    const std::uint64_t requested = outInterleaved.size() / channels_;
    std::fill(outInterleaved.begin(), outInterleaved.end(), 0.0f);
    if (start >= frameCount_ || requested == 0) {
        return 0;
    }
    const std::uint64_t available = std::min<std::uint64_t>(requested, frameCount_ - start);
    const std::size_t count = static_cast<std::size_t>(available * channels_);
    std::memcpy(outInterleaved.data(), interleaved_.data() + static_cast<std::size_t>(start * channels_),
                count * sizeof(float));
    return available;
}

void AudioFile::buildMono() {
    mono_.resize(static_cast<std::size_t>(frameCount_));
    if (channels_ == 1) {
        std::copy(interleaved_.begin(), interleaved_.end(), mono_.begin());
        return;
    }
    const float inv = 1.0f / static_cast<float>(channels_);
    for (std::uint64_t f = 0; f < frameCount_; ++f) {
        float sum = 0.0f;
        for (std::uint32_t c = 0; c < channels_; ++c) {
            sum += interleaved_[static_cast<std::size_t>(f * channels_ + c)];
        }
        mono_[static_cast<std::size_t>(f)] = sum * inv;
    }
}

} // namespace avgen::audio
