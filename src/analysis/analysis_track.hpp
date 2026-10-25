#pragma once

// Offline analysis of a whole AudioFile, addressable by timeline position (ADR-012). Uses the
// identical Analyzer, so live and offline features agree.

#include "analysis/analyzer.hpp"
#include "analysis/beat_tracker.hpp"
#include "analysis/meter.hpp"
#include "audio/audio_file.hpp"

#include <span>
#include <string>
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

// The offline (Ellis) tracker and ADR-896's refinement over one source's hops: `times` the hops' window centres,
// `onsetStrength` the Analyzer's onset strength per hop, `picked` how many hops picked an onset (fewer than four: no
// beats). Beat times are on the `times` clock; `hops`, when given, gets the hop each beat is stamped on (strictly
// increasing). What `AnalysisTrack::analyze` runs on the mix, and ADR-1581 on a stem.
[[nodiscard]] OfflineBeats trackGrid(std::span<const double> times, std::span<const float> onsetStrength,
                                     std::size_t picked, double hop, const BeatTrackerConfig& beatConfig = {},
                                     std::vector<std::size_t>* hops = nullptr);

// ADR-1581: the beat grid the project's clock runs on -- the mix's (`AnalysisTrack::beats` and its metre estimate), or
// the one tracked on the stem `feeds.beat` names, its times on the project clock (the stem's offset added) and its
// metre estimated over the mix's frames on those beats.
struct TrackedGrid {
    OfflineBeats beats;
    MeterEstimate meter;
    std::string sourceKey; // "" = the mix
    [[nodiscard]] double beatSeconds() const {
        return beats.tempoBpm > 0.0f ? 60.0 / static_cast<double>(beats.tempoBpm) : 0.0;
    }
    [[nodiscard]] bool empty() const { return beats.beatTimes.empty(); }
};

// ADR-896: live playback of a loaded file hears what a render hears.
//
// The live runner analyses the audio as the device plays it, with the causal beat tracker -- so in
// the editor `music.downbeat` counted beats from wherever play was pressed, while a render of the
// same second had the whole track's grid. This overlays a live frame with the whole-track analysis
// at the same position: the tracked beat (flag, count, phase, tempo), the onset rate and the energy
// composite (walked from the track's start, as a render reads them). The beat is OR-ed over the track
// frames in (`previousSeconds`, `live.timeSeconds`], so none is
// lost or repeated between two live frames; after a jump (time went back, or forward by more than
// 0.25 s) no event is carried, only the continuous fields. Returns the time to pass next call.
double overlayTrackFields(AnalysisFrame& live, const AnalysisTrack& track, double previousSeconds);

} // namespace avgen::analysis
