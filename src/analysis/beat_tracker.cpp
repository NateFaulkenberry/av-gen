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
// A leading or trailing beat whose local score is under this share of the mean at the beats is trimmed. ADR-1780: the
// beat strength is level-free, so a reverb tail's small rises score more than the onset ratio's did (0.1 then); on
// Rebuild three beats in its tail turned the bar estimate two beats.
constexpr double kEdgeShare = 0.4;
// ADR-1780: an edge beat whose interval differs from the local period by more than this share is off the grid.
constexpr double kEdgeIrregular = 0.08;
// ADR-1780: the metrical comb sums the autocorrelation at the first kCombMultiples multiples of a candidate period,
// scanned in steps of kLagStep hops, using lags up to kMaxLagShare of the window (a lag needs a quarter of the window to
// overlap itself).
constexpr int kCombMultiples = 4;
constexpr double kLagStep = 0.25;
constexpr double kMaxLagShare = 0.75;
// Live tracker tuning.
constexpr double kPeriodBlend = 0.3;          // weight of a new estimate near the held period
constexpr double kHoldWindow = 0.08;          // ADR-1781: a locked period searches its own peak within +-8%
constexpr double kHoldShare = 0.65;           // ... and is kept while that peak scores this share of the best
constexpr double kSameTempo = 0.06;           // two estimates within 6% are the same tempo
// ADR-1781: a tempo change seen in the newest lockSeconds alone -- they must be this confident, and the held period
// score under this share of their best.
constexpr double kChangeConfidence = 0.6;
constexpr double kChangeShare = 0.5;
constexpr double kRecentShare = 1.25;
// ADR-1781: a beat that falls due in silence waits this share of a period for the sound to come back.
constexpr double kBeatGrace = 0.08;
// ADR-1781: a gate open again within this many seconds was a break in the music, not the music stopping.
constexpr double kShortBreakSeconds = 2.0; // the newest 1.25 x lockSeconds (5 s): a bar and more at 60 BPM
constexpr double kOnsetCaptureFraction = 0.2; // onsets within +/- this * period pull the phase
// Picked onsets required inside the window before a tempo estimate is trusted. Without this a
// sub-threshold but periodic envelope (a steady tone's numerical flux noise, a slow tremolo)
// would lock the tracker even though nothing rhythmic is playing.
constexpr std::size_t kMinOnsetsInWindow = 4;
// ADR-1781: onsets after the silence gate opens that are checked against the held phase.
constexpr int kResumeChecks = 2;
// ADR-1780: the low register sets the phase where its own metrical score at the tracked period reads this confident
// (fully above kLowPhaseFull, not at all below kLowPhaseNone).
constexpr double kLowPhaseNone = 0.3;
constexpr double kLowPhaseFull = 0.5;

// ADR-1780: the envelope's dynamics compressed (square root) before anything is correlated. The onset strength is a
// ratio over an adaptive threshold, so a snare stacked on a kick reads several times a kick alone; uncompressed, the
// beats with the snare dominate the autocorrelation and a four-on-the-floor with a backbeat reads at half tempo.
float compress(float v) {
    return std::isfinite(v) && v > 0.0f ? std::sqrt(v) : 0.0f;
}

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

// The compressed, smoothed envelope.
std::vector<float> beatEnvelope(std::span<const float> in) {
    std::vector<float> c(in.size());
    std::transform(in.begin(), in.end(), c.begin(), compress);
    return smoothed(c, kSmoothSigmaHops);
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

// ADR-1780: the metrical tempo score of one window. score[j] belongs to the period minLag + j * kLagStep hops.
struct TempoScan {
    double minLag = 0.0;
    std::vector<double> score;
    double mean = 0.0;
    std::size_t best = 0;
    bool valid = false;
    [[nodiscard]] double lagAt(double j) const { return minLag + kLagStep * j; }
    [[nodiscard]] double confidence() const {
        if (!valid || !(mean > 0.0)) {
            return 0.0;
        }
        const double ratio = score[best] / mean;
        return std::clamp(1.0 - std::exp(-std::max(0.0, ratio - 1.0) / kConfidenceScale), 0.0, 1.0);
    }
    // The period at `j` refined by a parabola through its neighbours, in hops.
    [[nodiscard]] double refinedLag(std::size_t j) const {
        if (j == 0 || j + 1 >= score.size()) {
            return lagAt(static_cast<double>(j));
        }
        const double l = score[j - 1];
        const double c = score[j];
        const double r = score[j + 1];
        const double denom = l - 2.0 * c + r;
        const double offset = denom < 0.0 ? std::clamp(0.5 * (l - r) / denom, -0.5, 0.5) : 0.0;
        return lagAt(static_cast<double>(j) + offset);
    }
    // How confident the scan is in `lag` (within 3%): conf() with that lag's peak in place of the best.
    [[nodiscard]] double confidenceAt(double lag) const {
        const std::size_t j = bestNear(lag, 0.03);
        if (!valid || !(mean > 0.0) || j >= score.size()) {
            return 0.0;
        }
        const double ratio = score[j] / mean;
        return std::clamp(1.0 - std::exp(-std::max(0.0, ratio - 1.0) / kConfidenceScale), 0.0, 1.0);
    }
    // The best index within `share` (relative) of `lag`, or score.size() when none is in range.
    [[nodiscard]] std::size_t bestNear(double lag, double share) const {
        std::size_t out = score.size();
        for (std::size_t j = 0; j < score.size(); ++j) {
            const double l = lagAt(static_cast<double>(j));
            if (std::fabs(l - lag) <= share * lag && (out == score.size() || score[j] > score[out])) {
                out = j;
            }
        }
        return out;
    }
};

TempoScan scanTempo(const std::vector<float>& envelope, double hop, const BeatTrackerConfig& config) {
    TempoScan scan;
    const std::size_t n = envelope.size();
    if (n < 4 || !(hop > 0.0) || !(config.minBpm > 0.0f) || !(config.maxBpm > config.minBpm)) {
        return scan;
    }
    // Mean-removed, half-wave-rectified.
    std::vector<float> x(envelope);
    const double mean = std::accumulate(x.begin(), x.end(), 0.0) / static_cast<double>(n);
    for (auto& v : x) {
        v = std::max(0.0f, v - static_cast<float>(mean));
    }
    // Candidate periods for the configured tempo range, each overlapping at least half the window.
    const double minLag = std::max(1.0, 60.0 / (static_cast<double>(config.maxBpm) * hop));
    const double maxLag = std::min(60.0 / (static_cast<double>(config.minBpm) * hop), 0.5 * static_cast<double>(n));
    if (maxLag < minLag + 2.0) {
        return scan;
    }
    // Unbiased autocorrelation up to the longest lag a comb reads.
    const auto acfMax = static_cast<std::size_t>(
        std::min(std::floor(kMaxLagShare * static_cast<double>(n)), std::ceil(kCombMultiples * maxLag) + 1.0));
    std::vector<double> r(acfMax + 2, 0.0);
    for (std::size_t lag = 1; lag <= acfMax && lag < n; ++lag) {
        double acc = 0.0;
        for (std::size_t i = lag; i < n; ++i) {
            acc += static_cast<double>(x[i]) * static_cast<double>(x[i - lag]);
        }
        r[lag] = acc / static_cast<double>(n - lag);
    }
    const auto at = [&](double lag) {
        const auto lo = static_cast<std::size_t>(std::floor(lag));
        if (lo + 1 >= r.size()) {
            return 0.0;
        }
        const double f = lag - static_cast<double>(lo);
        return (1.0 - f) * r[lo] + f * r[lo + 1];
    };
    scan.minLag = minLag;
    const auto count = static_cast<std::size_t>(std::floor((maxLag - minLag) / kLagStep)) + 1;
    scan.score.assign(count, 0.0);
    double total = 0.0;
    for (std::size_t j = 0; j < count; ++j) {
        const double lag = scan.lagAt(static_cast<double>(j));
        double sum = 0.0;
        int terms = 0;
        for (int m = 1; m <= kCombMultiples; ++m) {
            const double y = static_cast<double>(m) * lag;
            if (y > static_cast<double>(acfMax)) {
                break;
            }
            sum += at(y);
            ++terms;
        }
        const double comb = terms > 0 ? sum / static_cast<double>(terms) : 0.0;
        scan.score[j] = comb * tempoPrior(lagToBpm(lag, hop), config);
        total += scan.score[j];
    }
    scan.mean = total / static_cast<double>(count);
    scan.best = static_cast<std::size_t>(
        std::distance(scan.score.begin(), std::max_element(scan.score.begin(), scan.score.end())));
    scan.valid = scan.mean > 0.0 && scan.score[scan.best] > 0.0;
    return scan;
}

// The share of the low register in the phase, from its confidence at the tracked period.
double lowPhaseWeight(double lowConfidence) {
    return std::clamp((lowConfidence - kLowPhaseNone) / (kLowPhaseFull - kLowPhaseNone), 0.0, 1.0);
}

// An envelope over its 99th percentile, so two envelopes of different scale can be blended.
void normaliseRobust(std::vector<float>& v) {
    if (v.empty()) {
        return;
    }
    std::vector<float> sorted(v);
    const auto k = static_cast<std::size_t>(0.99 * static_cast<double>(sorted.size() - 1));
    std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(k), sorted.end());
    const float top = sorted[k] > 0.0f ? sorted[k] : *std::max_element(v.begin(), v.end());
    for (auto& x : v) {
        x = top > 0.0f ? std::min(1.5f, x / top) : 0.0f;
    }
}

} // namespace

// ------------------------------------------------------------------------------------------------
// Tempo estimation
// ------------------------------------------------------------------------------------------------

std::pair<float, float> estimateTempo(std::span<const float> onsetStrength, float hopSeconds,
                                      const BeatTrackerConfig& config) {
    const auto hop = static_cast<double>(hopSeconds);
    const TempoScan scan = scanTempo(beatEnvelope(onsetStrength), hop, config);
    if (!scan.valid) {
        return {0.0f, 0.0f};
    }
    const double bpm = std::clamp(lagToBpm(scan.refinedLag(scan.best), hop), static_cast<double>(config.minBpm),
                                  static_cast<double>(config.maxBpm));
    return {static_cast<float>(bpm), static_cast<float>(scan.confidence())};
}

double localBpmAt(std::span<const double> beatTimes, double seconds) {
    const std::size_t n = beatTimes.size();
    if (n < 2) {
        return 0.0;
    }
    // The interval containing `seconds` (clamped to the first / last), and up to four either side.
    const auto it = std::upper_bound(beatTimes.begin(), beatTimes.end(), seconds);
    std::size_t i = it == beatTimes.begin() ? 0 : static_cast<std::size_t>(it - beatTimes.begin()) - 1;
    i = std::min(i, n - 2);
    const std::size_t lo = i >= 3 ? i - 3 : 0;
    const std::size_t hi = std::min(n - 2, i + 4);
    std::vector<double> intervals;
    intervals.reserve(hi - lo + 1);
    for (std::size_t k = lo; k <= hi; ++k) {
        intervals.push_back(beatTimes[k + 1] - beatTimes[k]);
    }
    const double interval = median(std::move(intervals));
    return interval > 0.0 ? 60.0 / interval : 0.0;
}

// ------------------------------------------------------------------------------------------------
// Offline tracker: the tempo path, then Ellis (2007) with the local period (ADR-1782)
// ------------------------------------------------------------------------------------------------

OfflineBeats trackBeatsOffline(std::span<const float> strength, std::span<const float> lowStrength, float hopSeconds,
                               const BeatTrackerConfig& config) {
    OfflineBeats out;
    const std::size_t n = strength.size();
    const auto hop = static_cast<double>(hopSeconds);
    if (n == 0 || !(hop > 0.0)) {
        return out;
    }
    const std::vector<float> envelope = beatEnvelope(strength);
    const bool haveLow = lowStrength.size() == n;
    const std::vector<float> lowEnvelope = haveLow ? beatEnvelope(lowStrength) : std::vector<float>{};

    // ---- the local tempo: the metrical scan over windows, every localStepSeconds ----
    const auto windowHops = std::min<std::size_t>(
        n, static_cast<std::size_t>(std::max<long long>(8, std::llround(static_cast<double>(config.windowSeconds) / hop))));
    const auto stepHops = static_cast<std::size_t>(
        std::max<long long>(1, std::llround(static_cast<double>(config.localStepSeconds) / hop)));
    std::vector<std::size_t> starts;
    for (std::size_t s = 0; s + windowHops <= n; s += stepHops) {
        starts.push_back(s);
    }
    if (starts.empty() || starts.back() + windowHops < n) {
        starts.push_back(n - windowHops);
    }
    std::vector<TempoScan> scans;
    std::vector<TempoScan> lowScans;
    scans.reserve(starts.size());
    std::vector<double> confidences;
    for (const std::size_t s : starts) {
        const auto first = static_cast<std::ptrdiff_t>(s);
        const auto last = static_cast<std::ptrdiff_t>(s + windowHops);
        scans.push_back(scanTempo(std::vector<float>(envelope.begin() + first, envelope.begin() + last), hop, config));
        confidences.push_back(scans.back().confidence());
        if (haveLow) {
            lowScans.push_back(
                scanTempo(std::vector<float>(lowEnvelope.begin() + first, lowEnvelope.begin() + last), hop, config));
        }
    }
    const std::size_t states = scans.front().score.size();
    if (states == 0) {
        return out;
    }
    // The file's confidence: the median over the windows that found a tempo, else the best window's.
    {
        std::vector<double> rhythmic;
        for (const double c : confidences) {
            if (c >= static_cast<double>(config.confidenceFloor)) {
                rhythmic.push_back(c);
            }
        }
        out.confidence = static_cast<float>(rhythmic.empty() ? *std::max_element(confidences.begin(), confidences.end())
                                                             : median(rhythmic));
    }
    if (out.confidence < config.confidenceFloor) {
        return out; // tempo unknown: no beats
    }

    // ---- the tempo path: Viterbi over the windows. A window's evidence is its normalised scan times its confidence
    // (a window of silence or noise has none, and the path holds its tempo through it); a change costs
    // tempoChangeCost per octave, so a new tempo must hold for about that many windows of full evidence. ----
    const double changeCost = static_cast<double>(config.tempoChangeCost);
    std::vector<double> logLag(states);
    for (std::size_t j = 0; j < states; ++j) {
        logLag[j] = std::log2(scans.front().lagAt(static_cast<double>(j)));
    }
    const std::size_t windows = scans.size();
    std::vector<double> score(states, 0.0);
    std::vector<double> next(states, 0.0);
    std::vector<std::uint16_t> back(windows * states, 0);
    const auto evidence = [&](std::size_t w, std::size_t j) {
        const TempoScan& s = scans[w];
        if (!s.valid || s.score.size() != states) {
            return 0.0;
        }
        return confidences[w] * s.score[j] / s.score[s.best];
    };
    for (std::size_t j = 0; j < states; ++j) {
        score[j] = evidence(0, j);
    }
    for (std::size_t w = 1; w < windows; ++w) {
        for (std::size_t k = 0; k < states; ++k) {
            double best = -std::numeric_limits<double>::infinity();
            std::size_t from = k;
            for (std::size_t j = 0; j < states; ++j) {
                const double v = score[j] - changeCost * std::fabs(logLag[j] - logLag[k]);
                if (v > best) {
                    best = v;
                    from = j;
                }
            }
            next[k] = best + evidence(w, k);
            back[w * states + k] = static_cast<std::uint16_t>(from);
        }
        score.swap(next);
    }
    std::vector<double> pathLag(windows, 0.0);
    {
        auto j = static_cast<std::size_t>(std::distance(score.begin(), std::max_element(score.begin(), score.end())));
        for (std::size_t w = windows; w-- > 0;) {
            const TempoScan& s = scans[w];
            pathLag[w] = s.valid && s.score.size() == states ? s.refinedLag(j) : s.lagAt(static_cast<double>(j));
            if (w > 0) {
                j = back[w * states + j];
            }
        }
    }
    // ADR-1780: per window, how much of the phase the low register sets -- its own confidence at the path's period.
    std::vector<double> lowShare(windows, 0.0);
    if (haveLow) {
        for (std::size_t w = 0; w < windows; ++w) {
            lowShare[w] = lowPhaseWeight(lowScans[w].confidenceAt(pathLag[w]));
        }
    }
    // The period at each hop (and the low share): interpolated between the windows' centres (the period in log),
    // held beyond the first and last.
    std::vector<double> period(n, pathLag.front());
    std::vector<double> lowAt(n, lowShare.front());
    {
        std::vector<double> centres(windows);
        for (std::size_t w = 0; w < windows; ++w) {
            centres[w] = static_cast<double>(starts[w]) + 0.5 * static_cast<double>(windowHops);
        }
        std::size_t w = 0;
        for (std::size_t i = 0; i < n; ++i) {
            const auto x = static_cast<double>(i);
            while (w + 1 < windows && centres[w + 1] <= x) {
                ++w;
            }
            if (x <= centres.front()) {
                period[i] = pathLag.front();
                lowAt[i] = lowShare.front();
            } else if (w + 1 >= windows) {
                period[i] = pathLag.back();
                lowAt[i] = lowShare.back();
            } else {
                const double f = (x - centres[w]) / std::max(1e-9, centres[w + 1] - centres[w]);
                period[i] = std::exp2((1.0 - f) * std::log2(pathLag[w]) + f * std::log2(pathLag[w + 1]));
                lowAt[i] = (1.0 - f) * lowShare[w] + f * lowShare[w + 1];
            }
        }
    }

    // Local score: the envelope over its robust peak, blended with the low register's where that sets the phase.
    std::vector<float> local = envelope;
    if (!(*std::max_element(local.begin(), local.end()) > 0.0f)) {
        return out;
    }
    normaliseRobust(local);
    if (haveLow) {
        std::vector<float> low = lowEnvelope;
        normaliseRobust(low);
        for (std::size_t i = 0; i < n; ++i) {
            const auto a = static_cast<float>(lowAt[i]);
            local[i] = a * low[i] + (1.0f - a) * local[i];
        }
    }

    // Dynamic programming: score[i] = local[i] + max over predecessors m in [i - 2P, i - P/2] of
    // score[m] - tightness * log(delta / P)^2, P the local period, with score 0 before the start of the track so a
    // beat sequence may begin anywhere at no cost.
    std::vector<double> cumulative(n, 0.0);
    std::vector<std::ptrdiff_t> previous(n, -1);
    for (std::size_t i = 0; i < n; ++i) {
        const double p = period[i];
        const auto minDelta = std::max<std::size_t>(1, static_cast<std::size_t>(std::llround(0.5 * p)));
        const auto maxDelta = std::max(minDelta + 1, static_cast<std::size_t>(std::llround(2.0 * p)));
        double best = -std::numeric_limits<double>::infinity();
        std::ptrdiff_t bestM = -1;
        for (std::size_t d = minDelta; d <= maxDelta; ++d) {
            const auto m = static_cast<std::ptrdiff_t>(i) - static_cast<std::ptrdiff_t>(d);
            const double l = std::log(static_cast<double>(d) / p);
            const double candidate = -kTightness * l * l + (m >= 0 ? cumulative[static_cast<std::size_t>(m)] : 0.0);
            if (candidate > best) {
                best = candidate;
                bestM = m;
            }
        }
        cumulative[i] = static_cast<double>(local[i]) + best;
        previous[i] = bestM >= 0 ? bestM : -1;
    }

    // Backtrace from the best final score within the last period (a chain may end before the track does).
    std::ptrdiff_t index = 0;
    {
        const auto lastPeriod = static_cast<std::size_t>(std::ceil(period.back()));
        const std::size_t from = n > lastPeriod ? n - lastPeriod : 0;
        index = static_cast<std::ptrdiff_t>(
            std::distance(cumulative.begin(), std::max_element(cumulative.begin() + static_cast<std::ptrdiff_t>(from),
                                                               cumulative.end())));
    }
    std::vector<std::size_t> beats;
    while (index >= 0) {
        beats.push_back(static_cast<std::size_t>(index));
        index = previous[static_cast<std::size_t>(index)];
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
        const double threshold = kEdgeShare * meanAtBeats;
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

    // ADR-1780: where the low register set the phase, each beat then moves to the strongest point of the whole-band
    // envelope within an eighth of a period: the low bands' rise peaks a few hops after the attack (a kick's body
    // builds after its click), and the beat belongs on the attack. An eighth is under a sixteenth, so this cannot move
    // a beat onto an off-beat.
    if (haveLow) {
        std::vector<float> full = envelope;
        normaliseRobust(full);
        for (std::size_t& b : beats) {
            if (!(lowAt[b] > 0.0)) {
                continue;
            }
            const auto reach = static_cast<std::size_t>(std::llround(period[b] / 8.0));
            const std::size_t lo = b > reach ? b - reach : 0;
            const std::size_t hi = std::min(n - 1, b + reach);
            std::size_t best = b;
            for (std::size_t k = lo; k <= hi; ++k) {
                if (full[k] > full[best]) {
                    best = k;
                }
            }
            b = best;
        }
        // A snap never reorders: two beats a period apart moved an eighth each stay apart.
        beats.erase(std::unique(beats.begin(), beats.end()), beats.end());
        // An edge beat the snap pulled off the grid (a pickup sound before the music's first beat, a tail after its
        // last) is not a beat of the grid: one whose interval differs from the local period by more than
        // kEdgeIrregular of it goes.
        const auto irregular = [&](std::size_t from, std::size_t to) {
            const double p = period[to];
            return std::fabs(static_cast<double>(to - from) - p) > kEdgeIrregular * p;
        };
        while (beats.size() >= 3 && irregular(beats[0], beats[1])) {
            beats.erase(beats.begin());
        }
        while (beats.size() >= 3 && irregular(beats[beats.size() - 2], beats.back())) {
            beats.pop_back();
        }
    }

    out.beatTimes.reserve(beats.size());
    for (const std::size_t b : beats) {
        out.beatTimes.push_back(static_cast<double>(b) * hop);
    }
    // The file's typical tempo: the median beat interval, each measured over eight beats (one interval is a whole
    // number of hops, and 43 or 44 hops is 130.8 or 128.6 BPM).
    if (out.beatTimes.size() >= 2) {
        const std::size_t span = std::min<std::size_t>(8, out.beatTimes.size() - 1);
        std::vector<double> intervals;
        intervals.reserve(out.beatTimes.size() - span);
        for (std::size_t b = span; b < out.beatTimes.size(); ++b) {
            intervals.push_back((out.beatTimes[b] - out.beatTimes[b - span]) / static_cast<double>(span));
        }
        out.tempoBpm = static_cast<float>(60.0 / median(std::move(intervals)));
    } else {
        out.tempoBpm = static_cast<float>(lagToBpm(pathLag.front(), hop));
    }
    return out;
}

// ------------------------------------------------------------------------------------------------
// Live tracker (ADR-1781)
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
    lowHistory_.assign(history_.size(), 0.0f);
    onsetHistory_.assign(history_.size(), 0);
    scratch_.reserve(history_.size());
    minHistoryHops_ = std::min(history_.size(), hops(config_.firstEstimateSeconds));
}

void BeatTracker::clearHistory() {
    historyHead_ = 0;
    historyCount_ = 0;
    hopsSinceTempo_ = 0;
}

void BeatTracker::reset() {
    state_ = BeatState{};
    clearHistory();
    hopsUnconfident_ = 0;
    period_ = 0.0;
    candidatePeriod_ = 0.0;
    nextBeatTime_ = 0.0;
    time_ = 0.0;
    lastBeatTime_ = -1.0;
    locked_ = false;
    silentHops_ = 0;
    gated_ = false;
    gatedSince_ = 0.0;
    heldConfidence_ = 0.0f;
    resumeChecks_ = 0;
    resuming_ = false;
    pendingSince_ = -1.0;
    hopIndex_ = 0;
}

void BeatTracker::initialisePhase() {
    // Comb over the history at the current period: the offset (hops back from now) whose
    // period-spaced samples sum highest marks the most recent beat. ADR-1780: over the low register's history when it
    // is itself confident of this period.
    const auto hop = static_cast<double>(hopSeconds_);
    const double periodHops = period_ / hop;
    const std::size_t capacity = history_.size();
    const std::size_t start = (historyHead_ + capacity - historyCount_) % capacity;
    std::vector<float> low(historyCount_);
    for (std::size_t i = 0; i < historyCount_; ++i) {
        low[i] = lowHistory_[(start + i) % capacity];
    }
    const bool fromLow =
        lowPhaseWeight(scanTempo(beatEnvelope(low), hop, config_).confidenceAt(periodHops)) >= 0.5;
    const std::vector<float>& ring = fromLow ? lowHistory_ : history_;
    const auto historyAt = [&](std::size_t logical) { return ring[(start + logical) % capacity]; };
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
            sum += static_cast<double>(compress(historyAt(historyCount_ - 1 - back)));
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
    const auto hop = static_cast<double>(hopSeconds_);
    const TempoScan scan = scanTempo(beatEnvelope(scratch_), hop, config_);
    const auto confidence = static_cast<float>(scan.confidence());
    state_.confidence = confidence;
    if (!scan.valid || confidence < config_.confidenceFloor) {
        return;
    }
    hopsUnconfident_ = 0;
    const auto periodOf = [&](std::size_t j) {
        const double bpm = std::clamp(lagToBpm(scan.refinedLag(j), hop), static_cast<double>(config_.minBpm),
                                      static_cast<double>(config_.maxBpm));
        return 60.0 / bpm;
    };
    if (period_ <= 0.0) {
        const double bestPeriod = periodOf(scan.best);
        period_ = bestPeriod; // the first estimate: acquiring, not yet locked
        locked_ = false;
        candidatePeriod_ = 0.0;
        initialisePhase();
        return;
    }
    // The context state (Davies-Plumbley): the held period's own peak, within +-8%, keeps it while it scores
    // kHoldShare of the best. A fill or a syncopated bar does not move a lock.
    // A lock is confirmed only by an estimate over lockSeconds of history (two bars at 120 BPM): a shorter window
    // cannot see a bar's metrical structure, and a syncopated pattern reads at 4:3 over one bar.
    const bool canLock = static_cast<double>(historyCount_) * hop >= static_cast<double>(config_.lockSeconds) - 1e-9;
    const std::size_t near = scan.bestNear(period_ / hop, kHoldWindow);
    bool holds = near < scan.score.size() &&
                 scan.score[near] >= (locked_ ? kHoldShare : 1.0) * scan.score[scan.best] - 1e-12;
    double bestPeriod = periodOf(scan.best);
    const double recentSeconds = kRecentShare * static_cast<double>(config_.lockSeconds);
    if (holds && locked_ && static_cast<double>(historyCount_) * hop >= recentSeconds + 2.0) {
        // A tempo change: the whole history still carries the old tempo for seconds after it. The newest
        // recentSeconds alone decide whether the held period still fits; if not, their best is the candidate.
        const auto recentHops = static_cast<std::size_t>(std::llround(recentSeconds / hop));
        const std::vector<float> recent(scratch_.end() - static_cast<std::ptrdiff_t>(recentHops), scratch_.end());
        const TempoScan r = scanTempo(beatEnvelope(recent), hop, config_);
        const std::size_t rn = r.bestNear(period_ / hop, kHoldWindow);
        if (r.valid && r.confidence() >= kChangeConfidence &&
            (rn >= r.score.size() || r.score[rn] < kChangeShare * r.score[r.best])) {
            const double recentPeriod =
                60.0 / std::clamp(lagToBpm(r.refinedLag(r.best), hop), static_cast<double>(config_.minBpm),
                                  static_cast<double>(config_.maxBpm));
            // A metrical relative of the held tempo (x2, x1/2, x3/2, x2/3, x4/3, x3/4) in a few seconds is the
            // ambiguity a short window has, not a tempo change: only the whole history may move to it.
            bool related = false;
            for (const double q : {2.0, 0.5, 1.5, 2.0 / 3.0, 4.0 / 3.0, 0.75}) {
                related = related || std::fabs(recentPeriod / (period_ * q) - 1.0) <= 0.03;
            }
            if (!related) {
                holds = false;
                bestPeriod = recentPeriod;
            }
        }
    }
    if (holds) {
        const double held = periodOf(near);
        period_ += kPeriodBlend * (held - period_);
        locked_ = locked_ || (canLock && std::fabs(held - period_) <= kSameTempo * period_);
        candidatePeriod_ = 0.0;
        return;
    }
    // Another tempo wins. Acquiring, it replaces the estimate at once; locked, it must win twice running.
    if (!locked_ || (candidatePeriod_ > 0.0 && std::fabs(bestPeriod - candidatePeriod_) <= kSameTempo * candidatePeriod_)) {
        period_ = bestPeriod;
        locked_ = false;
        candidatePeriod_ = 0.0;
        initialisePhase();
    } else {
        candidatePeriod_ = bestPeriod;
    }
}

BeatState BeatTracker::push(float strength, float lowStrength, bool onset, float rms) {
    const auto hop = static_cast<double>(hopSeconds_);
    time_ = static_cast<double>(hopIndex_) * hop;

    // ---- the silence gate (ADR-1781) ----
    const double floor = std::pow(10.0, static_cast<double>(config_.silenceFloorDb) / 20.0);
    const bool quiet = !(static_cast<double>(rms) > floor);
    silentHops_ = quiet ? silentHops_ + 1 : 0;
    // The gate closes when a beat falls due in silence (silence at least silenceHoldSeconds old) and the sound has
    // not come back within kBeatGrace of a period -- a rest between two hits ends on the next hit, so a click
    // track or a sparse pattern is never gated -- or after silenceHoldMaxSeconds of silence whatever the beat.
    const double silentFor = static_cast<double>(silentHops_) * hop;
    const bool overdue = period_ > 0.0 && pendingSince_ >= 0.0 && time_ - pendingSince_ >= kBeatGrace * period_;
    const bool noBeat = period_ <= 0.0 && silentFor >= static_cast<double>(config_.silenceHoldSeconds);
    // The longest silence that is still a rest: the cap, but never under a beat and its grace (a click track at
    // 60 BPM is a second of silence between clicks).
    const double cap = std::max(static_cast<double>(config_.silenceHoldMaxSeconds), (1.0 + 2.0 * kBeatGrace) * period_);
    if (!gated_ && (overdue || noBeat || silentFor >= cap)) {
        gated_ = true;
        gatedSince_ = time_;
        heldConfidence_ = std::max(heldConfidence_, state_.confidence);
        resuming_ = false;
        pendingSince_ = -1.0;
        // The beat that fell due in the silence never sounded: it is skipped, not emitted late.
        if (period_ > 0.0) {
            while (nextBeatTime_ <= time_ + 0.5 * hop) {
                lastBeatTime_ = nextBeatTime_;
                nextBeatTime_ += period_;
            }
        }
    } else if (gated_ && !quiet) {
        gated_ = false;
        if (time_ - gatedSince_ < kShortBreakSeconds) {
            // A short break (a rest bar, a stop): the lock simply carries on, with its confidence.
            resuming_ = false;
            state_.confidence = std::max(state_.confidence, heldConfidence_);
        } else {
            // The music is back after a real silence: the held lock stays, and the first onsets are checked
            // against its phase.
            resuming_ = period_ > 0.0;
            resumeChecks_ = kResumeChecks;
        }
    }

    // Ring of onset strengths over the tempo window: silence is written too, so the history keeps time.
    const std::size_t capacity = history_.size();
    const auto clean = [](float v) { return std::isfinite(v) ? std::max(0.0f, v) : 0.0f; };
    history_[historyHead_] = clean(strength);
    lowHistory_[historyHead_] = clean(lowStrength);
    onsetHistory_[historyHead_] = onset ? 1 : 0;
    historyHead_ = (historyHead_ + 1) % capacity;
    historyCount_ = std::min(historyCount_ + 1, capacity);

    if (gated_) {
        // Silence: no beat, no tempo, the confidence falling; the phase runs on inside, for a resume.
        const double fall = static_cast<double>(std::max(config_.silenceFallSeconds, 1e-3f));
        state_.confidence = static_cast<float>(static_cast<double>(state_.confidence) * std::exp(-hop / fall));
        if (time_ - gatedSince_ > static_cast<double>(config_.resumeSeconds)) {
            // Too long: the lock is forgotten and the next music is acquired from scratch.
            period_ = 0.0;
            lastBeatTime_ = -1.0;
            locked_ = false;
            candidatePeriod_ = 0.0;
            heldConfidence_ = 0.0f;
            clearHistory();
        }
        if (period_ > 0.0) {
            while (nextBeatTime_ <= time_ + 0.5 * hop) {
                lastBeatTime_ = nextBeatTime_;
                nextBeatTime_ += period_;
            }
        }
        ++hopsSinceTempo_; // no estimate in silence; the next comes as soon as the music is back
        state_.beat = false;
        state_.phase = 0.0f;
        state_.tempoBpm = 0.0f;
        state_.silent = true;
        ++hopIndex_;
        return state_;
    }
    state_.silent = false;

    // The resume check: does an onset after the silence fall on the held phase?
    if (resuming_ && onset && period_ > 0.0) {
        const double toNext = nextBeatTime_ - time_;
        const double sinceLast =
            lastBeatTime_ >= 0.0 ? time_ - lastBeatTime_ : std::numeric_limits<double>::infinity();
        const double error = sinceLast < toNext ? sinceLast : -toNext;
        if (std::fabs(error) <= kOnsetCaptureFraction * period_) {
            resuming_ = false; // it fits: the lock resumes with its confidence
            state_.confidence = std::max(state_.confidence, heldConfidence_);
            heldConfidence_ = 0.0f;
        } else if (--resumeChecks_ <= 0) {
            // It does not: the music came back elsewhere. Acquire again, from the music after the silence.
            resuming_ = false;
            period_ = 0.0;
            lastBeatTime_ = -1.0;
            locked_ = false;
            candidatePeriod_ = 0.0;
            heldConfidence_ = 0.0f;
            clearHistory();
            history_[historyHead_] = clean(strength);
            lowHistory_[historyHead_] = clean(lowStrength);
            onsetHistory_[historyHead_] = 1;
            historyHead_ = (historyHead_ + 1) % capacity;
            historyCount_ = 1;
        }
    }
    if (!resuming_ && !gated_) {
        heldConfidence_ = 0.0f;
    }

    // Periodic tempo re-estimation once enough history exists: faster while acquiring.
    const auto updateHops = static_cast<std::size_t>(std::max<long long>(
        1, std::llround(static_cast<double>(locked_ ? config_.tempoUpdateSeconds : config_.acquireUpdateSeconds) / hop)));
    if (++hopsSinceTempo_ >= updateHops && historyCount_ >= minHistoryHops_ && !resuming_) {
        hopsSinceTempo_ = 0;
        updateTempo();
    }

    // Free-run through unconfident (but not silent) stretches, but give up once a whole window has passed without
    // periodicity: the tempo becomes unknown again.
    if (period_ > 0.0 && state_.confidence < config_.confidenceFloor && !resuming_) {
        if (++hopsUnconfident_ >= capacity) {
            period_ = 0.0;
            lastBeatTime_ = -1.0;
            locked_ = false;
            candidatePeriod_ = 0.0;
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
        const bool due = nextBeatTime_ <= time_ + 0.5 * hop;
        if (due && quiet && silentHops_ * hop >= static_cast<double>(config_.silenceHoldSeconds) - 1e-9) {
            // Due in silence: held for kBeatGrace of a period. A hit in that time emits it (late by at most that);
            // none closes the gate above.
            if (pendingSince_ < 0.0) {
                pendingSince_ = time_;
            }
        } else if (due) {
            pendingSince_ = -1.0;
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
