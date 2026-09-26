#pragma once

// Offline analysis of a whole AudioFile, addressable by timeline position (ADR-012). Uses the
// identical Analyzer, so live and offline features agree.

#include "analysis/analyzer.hpp"
#include "analysis/beat_tracker.hpp"
#include "analysis/meter.hpp"
#include "audio/audio_file.hpp"

#include <vector>

namespace avgen::analysis {

class AnalysisTrack {
public:
    // Runs the analyzer (with the file's side channel, for stereo width), then the offline (Ellis)
    // beat tracker and stamps beat fields per frame, then the whole-track passes a stream cannot
    // make: the meter estimate (ADR-896) and the band-limited onsets with the percussive onset rate
    // and energy composite that depend on them (ADR-897/898).
    static AnalysisTrack analyze(const audio::AudioFile& file, AnalyzerConfig config, BeatTrackerConfig beatConfig = {});

    [[nodiscard]] const std::vector<AnalysisFrame>& frames() const { return frames_; }
    [[nodiscard]] bool empty() const { return frames_.empty(); }
    // Frame whose centre is nearest to `seconds` (clamped to the track range).
    [[nodiscard]] const AnalysisFrame& at(double seconds) const;
    [[nodiscard]] const AnalyzerConfig& config() const { return config_; }
    [[nodiscard]] const OfflineBeats& beats() const { return beats_; }
    // What the analysis concluded about the meter: which tracked beat is beat 1, and the phrase
    // length when it is clear (ADR-896). The engine resolves the project's pinned values over it.
    [[nodiscard]] const MeterEstimate& meterEstimate() const { return meter_; }
    // Seconds per beat of the tracked grid (60 / tempo), 0 when there is no tempo.
    [[nodiscard]] double beatSeconds() const {
        return beats_.tempoBpm > 0.0f ? 60.0 / static_cast<double>(beats_.tempoBpm) : 0.0;
    }

private:
    AnalyzerConfig config_;
    std::vector<AnalysisFrame> frames_;
    OfflineBeats beats_;
    MeterEstimate meter_;
};

// ADR-896/898: live playback of a loaded file hears what a render hears.
//
// The live runner analyses the audio as the device plays it, with the causal beat tracker and no
// band onsets -- so in the editor `music.downbeat` counted beats from wherever play was pressed and
// `audio.onsetLow` never fired, while a render of the same second had both. This overlays a live
// frame with the whole-track analysis at the same position: the tracked beat (flag, count, phase,
// tempo), the three band onsets, the percussive onset rate and the energy composite that includes
// it. Events are OR-ed over the track frames in (`previousSeconds`, `live.timeSeconds`], so none is
// lost or repeated between two live frames; after a jump (time went back, or forward by more than
// 0.25 s) no event is carried, only the continuous fields. Returns the time to pass next call.
double overlayTrackFields(AnalysisFrame& live, const AnalysisTrack& track, double previousSeconds);

} // namespace avgen::analysis
