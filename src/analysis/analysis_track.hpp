#pragma once

// Offline analysis of a whole AudioFile, addressable by timeline position (ADR-012). Uses the
// identical Analyzer, so live and offline features agree.

#include "analysis/analyzer.hpp"
#include "audio/audio_file.hpp"

#include <vector>

namespace avgen::analysis {

class AnalysisTrack {
public:
    static AnalysisTrack analyze(const audio::AudioFile& file, AnalyzerConfig config);

    [[nodiscard]] const std::vector<AnalysisFrame>& frames() const { return frames_; }
    [[nodiscard]] bool empty() const { return frames_.empty(); }
    // Frame whose centre is nearest to `seconds` (clamped to the track range).
    [[nodiscard]] const AnalysisFrame& at(double seconds) const;
    [[nodiscard]] const AnalyzerConfig& config() const { return config_; }

private:
    AnalyzerConfig config_;
    std::vector<AnalysisFrame> frames_;
};

} // namespace avgen::analysis
