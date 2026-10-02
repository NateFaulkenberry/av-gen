#include "analysis/causal_onsets.hpp"

#include "analysis/analyzer.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::analysis {

namespace {

constexpr float kCompression = 40.0f; // Y = log(1 + 40 |X| / ref)
constexpr double kKickRefractory = 0.11;
constexpr double kGridWindow = 4.0;   // seconds of accepted kicks the period is read from
constexpr double kGridTolerance = 0.02;
constexpr double kOnGrid = 0.03;      // seconds from the grid that count as on it
constexpr double kOffGrid = 0.08;     // ...and as clearly off it
constexpr float kOffGridPenalty = 0.0f;
constexpr float kLowFloorDb = -60.0f;  // the kick's rise is measured from no lower than this
constexpr float kLowLevelDb = -45.0f;  // ...and only once the low band reaches this
constexpr float kSnareHoldDb = 6.0f;
constexpr float kLowShareDb = 12.0f;   // the low band must be within this of the frame's level   // the snare's noise must hold within this of its attack peak

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
    case HitClass::Kick: p.refractory = kKickRefractory; p.fire = 1.0f; break; // decided by its score, not a ratio
    case HitClass::Low: p.refractory = 0.10; p.fire = 1.7f; break;
    case HitClass::Snare: p.refractory = 0.09; p.fire = 3.0f; break;
    case HitClass::Hat: p.refractory = 0.045; p.fire = 3.0f; break;
    case HitClass::Onset: p.refractory = 0.04; p.fire = 1.8f; break;
    case HitClass::Count: break;
    }
    return p;
}

CausalOnsetDetector::CausalOnsetDetector(CausalOnsetConfig config) : config_(config) {
    reset();
}

void CausalOnsetDetector::reset() {
    havePrevious_ = false;
    reference_ = 0.0f;
    seconds_ = 0.0;
    frames_ = 0;
    for (std::size_t c = 0; c < kHitClassCount; ++c) {
        history_[c].assign(static_cast<std::size_t>(std::max(config_.medianFrames, 1)), 0.0f);
        head_[c] = 0;
        pickers_[c] = defaultHitPicker(static_cast<HitClass>(c));
    }
    scratch_.assign(history_[0].size(), 0.0f);
    lowDb_.fill(-120.0f);
    subDb_.fill(-120.0f);
    rise_.fill(0.0f);
    click_.fill(0.0f);
    lastKick_ = -1e9;
    kicks_.clear();
    noiseHistory_.fill(-120.0f);
    pending_.clear();
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
    low_ = band(40.0f, 120.0f);
    sub_ = band(30.0f, 70.0f);
    snare_ = band(1500.0f, 5000.0f);
    hat_ = band(7000.0f, 16000.0f);
    broad_ = band(30.0f, 16000.0f);
    bass_ = band(30.0f, 150.0f);
    previous_.assign(bins, 0.0f);
    flux_.assign(bins, 0.0f);
    percussive_.assign(bins, 0.0f);
    window_.assign(static_cast<std::size_t>(2 * std::max(config_.percussiveHalfWidth, 1) + 1), 0.0f);
    havePrevious_ = false;
}

float CausalOnsetDetector::meanOf(const std::vector<float>& v, const Band& b) {
    if (b.to <= b.from) {
        return 0.0f;
    }
    double sum = 0.0;
    for (std::size_t k = b.from; k < b.to; ++k) {
        sum += v[k];
    }
    return static_cast<float>(sum / static_cast<double>(b.to - b.from));
}

float CausalOnsetDetector::bandDb(const AnalysisFrame& frame, const Band& b) const {
    double p = 0.0;
    for (std::size_t k = b.from; k < b.to && k < frame.magnitude.size(); ++k) {
        p += static_cast<double>(frame.magnitude[k]) * frame.magnitude[k];
    }
    return powerDb(p);
}

float CausalOnsetDetector::ratioOf(std::size_t c, float odf) {
    auto& h = history_[c];
    std::copy(h.begin(), h.end(), scratch_.begin());
    const auto mid = scratch_.begin() + static_cast<std::ptrdiff_t>(scratch_.size() / 2);
    std::nth_element(scratch_.begin(), mid, scratch_.end());
    const float threshold = config_.lambda * *mid + config_.delta[c];
    h[head_[c]] = odf;
    head_[c] = (head_[c] + 1) % h.size();
    return odf / std::max(threshold, 1e-9f);
}

void CausalOnsetDetector::decideKick(CausalOnsets& out, double hop) {
    constexpr auto kKick = static_cast<std::size_t>(HitClass::Kick);
    // The candidate is the previous hop: a local maximum of the rise, decided now that this hop is known.
    const float r = rise_[1];
    if (!(r >= config_.kickCandidateRise && r >= rise_[2] && r >= rise_[0])) {
        return;
    }
    const double t = seconds_ - hop;
    if (t - lastKick_ < kKickRefractory) {
        return;
    }
    // The click from three hops before the peak to this hop: a kick's sweep starts above the low band and falls into
    // it, so the band's rise peaks two or three hops after the beater's click.
    const float click = *std::max_element(click_.begin(), click_.end());
    // The period the recent kicks keep: a comb over their pairwise differences, the longest period that explains
    // most of them (its divisors explain them too, so the longest is the kick's own).
    float onGrid = 0.0f;
    float offGrid = 0.0f;
    while (!kicks_.empty() && t - kicks_.front() > kGridWindow) {
        kicks_.erase(kicks_.begin());
    }
    if (kicks_.size() >= 4) {
        std::size_t pairs = 0;
        for (std::size_t a = 0; a < kicks_.size(); ++a) {
            pairs += kicks_.size() - a - 1;
        }
        double bestScore = 0.0;
        std::array<double, 251> scores{}; // periods 0.25 .. 1.5 s in 5 ms steps
        for (std::size_t s = 0; s < scores.size(); ++s) {
            const double period = 0.25 + 0.005 * static_cast<double>(s);
            int fit = 0;
            for (std::size_t a = 0; a < kicks_.size(); ++a) {
                for (std::size_t b = a + 1; b < kicks_.size(); ++b) {
                    const double d = kicks_[b] - kicks_[a];
                    const double n = std::round(d / period);
                    fit += (n >= 1.0 && std::abs(d - n * period) <= kGridTolerance) ? 1 : 0;
                }
            }
            scores[s] = static_cast<double>(fit) / static_cast<double>(pairs);
            bestScore = std::max(bestScore, scores[s]);
        }
        if (bestScore >= 0.6) {
            double period = 0.25;
            for (std::size_t s = 0; s < scores.size(); ++s) {
                if (scores[s] >= 0.9 * bestScore) {
                    period = 0.25 + 0.005 * static_cast<double>(s);
                }
            }
            const double phase = std::fmod((t - kicks_.back()) / period, 1.0);
            const double distance = std::min(phase, 1.0 - phase) * period;
            onGrid = distance <= kOnGrid ? 1.0f : 0.0f;
            offGrid = distance >= kOffGrid ? 1.0f : 0.0f;
            out.kickPeriod = static_cast<float>(period);
        }
    }
    const float score = std::min(r / config_.kickRiseScale, config_.kickRiseCap) +
                        std::min(click / config_.kickClickScale, 1.5f) + config_.kickGrid * onGrid -
                        kOffGridPenalty * offGrid;
    const bool strong = r >= config_.kickStrongRise && click > config_.kickStrongClick;
    if (!strong && score < config_.kickThreshold) {
        return;
    }
    out.hit[kKick] = true;
    out.deferred[kKick] = true;
    out.kickScore = score;
    float strength = std::clamp(0.35f + 0.65f * (score - config_.kickThreshold) / 1.5f, 0.35f, 1.0f);
    if (strong) {
        strength = std::max(strength, 0.6f);
    }
    out.strength[kKick] = strength;
    lastKick_ = t;
    kicks_.push_back(t);
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
    seconds_ = static_cast<double>(frames_) * hopSeconds;
    ++frames_;
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
    const bool first = !havePrevious_;
    for (std::size_t k = 0; k < bins; ++k) {
        if (first) {
            flux_[k] = 0.0f;
            continue;
        }
        const auto prev = [&](std::size_t i) { return std::log1p(gain * previous_[i]); };
        const float ref = std::max(prev(k), std::max(prev(k > 0 ? k - 1 : k), prev(k + 1 < bins ? k + 1 : k)));
        flux_[k] = std::max(0.0f, std::log1p(gain * std::max(m[k], 0.0f)) - ref);
    }
    for (std::size_t k = 0; k < bins; ++k) {
        previous_[k] = std::max(m[k], 0.0f);
    }
    havePrevious_ = true;

    // ---- the frequency medians: percussive flux over 1.5-16 kHz, the noise floor over 1.5-5 kHz ----
    const auto hw = static_cast<std::size_t>(std::max(config_.percussiveHalfWidth, 1));
    const auto median = [&](const std::vector<float>& v, std::size_t k) {
        const std::size_t lo = k >= hw ? k - hw : 0;
        const std::size_t hi = std::min(bins, k + hw + 1);
        const std::size_t n = hi - lo;
        std::copy(v.begin() + static_cast<std::ptrdiff_t>(lo), v.begin() + static_cast<std::ptrdiff_t>(hi),
                  window_.begin());
        const auto mid = window_.begin() + static_cast<std::ptrdiff_t>(n / 2);
        std::nth_element(window_.begin(), mid, window_.begin() + static_cast<std::ptrdiff_t>(n));
        return *mid;
    };
    for (std::size_t k = snare_.from; k < hat_.to; ++k) {
        percussive_[k] = median(flux_, k);
    }
    double noise = 0.0;
    for (std::size_t k = snare_.from; k < snare_.to; ++k) {
        noise += median(m, k);
    }
    noise /= static_cast<double>(std::max<std::size_t>(snare_.to - snare_.from, 1));
    out.noiseDb = noise > 1e-12 ? static_cast<float>(20.0 * std::log10(noise)) : -120.0f;

    // ---- levels ----
    out.bassDb = bandDb(frame, bass_);
    out.levelDb = bandDb(frame, broad_);
    out.snareDb = bandDb(frame, snare_);
    out.hatDb = bandDb(frame, hat_);
    const float lowDb = bandDb(frame, low_);
    const float subDb = bandDb(frame, sub_);

    // ---- the ODFs and their ratios ----
    constexpr auto kKick = static_cast<std::size_t>(HitClass::Kick);
    constexpr auto kLow = static_cast<std::size_t>(HitClass::Low);
    constexpr auto kSnare = static_cast<std::size_t>(HitClass::Snare);
    constexpr auto kHat = static_cast<std::size_t>(HitClass::Hat);
    constexpr auto kOnset = static_cast<std::size_t>(HitClass::Onset);
    out.flux = meanOf(flux_, broad_);
    out.click = meanOf(percussive_, snare_);
    out.odf[kLow] = meanOf(flux_, low_);
    out.odf[kSnare] = meanOf(percussive_, snare_);
    out.odf[kHat] = meanOf(percussive_, hat_);
    out.odf[kOnset] = out.flux;
    for (const std::size_t c : {kLow, kSnare, kHat, kOnset}) {
        const float ratio = ratioOf(c, out.odf[c]);
        out.ratio[c] = first ? 0.0f : ratio;
    }
    for (const std::size_t c : {kLow, kHat, kOnset}) {
        const float s = pickers_[c].step(out.ratio[c], hopSeconds);
        out.hit[c] = s > 0.0f;
        out.strength[c] = s;
    }

    // ---- the kick: the low band's rise over its own floor (the minimum of the previous three hops), the click,
    // the period. lowDb_[1..3] hold the previous three hops. ----
    // Measured from no lower than kLowFloorDb, and only when the band is at least kLowLevelDb: from silence a hat's
    // sidelobe "rises" tens of dB.
    const auto riseOf = [](const std::array<float, 4>& db, float now) {
        if (now < kLowLevelDb) {
            return 0.0f;
        }
        return now - std::max(kLowFloorDb, std::min(db[1], std::min(db[2], db[3])));
    };
    // A kick's body is a large part of the frame; a snare's body leaking through the window's sidelobes is not.
    const bool lowCarries = std::max(lowDb, subDb) >= out.levelDb - kLowShareDb;
    const float rise = lowCarries ? std::max(riseOf(lowDb_, lowDb), riseOf(subDb_, subDb)) : 0.0f;
    for (std::size_t i = 1; i + 1 < lowDb_.size(); ++i) {
        lowDb_[i] = lowDb_[i + 1];
        subDb_[i] = subDb_[i + 1];
    }
    lowDb_.back() = lowDb;
    subDb_.back() = subDb;
    rise_ = {rise_[1], rise_[2], frames_ > 4 ? rise : 0.0f};
    std::rotate(click_.begin(), click_.begin() + 1, click_.end());
    click_.back() = out.click;
    out.lowRise = rise_[2];
    out.odf[kKick] = rise_[2];
    out.ratio[kKick] = rise_[2] / std::max(config_.kickCandidateRise, 1e-3f);
    // A kick's body is still there on the decision hop; a snare's onset smears into the low band for one frame.
    if (frames_ > 5 && lowCarries) {
        decideKick(out, hopSeconds);
    }

    // ---- the snare: an attack now, confirmed kDeferFrames hops later by a noise floor that stayed up ----
    for (Pending& p : pending_) {
        ++p.age;
        if (p.age <= 1) {
            p.peak = std::max(p.peak, out.noiseDb); // the attack and the hop after it
        }
    }
    while (!pending_.empty() && pending_.front().age >= kDeferFrames) {
        const Pending p = pending_.front();
        pending_.erase(pending_.begin());
        if (!out.hit[kSnare] && out.noiseDb - p.before >= config_.snareNoiseRise &&
            out.noiseDb >= p.peak - kSnareHoldDb) {
            out.hit[kSnare] = true;
            out.deferred[kSnare] = true;
            out.strength[kSnare] = p.strength;
        }
    }
    {
        const float s = pickers_[kSnare].step(out.ratio[kSnare], hopSeconds);
        if (s > 0.0f && pending_.size() < 8) {
            pending_.push_back(Pending{0, noiseHistory_.back(), s, out.noiseDb});
        }
    }
    for (std::size_t i = 0; i + 1 < noiseHistory_.size(); ++i) {
        noiseHistory_[i] = noiseHistory_[i + 1];
    }
    noiseHistory_.back() = out.noiseDb;

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
