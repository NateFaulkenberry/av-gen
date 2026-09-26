#pragma once

// What a stretch of the song sounds like, measured (ADR-899).
//
// A section's `energy` and `density` used to be the only audio facts a director was handed, and on a
// flat master both were wrong: energy was RMS rescaled to the track's range (so "Rebuild"'s sub-heavy
// break read 1.0) and density was the median of a per-hop onset flag (so it read 0 everywhere). This
// is the replacement: every level-free feature ADR-897 measures, averaged over a span, so a section,
// a plan section and the Director's musical context can all say "brighter and busier than the last
// one" in numbers that survive a limiter.

#include "analysis/analyzer.hpp"

#include <nlohmann/json_fwd.hpp>

#include <array>
#include <cstddef>

namespace avgen::analysis {

class AnalysisTrack;

struct SpanProfile {
    // The energy composite (ADR-897), mean over the span, 0..1. Not rescaled to the track.
    float energy = 0.0f;
    // Onsets per second over the span: all percussive band onsets (one per 30 ms), and each band's.
    // Live-only tracks (no band onsets) count the broadband picked onsets in `onsetRate`.
    float onsetRate = 0.0f;
    float kickRate = 0.0f;  // low band
    float snareRate = 0.0f; // mid band
    float hatRate = 0.0f;   // high band
    // Brightness: the mean spectral centroid over the span's non-silent frames, Hz.
    float brightnessHz = 0.0f;
    // Each analyzer band's mean power over the span, in dB, and on the long-term level scale
    // (`bandLevelFromPower`: 0 = -60 dB, 1 = 0 dB). Index = the analyzer's band order (bass, lowMid,
    // mid, highMid, treble by default).
    std::array<float, kMaxBands> bandDb{};
    std::array<float, kMaxBands> bandLevels{};
    std::size_t bandCount = 0;
    // Stereo width, mean side/mid RMS; `stereo` false for a mono source.
    float width = 0.0f;
    bool stereo = false;
    std::size_t frames = 0; // how many analysis frames the span covered (0 = nothing measured)

    [[nodiscard]] bool measured() const { return frames > 0; }
    friend bool operator==(const SpanProfile&, const SpanProfile&) = default;
};

// {"energy", "onsetRate", "kickRate", "snareRate", "hatRate", "brightnessHz", "bandsDb": {"bass": dB,
// ...}, "bandLevels": {...}, "width", "frames"} -- the shape `director.inspect_scene` and a song
// plan write. `fromJson` reads back exactly what `toJson` wrote.
[[nodiscard]] nlohmann::json spanProfileToJson(const SpanProfile& profile);
[[nodiscard]] SpanProfile spanProfileFromJson(const nlohmann::json& j);

// Measures [startSeconds, endSeconds). An empty or inverted span, or one outside the track, measures
// nothing (`frames` 0) rather than borrowing a neighbour's numbers.
[[nodiscard]] SpanProfile profileSpan(const AnalysisTrack& track, double startSeconds, double endSeconds);

// The analyzer's band names, in band order, for anything that labels `bandDb` / `bandLevels`.
[[nodiscard]] const std::array<const char*, 5>& defaultBandNames();

} // namespace avgen::analysis
