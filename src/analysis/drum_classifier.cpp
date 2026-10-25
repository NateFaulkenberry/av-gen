#include "analysis/drum_classifier.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::analysis {

namespace {

constexpr double kKickRefractory = 0.11;
constexpr double kGridWindow = 4.0;   // seconds of accepted kicks the period is read from
constexpr double kGridTolerance = 0.02;
constexpr double kOnGrid = 0.03;      // seconds from the grid that count as on it
constexpr float kLowFloorDb = -60.0f; // the kick's rise is measured from no lower than this
constexpr float kLowLevelDb = -45.0f; // ...and only once the low band reaches this
constexpr float kSnareHoldDb = 6.0f;  // the snare's noise must hold within this of its attack peak
constexpr float kLowShareDb = 12.0f;  // the low band must be within this of the frame's level

} // namespace

const char* drumClassName(DrumClass c) {
    switch (c) {
    case DrumClass::Kick: return "kick";
    case DrumClass::Snare: return "snare";
    case DrumClass::Hat: return "hat";
    case DrumClass::Count: break;
    }
    return "unknown";
}

DrumClassifier::DrumClassifier(DrumClassifierConfig config) : config_(config) {
    reset();
}

void DrumClassifier::reset() {
    seconds_ = 0.0;
    frames_ = 0;
    snareThreshold_.reset(config_.medianFrames);
    hatThreshold_.reset(config_.medianFrames);
    snarePicker_ = HitPicker{};
    snarePicker_.refractory = 0.09;
    snarePicker_.fire = 3.0f;
    hatPicker_ = HitPicker{};
    hatPicker_.refractory = 0.045;
    hatPicker_.fire = 3.0f;
    lowDb_.fill(-120.0f);
    subDb_.fill(-120.0f);
    rise_.fill(0.0f);
    click_.fill(0.0f);
    lastKick_ = -1e9;
    kicks_.clear();
    noiseHistory_.fill(-120.0f);
    pending_.clear();
}

void DrumClassifier::layout(std::size_t bins, float binHz) {
    bins_ = bins;
    binHz_ = binHz;
    low_ = binRange(40.0f, 120.0f, bins, binHz);
    sub_ = binRange(30.0f, 70.0f, bins, binHz);
    snare_ = binRange(1500.0f, 5000.0f, bins, binHz);
    hat_ = binRange(7000.0f, 16000.0f, bins, binHz);
    broad_ = binRange(30.0f, 16000.0f, bins, binHz);
    percussive_.assign(bins, 0.0f);
    window_.assign(static_cast<std::size_t>(2 * std::max(config_.percussiveHalfWidth, 1) + 1), 0.0f);
}

void DrumClassifier::decideKick(DrumHits& out, double hop) {
    constexpr auto kKick = static_cast<std::size_t>(DrumClass::Kick);
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
        }
    }
    const float score = std::min(r / config_.kickRiseScale, config_.kickRiseCap) +
                        std::min(click / config_.kickClickScale, 1.5f) + config_.kickGrid * onGrid;
    const bool strong = r >= config_.kickStrongRise && click > config_.kickStrongClick;
    if (!strong && score < config_.kickThreshold) {
        return;
    }
    out.hit[kKick] = true;
    float strength = std::clamp(0.35f + 0.65f * (score - config_.kickThreshold) / 1.5f, 0.35f, 1.0f);
    if (strong) {
        strength = std::max(strength, 0.6f);
    }
    out.strength[kKick] = strength;
    lastKick_ = t;
    kicks_.push_back(t);
}

DrumHits DrumClassifier::process(std::span<const float> m, std::span<const float> flux, bool first, float binHz,
                                 double hopSeconds) {
    DrumHits out;
    const std::size_t bins = m.size();
    if (bins < 8 || binHz <= 0.0f || flux.size() != bins) {
        return out;
    }
    if (bins != bins_ || binHz != binHz_) {
        layout(bins, binHz);
    }
    seconds_ = static_cast<double>(frames_) * hopSeconds;
    ++frames_;

    // ---- the frequency medians: percussive flux over 1.5-16 kHz, the noise floor over 1.5-5 kHz ----
    const auto hw = static_cast<std::size_t>(std::max(config_.percussiveHalfWidth, 1));
    const auto median = [&](std::span<const float> v, std::size_t k) {
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
        percussive_[k] = median(flux, k);
    }
    double noise = 0.0;
    for (std::size_t k = snare_.from; k < snare_.to; ++k) {
        noise += median(m, k);
    }
    noise /= static_cast<double>(std::max<std::size_t>(snare_.to - snare_.from, 1));
    const float noiseDb = noise > 1e-12 ? static_cast<float>(20.0 * std::log10(noise)) : -120.0f;

    // ---- levels ----
    const float levelDb = bandPowerDb(m, broad_.from, broad_.to);
    const float lowDb = bandPowerDb(m, low_.from, low_.to);
    const float subDb = bandPowerDb(m, sub_.from, sub_.to);

    // ---- the snare's and the hat's ODFs, ratios and timbre gates (ADR-1068) ----
    const float click = bandMean(percussive_, snare_.from, snare_.to);
    const float snareOdf = click;
    const float hatOdf = bandMean(percussive_, hat_.from, hat_.to);
    float snareRatio = snareThreshold_.ratio(snareOdf, config_.lambda, config_.snareDelta);
    float hatRatio = hatThreshold_.ratio(hatOdf, config_.lambda, config_.hatDelta);
    if (first) {
        snareRatio = 0.0f;
        hatRatio = 0.0f;
    }
    // A drum's attack is noise, so nearly all of its band's flux survives the frequency median; a pluck's attack is
    // mostly its partials. And a hat lives above 7 kHz, where a click's attack spreads evenly or falls off.
    const float rawSnare = bandMean(flux, snare_.from, snare_.to);
    const float snareNoise = rawSnare > 1e-9f ? snareOdf / rawSnare : 0.0f;
    const float hatTilt = hatOdf / std::max(snareOdf, 1e-6f);
    if (hatTilt < config_.hatTilt) {
        hatRatio = std::min(hatRatio, 0.0f);
    }
    if (snareNoise < config_.noiseShare) {
        snareRatio = std::min(snareRatio, 0.0f);
    }
    constexpr auto kSnare = static_cast<std::size_t>(DrumClass::Snare);
    constexpr auto kHat = static_cast<std::size_t>(DrumClass::Hat);
    {
        const float s = hatPicker_.step(hatRatio, hopSeconds);
        out.hit[kHat] = s > 0.0f;
        out.strength[kHat] = s;
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
    const bool lowCarries = std::max(lowDb, subDb) >= levelDb - kLowShareDb;
    const float rise = lowCarries ? std::max(riseOf(lowDb_, lowDb), riseOf(subDb_, subDb)) : 0.0f;
    for (std::size_t i = 1; i + 1 < lowDb_.size(); ++i) {
        lowDb_[i] = lowDb_[i + 1];
        subDb_[i] = subDb_[i + 1];
    }
    lowDb_.back() = lowDb;
    subDb_.back() = subDb;
    rise_ = {rise_[1], rise_[2], frames_ > 4 ? rise : 0.0f};
    std::rotate(click_.begin(), click_.begin() + 1, click_.end());
    click_.back() = click;
    // A kick's body is still there on the decision hop; a snare's onset smears into the low band for one frame.
    if (frames_ > 5 && lowCarries) {
        decideKick(out, hopSeconds);
    }

    // ---- the snare: an attack now, confirmed kDeferFrames hops later by a noise floor that stayed up ----
    for (Pending& p : pending_) {
        ++p.age;
        if (p.age <= 1) {
            p.peak = std::max(p.peak, noiseDb); // the attack and the hop after it
        }
    }
    while (!pending_.empty() && pending_.front().age >= kDeferFrames) {
        const Pending p = pending_.front();
        pending_.erase(pending_.begin());
        if (!out.hit[kSnare] && noiseDb - p.before >= config_.snareNoiseRise && noiseDb >= p.peak - kSnareHoldDb) {
            out.hit[kSnare] = true;
            out.strength[kSnare] = p.strength;
        }
    }
    {
        const float s = snarePicker_.step(snareRatio, hopSeconds);
        if (s > 0.0f && pending_.size() < 8) {
            pending_.push_back(Pending{0, noiseHistory_.back(), s, noiseDb});
        }
    }
    for (std::size_t i = 0; i + 1 < noiseHistory_.size(); ++i) {
        noiseHistory_[i] = noiseHistory_[i + 1];
    }
    noiseHistory_.back() = noiseDb;
    return out;
}

} // namespace avgen::analysis
