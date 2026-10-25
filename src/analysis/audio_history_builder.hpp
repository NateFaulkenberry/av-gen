#pragma once

// ADR-1116: what fills a spatial::AudioHistory.
//
// Offline (and live playback of a loaded file, which hears what a render hears): the whole analysed
// track, folded once. Each analysis frame's 1,025-bin log spectrum becomes one 64-bin row on a
// log-frequency axis (32 Hz-16 kHz), and then every bin is stretched over its own range across the
// whole track: 0 at the bin's 20th percentile, 1 at its 98th. The stretch is what keeps a quiet
// intro dark and makes the treble as legible as the bass. The onset lists are the track's onsets,
// each on the row of its register (`onsetRows`), and its beats.
//
// Live input: there is no track. Each render frame pushes the newest analysis frame; the stretch
// uses running estimates of the same two percentiles (a slow floor follower and a decaying peak),
// so a live history is *visually equivalent* to an offline one, never equal. Onsets are the live
// frame's latched events (ADR-1060).

#include "analysis/analysis_track.hpp"
#include "analysis/stem_analysis.hpp"
#include "spatial/audio_history.hpp"

#include <array>
#include <memory>
#include <span>

namespace avgen::analysis {

// The log-frequency fold of one frame's `spectrum` (raw, before any stretch): kAudioBins values.
void foldSpectrum(const AnalysisFrame& frame, const AnalyzerConfig& config, std::span<float> out);

// The whole track, folded and stretched. Empty history (no rows) for an empty track.
[[nodiscard]] spatial::AudioHistory buildAudioHistory(const AnalysisTrack& track);

// ADR-1631: a history heard from an analysis source (a stem) instead of the mix -- its spectrogram, its onset rows, or
// both (`feeds.history`). Null `track`: the mix's.
struct HistoryFrom {
    const StemTrack* track = nullptr;
    double offsetSeconds = 0.0; // where the source's first sample sits on the project timeline
    bool classes = false;       // a drums source (ADR-1630): its kick / snare / hat are the low / mid / high rows
};
// The onset rows (low, mid, high) one picked onset contributes, as strengths (0 = none): the onset lands on the row
// of its register's largest share (ADR-1604: low 30-250 Hz, mid 250 Hz-4 kHz, high 4-16 kHz), at the strength the
// bus gives it (`onsetStrength` x 0.5, floored at 0.05 and capped at 1); one whose register is unknown (no band
// gained) is on all three. Where an attack is, never what made it: the rule for the mix and every source that is not
// a drums source.
[[nodiscard]] std::array<float, 3> onsetRows(bool onset, float onsetStrength, const std::array<float, 3>& reg);
// The mix frame's (its `onset` and `onsetRegister`).
[[nodiscard]] std::array<float, 3> onsetRows(const AnalysisFrame& frame);
// A source's hop. A drums source's rows are its measured classes -- kick, snare, hat (ADR-1630); any other source's
// are its onset by register (`onsetRows`).
[[nodiscard]] std::array<float, 3> sourceOnsetRows(const StemFrame& frame, bool classes);
// The whole track's history with the spectrogram and the low/mid/high onset rows taken from those sources. The beat
// row is always the mix's (ADR-1630 recommendation 6: the mix's grid stays the default). A source's spectrogram is
// stretched over its own percentiles like the mix's, sits at its offset, and reads 0 before and after its span. A pure
// function of the inputs, so a seek is exact as before.
[[nodiscard]] spatial::AudioHistory buildAudioHistory(const AnalysisTrack& track, const HistoryFrom& spectrum,
                                                      const HistoryFrom& onsets);

class LiveAudioHistoryFeed {
public:
    explicit LiveAudioHistoryFeed(const AnalyzerConfig& config);
    // Push the render frame's newest analysis frame (events already latched).
    void push(const AnalysisFrame& frame);
    // ADR-1631: push one row from any source -- `rawRow` a `foldSpectrum` row before the stretch (a source's
    // `StemFrame::spectrumRow`, or zeros for a silent / inactive one) -- with this frame's onsets: strengths per row
    // (low, mid, high, beat), 0 for none. Times must increase.
    void pushRaw(double time, std::span<const float> rawRow, const std::array<float, spatial::kOnsetSources>& onsets);
    [[nodiscard]] std::shared_ptr<spatial::AudioHistory> history() const { return history_; }

private:
    AnalyzerConfig config_;
    std::shared_ptr<spatial::AudioHistory> history_;
    std::array<float, spatial::kAudioBins> floor_{};
    std::array<float, spatial::kAudioBins> peak_{};
    double lastTime_ = -1.0;
    bool primed_ = false;
};

} // namespace avgen::analysis
