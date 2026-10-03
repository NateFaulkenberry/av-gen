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
    if (!frames_.acquire()) {
        return false;
    }
    consumed_.store(frames_.front().liveSerial, std::memory_order_release);
    return true;
}

void AnalysisRunner::carryEvents(AnalysisFrame& frame) {
    frame.liveSerial = ++serial_;
    const std::uint64_t consumed = consumed_.load(std::memory_order_acquire);
    // One kind: this frame's own event replaces the pending one; otherwise a pending event not yet seen by the
    // render thread rides on this frame too.
    const auto carry = [&](bool& flag, float* strength, std::uint64_t& stamp, bool& pFlag, float* pStrength,
                           std::uint64_t& pStamp) {
        if (flag) {
            stamp = frame.liveSerial;
            pFlag = true;
            pStamp = stamp;
            if (strength != nullptr) {
                *pStrength = *strength;
            }
            return;
        }
        if (pFlag && pStamp > consumed) {
            flag = true;
            stamp = pStamp;
            if (strength != nullptr) {
                *strength = *pStrength;
            }
        } else {
            pFlag = false;
        }
    };
    auto& p = pending_;
    carry(frame.onset, &frame.onsetStrength, frame.onsetStamp, p.onset, &p.onsetStrength, p.onsetStamp);
    carry(frame.beat, nullptr, frame.beatStamp, p.beat, nullptr, p.beatStamp);
    carry(frame.lowOnset, &frame.lowOnsetStrength, frame.lowStamp, p.low, &p.lowStrength, p.lowStamp);
    carry(frame.midOnset, &frame.midOnsetStrength, frame.midStamp, p.mid, &p.midStrength, p.midStamp);
    carry(frame.highOnset, &frame.highOnsetStrength, frame.highStamp, p.high, &p.highStrength, p.highStamp);
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
            // Seek or restart: the analyzer restamps and the beat tracker starts from unknown.
            analyzer_.reset(result.startFrame);
            beatTracker_.reset();
            causal_.reset();
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
            // ADR-1060: live kick, snare and hat (the band-onset fields a file gets from ADR-898's offline pass).
            causal_.process(frame, static_cast<float>(config_.sampleRate) / static_cast<float>(config_.windowSize),
                            static_cast<double>(config_.hopSize) / static_cast<double>(config_.sampleRate));
            {
                const CausalOnsets& c = frame.causal;
                frame.lowOnset = c.hit[static_cast<std::size_t>(HitClass::Kick)];
                frame.lowOnsetStrength = c.strength[static_cast<std::size_t>(HitClass::Kick)];
                frame.midOnset = c.hit[static_cast<std::size_t>(HitClass::Snare)];
                frame.midOnsetStrength = c.strength[static_cast<std::size_t>(HitClass::Snare)];
                frame.highOnset = c.hit[static_cast<std::size_t>(HitClass::Hat)];
                frame.highOnsetStrength = c.strength[static_cast<std::size_t>(HitClass::Hat)];
            }
            if (tap_ != nullptr) {
                tap_->onFrame(frame);
            }
            carryEvents(frame);
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
