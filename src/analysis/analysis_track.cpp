#include "analysis/analysis_track.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <utility>

namespace avgen::analysis {

AnalysisTrack AnalysisTrack::analyze(const audio::AudioFile& file, AnalyzerConfig config, BeatTrackerConfig /*beatConfig*/) {
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
