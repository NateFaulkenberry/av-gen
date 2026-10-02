#include "analysis/analysis_track.hpp"

#include "analysis/band_onsets.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <utility>

namespace avgen::analysis {

namespace {

// Picked onsets required before the track is considered rhythmic at all (matches the live
// tracker's gate): a steady tone's sub-threshold flux noise must not produce beats.
constexpr std::size_t kMinOnsets = 4;

// ADR-896: the tracked grid, refined below the hop.
//
// The dynamic-programming tracker places beats on analysis hops (10.7 ms at 48 kHz) and, where a
// passage has no drums, on whatever onsets are there: on "Rebuild" its beats wander up to 33 ms off
// the true grid through the two drum-less bars of the pull-back, and take ten beats to walk back at
// one hop per four beats -- one bar line 30.4 ms out. A bar line has to be within 30 ms of the bar.
//
// So each beat is replaced by a robust local fit of a constant tempo through its neighbours: a
// weighted least-squares line through the beats within `kHalfWindow` of it, the weights the onset
// strength at each beat (a beat in a drum-less bar counts for little) times a Tukey biweight on its
// residual (a beat that followed an off-grid riff note is an outlier and counts for nothing). A beat
// moves at most `kMaxShiftBeats` of a period: the fit refines where the tracker put a beat, it never
// relocates one. The offline tracker already assumes one tempo for the whole track, so a locally
// constant one is not a new assumption.
constexpr int kHalfWindow = 8;
constexpr double kTukeySeconds = 0.025;
constexpr double kMaxShiftBeats = 0.1;

void refineGrid(std::vector<double>& times, const std::vector<double>& strength, double period) {
    const auto n = static_cast<int>(times.size());
    if (n < 2 * kHalfWindow + 1 || !(period > 0.0)) {
        return;
    }
    const std::vector<double> original = times;
    for (int i = 0; i < n; ++i) {
        const int lo = std::max(0, i - kHalfWindow);
        const int hi = std::min(n - 1, i + kHalfWindow);
        std::vector<double> w(static_cast<std::size_t>(hi - lo + 1));
        for (int j = lo; j <= hi; ++j) {
            w[static_cast<std::size_t>(j - lo)] = std::max(strength[static_cast<std::size_t>(j)], 1e-3);
        }
        double fitted = original[static_cast<std::size_t>(i)];
        std::vector<double> robust = w;
        for (int iteration = 0; iteration < 4; ++iteration) {
            // Weighted least squares for y = a + b (j - i); `a` is the refined time of beat i.
            double sw = 0.0, sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
            for (int j = lo; j <= hi; ++j) {
                const double wj = robust[static_cast<std::size_t>(j - lo)];
                const double x = static_cast<double>(j - i);
                const double y = original[static_cast<std::size_t>(j)];
                sw += wj;
                sx += wj * x;
                sy += wj * y;
                sxx += wj * x * x;
                sxy += wj * x * y;
            }
            const double det = sw * sxx - sx * sx;
            if (!(sw > 0.0) || std::fabs(det) < 1e-12) {
                break;
            }
            const double b = (sw * sxy - sx * sy) / det;
            const double a = (sy - b * sx) / sw;
            fitted = a;
            for (int j = lo; j <= hi; ++j) {
                const double residual = original[static_cast<std::size_t>(j)] - (a + b * static_cast<double>(j - i));
                const double u = residual / kTukeySeconds;
                const double biweight = std::fabs(u) < 1.0 ? (1.0 - u * u) * (1.0 - u * u) : 0.0;
                robust[static_cast<std::size_t>(j - lo)] = w[static_cast<std::size_t>(j - lo)] * biweight;
            }
        }
        const double shift = fitted - original[static_cast<std::size_t>(i)];
        if (std::isfinite(fitted) && std::fabs(shift) <= kMaxShiftBeats * period) {
            times[static_cast<std::size_t>(i)] = fitted;
        }
    }
    // A refinement never reorders: two beats a period apart cannot cross by moving a tenth of one.
    for (int i = 1; i < n; ++i) {
        if (!(times[static_cast<std::size_t>(i)] > times[static_cast<std::size_t>(i - 1)])) {
            times = original;
            return;
        }
    }
}

// The frame whose centre is nearest `seconds`.
std::size_t nearestFrame(const std::vector<AnalysisFrame>& frames, double seconds) {
    const auto it = std::lower_bound(frames.begin(), frames.end(), seconds,
                                     [](const AnalysisFrame& f, double t) { return f.timeSeconds < t; });
    auto i = static_cast<std::size_t>(it - frames.begin());
    if (i >= frames.size()) {
        return frames.size() - 1;
    }
    if (i > 0 && seconds - frames[i - 1].timeSeconds <= frames[i].timeSeconds - seconds) {
        --i;
    }
    return i;
}

// Runs the offline beat tracker over the frames' onset strengths and stamps the beat fields.
void stampBeats(std::vector<AnalysisFrame>& frames, OfflineBeats& beats, const AnalyzerConfig& cfg,
                const BeatTrackerConfig& beatConfig) {
    beats = OfflineBeats{};
    if (frames.empty() || cfg.sampleRate == 0) {
        return;
    }
    const double hop = static_cast<double>(cfg.hopSize) / static_cast<double>(cfg.sampleRate);
    std::vector<float> onsets;
    onsets.reserve(frames.size());
    std::size_t picked = 0;
    for (const auto& f : frames) {
        onsets.push_back(f.onsetStrength);
        picked += f.onset ? 1 : 0;
    }
    if (picked >= kMinOnsets) {
        beats = trackBeatsOffline(onsets, static_cast<float>(hop), beatConfig);
    }

    // The tracker's beat times are hop-relative (hopIndex * hop); move them onto the frames'
    // clock (window-centre times) so they compare directly with AnalysisFrame::timeSeconds.
    std::vector<std::size_t> beatFrames;
    beatFrames.reserve(beats.beatTimes.size());
    for (double& t : beats.beatTimes) {
        const auto index = static_cast<std::size_t>(
            std::clamp<long long>(std::llround(t / hop), 0, static_cast<long long>(frames.size()) - 1));
        if (!beatFrames.empty() && beatFrames.back() == index) {
            continue;
        }
        beatFrames.push_back(index);
        t = frames[index].timeSeconds;
    }
    beats.beatTimes.resize(beatFrames.size());

    const double period = beats.tempoBpm > 0.0f ? 60.0 / static_cast<double>(beats.tempoBpm) : 0.0;

    // ADR-896: refine the grid below the hop, weighting each beat by the onset strength around it.
    if (!beatFrames.empty()) {
        std::vector<double> strength(beatFrames.size(), 0.0);
        double strongest = 0.0;
        for (std::size_t b = 0; b < beatFrames.size(); ++b) {
            const std::size_t f = beatFrames[b];
            double sum = 0.0;
            int count = 0;
            for (std::size_t g = f > 0 ? f - 1 : 0; g <= f + 1 && g < frames.size(); ++g, ++count) {
                sum += static_cast<double>(frames[g].onsetStrength);
            }
            strength[b] = count > 0 ? sum / count : 0.0;
            strongest = std::max(strongest, strength[b]);
        }
        if (strongest > 0.0) {
            for (double& s : strength) {
                s /= strongest;
            }
        }
        refineGrid(beats.beatTimes, strength, period);
        // The frame each refined beat is stamped on is the one nearest its refined time; the
        // refinement moves beats less than half a hop almost everywhere, so this rarely changes.
        for (std::size_t b = 0; b < beatFrames.size(); ++b) {
            beatFrames[b] = nearestFrame(frames, beats.beatTimes[b]);
        }
        // Two beats cannot share a frame (they are a period apart and moved a tenth of one); keep the
        // stamping strictly increasing regardless, so a beat is never counted twice.
        for (std::size_t b = 1; b < beatFrames.size(); ++b) {
            beatFrames[b] = std::max(beatFrames[b], std::min(beatFrames[b - 1] + 1, frames.size() - 1));
        }
    }

    std::size_t next = 0; // index of the first beat after the frame
    for (std::size_t i = 0; i < frames.size(); ++i) {
        AnalysisFrame& f = frames[i];
        f.tempoBpm = beats.tempoBpm;
        f.tempoConfidence = beats.confidence;
        while (next < beatFrames.size() && beatFrames[next] <= i) {
            ++next;
        }
        f.beat = next > 0 && beatFrames[next - 1] == i;
        f.beatCount = static_cast<std::uint32_t>(next);
        double phase = 0.0;
        if (next > 0 && next < beatFrames.size()) {
            const double t0 = beats.beatTimes[next - 1];
            const double t1 = beats.beatTimes[next];
            phase = t1 > t0 ? (f.timeSeconds - t0) / (t1 - t0) : 0.0;
        } else if (!beatFrames.empty() && period > 0.0) {
            // Before the first / after the last beat: extrapolate with the global tempo.
            const double anchor = next == 0 ? beats.beatTimes.front() : beats.beatTimes.back();
            double x = (f.timeSeconds - anchor) / period;
            if (next > 0 && x < 0.0) {
                // The last beat's own frame can sit a fraction of a hop before the refined beat
                // (ADR-896): the beat has landed, so the phase is 0, not the end of the one before.
                x = 0.0;
            }
            phase = x - std::floor(x);
        }
        f.beatPhase = static_cast<float>(std::clamp(phase, 0.0, 1.0));
    }
}

} // namespace

AnalysisTrack AnalysisTrack::analyze(const audio::AudioFile& file, AnalyzerConfig config,
                                     BeatTrackerConfig beatConfig) {
    config.sampleRate = file.sampleRate();
    AnalysisTrack track;
    track.config_ = config;

    Analyzer analyzer(config);
    const auto mono = file.mono();
    const auto& cfg = analyzer.config();
    if (mono.size() >= cfg.windowSize) {
        track.frames_.reserve((mono.size() - cfg.windowSize) / cfg.hopSize + 1);
    }
    // The side channel, (left - right) / 2 of the first two channels, beside the mono downmix the
    // analyzer already takes as its mid: what the stereo width is measured from (ADR-897). A mono
    // file has none and its frames say so rather than reading as a perfectly narrow image.
    std::vector<float> side;
    if (file.channels() >= 2) {
        const auto interleaved = file.interleaved();
        const std::size_t channels = file.channels();
        side.resize(mono.size());
        for (std::size_t i = 0; i < side.size(); ++i) {
            side[i] = 0.5f * (interleaved[i * channels] - interleaved[i * channels + 1]);
        }
    }

    // Feed hop-sized chunks and drain after each so the analyzer's ready queue stays short; the
    // result is identical to pushing everything at once (chunking invariance).
    AnalysisFrame frame;
    for (std::size_t offset = 0; offset < mono.size(); offset += cfg.hopSize) {
        const std::size_t take = std::min<std::size_t>(cfg.hopSize, mono.size() - offset);
        analyzer.push(mono.subspan(offset, take),
                      side.empty() ? std::span<const float>{} : std::span<const float>(side).subspan(offset, take));
        while (analyzer.pop(frame)) {
            track.frames_.push_back(std::move(frame));
        }
    }
    track.config_ = analyzer.config();
    stampBeats(track.frames_, track.beats_, track.config_, beatConfig);

    // The passes that need the whole track (ADR-896/897/898).
    const float binHz = static_cast<float>(track.config_.sampleRate) / static_cast<float>(track.config_.windowSize);
    track.meter_ = estimateMeter(track.frames_, track.beats_.beatTimes, binHz);
    const BandOnsets onsets =
        detectBandOnsets(track.frames_, binHz, track.beats_.beatTimes, track.beatSeconds());
    stampBandOnsets(track.frames_, onsets, track.config_);
    // ADR-1060: the causal detector the live runner runs, over the file in the same order (file == live).
    detectCausalOnsets(track.frames_, binHz,
                       static_cast<double>(track.config_.hopSize) / static_cast<double>(track.config_.sampleRate));
    return track;
}

double overlayTrackFields(AnalysisFrame& live, const AnalysisTrack& track, double previousSeconds) {
    const auto& frames = track.frames();
    if (frames.empty()) {
        return live.timeSeconds;
    }
    const double now = live.timeSeconds;
    const AnalysisFrame& here = track.at(now);
    live.tempoBpm = here.tempoBpm;
    live.tempoConfidence = here.tempoConfidence;
    live.beatCount = here.beatCount;
    live.beatPhase = here.beatPhase;
    live.onsetRate = here.onsetRate;
    live.onsetRateIsPercussive = here.onsetRateIsPercussive;
    live.energy = here.energy;
    live.beat = false;
    live.lowOnset = live.midOnset = live.highOnset = false;
    live.lowOnsetStrength = live.midOnsetStrength = live.highOnsetStrength = 0.0f;
    constexpr double kMaxStepSeconds = 0.25;
    const bool continuous = now > previousSeconds && now - previousSeconds <= kMaxStepSeconds;
    if (continuous) {
        const auto from = std::upper_bound(frames.begin(), frames.end(), previousSeconds,
                                           [](double t, const AnalysisFrame& f) { return t < f.timeSeconds; });
        for (auto it = from; it != frames.end() && it->timeSeconds <= now; ++it) {
            live.beat = live.beat || it->beat;
            if (it->lowOnset) {
                live.lowOnset = true;
                live.lowOnsetStrength = std::max(live.lowOnsetStrength, it->lowOnsetStrength);
            }
            if (it->midOnset) {
                live.midOnset = true;
                live.midOnsetStrength = std::max(live.midOnsetStrength, it->midOnsetStrength);
            }
            if (it->highOnset) {
                live.highOnset = true;
                live.highOnsetStrength = std::max(live.highOnsetStrength, it->highOnsetStrength);
            }
        }
    }
    return now;
}

const AnalysisFrame& AnalysisTrack::at(double seconds) const {
    assert(!frames_.empty() && "AnalysisTrack::at on an empty track");
    if (frames_.empty()) {
        static const AnalysisFrame kEmpty{};
        return kEmpty;
    }
    if (std::isnan(seconds)) {
        return frames_.front();
    }
    // First frame at or after `seconds`; the nearest is either that one or its predecessor.
    const auto upper = std::lower_bound(frames_.begin(), frames_.end(), seconds,
                                        [](const AnalysisFrame& f, double t) { return f.timeSeconds < t; });
    if (upper == frames_.begin()) {
        return *upper;
    }
    if (upper == frames_.end()) {
        return frames_.back();
    }
    const auto lower = std::prev(upper);
    return (seconds - lower->timeSeconds) <= (upper->timeSeconds - seconds) ? *lower : *upper;
}

} // namespace avgen::analysis
