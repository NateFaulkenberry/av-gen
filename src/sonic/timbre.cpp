#include "sonic/timbre.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::sonic {

namespace {

constexpr float kLowSplitHz = 250.0f;
constexpr float kFlatLowHz = 60.0f;
constexpr float kFlatHighHz = 16000.0f;
constexpr float kRolloff = 0.85f;
constexpr float kMaxHarmonicHz = 10000.0f;
constexpr int kCandidatesPerOctave = 48;
// Klapuri (2006) harmonic weighting g(f0, h) = (f0 + alpha) / (h f0 + beta).
constexpr float kAlpha = 52.0f;
constexpr float kBeta = 320.0f;

double dbToPower(double db) {
    return std::pow(10.0, db / 10.0);
}

} // namespace

TimbreAnalyzer::TimbreAnalyzer(std::uint32_t sampleRate, std::uint32_t windowSize, TimbreConfig config)
    : sampleRate_(sampleRate == 0 ? 48000 : sampleRate)
    , windowSize_(windowSize < 2 ? 2048 : windowSize)
    , binHz_(static_cast<float>(sampleRate_) / static_cast<float>(windowSize_))
    , config_(config) {
    config_.maxF0Count = std::clamp(config_.maxF0Count, 1, 8);
    config_.maxPeaks = std::clamp(config_.maxPeaks, 4, 256);
    config_.dissonancePeaks = std::clamp(config_.dissonancePeaks, 2, config_.maxPeaks);
    db_.reserve(windowSize_ / 2 + 1);
    peaks_.reserve(static_cast<std::size_t>(config_.maxPeaks));
    scratch_.reserve(windowSize_ / 4);
    const float lo = std::max(config_.minF0Hz, binHz_);
    const float hi = std::max(lo * 1.01f, config_.maxF0Hz);
    const int count = static_cast<int>(std::ceil(std::log2(hi / lo) * kCandidatesPerOctave)) + 1;
    candidates_.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        candidates_.push_back(lo * std::exp2(static_cast<float>(i) / kCandidatesPerOctave));
    }
    f0s_.reserve(static_cast<std::size_t>(config_.maxF0Count));
    residual_.reserve(static_cast<std::size_t>(config_.maxPeaks));
}

void TimbreAnalyzer::findPeaks(const std::vector<float>& magnitude) {
    const std::size_t bins = magnitude.size();
    db_.resize(bins);
    float maxDb = -240.0f;
    for (std::size_t k = 0; k < bins; ++k) {
        db_[k] = 20.0f * std::log10(magnitude[k] + 1e-9f);
        if (k >= 1) {
            maxDb = std::max(maxDb, db_[k]);
        }
    }
    scratch_.clear();
    peaks_.clear();
    if (bins < 8) {
        return;
    }
    const float floorDb = maxDb - config_.peakRangeDb;
    constexpr std::size_t kFar = 8;
    for (std::size_t k = 2; k + 2 < bins; ++k) {
        const float m = magnitude[k];
        if (!(m > magnitude[k - 1] && m >= magnitude[k + 1]) || db_[k] < floorDb) {
            continue;
        }
        // Prominence, topographic: how far the peak stands above the higher of the two valleys that separate it
        // from its neighbourhood (up to kFar bins either side, stopping at a higher peak). A mean over the
        // neighbourhood would call every partial of a dense harmonic comb "not prominent", because the
        // neighbourhood is other partials.
        const auto valley = [&](int step) {
            float lowest = db_[k];
            for (std::size_t d = 1; d <= kFar; ++d) {
                const auto j = static_cast<std::ptrdiff_t>(k) + step * static_cast<std::ptrdiff_t>(d);
                if (j < 1 || j >= static_cast<std::ptrdiff_t>(bins)) {
                    break;
                }
                const float v = db_[static_cast<std::size_t>(j)];
                if (v > db_[k]) {
                    break;
                }
                lowest = std::min(lowest, v);
            }
            return lowest;
        };
        const float around = std::max(valley(-1), valley(+1));
        if (db_[k] - around < config_.prominenceDb) {
            continue;
        }
        // Parabolic interpolation on the dB spectrum for the peak's frequency and height.
        const float a = db_[k - 1];
        const float b = db_[k];
        const float c = db_[k + 1];
        const float denom = a - 2.0f * b + c;
        const float p = std::abs(denom) > 1e-9f ? std::clamp(0.5f * (a - c) / denom, -0.5f, 0.5f) : 0.0f;
        Peak peak;
        peak.hz = (static_cast<float>(k) + p) * binHz_;
        peak.amplitude = std::pow(10.0f, (b - 0.25f * (a - c) * p) / 20.0f);
        double energy = 0.0;
        for (std::size_t j = k - 2; j <= k + 2; ++j) {
            energy += static_cast<double>(magnitude[j]) * static_cast<double>(magnitude[j]);
        }
        peak.energy = static_cast<float>(energy);
        scratch_.push_back(peak);
    }
    const auto keep = std::min<std::size_t>(scratch_.size(), static_cast<std::size_t>(config_.maxPeaks));
    std::partial_sort(scratch_.begin(), scratch_.begin() + static_cast<std::ptrdiff_t>(keep), scratch_.end(),
                      [](const Peak& x, const Peak& y) { return x.amplitude > y.amplitude; });
    peaks_.assign(scratch_.begin(), scratch_.begin() + static_cast<std::ptrdiff_t>(keep));
    std::sort(peaks_.begin(), peaks_.end(), [](const Peak& x, const Peak& y) { return x.hz < y.hz; });
}

int TimbreAnalyzer::nearestPeak(float hz, float toleranceHz) const {
    // peaks_ is sorted by frequency: binary search, then look at the two neighbours.
    const auto it = std::lower_bound(peaks_.begin(), peaks_.end(), hz,
                                     [](const Peak& p, float f) { return p.hz < f; });
    int best = -1;
    float bestDistance = toleranceHz;
    const auto consider = [&](std::ptrdiff_t i) {
        if (i < 0 || i >= static_cast<std::ptrdiff_t>(peaks_.size())) {
            return;
        }
        const float d = std::abs(peaks_[static_cast<std::size_t>(i)].hz - hz);
        if (d <= bestDistance) {
            bestDistance = d;
            best = static_cast<int>(i);
        }
    };
    const std::ptrdiff_t at = it - peaks_.begin();
    consider(at);
    consider(at - 1);
    return best;
}

float TimbreAnalyzer::salience(std::size_t candidate, int minMatched) const {
    const float f0 = candidates_[candidate];
    int matched = 0;
    const std::int16_t* row = matches_.data() + candidate * kMaxHarmonics;
    float sum = 0.0f;
    for (int h = 1; h <= kMaxHarmonics; ++h) {
        const std::int16_t idx = row[h - 1];
        if (idx == kNoHarmonic) {
            break; // past the top harmonic
        }
        const float a = idx >= 0 ? residual_[static_cast<std::size_t>(idx)] : 0.0f;
        if (h == 1 && !(a > 0.0f)) {
            // A fundamental with no partial of its own is how a low subharmonic "explains" a sparse
            // inharmonic spectrum (an FM bell's partials are all multiples of some f0/5). Refused.
            return 0.0f;
        }
        if (a > 0.0f) {
            ++matched;
        }
        sum += a * (f0 + kAlpha) / (f0 * static_cast<float>(h) + kBeta);
    }
    // Every partial is the first harmonic of something. A series needs more than one member before it explains
    // anything, or estimate-and-cancel would "explain" an inharmonic spectrum one partial at a time.
    return matched >= minMatched ? sum : 0.0f;
}

void TimbreAnalyzer::estimateFundamentals(TimbreFeatures& out, double totalPower) {
    f0s_.clear();
    residual_.resize(peaks_.size());
    double peakEnergy = 0.0;
    for (std::size_t i = 0; i < peaks_.size(); ++i) {
        residual_[i] = peaks_[i].amplitude;
        peaks_[i].f0 = -1;
        peaks_[i].misfit = 1.0f;
        peakEnergy += peaks_[i].energy;
    }
    if (peaks_.empty() || !(totalPower > 0.0)) {
        return;
    }
    // Which peak (if any) sits at each harmonic of each candidate: a function of the peaks alone, so it is
    // built once per frame and every estimate-and-cancel iteration only re-reads the residual.
    {
        const float top = std::min(0.5f * static_cast<float>(sampleRate_), kMaxHarmonicHz);
        matches_.assign(candidates_.size() * kMaxHarmonics, kNoHarmonic);
        for (std::size_t c = 0; c < candidates_.size(); ++c) {
            std::int16_t* row = matches_.data() + c * kMaxHarmonics;
            for (int h = 1; h <= kMaxHarmonics; ++h) {
                const float target = candidates_[c] * static_cast<float>(h);
                if (target > top) {
                    break;
                }
                const float tolerance = std::max(config_.harmonicTolerance * target, binHz_);
                row[h - 1] = static_cast<std::int16_t>(nearestPeak(target, tolerance));
            }
        }
    }
    float first = 0.0f;
    for (int n = 0; n < config_.maxF0Count; ++n) {
        float best = 0.0f;
        float bestF0 = 0.0f;
        for (std::size_t c = 0; c < candidates_.size(); ++c) {
            const float s = salience(c, n == 0 ? 1 : config_.minHarmonics);
            if (s > best) {
                best = s;
                bestF0 = candidates_[c];
            }
        }
        if (!(best > 0.0f)) {
            break;
        }
        // Refine to the fundamental's own peak: the grid is 1/48 octave, the peak is interpolated.
        const int fundamental = nearestPeak(bestF0, std::max(config_.harmonicTolerance * bestF0, binHz_));
        if (fundamental >= 0) {
            bestF0 = peaks_[static_cast<std::size_t>(fundamental)].hz;
        }
        if (n == 0) {
            first = best;
        } else if (best < config_.nextF0Ratio * first) {
            break;
        }
        const int index = static_cast<int>(f0s_.size());
        f0s_.push_back(bestF0);
        // Cancel: every harmonic this fundamental explains is removed from the residual.
        const float top = std::min(0.5f * static_cast<float>(sampleRate_), kMaxHarmonicHz);
        for (int h = 1; h <= kMaxHarmonics; ++h) {
            const float target = bestF0 * static_cast<float>(h);
            if (target > top) {
                break;
            }
            const float tolerance = std::max(config_.harmonicTolerance * target, binHz_);
            const int idx = nearestPeak(target, tolerance);
            if (idx < 0 || !(residual_[static_cast<std::size_t>(idx)] > 0.0f)) {
                continue;
            }
            Peak& p = peaks_[static_cast<std::size_t>(idx)];
            p.f0 = index;
            p.misfit = std::clamp(std::abs(p.hz - target) / tolerance, 0.0f, 1.0f);
            residual_[static_cast<std::size_t>(idx)] = 0.0f;
        }
    }
    double explained = 0.0;
    double explainedFirst = 0.0;
    double misfit = 0.0;
    for (const Peak& p : peaks_) {
        if (p.f0 >= 0) {
            explained += p.energy;
            if (p.f0 == 0) {
                explainedFirst += p.energy;
            }
        }
        misfit += static_cast<double>(p.energy) * static_cast<double>(p.f0 >= 0 ? p.misfit : 1.0f);
    }
    out.f0Count = static_cast<int>(f0s_.size());
    out.f0Hz = f0s_.empty() ? 0.0f : f0s_.front();
    out.harmonicity = static_cast<float>(std::clamp(explained / totalPower, 0.0, 1.0));
    out.pitchConfidence = peakEnergy > 0.0 ? static_cast<float>(std::clamp(explainedFirst / peakEnergy, 0.0, 1.0)) : 0.0f;
    out.inharmonicity = peakEnergy > 0.0 ? static_cast<float>(std::clamp(misfit / peakEnergy, 0.0, 1.0)) : 0.0f;
}

TimbreFeatures TimbreAnalyzer::analyze(const analysis::AnalysisFrame& frame) {
    TimbreFeatures out;
    out.loudnessDb = frame.rms > 1e-6f ? 20.0f * std::log10(frame.rms) : -120.0f;
    out.centroidHz = frame.centroidHz;
    out.highRatioDb = frame.highRatioDb;
    out.relativeFlux = frame.relativeFlux;
    out.width = frame.width;
    out.stereo = frame.stereo;
    out.causal = frame.causal; // ADR-1060/1062: the causal onsets, carried with the snapshot to the response model
    const std::vector<float>& m = frame.magnitude;
    const std::size_t bins = m.size();
    if (bins < 8) {
        return out;
    }
    double total = 0.0;
    double low = 0.0;
    double weighted = 0.0;
    double magSum = 0.0;
    for (std::size_t k = 1; k < bins; ++k) {
        const double mk = m[k];
        const double p = mk * mk;
        const float hz = static_cast<float>(k) * binHz_;
        total += p;
        magSum += mk;
        if (hz < kLowSplitHz) {
            low += p;
        }
        const double d = static_cast<double>(hz) - static_cast<double>(frame.centroidHz);
        weighted += mk * d * d;
    }
    out.silent = !(total > 1e-12);
    if (out.silent) {
        return out;
    }
    out.bandwidthHz = magSum > 0.0 ? static_cast<float>(std::sqrt(weighted / magSum)) : 0.0f;
    out.lowRatioDb = static_cast<float>(10.0 * std::log10(std::max(low, 1e-12) / total));
    // Rolloff: the frequency below which kRolloff of the power lies.
    {
        const double target = total * static_cast<double>(kRolloff);
        double acc = 0.0;
        for (std::size_t k = 1; k < bins; ++k) {
            acc += static_cast<double>(m[k]) * static_cast<double>(m[k]);
            if (acc >= target) {
                out.rolloffHz = static_cast<float>(k) * binHz_;
                break;
            }
        }
    }
    // Flatness over the musical range, with a floor 60 dB under the mean power so that bins a synthesizer left
    // exactly empty do not drag the geometric mean to zero.
    {
        const auto first = static_cast<std::size_t>(std::ceil(kFlatLowHz / binHz_));
        const auto last = std::min(bins - 1, static_cast<std::size_t>(kFlatHighHz / binHz_));
        if (last > first) {
            double mean = 0.0;
            for (std::size_t k = first; k <= last; ++k) {
                mean += static_cast<double>(m[k]) * static_cast<double>(m[k]);
            }
            const auto n = static_cast<double>(last - first + 1);
            mean /= n;
            const double floor = std::max(mean * dbToPower(-60.0), 1e-20);
            double logSum = 0.0;
            for (std::size_t k = first; k <= last; ++k) {
                logSum += std::log(static_cast<double>(m[k]) * static_cast<double>(m[k]) + floor);
            }
            out.flatness = static_cast<float>(std::clamp(std::exp(logSum / n) / (mean + floor), 0.0, 1.0));
        }
    }
    findPeaks(m);
    double peakEnergy = 0.0;
    for (const Peak& p : peaks_) {
        peakEnergy += p.energy;
    }
    out.peakCount = static_cast<float>(peaks_.size());
    out.tonalness = static_cast<float>(std::clamp(peakEnergy / total, 0.0, 1.0));
    estimateFundamentals(out, total);
    // Sensory dissonance (Sethares 1993, after Plomp and Levelt): every pair of the strongest peaks, weighted by
    // the product of their amplitudes, divided by the sum of squared amplitudes so the level cancels.
    {
        scratch_.assign(peaks_.begin(), peaks_.end());
        const auto n = std::min<std::size_t>(scratch_.size(), static_cast<std::size_t>(config_.dissonancePeaks));
        std::partial_sort(scratch_.begin(), scratch_.begin() + static_cast<std::ptrdiff_t>(n), scratch_.end(),
                          [](const Peak& x, const Peak& y) { return x.amplitude > y.amplitude; });
        // Each peak's weight is its amplitude compressed toward loudness (a^0.6), so a dense comb of upper partials
        // (distortion's signature) counts for what it sounds like rather than vanishing under the fundamental.
        double sum = 0.0;
        double norm = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            const Peak& a = scratch_[i];
            const double la = std::pow(static_cast<double>(a.amplitude), 0.6);
            norm += la * la;
            for (std::size_t j = i + 1; j < n; ++j) {
                const Peak& b = scratch_[j];
                const double lb = std::pow(static_cast<double>(b.amplitude), 0.6);
                const double fmin = std::min(a.hz, b.hz);
                const double s = 0.24 / (0.0207 * fmin + 18.96);
                const double x = s * std::abs(static_cast<double>(b.hz) - static_cast<double>(a.hz));
                sum += la * lb * (std::exp(-3.5 * x) - std::exp(-5.75 * x));
            }
        }
        out.dissonance = norm > 0.0 ? static_cast<float>(sum / norm) : 0.0f;
    }
    return out;
}

std::vector<TimbreFeatures> analyzeTimbre(const analysis::AnalysisTrack& track, TimbreConfig config) {
    std::vector<TimbreFeatures> out;
    if (track.empty()) {
        return out;
    }
    TimbreAnalyzer analyzer(track.config().sampleRate, track.config().windowSize, config);
    out.reserve(track.frames().size());
    for (const analysis::AnalysisFrame& frame : track.frames()) {
        out.push_back(analyzer.analyze(frame));
    }
    return out;
}

} // namespace avgen::sonic
