#include "analysis/analysis_runner.hpp"

#include "core/log.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

namespace avgen::analysis {

AnalysisRunner::AnalysisRunner(AnalyzerConfig config, audio::AnalysisStream& stream,
                               BeatTrackerConfig beatConfig)
    : config_(std::move(config))
    , stream_(stream)
    , analyzer_(config_)
    , beatTracker_(beatConfig, static_cast<float>(config_.hopSize) / static_cast<float>(config_.sampleRate)) {
    history_.reserve(kHistorySize);
}

AnalysisRunner::~AnalysisRunner() {
    stop();
}

void AnalysisRunner::start() {
    if (running_.load()) {
        return;
    }
    running_.store(true);
    thread_ = std::jthread([this](std::stop_token token) { threadMain(token); });
    log::debug("AnalysisRunner started (window {}, hop {}, {} Hz)", config_.windowSize, config_.hopSize,
               config_.sampleRate);
}

void AnalysisRunner::stop() {
    if (thread_.joinable()) {
        thread_.request_stop();
        thread_.join();
    }
    running_.store(false);
}

bool AnalysisRunner::acquire() {
    return frames_.acquire();
}

std::vector<AnalysisFrame> AnalysisRunner::history(std::size_t count) const {
    std::vector<AnalysisFrame> out;
    const std::lock_guard lock(historyMutex_);
    const std::size_t size = history_.size();
    const std::size_t n = std::min(count, size);
    out.reserve(n);
    // Logical order is oldest first starting at historyHead_ (== size while the ring is filling,
    // so the modulo below reduces to a plain index).
    for (std::size_t i = size - n; i < size; ++i) {
        out.push_back(history_[(historyHead_ + i) % size]);
    }
    return out;
}

void AnalysisRunner::threadMain(std::stop_token token) {
    using clock = std::chrono::steady_clock;
    constexpr double kEmaWeight = 0.1;

    std::vector<float> chunk(config_.hopSize);
    AnalysisFrame frame;
    while (!token.stop_requested()) {
        const auto result = stream_.read(chunk);
        if (result.count == 0 && !result.discontinuity) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        const auto started = clock::now();
        if (result.discontinuity) {
            // Seek or restart: the analyser restamps and the beat tracker starts from unknown.
            analyzer_.reset(result.startFrame);
            beatTracker_.reset();
        }
        analyzer_.push(std::span<const float>(chunk.data(), result.count));

        std::size_t producedNow = 0;
        while (analyzer_.pop(frame)) {
            const BeatState beat = beatTracker_.push(frame.onsetStrength, frame.onset);
            frame.tempoBpm = beat.tempoBpm;
            frame.tempoConfidence = beat.confidence;
            frame.beat = beat.beat;
            frame.beatPhase = beat.phase;
            frame.beatCount = beat.beatCount;
            {
                const std::lock_guard lock(historyMutex_);
                if (history_.size() < kHistorySize) {
                    history_.push_back(frame);
                    historyHead_ = history_.size() % kHistorySize;
                } else {
                    history_[historyHead_] = frame;
                    historyHead_ = (historyHead_ + 1) % kHistorySize;
                }
            }
            frames_.back() = std::move(frame);
            frames_.publish();
            ++producedNow;
        }
        if (producedNow > 0) {
            const auto elapsed = std::chrono::duration<double, std::micro>(clock::now() - started).count();
            const double perHop = elapsed / static_cast<double>(producedNow);
            const double previous = hopMicros_.load(std::memory_order_relaxed);
            const double next = previous == 0.0 ? perHop : previous + (perHop - previous) * kEmaWeight;
            hopMicros_.store(next, std::memory_order_relaxed);
            produced_.fetch_add(producedNow, std::memory_order_relaxed);
        }
    }
}

} // namespace avgen::analysis
