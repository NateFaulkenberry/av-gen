#pragma once

// Offline analysis of a whole AudioFile, addressable by timeline position (ADR-012). Uses the
// identical Analyzer, so live and offline features agree.

#include "analysis/analyzer.hpp"
#include "analysis/beat_tracker.hpp"
#include "audio/audio_file.hpp"

#include <vector>

namespace avgen::analysis {

class AnalysisTrack {
public:
    // Runs the analyzer, then the offline (Ellis) beat tracker and stamps beat fields per frame.
    static AnalysisTrack analyze(const audio::AudioFile& file, AnalyzerConfig config, BeatTrackerConfig beatConfig = {});

    [[nodiscard]] const std::vector<AnalysisFrame>& frames() const { return frames_; }
    [[nodiscard]] bool empty() const { return frames_.empty(); }
    // Frame whose centre is nearest to `seconds` (clamped to the track range).
    [[nodiscard]] const AnalysisFrame& at(double seconds) const;
    [[nodiscard]] const AnalyzerConfig& config() const { return config_; }
    [[nodiscard]] const OfflineBeats& beats() const { return beats_; }

private:
    AnalyzerConfig config_;
    std::vector<AnalysisFrame> frames_;
    OfflineBeats beats_;
};

} // namespace avgen::analysis
