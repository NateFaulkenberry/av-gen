#pragma once

// "Rebuild", the song of Glowmere Valley 3, analysed once per process -- for the tests that measure
// the engine against it (ADR-896..899, ADR-920..923).
//
// The song is the owner's and is never in the repository. It is read from $AVGEN_REBUILD_AUDIO, or
// ~/Desktop/Rebuild.mp3, and a test SKIPs when it is not there -- or when it is not the master the
// hand-measured ground truth (docs/glowmere-valley-3/01-music.md) was taken on (its SHA-256 differs).
//
// The ground truth: a constant 130.000 BPM grid, the first downbeat at 0.480 s, 8-bar phrases.

#include "analysis/analysis_track.hpp"
#include "audio/audio_file.hpp"
#include "core/hash.hpp"

#include <cstdlib>
#include <filesystem>
#include <optional>

namespace avgen::testing {

inline constexpr const char* kRebuildSha256 = "53a7c00cbdca9a078338db1f5a1cc725fa6b926623dccb7728f55e948838a030";
inline constexpr double kRebuildBpm = 130.0;
inline constexpr double kRebuildBeat = 60.0 / kRebuildBpm;
inline constexpr double kRebuildBar = 4.0 * kRebuildBeat;
inline constexpr double kRebuildFirstDownbeat = 0.480;

// The true second of bar `bar`, beat `beat` (both 1-based), on the hand-measured grid.
inline double rebuildBeatAt(double bar, double beat = 1.0) {
    return kRebuildFirstDownbeat + (bar - 1.0) * kRebuildBar + (beat - 1.0) * kRebuildBeat;
}

inline const analysis::AnalysisTrack* rebuildTrack() {
    static const std::optional<analysis::AnalysisTrack> track = []() -> std::optional<analysis::AnalysisTrack> {
        std::filesystem::path path;
        if (const char* env = std::getenv("AVGEN_REBUILD_AUDIO"); env != nullptr && *env != '\0') {
            path = env;
        } else if (const char* home = std::getenv("HOME"); home != nullptr) {
            path = std::filesystem::path(home) / "Desktop" / "Rebuild.mp3";
        }
        if (path.empty() || !std::filesystem::exists(path)) {
            return std::nullopt;
        }
        const auto sha = sha256File(path);
        if (!sha || *sha != kRebuildSha256) {
            return std::nullopt;
        }
        auto file = audio::AudioFile::load(path);
        if (!file) {
            return std::nullopt;
        }
        return analysis::AnalysisTrack::analyze(*file, analysis::AnalyzerConfig{});
    }();
    return track ? &*track : nullptr;
}

} // namespace avgen::testing
