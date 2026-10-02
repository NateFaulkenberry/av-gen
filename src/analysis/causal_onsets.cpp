#include "analysis/causal_onsets.hpp"

#include "analysis/analyzer.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::analysis {

namespace {

constexpr float kCompression = 40.0f; // Y = log(1 + 40 |X| / ref): tuned on the kit at full level and 12 dB down

float smoothstep(float a, float b, float x) {
    const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float powerDb(double power) {
    // Summed squared sine-normalised magnitude over kHannEnergyGain reads a full-scale sine as 1 (0 dB).
    const double p = power / kHannEnergyGain;
    return p > 1e-12 ? static_cast<float>(10.0 * std::log10(p)) : -120.0f;
}

} // namespace

const char* hitClassName(HitClass c) {
    switch (c) {
    case HitClass::Kick: return "kick";
    case HitClass::Low: return "low";
    case HitClass::Snare: return "snare";
    case HitClass::Hat: return "hat";
    case HitClass::Onset: return "onset";
    case HitClass::Count: break;
    }
    return "unknown";
}

float HitPicker::step(float ratio, double dt) {
    since += dt;
    float fired = 0.0f;
    if (armed && ratio >= fire && ratio >= previous && since >= refractory) {
        fired = std::clamp((ratio - fire) / std::max(span, 1e-3f) + floor, floor, 1.0f);
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
    case HitClass::Kick: p.refractory = 0.11; p.fire = 2.0f; p.span = 20.0f; break;
    case HitClass::Low: p.refractory = 0.10; p.fire = 1.7f; p.span = 20.0f; break;
    case HitClass::Snare: p.refractory = 0.09; p.fire = 4.0f; p.span = 40.0f; break;
    case HitClass::Hat: p.refractory = 0.045; p.fire = 2.5f; p.span = 15.0f; break;
    case HitClass::Onset: p.refractory = 0.04; p.fire = 1.8f; p.span = 15.0f; break;
    case HitClass::Count: break;
    }
    return p;
}

CausalOnsetDetector::CausalOnsetDetector(CausalOnsetConfig config) : config_(config) {
    reset();
}

void CausalOnsetDetector::reset() {
    havePrevious_ = false;
    for (std::size_t c = 0; c < kHitClassCount; ++c) {
        history_[c].assign(static_cast<std::size_t>(std::max(config_.medianFrames, 1)), 0.0f);
        head_[c] = 0;
        pickers_[c] = defaultHitPicker(static_cast<HitClass>(c));
        peak_[c] = 0.0f;
    }
    previousOnsetRatio_ = 0.0f;
    snareFloorDb_ = -120.0f;
    reference_ = 0.0f;
}

void CausalOnsetDetector::layout(std::size_t bins, float binHz) {
    bins_ = bins;
    binHz_ = binHz;
    const auto band = [&](float lo, float hi) {
        Band b;
        b.from = std::min(bins, static_cast<std::size_t>(std::max(1.0f, std::round(lo / binHz))));
        b.to = std::min(bins, std::max(b.from + 1, static_cast<std::size_t>(std::round(hi / binHz)) + 1));
        return b;
    };
    kick_ = band(40.0f, 120.0f);
    harmonic_ = band(130.0f, 400.0f);
    body_ = band(150.0f, 300.0f);
    snare_ = band(1500.0f, 5000.0f);
    hat_ = band(7000.0f, 16000.0f);
    broad_ = band(30.0f, 16000.0f);
    bass_ = band(30.0f, 150.0f);
    split_ = {band(40.0f, 120.0f), band(121.0f, 400.0f), band(401.0f, 1500.0f), band(1501.0f, 5000.0f),
              band(5001.0f, 7000.0f), band(7001.0f, 16000.0f)};
    previous_.assign(bins, 0.0f);
    flux_.assign(bins, 0.0f);
    scratch_.assign(history_[0].size(), 0.0f);
    havePrevious_ = false;
}

float CausalOnsetDetector::bandMean(const Band& b) const {
    if (b.to <= b.from) {
        return 0.0f;
    }
    double sum = 0.0;
    for (std::size_t k = b.from; k < b.to; ++k) {
        sum += flux_[k];
    }
    return static_cast<float>(sum / static_cast<double>(b.to - b.from));
}

float CausalOnsetDetector::bandPower(const AnalysisFrame& frame, const Band& b) const {
    double p = 0.0;
    for (std::size_t k = b.from; k < b.to && k < frame.magnitude.size(); ++k) {
        p += static_cast<double>(frame.magnitude[k]) * frame.magnitude[k];
    }
    return powerDb(p);
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
    // ---- SuperFlux over the compressed spectrum ----
    // The compression is relative to a causal peak of the spectrum (up at once, down over 3 s, never below -80 dB),
    // so a take 12 dB quieter gives the same flux: log(1 + 100 |X|) alone is linear for quiet partials and
    // logarithmic for loud ones, which moved the band shares the classes read with the input level.
    const auto& m = frame.magnitude;
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
    for (std::size_t k = 0; k < bins; ++k) {
        const float y = std::log1p(gain * std::max(m[k], 0.0f));
        if (havePrevious_) {
            const auto prev = [&](std::size_t i) { return std::log1p(gain * previous_[i]); };
            const float ref = std::max(prev(k), std::max(prev(k > 0 ? k - 1 : k), prev(k + 1 < bins ? k + 1 : k)));
            flux_[k] = std::max(0.0f, y - ref);
        } else {
            flux_[k] = 0.0f;
        }
    }
    // The previous frame is kept raw and compressed with this frame's reference, so the two are compared alike.
    for (std::size_t k = 0; k < bins; ++k) {
        previous_[k] = std::max(m[k], 0.0f);
    }
    const bool first = !havePrevious_;
    havePrevious_ = true;

    const float kick = bandMean(kick_);
    const float harmonic = bandMean(harmonic_);
    const float body = bandMean(body_);
    const float snare = bandMean(snare_);
    const float hat = bandMean(hat_);
    const float broad = bandMean(broad_);
    out.flux = broad;
    out.bassDb = bandPower(frame, bass_);
    out.levelDb = bandPower(frame, broad_);
    out.snareDb = bandPower(frame, snare_);
    out.hatDb = bandPower(frame, hat_);
    // How much of the low attack is the kick band's own: a kick's attack sits below 120 Hz, a bass note's carries up
    // its harmonics. Summed rather than averaged, so the wider harmonic band is not diluted.
    const double kickSum = static_cast<double>(kick) * static_cast<double>(kick_.to - kick_.from);
    const double harmonicSum = static_cast<double>(harmonic) * static_cast<double>(harmonic_.to - harmonic_.from);
    for (std::size_t b = 0; b < split_.size(); ++b) {
        out.bandFlux[b] = bandMean(split_[b]) * static_cast<float>(split_[b].to - split_[b].from);
    }
    out.bodyRatio = body / std::max(snare, 1e-4f);
    out.kickShape = kickSum + harmonicSum > 1e-9 ? static_cast<float>(kickSum / (kickSum + harmonicSum)) : 0.0f;
    // Where this frame's new energy is: each band's share of the summed log-flux. A kick's attack puts about 5-16%
    // of it below 120 Hz (a bright bass pluck 1-2%: its attack spreads up the harmonics); a hat's about 40-90% above
    // 7 kHz.
    float totalFlux = 0.0f;
    for (const float v : out.bandFlux) {
        totalFlux += v;
    }
    const float lowShare = totalFlux > 1e-6f ? out.bandFlux[0] / totalFlux : 0.0f;
    const float hatShare = totalFlux > 1e-6f ? out.bandFlux[5] / totalFlux : 0.0f;
    // How far the snare band's power rose over its own recent floor (a follower that falls at once and rises over
    // 0.4 s): a snare's noise lifts it 10 dB or more, a kick's click (a millisecond in a 43 ms window) a few.
    const float snareRise = out.snareDb - snareFloorDb_;
    snareFloorDb_ = out.snareDb < snareFloorDb_
                        ? out.snareDb
                        : snareFloorDb_ + static_cast<float>(1.0 - std::exp(-hopSeconds / 0.4)) * (out.snareDb - snareFloorDb_);
    out.snareRise = snareRise;

    const std::array<float, kHitClassCount> odf{kick, kick, std::max(snare, 0.5f * (snare + body)), hat, broad};
    // The classes are evaluated with the broadband onset first, so the kick can ask whether a click came with it.
    constexpr std::array<std::size_t, kHitClassCount> kOrder{4, 0, 1, 2, 3};
    float onsetRatio = 0.0f;
    for (const std::size_t c : kOrder) {
        auto& h = history_[c];
        std::copy(h.begin(), h.end(), scratch_.begin());
        const auto mid = scratch_.begin() + static_cast<std::ptrdiff_t>(scratch_.size() / 2);
        std::nth_element(scratch_.begin(), mid, scratch_.end());
        peak_[c] *= static_cast<float>(std::exp(-hopSeconds / std::max(config_.peakSeconds, 1e-3)));
        const float threshold = std::max(config_.lambda * *mid + config_.delta[c], config_.peakShare * peak_[c]);
        peak_[c] = std::max(peak_[c], odf[c]);
        float ratio = first ? 0.0f : odf[c] / threshold;
        switch (static_cast<HitClass>(c)) {
        case HitClass::Kick:
            ratio *= smoothstep(2.6f, 5.0f, std::max(onsetRatio, previousOnsetRatio_)) * smoothstep(0.03f, 0.06f, lowShare);
            break;
        case HitClass::Snare: ratio *= smoothstep(-19.5f, -17.0f, out.snareDb - out.levelDb); break;
        case HitClass::Hat: ratio *= smoothstep(0.25f, 0.35f, hatShare); break;
        default: break;
        }
        out.ratio[c] = ratio;
        out.odf[c] = odf[c];
        if (c == static_cast<std::size_t>(HitClass::Onset)) {
            onsetRatio = ratio;
        }
        h[head_[c]] = odf[c];
        head_[c] = (head_[c] + 1) % h.size();
        const float s = pickers_[c].step(ratio, hopSeconds);
        out.hit[c] = s > 0.0f;
        out.strength[c] = s;
    }
    previousOnsetRatio_ = onsetRatio;
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
