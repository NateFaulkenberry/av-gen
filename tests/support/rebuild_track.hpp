#pragma once

// "Rebuild", the song of Glowmere Valley 3, analysed once per process -- for the tests that measure
// the engine against it (ADR-896..899, ADR-920..923).
//
// The song is the owner's: it lives in the checkout at assets/audio/rebuild.mp3, gitignored and never
// committed (the repository is public). It is read from $AVGEN_REBUILD_AUDIO, or that path, and a test SKIPs
// when it is not there -- or when it is not the master the hand-measured ground truth
// (docs/glowmere-valley-3/01-music.md) was taken on (its SHA-256 differs).
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

// The text every Rebuild test SKIPs with.
inline constexpr const char* kRebuildMissing =
    "rebuild.mp3 (the GV3 master, sha256 53a7c00c...) is not present: put it at assets/audio/rebuild.mp3 "
    "(gitignored) or set AVGEN_REBUILD_AUDIO";

// Where the song is read from: $AVGEN_REBUILD_AUDIO, else assets/audio/rebuild.mp3, the path every project that
// plays it names.
inline std::filesystem::path rebuildAudioPath() {
    if (const char* env = std::getenv("AVGEN_REBUILD_AUDIO"); env != nullptr && *env != '\0') {
        return env;
    }
    return std::filesystem::path(AVGEN_SOURCE_DIR) / "assets" / "audio" / "rebuild.mp3";
}

inline const analysis::AnalysisTrack* rebuildTrack() {
    static const std::optional<analysis::AnalysisTrack> track = []() -> std::optional<analysis::AnalysisTrack> {
        const std::filesystem::path path = rebuildAudioPath();
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
