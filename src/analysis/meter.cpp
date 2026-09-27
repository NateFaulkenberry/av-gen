#include "analysis/meter.hpp"

#include "analysis/analyzer.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace avgen::analysis {

namespace {

std::int64_t floorDiv(std::int64_t a, std::int64_t b) {
    std::int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) {
        --q;
    }
    return q;
}

std::int64_t floorMod(std::int64_t a, std::int64_t b) { return a - floorDiv(a, b) * b; }

double fraction(double x) { return x - std::floor(x); }

float median(std::vector<float> v) {
    if (v.empty()) {
        return 0.0f;
    }
    const auto mid = v.begin() + static_cast<std::ptrdiff_t>(v.size() / 2);
    std::nth_element(v.begin(), mid, v.end());
    return *mid;
}

// The per-frame spectral summary the estimate is made from: log energy in nine octave-ish bands
// (timbre, for "where things change") and in 2-16 kHz (the backbeat's snare or clap).
struct FrameBands {
    static constexpr std::size_t kBands = 9;
    std::vector<std::array<float, kBands>> bands; // dB
    std::vector<float> high;                      // dB, 2-16 kHz
};

FrameBands frameBands(std::span<const AnalysisFrame> frames, float binHz) {
    static constexpr std::array<float, FrameBands::kBands + 1> kEdges{30.0f,   60.0f,   120.0f,  250.0f, 500.0f,
                                                                       1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f};
    FrameBands out;
    out.bands.resize(frames.size());
    out.high.resize(frames.size());
    for (std::size_t f = 0; f < frames.size(); ++f) {
        const std::vector<float>& m = frames[f].magnitude;
        std::array<double, FrameBands::kBands> energy{};
        double high = 0.0;
        for (std::size_t k = 1; k < m.size(); ++k) {
            const float hz = static_cast<float>(k) * binHz;
            const double p = static_cast<double>(m[k]) * static_cast<double>(m[k]);
            if (hz >= 2000.0f && hz < 16000.0f) {
                high += p;
            }
            if (hz < kEdges.front() || hz >= kEdges.back()) {
                continue;
            }
            std::size_t b = 0;
            while (b + 1 < kEdges.size() - 1 && hz >= kEdges[b + 1]) {
                ++b;
            }
            energy[b] += p;
        }
        for (std::size_t b = 0; b < FrameBands::kBands; ++b) {
            out.bands[f][b] = static_cast<float>(10.0 * std::log10(energy[b] + 1e-10));
        }
        out.high[f] = static_cast<float>(10.0 * std::log10(high + 1e-10));
    }
    return out;
}

// The frame whose centre is nearest `seconds`.
std::size_t nearestFrame(std::span<const AnalysisFrame> frames, double seconds) {
    const auto it = std::lower_bound(frames.begin(), frames.end(), seconds,
                                     [](const AnalysisFrame& f, double t) { return f.timeSeconds < t; });
    auto i = static_cast<std::size_t>(it - frames.begin());
    if (i >= frames.size()) {
        return frames.size() - 1;
    }
    if (i > 0 && seconds - frames[i - 1].timeSeconds < frames[i].timeSeconds - seconds) {
        --i;
    }
    return i;
}

// Spectral change across each beat boundary: the distance between the mean band profile of the `w`
// beats before beat i and the `w` beats from it. 0 where the window does not fit.
std::vector<float> beatNovelty(const std::vector<std::array<float, FrameBands::kBands>>& beatBands, int w) {
    const auto n = static_cast<int>(beatBands.size());
    std::vector<float> out(beatBands.size(), 0.0f);
    for (int i = w; i + w <= n; ++i) {
        double sum = 0.0;
        for (std::size_t b = 0; b < FrameBands::kBands; ++b) {
            double before = 0.0;
            double after = 0.0;
            for (int j = 0; j < w; ++j) {
                before += beatBands[static_cast<std::size_t>(i - 1 - j)][b];
                after += beatBands[static_cast<std::size_t>(i + j)][b];
            }
            const double d = (after - before) / static_cast<double>(w);
            sum += d * d;
        }
        out[static_cast<std::size_t>(i)] = static_cast<float>(std::sqrt(sum));
    }
    return out;
}

// Robust z-scores against the curve's own median and median absolute deviation, clipped at zero:
// only changes that stand out count as evidence.
std::vector<float> positiveZ(std::span<const float> v) {
    std::vector<float> copy(v.begin(), v.end());
    const float m = median(copy);
    std::vector<float> dev(v.size());
    for (std::size_t i = 0; i < v.size(); ++i) {
        dev[i] = std::fabs(v[i] - m);
    }
    const float mad = std::max(median(dev), 1e-6f);
    std::vector<float> z(v.size());
    for (std::size_t i = 0; i < v.size(); ++i) {
        z[i] = std::max(0.0f, (v[i] - m) / mad);
    }
    return z;
}

} // namespace

int Meter::beatInBar(std::int64_t musicalBeat) const {
    return static_cast<int>(floorMod(musicalBeat, std::max(1, beatsPerBar)));
}

std::int64_t Meter::bar(std::int64_t musicalBeat) const { return floorDiv(musicalBeat, std::max(1, beatsPerBar)); }

std::int64_t Meter::phrase(std::int64_t musicalBeat) const {
    return floorDiv(musicalBeat, std::max(1, beatsPerPhrase()));
}

std::int64_t Meter::section(std::int64_t musicalBeat) const {
    return floorDiv(musicalBeat, std::max(1, beatsPerSection()));
}

double Meter::barPhase(double musicalBeats) const {
    return fraction(musicalBeats / static_cast<double>(std::max(1, beatsPerBar)));
}

double Meter::phrasePhase(double musicalBeats) const {
    return fraction(musicalBeats / static_cast<double>(std::max(1, beatsPerPhrase())));
}

double Meter::sectionPhase(double musicalBeats) const {
    return fraction(musicalBeats / static_cast<double>(std::max(1, beatsPerSection())));
}

std::vector<double> Meter::barTimes(std::span<const double> beatTimes) const {
    std::vector<double> out;
    const auto per = static_cast<std::int64_t>(std::max(1, beatsPerBar));
    // Bar 1 and every bar after it that the grid reaches. A negative downbeat (bar 1 began before
    // the first tracked beat) starts at the first bar line the grid holds.
    for (std::int64_t i = downbeat >= 0 ? downbeat : floorMod(downbeat, per);
         i < static_cast<std::int64_t>(beatTimes.size()); i += per) {
        out.push_back(beatTimes[static_cast<std::size_t>(i)]);
    }
    return out;
}

Meter Meter::sanitized() const {
    Meter m = *this;
    m.beatsPerBar = std::clamp(m.beatsPerBar, 1, 32);
    m.phraseBars = std::clamp(m.phraseBars, 1, 256);
    m.sectionPhrases = std::clamp(m.sectionPhrases, 1, 256);
    return m;
}

double clockBeatsAt(std::span<const double> beatTimes, double periodSeconds, double seconds) {
    const std::size_t n = beatTimes.size();
    if (n == 0 || !std::isfinite(seconds)) {
        return 0.0;
    }
    double period = periodSeconds;
    if (!(period > 0.0) && n >= 2) {
        period = (beatTimes.back() - beatTimes.front()) / static_cast<double>(n - 1);
    }
    if (seconds < beatTimes.front()) {
        return period > 0.0 ? (seconds - beatTimes.front()) / period : 0.0;
    }
    if (seconds >= beatTimes.back()) {
        return static_cast<double>(n - 1) + (period > 0.0 ? (seconds - beatTimes.back()) / period : 0.0);
    }
    const auto it = std::upper_bound(beatTimes.begin(), beatTimes.end(), seconds);
    const auto i = static_cast<std::size_t>(it - beatTimes.begin()) - 1;
    const double span = beatTimes[i + 1] - beatTimes[i];
    return static_cast<double>(i) + (span > 0.0 ? (seconds - beatTimes[i]) / span : 0.0);
}

double secondsAtClockBeats(std::span<const double> beatTimes, double periodSeconds, double clockBeats) {
    const std::size_t n = beatTimes.size();
    if (n == 0 || !std::isfinite(clockBeats)) {
        return 0.0;
    }
    double period = periodSeconds;
    if (!(period > 0.0) && n >= 2) {
        period = (beatTimes.back() - beatTimes.front()) / static_cast<double>(n - 1);
    }
    if (clockBeats < 0.0) {
        return beatTimes.front() + clockBeats * std::max(period, 0.0);
    }
    const double last = static_cast<double>(n - 1);
    if (clockBeats >= last) {
        return beatTimes.back() + (clockBeats - last) * std::max(period, 0.0);
    }
    const auto i = static_cast<std::size_t>(std::floor(clockBeats));
    return beatTimes[i] + (clockBeats - static_cast<double>(i)) * (beatTimes[i + 1] - beatTimes[i]);
}

MeterEstimate estimateMeter(std::span<const AnalysisFrame> frames, std::span<const double> beatTimes,
                            float binHz, int beatsPerBar) {
    MeterEstimate out;
    const int per = std::max(1, beatsPerBar);
    // Eight bars of grid is the least worth estimating from: fewer, and one fill decides the phase.
    if (frames.empty() || beatTimes.size() < static_cast<std::size_t>(8 * per) ||
        frames.front().magnitude.size() < 2 || !(binHz > 0.0f)) {
        return out;
    }
    const FrameBands fb = frameBands(frames, binHz);

    const std::size_t nb = beatTimes.size();
    std::vector<std::size_t> beatFrame(nb);
    for (std::size_t i = 0; i < nb; ++i) {
        beatFrame[i] = nearestFrame(frames, beatTimes[i]);
    }
    // Each beat's mean band profile over its own interval.
    std::vector<std::array<float, FrameBands::kBands>> beatBands(nb);
    for (std::size_t i = 0; i < nb; ++i) {
        const std::size_t from = beatFrame[i];
        // The last beat has no successor: it is given the length of the one before it.
        const std::size_t length = i + 1 < nb ? beatFrame[i + 1] - from : from - beatFrame[i - 1];
        const std::size_t to = std::min(frames.size(), from + std::max<std::size_t>(length, 1));
        std::array<double, FrameBands::kBands> acc{};
        std::size_t count = 0;
        for (std::size_t f = from; f < to && f < frames.size(); ++f, ++count) {
            for (std::size_t b = 0; b < FrameBands::kBands; ++b) {
                acc[b] += fb.bands[f][b];
            }
        }
        for (std::size_t b = 0; b < FrameBands::kBands; ++b) {
            beatBands[i][b] = count > 0 ? static_cast<float>(acc[b] / static_cast<double>(count)) : -100.0f;
        }
    }

    // ---- the backbeat: how hard the high band jumps at each beat ------------------------------------
    std::array<double, 4> backbeat{};
    if (per == 4) {
        std::array<double, 4> sum{};
        std::array<int, 4> count{};
        for (std::size_t i = 0; i < nb; ++i) {
            const auto centre = static_cast<std::ptrdiff_t>(beatFrame[i]);
            float hit = 0.0f;
            for (std::ptrdiff_t f = std::max<std::ptrdiff_t>(2, centre - 3);
                 f <= centre + 4 && f < static_cast<std::ptrdiff_t>(frames.size()); ++f) {
                const auto u = static_cast<std::size_t>(f);
                hit = std::max(hit, fb.high[u] - std::min(fb.high[u - 1], fb.high[u - 2]));
            }
            sum[i % 4] += hit;
            ++count[i % 4];
        }
        std::array<double, 4> mean{};
        for (std::size_t p = 0; p < 4; ++p) {
            mean[p] = count[p] > 0 ? sum[p] / count[p] : 0.0;
        }
        for (std::size_t d = 0; d < 4; ++d) {
            // Downbeat on tracked phase d: beats 2 and 4 are phases d+1 and d+3.
            const double on = 0.5 * (mean[(d + 1) % 4] + mean[(d + 3) % 4]);
            const double off = 0.5 * (mean[d % 4] + mean[(d + 2) % 4]);
            // A 6 dB backbeat is full evidence; a track with none contributes nothing, rather than
            // having its noise normalised up to a verdict.
            backbeat[d] = std::clamp((on - off) / 6.0, -1.0, 1.0);
        }
    }

    // ---- where things change ------------------------------------------------------------------------
    // The spectral change across each beat, over windows of 1, 2 and 4 beats (a note change, a fill, a
    // layer entering). Only the clear changes count -- a local maximum standing kZMin robust units
    // above the curve's median -- and each counts once, at its peak: a smooth swell (a pad breathing
    // over two bars) changes a little on every beat and, summed without that suppression, outvoted
    // the bar lines on the synthetic track whose bars start on its fourth beat.
    constexpr float kZMin = 2.5f;
    std::vector<double> change(static_cast<std::size_t>(per), 0.0);
    for (const int w : {1, 2, 4}) {
        const std::vector<float> nv = beatNovelty(beatBands, w);
        const auto first = static_cast<std::size_t>(w);
        const std::size_t last = nv.size() - static_cast<std::size_t>(w); // inclusive
        const std::vector<float> zs = positiveZ(std::span<const float>(nv).subspan(first, last - first + 1));
        const auto z = [&](std::size_t i) { return zs[i - first]; };
        for (std::size_t i = first; i <= last; ++i) {
            const float v = z(i);
            if (v < kZMin) {
                continue;
            }
            const bool peak = (i == first || v > z(i - 1)) && (i == last || v >= z(i + 1));
            if (peak) {
                change[i % static_cast<std::size_t>(per)] += static_cast<double>(v) * static_cast<double>(v);
            }
        }
    }
    const double changeTotal = std::accumulate(change.begin(), change.end(), 0.0);

    std::vector<double> score(static_cast<std::size_t>(per), 0.0);
    for (std::size_t d = 0; d < score.size(); ++d) {
        score[d] = (per == 4 ? 0.5 * backbeat[d] : 0.0) + (changeTotal > 0.0 ? change[d] / changeTotal : 0.0) +
                   (d == 0 ? 0.05 : 0.0);
    }
    const auto best = static_cast<std::size_t>(std::max_element(score.begin(), score.end()) - score.begin());
    double second = -1e9;
    for (std::size_t d = 0; d < score.size(); ++d) {
        if (d != best) {
            second = std::max(second, score[d]);
        }
    }
    out.valid = true;
    out.downbeat = static_cast<int>(best);
    out.downbeatConfidence = static_cast<float>(std::clamp((score[best] - second) / 0.3, 0.0, 1.0));
    for (std::size_t d = 0; d < out.downbeatScores.size() && d < score.size(); ++d) {
        out.downbeatScores[d] = static_cast<float>(score[d]);
    }

    // ---- the phrase: the longest of 4, 8, 16 bars whose starts carry the changes --------------------
    {
        constexpr int kWindow = 4;
        const std::vector<float> nv = beatNovelty(beatBands, kWindow);
        std::vector<float> barChange;
        for (std::size_t i = best; i < nb; i += static_cast<std::size_t>(per)) {
            if (i >= static_cast<std::size_t>(kWindow) && i + kWindow <= nb) {
                barChange.push_back(nv[i]);
            } else {
                barChange.push_back(-1.0f); // outside the window: no evidence either way
            }
        }
        std::vector<float> valid;
        for (const float v : barChange) {
            if (v >= 0.0f) {
                valid.push_back(v);
            }
        }
        const std::vector<float> zValid = positiveZ(valid);
        std::vector<float> z(barChange.size(), -1.0f);
        for (std::size_t b = 0, k = 0; b < barChange.size(); ++b) {
            if (barChange[b] >= 0.0f) {
                z[b] = zValid[k++];
            }
        }
        const auto meanAt = [&z](int period, int offset, int& count) {
            double sum = 0.0;
            count = 0;
            for (std::size_t b = 0; b < z.size(); ++b) {
                if (z[b] >= 0.0f && static_cast<int>(b % static_cast<std::size_t>(period)) == offset) {
                    sum += z[b];
                    ++count;
                }
            }
            return count > 0 ? sum / count : 0.0;
        };
        for (const int period : {4, 8, 16}) {
            int starts = 0;
            int halves = 0;
            const double atStart = meanAt(period, 0, starts);
            const double atHalf = meanAt(period, period / 2, halves);
            const double ratio = (atStart + 0.1) / (atHalf + 0.1);
            // A phrase length is accepted only when its starts are real changes (a mean z of 1.5),
            // there are enough of them to be a pattern, and they carry twice what the half-phrase
            // points do -- the half-phrase is where the next-shorter phrase length would put a start.
            if (starts < 3 || halves < 2 || atStart < 1.5 || ratio < 2.0) {
                break;
            }
            out.phraseBars = period;
            out.phraseEvidence = static_cast<float>(ratio);
        }
    }
    return out;
}

} // namespace avgen::analysis
