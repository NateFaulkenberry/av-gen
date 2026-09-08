#include "analysis/beat_tracker.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace avgen::analysis {

namespace {

// Smoothing applied to the onset envelope before autocorrelation and DP (Ellis 2007 smooths with
// a Gaussian of a few hops). One hop of sigma is enough to turn an impulse train whose period is
// not an integer number of hops into a curve whose peaks interpolate cleanly.
constexpr float kSmoothSigmaHops = 1.0f;
// Ellis "tightness" for the DP transition penalty, with the onset envelope normalised to 0..~1.
constexpr double kTightness = 400.0;
// Confidence squash: conf = 1 - exp(-(peak / mean - 1) / kConfidenceScale). A periodic envelope
// gives peak/mean of 5-30 (conf 0.75-1); a noisy envelope 1.2-1.6 (conf < 0.2).
constexpr double kConfidenceScale = 3.0;
// Live tracker tuning.
constexpr double kPeriodBlend = 0.3;          // weight of a new estimate within the jump threshold
constexpr double kPeriodJumpFraction = 0.15;  // larger relative changes replace the period outright
constexpr double kOnsetCaptureFraction = 0.2; // onsets within +/- this * period pull the phase
// Picked onsets required inside the window before a tempo estimate is trusted. Without this a
// sub-threshold but periodic envelope (a steady tone's numerical flux noise, a slow tremolo)
// would lock the tracker even though nothing rhythmic is playing.
constexpr std::size_t kMinOnsetsInWindow = 4;

std::vector<float> smoothed(std::span<const float> in, float sigmaHops) {
    const std::size_t n = in.size();
    std::vector<float> out(n, 0.0f);
    if (n == 0) {
        return out;
    }
    const auto radius = static_cast<std::size_t>(std::ceil(3.0f * sigmaHops));
    std::vector<double> kernel(2 * radius + 1);
    const auto sigma = static_cast<double>(sigmaHops);
    for (std::size_t k = 0; k < kernel.size(); ++k) {
        const double d = static_cast<double>(k) - static_cast<double>(radius);
        kernel[k] = std::exp(-0.5 * d * d / (sigma * sigma));
    }
    for (std::size_t i = 0; i < n; ++i) {
        // Renormalise over the taps that fall inside the signal so a flat input stays flat at
        // the edges.
        double acc = 0.0;
        double weight = 0.0;
        for (std::size_t k = 0; k < kernel.size(); ++k) {
            const auto j = static_cast<std::ptrdiff_t>(i) + static_cast<std::ptrdiff_t>(k) -
                           static_cast<std::ptrdiff_t>(radius);
            if (j >= 0 && j < static_cast<std::ptrdiff_t>(n)) {
                acc += kernel[k] * static_cast<double>(in[static_cast<std::size_t>(j)]);
                weight += kernel[k];
            }
        }
        out[i] = weight > 0.0 ? static_cast<float>(acc / weight) : 0.0f;
    }
    return out;
}

double lagToBpm(double lagHops, double hopSeconds) {
    return 60.0 / (lagHops * hopSeconds);
}

// Ellis log-Gaussian tempo prior (sigma in octaves around the preferred tempo).
double tempoPrior(double bpm, const BeatTrackerConfig& config) {
    const double width = std::max(1e-3, static_cast<double>(config.priorWidthOctaves));
    const double octaves = std::log2(bpm / static_cast<double>(config.preferredBpm));
    return std::exp(-0.5 * (octaves / width) * (octaves / width));
}

double median(std::vector<double> values) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const std::size_t mid = values.size() / 2;
    return (values.size() % 2 == 1) ? values[mid] : 0.5 * (values[mid - 1] + values[mid]);
}

} // namespace

// ------------------------------------------------------------------------------------------------
// Tempo estimation
// ------------------------------------------------------------------------------------------------

std::pair<float, float> estimateTempo(std::span<const float> onsetStrength, float hopSeconds,
                                      const BeatTrackerConfig& config) {
    const std::size_t n = onsetStrength.size();
    const auto hop = static_cast<double>(hopSeconds);
    if (n < 4 || !(hop > 0.0) || !(config.minBpm > 0.0f) || !(config.maxBpm > config.minBpm)) {
        return {0.0f, 0.0f};
    }

    // Smoothed, mean-removed, half-wave-rectified envelope.
    std::vector<float> x = smoothed(onsetStrength, kSmoothSigmaHops);
    const double mean = std::accumulate(x.begin(), x.end(), 0.0) / static_cast<double>(n);
    for (auto& v : x) {
        v = std::max(0.0f, v - static_cast<float>(mean));
    }

    // Lag range for the configured tempo range, limited so every lag overlaps at least half the
    // window: a window shorter than two periods cannot estimate that tempo.
    const auto minLag = std::max<std::size_t>(
        1, static_cast<std::size_t>(std::floor(60.0 / (static_cast<double>(config.maxBpm) * hop))));
    const auto maxLag = std::min<std::size_t>(
        static_cast<std::size_t>(std::ceil(60.0 / (static_cast<double>(config.minBpm) * hop))), n / 2);
    if (maxLag < minLag + 2) {
        return {0.0f, 0.0f};
    }

    // Prior-weighted autocorrelation over [minLag - 1, maxLag + 1]; the extra lag on each side
    // lets a peak at the range edge be interpolated.
    const std::size_t lo = minLag - 1;
    const std::size_t hi = std::min(maxLag + 1, n - 1);
    std::vector<double> weighted(hi + 1, 0.0);
    for (std::size_t lag = std::max<std::size_t>(lo, 1); lag <= hi; ++lag) {
        double acc = 0.0;
        for (std::size_t i = lag; i < n; ++i) {
            acc += static_cast<double>(x[i]) * static_cast<double>(x[i - lag]);
        }
        const double r = acc / static_cast<double>(n - lag);
        weighted[lag] = r * tempoPrior(lagToBpm(static_cast<double>(lag), hop), config);
    }

    double meanWeighted = 0.0;
    for (std::size_t lag = minLag; lag <= maxLag; ++lag) {
        meanWeighted += weighted[lag];
    }
    meanWeighted /= static_cast<double>(maxLag - minLag + 1);
    if (!(meanWeighted > 0.0)) {
        return {0.0f, 0.0f};
    }

    // Best local maximum after parabolic interpolation. Comparing interpolated peak values
    // matters when the true period falls between two lags (e.g. 22.5 hops at 160 BPM), where the
    // sampled peak is lower than a competing octave peak that lands on an integer lag.
    double bestValue = -1.0;
    double bestLag = 0.0;
    for (std::size_t lag = minLag; lag <= maxLag; ++lag) {
        const double left = weighted[lag - 1];
        const double centre = weighted[lag];
        const double right = lag + 1 < weighted.size() ? weighted[lag + 1] : 0.0;
        const bool isPeak = (centre >= left && centre > right) || (centre > left && centre >= right);
        if (!isPeak) {
            continue;
        }
        const double denom = left - 2.0 * centre + right;
        double offset = 0.0;
        double value = centre;
        if (denom < 0.0) {
            offset = std::clamp(0.5 * (left - right) / denom, -0.5, 0.5);
            value = centre - 0.25 * (left - right) * offset;
        }
        if (value > bestValue) {
            bestValue = value;
            bestLag = static_cast<double>(lag) + offset;
        }
    }
    if (bestValue <= 0.0) {
        // No interior peak (monotonic over the range): take the largest sample as is.
        for (std::size_t lag = minLag; lag <= maxLag; ++lag) {
            if (weighted[lag] > bestValue) {
                bestValue = weighted[lag];
                bestLag = static_cast<double>(lag);
            }
        }
        if (bestValue <= 0.0) {
            return {0.0f, 0.0f};
        }
    }

    const double ratio = bestValue / meanWeighted;
    const double confidence =
        std::clamp(1.0 - std::exp(-std::max(0.0, ratio - 1.0) / kConfidenceScale), 0.0, 1.0);
    const double bpm = std::clamp(lagToBpm(bestLag, hop), static_cast<double>(config.minBpm),
                                  static_cast<double>(config.maxBpm));
    return {static_cast<float>(bpm), static_cast<float>(confidence)};
}

// ------------------------------------------------------------------------------------------------
// Offline (Ellis 2007) tracker
// ------------------------------------------------------------------------------------------------

OfflineBeats trackBeatsOffline(std::span<const float> onsetStrength, float hopSeconds,
                               const BeatTrackerConfig& config) {
    OfflineBeats out;
    const std::size_t n = onsetStrength.size();
    const auto hop = static_cast<double>(hopSeconds);
    if (n == 0 || !(hop > 0.0)) {
        return out;
    }

    // Global tempo: the median of windowed estimates when the track spans several windows (robust
    // against a few noisy stretches), else a single estimate over everything.
    const auto windowHops = static_cast<std::size_t>(
        std::max<long long>(0, std::llround(static_cast<double>(config.windowSeconds) / hop)));
    float bpm = 0.0f;
    float confidence = 0.0f;
    if (windowHops >= 8 && n >= 2 * windowHops) {
        std::vector<double> bpms;
        std::vector<double> confidences;
        const std::size_t step = std::max<std::size_t>(1, windowHops / 2);
        for (std::size_t start = 0; start + windowHops <= n; start += step) {
            const auto [wBpm, wConf] =
                estimateTempo(onsetStrength.subspan(start, windowHops), hopSeconds, config);
            if (wConf >= config.confidenceFloor && wBpm > 0.0f) {
                bpms.push_back(static_cast<double>(wBpm));
                confidences.push_back(static_cast<double>(wConf));
            }
        }
        if (!bpms.empty()) {
            bpm = static_cast<float>(median(bpms));
            confidence = static_cast<float>(median(confidences));
        }
    }
    if (bpm <= 0.0f) {
        const auto [wBpm, wConf] = estimateTempo(onsetStrength, hopSeconds, config);
        bpm = wBpm;
        confidence = wConf;
    }
    out.confidence = confidence;
    if (bpm <= 0.0f || confidence < config.confidenceFloor) {
        return out; // tempo unknown: no beats
    }
    out.tempoBpm = bpm;

    // Local score: smoothed envelope normalised to 0..1.
    std::vector<float> local = smoothed(onsetStrength, kSmoothSigmaHops);
    const float peak = *std::max_element(local.begin(), local.end());
    if (!(peak > 0.0f)) {
        return out;
    }
    for (auto& v : local) {
        v = std::max(0.0f, v / peak);
    }

    // Dynamic programming: score[i] = local[i] + max over predecessors m in [i - 2P, i - P/2] of
    // score[m] - tightness * log(delta / P)^2, with score 0 before the start of the track so a
    // beat sequence may begin anywhere at no cost.
    const double periodHops = 60.0 / (static_cast<double>(bpm) * hop);
    const auto minDelta = std::max<std::size_t>(1, static_cast<std::size_t>(std::llround(0.5 * periodHops)));
    const auto maxDelta = std::max(minDelta + 1, static_cast<std::size_t>(std::llround(2.0 * periodHops)));
    std::vector<double> penalty(maxDelta + 1, 0.0);
    for (std::size_t d = minDelta; d <= maxDelta; ++d) {
        const double l = std::log(static_cast<double>(d) / periodHops);
        penalty[d] = -kTightness * l * l;
    }

    std::vector<double> score(n, 0.0);
    std::vector<std::ptrdiff_t> back(n, -1);
    for (std::size_t i = 0; i < n; ++i) {
        double best = -std::numeric_limits<double>::infinity();
        std::ptrdiff_t bestM = -1;
        for (std::size_t d = minDelta; d <= maxDelta; ++d) {
            const auto m = static_cast<std::ptrdiff_t>(i) - static_cast<std::ptrdiff_t>(d);
            const double candidate = penalty[d] + (m >= 0 ? score[static_cast<std::size_t>(m)] : 0.0);
            if (candidate > best) {
                best = candidate;
                bestM = m;
            }
        }
        score[i] = static_cast<double>(local[i]) + best;
        back[i] = bestM >= 0 ? bestM : -1;
    }

    // Backtrace from the best final score.
    auto index = static_cast<std::ptrdiff_t>(
        std::distance(score.begin(), std::max_element(score.begin(), score.end())));
    std::vector<std::size_t> beats;
    while (index >= 0) {
        beats.push_back(static_cast<std::size_t>(index));
        index = back[static_cast<std::size_t>(index)];
    }
    std::reverse(beats.begin(), beats.end());

    // Trim beats the DP placed in silence before the first / after the last real onset: the free
    // start lets a perfectly regular chain extend through leading silence at no cost.
    if (!beats.empty()) {
        double meanAtBeats = 0.0;
        for (const std::size_t b : beats) {
            meanAtBeats += static_cast<double>(local[b]);
        }
        meanAtBeats /= static_cast<double>(beats.size());
        const double threshold = 0.1 * meanAtBeats;
        std::size_t first = 0;
        std::size_t last = beats.size();
        while (first < last && static_cast<double>(local[beats[first]]) <= threshold) {
            ++first;
        }
        while (last > first && static_cast<double>(local[beats[last - 1]]) <= threshold) {
            --last;
        }
        beats.erase(beats.begin() + static_cast<std::ptrdiff_t>(last), beats.end());
        beats.erase(beats.begin(), beats.begin() + static_cast<std::ptrdiff_t>(first));
    }

    out.beatTimes.reserve(beats.size());
    for (const std::size_t b : beats) {
        out.beatTimes.push_back(static_cast<double>(b) * hop);
    }
    return out;
}

// ------------------------------------------------------------------------------------------------
// Live tracker
// ------------------------------------------------------------------------------------------------

BeatTracker::BeatTracker(BeatTrackerConfig config, float hopSeconds)
    : config_(config)
    , hopSeconds_(hopSeconds) {
    const double hop = hopSeconds > 0.0f ? static_cast<double>(hopSeconds) : 1.0;
    const auto hops = [hop](float seconds) {
        return static_cast<std::size_t>(
            std::max<long long>(1, std::llround(static_cast<double>(seconds) / hop)));
    };
    history_.assign(hops(config_.windowSeconds), 0.0f);
    onsetHistory_.assign(history_.size(), 0);
    scratch_.reserve(history_.size());
    tempoUpdateHops_ = hops(config_.tempoUpdateSeconds);
    // Wait for two periods of the slowest tempo before the first estimate (at most the window).
    const float minSeconds = config_.minBpm > 0.0f ? 2.0f * 60.0f / config_.minBpm : config_.windowSeconds;
    minHistoryHops_ = std::min(history_.size(), hops(minSeconds));
}

void BeatTracker::reset() {
    state_ = BeatState{};
    historyHead_ = 0;
    historyCount_ = 0;
    hopsSinceTempo_ = 0;
    hopsUnconfident_ = 0;
    period_ = 0.0;
    nextBeatTime_ = 0.0;
    time_ = 0.0;
    lastBeatTime_ = -1.0;
    hopIndex_ = 0;
}

float BeatTracker::historyAt(std::size_t logicalIndex) const {
    // logicalIndex 0 is the oldest sample kept, historyCount_ - 1 the newest.
    const std::size_t capacity = history_.size();
    const std::size_t start = (historyHead_ + capacity - historyCount_) % capacity;
    return history_[(start + logicalIndex) % capacity];
}

void BeatTracker::initialisePhase() {
    // Comb over the history at the current period: the offset (hops back from now) whose
    // period-spaced samples sum highest marks the most recent beat.
    const auto hop = static_cast<double>(hopSeconds_);
    const double periodHops = period_ / hop;
    const auto maxOffset = std::min(historyCount_, static_cast<std::size_t>(std::floor(periodHops)));
    std::size_t bestOffset = 0;
    double bestSum = -1.0;
    for (std::size_t offset = 0; offset < maxOffset; ++offset) {
        double sum = 0.0;
        for (std::size_t j = 0;; ++j) {
            const auto back =
                offset + static_cast<std::size_t>(std::llround(static_cast<double>(j) * periodHops));
            if (back >= historyCount_) {
                break;
            }
            sum += static_cast<double>(historyAt(historyCount_ - 1 - back));
        }
        if (sum > bestSum) {
            bestSum = sum;
            bestOffset = offset;
        }
    }
    if (bestOffset == 0) {
        // The beat is in this hop: schedule it now so this push emits it.
        nextBeatTime_ = time_;
        lastBeatTime_ = time_ - period_;
    } else {
        lastBeatTime_ = time_ - static_cast<double>(bestOffset) * hop;
        nextBeatTime_ = lastBeatTime_ + period_;
    }
}

void BeatTracker::updateTempo() {
    const std::size_t capacity = history_.size();
    const std::size_t start = (historyHead_ + capacity - historyCount_) % capacity;
    scratch_.resize(historyCount_);
    std::size_t onsets = 0;
    for (std::size_t i = 0; i < historyCount_; ++i) {
        const std::size_t slot = (start + i) % capacity;
        scratch_[i] = history_[slot];
        onsets += onsetHistory_[slot];
    }
    if (onsets < kMinOnsetsInWindow) {
        state_.confidence = 0.0f; // nothing rhythmic in the window
        return;
    }
    const auto [bpm, confidence] = estimateTempo(scratch_, hopSeconds_, config_);
    state_.confidence = confidence;
    if (confidence >= config_.confidenceFloor && bpm > 0.0f) {
        hopsUnconfident_ = 0;
        const double newPeriod = 60.0 / static_cast<double>(bpm);
        if (period_ <= 0.0 || std::fabs(newPeriod - period_) > kPeriodJumpFraction * period_) {
            period_ = newPeriod;
            initialisePhase();
        } else {
            period_ += kPeriodBlend * (newPeriod - period_);
        }
    }
}

BeatState BeatTracker::push(float onsetStrength, bool onset) {
    const auto hop = static_cast<double>(hopSeconds_);
    time_ = static_cast<double>(hopIndex_) * hop;

    // Ring of onset strengths over the tempo window.
    const std::size_t capacity = history_.size();
    history_[historyHead_] = std::isfinite(onsetStrength) ? std::max(0.0f, onsetStrength) : 0.0f;
    onsetHistory_[historyHead_] = onset ? 1 : 0;
    historyHead_ = (historyHead_ + 1) % capacity;
    historyCount_ = std::min(historyCount_ + 1, capacity);

    // Periodic tempo re-estimation once enough history exists.
    if (++hopsSinceTempo_ >= tempoUpdateHops_ && historyCount_ >= minHistoryHops_) {
        hopsSinceTempo_ = 0;
        updateTempo();
    }

    // Free-run through unconfident stretches, but give up once a whole window has passed without
    // periodicity: the tempo becomes unknown again.
    if (period_ > 0.0 && state_.confidence < config_.confidenceFloor) {
        if (++hopsUnconfident_ >= capacity) {
            period_ = 0.0;
            lastBeatTime_ = -1.0;
            hopsUnconfident_ = 0;
        }
    }

    // Phase-locked loop: a picked onset near a predicted beat pulls the next prediction toward it.
    if (onset && period_ > 0.0) {
        const double toNext = nextBeatTime_ - time_;
        const double sinceLast =
            lastBeatTime_ >= 0.0 ? time_ - lastBeatTime_ : std::numeric_limits<double>::infinity();
        const double error = sinceLast < toNext ? sinceLast : -toNext; // onset minus nearest prediction
        if (std::fabs(error) <= kOnsetCaptureFraction * period_) {
            nextBeatTime_ += static_cast<double>(config_.phaseLockStrength) * error;
        }
    }

    // Beat emission and phase.
    state_.beat = false;
    if (period_ > 0.0) {
        if (nextBeatTime_ <= time_ + 0.5 * hop) {
            state_.beat = true;
            ++state_.beatCount;
            lastBeatTime_ = nextBeatTime_;
            nextBeatTime_ += period_;
            while (nextBeatTime_ <= time_ + 0.5 * hop) { // period < hop cannot happen; stay safe
                nextBeatTime_ += period_;
            }
        }
        if (state_.beat || lastBeatTime_ < 0.0) {
            state_.phase = 0.0f;
        } else {
            // Fraction of the predicted interval, so the PLL's adjustments keep it continuous.
            const double interval = std::max(nextBeatTime_ - lastBeatTime_, 1e-9);
            state_.phase = static_cast<float>(std::clamp((time_ - lastBeatTime_) / interval, 0.0, 1.0));
        }
        state_.tempoBpm = static_cast<float>(60.0 / period_);
    } else {
        state_.phase = 0.0f;
        state_.tempoBpm = 0.0f;
    }

    ++hopIndex_;
    return state_;
}

} // namespace avgen::analysis
