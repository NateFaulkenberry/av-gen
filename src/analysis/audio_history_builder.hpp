#pragma once

// ADR-1116: what fills a spatial::AudioHistory.
//
// Offline (and live playback of a loaded file, which hears what a render hears): the whole analysed
// track, folded once. Each analysis frame's 1,025-bin log spectrum becomes one 64-bin row on a
// log-frequency axis (32 Hz-16 kHz), and then every bin is stretched over its own range across the
// whole track: 0 at the bin's 20th percentile, 1 at its 98th. The stretch is what keeps a quiet
// intro dark and makes the treble as legible as the bass. The onset lists are the track's band
// onsets (ADR-898) and its beats.
//
// Live input: there is no track. Each render frame pushes the newest analysis frame; the stretch
// uses running estimates of the same two percentiles (a slow floor follower and a decaying peak),
// so a live history is *visually equivalent* to an offline one, never equal. Onsets are the live
// frame's latched events (ADR-1060).

#include "analysis/analysis_track.hpp"
#include "spatial/audio_history.hpp"

#include <array>
#include <memory>
#include <span>

namespace avgen::analysis {

// The log-frequency fold of one frame's `spectrum` (raw, before any stretch): kAudioBins values.
void foldSpectrum(const AnalysisFrame& frame, const AnalyzerConfig& config, std::span<float> out);

// The whole track, folded and stretched. Empty history (no rows) for an empty track.
[[nodiscard]] spatial::AudioHistory buildAudioHistory(const AnalysisTrack& track);

class LiveAudioHistoryFeed {
public:
    explicit LiveAudioHistoryFeed(const AnalyzerConfig& config);
    // Push the render frame's newest analysis frame (events already latched).
    void push(const AnalysisFrame& frame);
    [[nodiscard]] std::shared_ptr<const spatial::AudioHistory> history() const { return history_; }

private:
    AnalyzerConfig config_;
    std::shared_ptr<spatial::AudioHistory> history_;
    std::array<float, spatial::kAudioBins> floor_{};
    std::array<float, spatial::kAudioBins> peak_{};
    double lastTime_ = -1.0;
    bool primed_ = false;
};

} // namespace avgen::analysis
