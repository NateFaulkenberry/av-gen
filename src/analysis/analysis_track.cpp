#include "analysis/analysis_track.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <utility>

namespace avgen::analysis {

namespace {

// Picked onsets required before the track is considered rhythmic at all (matches the live
// tracker's gate): a steady tone's sub-threshold flux noise must not produce beats.
constexpr std::size_t kMinOnsets = 4;

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
            const double x = (f.timeSeconds - anchor) / period;
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

    // Feed hop-sized chunks and drain after each so the analyser's ready queue stays short; the
    // result is identical to pushing everything at once (chunking invariance).
    AnalysisFrame frame;
    for (std::size_t offset = 0; offset < mono.size(); offset += cfg.hopSize) {
        const std::size_t take = std::min<std::size_t>(cfg.hopSize, mono.size() - offset);
        analyzer.push(mono.subspan(offset, take));
        while (analyzer.pop(frame)) {
            track.frames_.push_back(std::move(frame));
        }
    }
    track.config_ = analyzer.config();
    stampBeats(track.frames_, track.beats_, track.config_, beatConfig);
    return track;
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
