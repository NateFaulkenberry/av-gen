#include "analysis/band_onsets.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::analysis {

namespace {

std::size_t binAt(float hz, float binHz) {
    return static_cast<std::size_t>(std::max(0.0f, std::ceil(hz / binHz - 1e-4f)));
}

float medianOf(std::vector<float>& scratch) {
    const auto mid = scratch.begin() + static_cast<std::ptrdiff_t>(scratch.size() / 2);
    std::nth_element(scratch.begin(), mid, scratch.end());
    return *mid;
}

// Centred running median and maximum over +-`half` frames, the ends padded with the edge value.
std::vector<float> runningMedian(const std::vector<float>& e, std::size_t half) {
    const std::size_t n = e.size();
    std::vector<float> out(n, 0.0f);
    std::vector<float> scratch(2 * half + 1);
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t k = 0; k < scratch.size(); ++k) {
            const auto j = static_cast<std::ptrdiff_t>(i + k) - static_cast<std::ptrdiff_t>(half);
            scratch[k] = e[static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(j, 0, static_cast<std::ptrdiff_t>(n) - 1))];
        }
        out[i] = medianOf(scratch);
    }
    return out;
}

std::vector<float> runningMax(const std::vector<float>& e, std::size_t half) {
    const std::size_t n = e.size();
    std::vector<float> out(n, 0.0f);
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t from = i >= half ? i - half : 0;
        const std::size_t to = std::min(n, i + half + 1);
        out[i] = *std::max_element(e.begin() + static_cast<std::ptrdiff_t>(from), e.begin() + static_cast<std::ptrdiff_t>(to));
    }
    return out;
}

// A peak of `e` at i: over the silence floor, over `threshold` x the running median, the largest in
// its refractory window (strictly larger than anything before it there, so a plateau fires once).
bool isPeak(const std::vector<float>& e, const std::vector<float>& median, std::size_t i, std::size_t window,
            float threshold, double floorPower) {
    const float v = e[i];
    if (!(static_cast<double>(v) > floorPower) || v < threshold * median[i]) {
        return false;
    }
    const std::size_t from = i >= window ? i - window : 0;
    const std::size_t to = std::min(e.size(), i + window + 1);
    for (std::size_t j = from; j < to; ++j) {
        if (j < i ? e[j] >= v : e[j] > v) {
            return false;
        }
    }
    return true;
}

// The attack frame of the peak at i: the steepest one-frame rise of the band's log power in the
// `back` frames up to it. What the onset is stamped on, so its time is the attack, not the crest.
std::size_t attackFrame(const std::vector<float>& e, std::size_t i, std::size_t back) {
    const std::size_t from = std::max<std::size_t>(1, i >= back ? i - back : 0);
    std::size_t best = i;
    double steepest = -1e30;
    for (std::size_t j = from; j <= i; ++j) {
        const double rise = std::log(static_cast<double>(e[j]) + 1e-20) - std::log(static_cast<double>(e[j - 1]) + 1e-20);
        if (rise > steepest) {
            steepest = rise;
            best = j;
        }
    }
    return best;
}

std::vector<float> bandPower(std::span<const AnalysisFrame> frames, std::size_t from, std::size_t to) {
    std::vector<float> e(frames.size(), 0.0f);
    for (std::size_t f = 0; f < frames.size(); ++f) {
        const std::vector<float>& m = frames[f].magnitude;
        double sum = 0.0;
        for (std::size_t k = from; k < to && k < m.size(); ++k) {
            sum += static_cast<double>(m[k]) * static_cast<double>(m[k]);
        }
        e[f] = static_cast<float>(sum);
    }
    return e;
}

std::vector<BandOnset> pickBand(std::span<const AnalysisFrame> frames, const std::vector<float>& e,
                                double hopSeconds, double refractorySeconds, float relative, std::size_t back,
                                const BandOnsetConfig& config) {
    std::vector<BandOnset> out;
    if (frames.size() < 3 || !(hopSeconds > 0.0)) {
        return out;
    }
    const auto half = static_cast<std::size_t>(std::lround(config.medianSeconds / hopSeconds));
    const auto wide = static_cast<std::size_t>(std::lround(config.maxSeconds / hopSeconds));
    const auto window = std::max<std::size_t>(1, static_cast<std::size_t>(std::lround(refractorySeconds / hopSeconds)));
    const std::vector<float> median = runningMedian(e, half);
    const std::vector<float> largest = runningMax(e, wide);
    for (std::size_t i = 1; i + 1 < e.size(); ++i) {
        if (!isPeak(e, median, i, window, config.threshold, config.floorPower)) {
            continue;
        }
        const float rel = largest[i] > 0.0f ? e[i] / largest[i] : 0.0f;
        if (rel < relative) {
            continue;
        }
        out.push_back(BandOnset{attackFrame(e, i, back), std::clamp(rel, 0.0f, 1.0f)});
    }
    return out;
}

} // namespace

BandOnsets detectBandOnsets(std::span<const AnalysisFrame> frames, float binHz, std::span<const double> beatTimes,
                            double beatSeconds, const BandOnsetConfig& config) {
    BandOnsets out;
    if (frames.size() < 3 || frames.front().magnitude.empty() || !(binHz > 0.0f)) {
        return out;
    }
    const double hopSeconds = frames[1].timeSeconds - frames[0].timeSeconds;
    if (!(hopSeconds > 0.0)) {
        return out;
    }
    const std::size_t bins = frames.front().magnitude.size();
    const std::size_t n = frames.size();

    // ---- mid and high: plain band power --------------------------------------------------------------
    out.mid = pickBand(frames, bandPower(frames, binAt(config.midFromHz, binHz), binAt(config.midToHz, binHz)),
                       hopSeconds, config.midRefractorySeconds, config.midRelative, 3, config);
    out.high = pickBand(frames, bandPower(frames, binAt(config.highFromHz, binHz), binAt(config.highToHz, binHz)),
                        hopSeconds, config.highRefractorySeconds, config.highRelative, 3, config);

    // ---- low: the percussive part of the low spectrum ------------------------------------------------
    // Harmonic/percussive separation on the bins the kick needs, plus the neighbours the frequency
    // median reads. A bass note is a horizontal line in the spectrogram and the time median keeps it;
    // a kick is a vertical one and the frequency median keeps it. The soft (Wiener) mask splits each
    // bin's magnitude between the two.
    const std::size_t halfT = static_cast<std::size_t>(std::max(1, config.hpssFrames / 2));
    const std::size_t halfF = static_cast<std::size_t>(std::max(1, config.hpssBins / 2));
    const std::size_t lowFrom = binAt(config.lowFromHz, binHz);
    const std::size_t lowTo = std::min(bins, binAt(config.lowToHz, binHz));
    const std::size_t shareFrom = binAt(config.shareFromHz, binHz);
    const std::size_t shareTo = std::min(bins, binAt(config.shareToHz, binHz));
    const std::size_t used = std::max(lowTo, shareTo);
    const std::size_t read = std::min(bins, used + halfF + 1);
    std::vector<float> percussive(n * used, 0.0f);
    std::vector<float> harmonic(n * used, 0.0f);
    {
        std::vector<float> timeScratch(2 * halfT + 1);
        std::vector<float> freqScratch(2 * halfF + 1);
        for (std::size_t f = 0; f < n; ++f) {
            const std::vector<float>& m = frames[f].magnitude;
            for (std::size_t k = 0; k < used; ++k) {
                for (std::size_t j = 0; j < timeScratch.size(); ++j) {
                    const auto g = static_cast<std::ptrdiff_t>(f + j) - static_cast<std::ptrdiff_t>(halfT);
                    timeScratch[j] = frames[static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(
                                                g, 0, static_cast<std::ptrdiff_t>(n) - 1))]
                                         .magnitude[k];
                }
                for (std::size_t j = 0; j < freqScratch.size(); ++j) {
                    const auto b = static_cast<std::ptrdiff_t>(k + j) - static_cast<std::ptrdiff_t>(halfF);
                    freqScratch[j] = m[static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(
                        b, 0, static_cast<std::ptrdiff_t>(read) - 1))];
                }
                const double h = medianOf(timeScratch);
                const double p = medianOf(freqScratch);
                const double mask = (h * h + p * p) > 0.0 ? (p * p) / (h * h + p * p) : 0.0;
                percussive[f * used + k] = static_cast<float>(m[k] * mask);
                harmonic[f * used + k] = static_cast<float>(m[k] * (1.0 - mask));
            }
        }
    }
    std::vector<float> attack(n, 0.0f);
    std::vector<double> shareP(n, 0.0);
    std::vector<double> shareH(n, 0.0);
    for (std::size_t f = 0; f < n; ++f) {
        double a = 0.0;
        for (std::size_t k = lowFrom; k < lowTo; ++k) {
            a += static_cast<double>(percussive[f * used + k]) * static_cast<double>(percussive[f * used + k]);
        }
        attack[f] = static_cast<float>(a);
        for (std::size_t k = shareFrom; k < shareTo; ++k) {
            shareP[f] += static_cast<double>(percussive[f * used + k]) * static_cast<double>(percussive[f * used + k]);
            shareH[f] += static_cast<double>(harmonic[f * used + k]) * static_cast<double>(harmonic[f * used + k]);
        }
    }

    const auto half = static_cast<std::size_t>(std::lround(config.medianSeconds / hopSeconds));
    const auto wide = static_cast<std::size_t>(std::lround(config.maxSeconds / hopSeconds));
    const auto window =
        std::max<std::size_t>(1, static_cast<std::size_t>(std::lround(config.lowRefractorySeconds / hopSeconds)));
    const std::vector<float> median = runningMedian(attack, half);
    const std::vector<float> largest = runningMax(attack, wide);
    const bool haveGrid = !beatTimes.empty() && beatSeconds > 0.0;
    std::vector<std::size_t> peaks;
    for (std::size_t i = 1; i + 1 < n; ++i) {
        if (isPeak(attack, median, i, window, config.threshold, config.floorPower)) {
            peaks.push_back(i);
        }
    }
    // The track's own scale for a kick: the 90th percentile of its candidate attacks.
    float reference = 0.0f;
    if (!peaks.empty()) {
        std::vector<float> sizes;
        sizes.reserve(peaks.size());
        for (const std::size_t i : peaks) {
            sizes.push_back(attack[i]);
        }
        const auto nth = sizes.begin() + static_cast<std::ptrdiff_t>((sizes.size() - 1) * 9 / 10);
        std::nth_element(sizes.begin(), nth, sizes.end());
        reference = *nth;
    }
    for (const std::size_t i : peaks) {
        if (attack[i] < config.globalRelative * reference) {
            continue;
        }
        const std::size_t at = attackFrame(attack, i, 4);
        double p = 0.0;
        double h = 0.0;
        for (std::size_t f = at; f < std::min(n, at + static_cast<std::size_t>(std::max(1, config.shareFrames))); ++f) {
            p += shareP[f];
            h += shareH[f];
        }
        const float share = p + h > 0.0 ? static_cast<float>(p / (p + h)) : 0.0f;
        const float rel = largest[i] > 0.0f ? attack[i] / largest[i] : 0.0f;
        float needRelative = config.noGridRelative;
        float needShare = config.noGridShare;
        if (haveGrid) {
            const double t = frames[at].timeSeconds;
            const auto it = std::lower_bound(beatTimes.begin(), beatTimes.end(), t);
            double nearest = 1e30;
            if (it != beatTimes.end()) {
                nearest = std::min(nearest, std::fabs(*it - t));
            }
            if (it != beatTimes.begin()) {
                nearest = std::min(nearest, std::fabs(t - *std::prev(it)));
            }
            const bool onGrid = nearest / beatSeconds <= static_cast<double>(config.gridBeats);
            needRelative = onGrid ? config.onGridRelative : config.offGridRelative;
            needShare = onGrid ? config.onGridShare : config.offGridShare;
        }
        if (rel < needRelative || share < needShare) {
            continue;
        }
        out.low.push_back(BandOnset{at, std::clamp(rel, 0.0f, 1.0f)});
    }
    return out;
}

void stampBandOnsets(std::span<AnalysisFrame> frames, const BandOnsets& onsets, const AnalyzerConfig& config) {
    for (AnalysisFrame& f : frames) {
        f.lowOnset = f.midOnset = f.highOnset = false;
        f.lowOnsetStrength = f.midOnsetStrength = f.highOnsetStrength = 0.0f;
    }
    const auto stamp = [&frames](const std::vector<BandOnset>& list, bool AnalysisFrame::*flag,
                                 float AnalysisFrame::*strength) {
        for (const BandOnset& o : list) {
            if (o.frame < frames.size()) {
                frames[o.frame].*flag = true;
                frames[o.frame].*strength = std::max(frames[o.frame].*strength, o.strength);
            }
        }
    };
    stamp(onsets.low, &AnalysisFrame::lowOnset, &AnalysisFrame::lowOnsetStrength);
    stamp(onsets.mid, &AnalysisFrame::midOnset, &AnalysisFrame::midOnsetStrength);
    stamp(onsets.high, &AnalysisFrame::highOnset, &AnalysisFrame::highOnsetStrength);

    // The percussive onset rate and the composite that includes it, walked forward exactly as the
    // analyzer walks its own one-poles, so the offline and streaming versions differ only in their
    // inputs.
    if (frames.empty() || config.sampleRate == 0) {
        return;
    }
    const double hop = static_cast<double>(config.hopSize) / static_cast<double>(config.sampleRate);
    const auto keep = [hop](float seconds) {
        return seconds > 0.0f ? std::exp(-hop / static_cast<double>(seconds)) : 0.0;
    };
    const double rateKeep = keep(config.onsetRateSeconds);
    const double rateImpulse = config.onsetRateSeconds > 0.0f ? 1.0 / static_cast<double>(config.onsetRateSeconds) : 0.0;
    const double energyKeep = keep(config.energySmoothingSeconds);
    constexpr double kSameHitSeconds = 0.03; // a kick with its clap is one hit, not two
    double rate = 0.0;
    double energy = 0.0;
    double lastHit = -1e30;
    for (std::size_t i = 0; i < frames.size(); ++i) {
        AnalysisFrame& f = frames[i];
        rate *= rateKeep;
        if ((f.lowOnset || f.midOnset || f.highOnset) && f.timeSeconds - lastHit >= kSameHitSeconds) {
            rate += rateImpulse;
            lastHit = f.timeSeconds;
        }
        f.onsetRate = static_cast<float>(rate);
        f.onsetRateIsPercussive = true;
        EnergyTerms terms;
        terms.highRatioDb = f.highRatioDb;
        terms.centroidHz = f.centroidHz;
        terms.relativeFlux = f.relativeFlux;
        terms.onsetRate = f.onsetRate;
        terms.hasOnsetRate = true;
        terms.width = f.width;
        terms.hasWidth = f.stereo;
        terms.silent = !(f.rms > 1e-7f);
        const double raw = energyComposite(terms);
        energy = i == 0 ? raw : energyKeep * energy + (1.0 - energyKeep) * raw;
        f.energy = static_cast<float>(energy);
    }
}

} // namespace avgen::analysis
