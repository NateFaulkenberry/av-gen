#include "audio/synthetic_capture.hpp"

#include <chrono>
#include <cmath>
#include <numbers>
#include <string>

namespace avgen::audio {

SyntheticCapture::SyntheticCapture(SyntheticCaptureConfig config)
    : config_(config) {}

SyntheticCapture::~SyntheticCapture() {
    stop();
}

Result<CaptureFormat> SyntheticCapture::open(CaptureSink& sink) {
    if (config_.channels == 0 || config_.sampleRate == 0 || config_.blockFrames == 0) {
        return fail("synthetic capture: channels, rate and block size must be positive");
    }
    sink_ = &sink;
    buffers_.assign(config_.channels, std::vector<float>(config_.blockFrames, 0.0f));
    pointers_.resize(config_.channels);
    for (std::uint32_t c = 0; c < config_.channels; ++c) {
        pointers_[c] = buffers_[c].data();
    }
    return CaptureFormat{config_.sampleRate, config_.channels};
}

Result<void> SyntheticCapture::start() {
    if (sink_ == nullptr) {
        return fail("synthetic capture: not open");
    }
    running_.store(true);
    if (config_.realtime) {
        thread_ = std::jthread([this](std::stop_token token) {
            using clock = std::chrono::steady_clock;
            const auto period = std::chrono::duration<double>(static_cast<double>(config_.blockFrames) /
                                                              static_cast<double>(config_.sampleRate));
            auto next = clock::now();
            while (!token.stop_requested()) {
                deliver();
                next += std::chrono::duration_cast<clock::duration>(period);
                std::this_thread::sleep_until(next);
            }
        });
    }
    return {};
}

void SyntheticCapture::stop() {
    if (thread_.joinable()) {
        thread_.request_stop();
        thread_.join();
    }
    running_.store(false);
}

std::string SyntheticCapture::deviceName() const {
    return "Synthetic " + std::to_string(config_.channels) + "-channel test input";
}

std::string SyntheticCapture::deviceId() const {
    return "synthetic:" + std::to_string(config_.channels);
}

std::uint64_t SyntheticCapture::impulseFrame(std::uint32_t channel, std::uint64_t k) const {
    return k * config_.impulsePeriodFrames + static_cast<std::uint64_t>(channel) * config_.impulseStaggerFrames;
}

void SyntheticCapture::pump(std::uint32_t blocks) {
    if (config_.realtime || !running_.load()) {
        return;
    }
    for (std::uint32_t b = 0; b < blocks; ++b) {
        deliver();
    }
}

void SyntheticCapture::deliver() {
    frame_ += skip_;
    const std::uint64_t first = frame_;
    const double rate = static_cast<double>(config_.sampleRate);
    for (std::uint32_t c = 0; c < config_.channels; ++c) {
        std::vector<float>& out = buffers_[c];
        if ((config_.silentChannelMask >> c) & 1u) {
            std::fill(out.begin(), out.end(), 0.0f);
            continue;
        }
        const double w = 2.0 * std::numbers::pi * static_cast<double>(SyntheticCaptureConfig::toneHz(c)) / rate;
        for (std::uint32_t f = 0; f < config_.blockFrames; ++f) {
            const std::uint64_t n = first + f;
            float v = config_.amplitude * static_cast<float>(std::sin(w * static_cast<double>(n)));
            if (config_.impulses && config_.impulsePeriodFrames > 0) {
                const std::uint64_t stagger = static_cast<std::uint64_t>(c) * config_.impulseStaggerFrames;
                if (n >= stagger && (n - stagger) % config_.impulsePeriodFrames < 8) {
                    v = ((n - stagger) % 2 == 0) ? 0.95f : -0.95f;
                }
            }
            out[f] = v;
        }
    }
    CaptureBlockTiming timing;
    timing.hasSampleTime = true;
    timing.sampleTime = static_cast<double>(first);
    sink_->onBlock(pointers_.data(), config_.channels, config_.blockFrames, timing);
    frame_ += config_.blockFrames;
    skip_ = 0;
}

} // namespace avgen::audio
