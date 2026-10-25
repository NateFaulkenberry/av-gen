#include "analysis/causal_onsets.hpp"

#include "analysis/analyzer.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::analysis {

namespace {

constexpr float kCompression = 40.0f; // Y = log(1 + 40 |X| / ref)

} // namespace

const char* hitClassName(HitClass c) {
    switch (c) {
    case HitClass::Low: return "low";
    case HitClass::Onset: return "onset";
    case HitClass::Count: break;
    }
    return "unknown";
}

float HitPicker::step(float ratio, double dt) {
    since += dt;
    float fired = 0.0f;
    if (armed && ratio >= fire && ratio >= previous && since >= refractory) {
        fired = std::clamp(floor + (1.0f - floor) * (1.0f - fire / std::max(ratio, 1e-6f)), floor, 1.0f);
        armed = false;
        since = 0.0;
    } else if (!armed && ratio < rearm * fire) {
        armed = true;
    }
    previous = ratio;
    return fired;
}

HitPicker defaultHitPicker(HitClass c) {
    HitPicker p;
    switch (c) {
    case HitClass::Low: p.refractory = 0.10; p.fire = 1.7f; break;
    case HitClass::Onset: p.refractory = 0.04; p.fire = 1.8f; break;
    case HitClass::Count: break;
    }
    return p;
}

void CausalThreshold::reset(int frames) {
    history_.assign(static_cast<std::size_t>(std::max(frames, 1)), 0.0f);
    scratch_.assign(history_.size(), 0.0f);
    head_ = 0;
}

float CausalThreshold::ratio(float odf, float lambda, float delta) {
    std::copy(history_.begin(), history_.end(), scratch_.begin());
    const auto mid = scratch_.begin() + static_cast<std::ptrdiff_t>(scratch_.size() / 2);
    std::nth_element(scratch_.begin(), mid, scratch_.end());
    const float threshold = lambda * *mid + delta;
    history_[head_] = odf;
    head_ = (head_ + 1) % history_.size();
    return odf / std::max(threshold, 1e-9f);
}

float bandMean(std::span<const float> v, std::size_t from, std::size_t to) {
    if (to <= from || to > v.size()) {
        return 0.0f;
    }
    double sum = 0.0;
    for (std::size_t k = from; k < to; ++k) {
        sum += v[k];
    }
    return static_cast<float>(sum / static_cast<double>(to - from));
}

float bandPowerDb(std::span<const float> magnitude, std::size_t from, std::size_t to) {
    double p = 0.0;
    for (std::size_t k = from; k < to && k < magnitude.size(); ++k) {
        p += static_cast<double>(magnitude[k]) * magnitude[k];
    }
    // Summed squared sine-normalised magnitude over kHannEnergyGain reads a full-scale sine as 1 (0 dB).
    p /= kHannEnergyGain;
    return p > 1e-12 ? static_cast<float>(10.0 * std::log10(p)) : -120.0f;
}

BinRange binRange(float lo, float hi, std::size_t bins, float binHz) {
    BinRange b;
    b.from = std::min(bins, static_cast<std::size_t>(std::max(1.0f, std::round(lo / binHz))));
    b.to = std::min(bins, std::max(b.from + 1, static_cast<std::size_t>(std::round(hi / binHz)) + 1));
    return b;
}

CausalOnsetDetector::CausalOnsetDetector(CausalOnsetConfig config) : config_(config) {
    reset();
}

void CausalOnsetDetector::reset() {
    havePrevious_ = false;
    first_ = true;
    reference_ = 0.0f;
    for (std::size_t c = 0; c < kHitClassCount; ++c) {
        thresholds_[c].reset(config_.medianFrames);
        pickers_[c] = defaultHitPicker(static_cast<HitClass>(c));
    }
}

void CausalOnsetDetector::layout(std::size_t bins, float binHz) {
    bins_ = bins;
    binHz_ = binHz;
    low_ = binRange(40.0f, 120.0f, bins, binHz);
    broad_ = binRange(30.0f, 16000.0f, bins, binHz);
    bass_ = binRange(30.0f, 150.0f, bins, binHz);
    previous_.assign(bins, 0.0f);
    flux_.assign(bins, 0.0f);
    havePrevious_ = false;
}

std::array<float, 3> registerShares(std::span<const float> now, std::span<const float> previous, float binHz) {
    std::array<float, 3> out{};
    const std::size_t bins = std::min(now.size(), previous.size());
    if (!(binHz > 0.0f) || bins < 2) {
        return out;
    }
    constexpr std::array<std::array<float, 2>, 3> kBands{{{30.0f, 250.0f}, {250.0f, 4000.0f}, {4000.0f, 16000.0f}}};
    float total = 0.0f;
    for (std::size_t b = 0; b < kBands.size(); ++b) {
        const auto lo = std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(kBands[b][0] / binHz)));
        const auto hi = std::min(bins, static_cast<std::size_t>(std::ceil(kBands[b][1] / binHz)));
        double gained = 0.0;
        for (std::size_t k = lo; k < hi; ++k) {
            gained += static_cast<double>(now[k]) * now[k] - static_cast<double>(previous[k]) * previous[k];
        }
        out[b] = std::max(0.0f, static_cast<float>(gained));
        total += out[b];
    }
    if (!(total > 0.0f)) {
        return {};
    }
    for (float& v : out) {
        v /= total;
    }
    return out;
}

void CausalOnsetDetector::process(AnalysisFrame& frame, float binHz, double hopSeconds) {
    CausalOnsets out;
    const std::size_t bins = frame.magnitude.size();
    if (bins < 8 || binHz <= 0.0f) {
        frame.causal = out;
        return;
    }
    if (bins != bins_ || binHz != binHz_) {
        layout(bins, binHz);
    }
    const auto& m = frame.magnitude;

    // ---- the compressed spectrum's flux, against a causal spectral peak (level-free) ----
    float framePeak = 0.0f;
    for (std::size_t k = 1; k < bins; ++k) {
        framePeak = std::max(framePeak, m[k]);
    }
    if (framePeak > reference_) {
        reference_ = framePeak;
    } else {
        reference_ += static_cast<float>(1.0 - std::exp(-hopSeconds / 3.0)) * (framePeak - reference_);
    }
    const float gain = kCompression / std::max(reference_, 1e-4f);
    first_ = !havePrevious_;
    for (std::size_t k = 0; k < bins; ++k) {
        if (first_) {
            flux_[k] = 0.0f;
            continue;
        }
        const auto prev = [&](std::size_t i) { return std::log1p(gain * previous_[i]); };
        const float ref = std::max(prev(k), std::max(prev(k > 0 ? k - 1 : k), prev(k + 1 < bins ? k + 1 : k)));
        flux_[k] = std::max(0.0f, std::log1p(gain * std::max(m[k], 0.0f)) - ref);
    }
    if (!first_) {
        out.onsetRegister = registerShares(m, previous_, binHz); // ADR-1604
    }
    for (std::size_t k = 0; k < bins; ++k) {
        previous_[k] = std::max(m[k], 0.0f);
    }
    havePrevious_ = true;

    // ---- levels ----
    out.bassDb = bandPowerDb(m, bass_.from, bass_.to);
    out.levelDb = bandPowerDb(m, broad_.from, broad_.to);

    // ---- the ODFs, their ratios and the decisions ----
    constexpr auto kLow = static_cast<std::size_t>(HitClass::Low);
    constexpr auto kOnset = static_cast<std::size_t>(HitClass::Onset);
    out.flux = bandMean(flux_, broad_.from, broad_.to);
    out.odf[kLow] = bandMean(flux_, low_.from, low_.to);
    out.odf[kOnset] = out.flux;
    for (const std::size_t c : {kLow, kOnset}) {
        const float ratio = thresholds_[c].ratio(out.odf[c], config_.lambda, config_.delta[c]);
        out.ratio[c] = first_ ? 0.0f : ratio;
        const float s = pickers_[c].step(out.ratio[c], hopSeconds);
        out.hit[c] = s > 0.0f;
        out.strength[c] = s;
    }

    out.valid = true;
    frame.causal = out;
}

void detectCausalOnsets(std::span<AnalysisFrame> frames, float binHz, double hopSeconds,
                        const CausalOnsetConfig& config) {
    CausalOnsetDetector detector(config);
    for (AnalysisFrame& f : frames) {
        detector.process(f, binHz, hopSeconds);
    }
}

} // namespace avgen::analysis
