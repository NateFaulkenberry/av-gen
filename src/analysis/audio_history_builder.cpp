#include "analysis/audio_history_builder.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace avgen::analysis {

namespace {

using spatial::kAudioBins;

// The value of the linear-bin spectrum at frequency `hz`, linearly interpolated between bins.
float sampleBins(const std::vector<float>& spectrum, float binHz, float hz) {
    if (spectrum.empty()) {
        return 0.0f;
    }
    const float x = hz / binHz;
    const auto top = static_cast<float>(spectrum.size() - 1);
    const float c = std::clamp(x, 0.0f, top);
    const auto i0 = static_cast<std::size_t>(std::floor(c));
    const std::size_t i1 = std::min(i0 + 1, spectrum.size() - 1);
    const float f = c - static_cast<float>(i0);
    return spectrum[i0] + (spectrum[i1] - spectrum[i0]) * f;
}

// The stretch: 0 at `lo`, 1 at `hi`.
float stretch(float v, float lo, float hi) {
    const float span = std::max(hi - lo, 0.02f);
    return std::clamp((v - lo) / span, 0.0f, 1.0f);
}

float percentile(std::vector<float>& values, float q) {
    if (values.empty()) {
        return 0.0f;
    }
    const auto k = static_cast<std::size_t>(std::clamp(q, 0.0f, 1.0f) * static_cast<float>(values.size() - 1));
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(k), values.end());
    return values[k];
}

} // namespace

void foldSpectrum(const AnalysisFrame& frame, const AnalyzerConfig& config, std::span<float> out) {
    const float binHz = static_cast<float>(config.sampleRate) / static_cast<float>(std::max(config.windowSize, 2u));
    const float ratio = spatial::kAudioMaxHz / spatial::kAudioMinHz;
    for (int b = 0; b < kAudioBins && static_cast<std::size_t>(b) < out.size(); ++b) {
        // Band b covers [f0, f1) on the log axis; the band's value is the loudest linear bin in it, or
        // the interpolated value at its centre when it is narrower than one linear bin (the lows).
        const float f0 = spatial::kAudioMinHz * std::pow(ratio, static_cast<float>(b) / kAudioBins);
        const float f1 = spatial::kAudioMinHz * std::pow(ratio, static_cast<float>(b + 1) / kAudioBins);
        float v = sampleBins(frame.spectrum, binHz, std::sqrt(f0 * f1));
        const auto i0 = static_cast<std::size_t>(std::ceil(f0 / binHz));
        const auto i1 = static_cast<std::size_t>(std::floor(f1 / binHz));
        for (std::size_t i = i0; i <= i1 && i < frame.spectrum.size(); ++i) {
            v = std::max(v, frame.spectrum[i]);
        }
        out[static_cast<std::size_t>(b)] = v;
    }
}

spatial::AudioHistory buildAudioHistory(const AnalysisTrack& track) {
    const auto& frames = track.frames();
    const AnalyzerConfig& config = track.config();
    const double rate = static_cast<double>(config.sampleRate) / static_cast<double>(std::max(config.hopSize, 1u));
    std::vector<float> rows(frames.size() * kAudioBins, 0.0f);
    for (std::size_t r = 0; r < frames.size(); ++r) {
        foldSpectrum(frames[r], config, std::span<float>(rows.data() + r * kAudioBins, kAudioBins));
    }
    // Per bin: stretch its own 20th..98th percentile over [0, 1].
    std::vector<float> column(frames.size());
    for (int b = 0; b < kAudioBins; ++b) {
        for (std::size_t r = 0; r < frames.size(); ++r) {
            column[r] = rows[r * kAudioBins + static_cast<std::size_t>(b)];
        }
        const float lo = percentile(column, 0.20f);
        const float hi = percentile(column, 0.98f);
        for (std::size_t r = 0; r < frames.size(); ++r) {
            float& v = rows[r * kAudioBins + static_cast<std::size_t>(b)];
            v = stretch(v, lo, hi);
        }
    }
    std::array<std::vector<spatial::AudioOnset>, spatial::kOnsetSources> onsets{};
    for (const AnalysisFrame& f : frames) {
        if (f.lowOnset) {
            onsets[0].push_back({f.timeSeconds, std::max(f.lowOnsetStrength, 0.05f)});
        }
        if (f.midOnset) {
            onsets[1].push_back({f.timeSeconds, std::max(f.midOnsetStrength, 0.05f)});
        }
        if (f.highOnset) {
            onsets[2].push_back({f.timeSeconds, std::max(f.highOnsetStrength, 0.05f)});
        }
    }
    for (const double t : track.beats().beatTimes) {
        onsets[3].push_back({t, 1.0f});
    }
    const double first = frames.empty() ? 0.0 : frames.front().timeSeconds;
    return spatial::AudioHistory::whole(rate, first, std::move(rows), std::move(onsets));
}

LiveAudioHistoryFeed::LiveAudioHistoryFeed(const AnalyzerConfig& config)
    : config_(config),
      history_(std::make_shared<spatial::AudioHistory>(spatial::AudioHistory::livePlaceholder(
          static_cast<double>(config.sampleRate) / static_cast<double>(std::max(config.hopSize, 1u))))) {}

void LiveAudioHistoryFeed::push(const AnalysisFrame& frame) {
    if (frame.spectrum.empty() || frame.timeSeconds <= lastTime_) {
        return;
    }
    const double dt = lastTime_ < 0.0 ? 0.0 : frame.timeSeconds - lastTime_;
    lastTime_ = frame.timeSeconds;
    std::array<float, kAudioBins> row{};
    foldSpectrum(frame, config_, row);
    // Running stand-ins for the offline percentiles: the floor creeps up slowly and drops at once; the
    // peak jumps at once and decays over ~8 s.
    const auto rise = static_cast<float>(1.0 - std::exp(-dt / 20.0));
    const auto fall = static_cast<float>(std::exp(-dt / 8.0));
    for (int b = 0; b < kAudioBins; ++b) {
        const auto i = static_cast<std::size_t>(b);
        if (!primed_) {
            floor_[i] = row[i];
            peak_[i] = row[i] + 0.1f;
        }
        floor_[i] = row[i] < floor_[i] ? row[i] : floor_[i] + (row[i] - floor_[i]) * rise;
        peak_[i] = std::max(row[i], floor_[i] + (peak_[i] - floor_[i]) * fall);
        row[i] = stretch(row[i], floor_[i], peak_[i]);
    }
    primed_ = true;
    history_->appendLive(frame.timeSeconds, row);
    if (frame.lowOnset) {
        history_->addLiveOnset(spatial::OnsetSource::Low, frame.timeSeconds, std::max(frame.lowOnsetStrength, 0.05f));
    }
    if (frame.midOnset) {
        history_->addLiveOnset(spatial::OnsetSource::Mid, frame.timeSeconds, std::max(frame.midOnsetStrength, 0.05f));
    }
    if (frame.highOnset) {
        history_->addLiveOnset(spatial::OnsetSource::High, frame.timeSeconds, std::max(frame.highOnsetStrength, 0.05f));
    }
    if (frame.beat) {
        history_->addLiveOnset(spatial::OnsetSource::Beat, frame.timeSeconds, 1.0f);
    }
}

} // namespace avgen::analysis
