#pragma once

// Offline analysis stems (stems-multichannel, ADR-1520): what a project says about each stem, and what was found when
// the file was checked.
//
// A stem is an ANALYSIS-ONLY source. It is never played, never mixed into playback and never substituted for the mix:
// the project's stereo mix (assets.audio / assets.audioClips) is the only thing the audio device hears. That is why
// stems are their own list (`assets.stems`) and not arrangement clips, which ADR-103 mixes down.

#include "core/error.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace avgen::audio {

class AudioFile;

struct StemSource {
    // The signal identity, `stem.<key>.*`: [a-z0-9_]+, unique in the project. Made from the label when the stem is
    // added (`makeStemKey`) and stored -- never re-derived -- so renaming the label moves no route.
    std::string key;
    std::string label;              // free text, shown to the artist; duplicates allowed
    std::filesystem::path file;
    // Where the stem's first sample sits on the project timeline, seconds. Positive: the stem starts later. Explicit
    // only -- AV Gen never guesses an offset from the audio.
    double offsetSeconds = 0.0;
    bool hits = false;              // also run the kick/snare/hat classifier (ADR-1060) on this stem
    [[nodiscard]] bool operator==(const StemSource&) const = default;
};

// `label` -> a key not in `taken`: lower case, every run of characters outside [a-z0-9] becomes one '_', trimmed;
// empty becomes "stem"; a key already taken gets "_2", "_3", ...
[[nodiscard]] std::string makeStemKey(std::string_view label, std::span<const std::string> taken);
// Whether `key` is a valid stem key ([a-z0-9_]+, not empty).
[[nodiscard]] bool validStemKey(std::string_view key);

enum class StemSeverity : std::uint8_t { Info, Warning, Error };

struct StemDiagnostic {
    StemSeverity severity = StemSeverity::Info;
    std::string message; // names the file
};

// What checking one stem file found. `ok()` is false only when the stem cannot be analysed at all.
struct StemReport {
    bool decoded = false;
    std::uint32_t sampleRate = 0;
    std::uint32_t channels = 0;
    std::uint64_t frames = 0;
    double durationSeconds = 0.0;
    std::string format;             // the file's own sample format as decoded natively, e.g. "s16", "s24", "f32"
    float peak = 0.0f;
    bool silent = false;            // every sample is exactly 0
    std::size_t nonFinite = 0;      // NaN / Inf samples: the stem is not analysed (an error)
    std::vector<StemDiagnostic> diagnostics;
    [[nodiscard]] bool ok() const { return decoded && nonFinite == 0; }
    [[nodiscard]] bool hasError() const;
};

// What the stem is checked against: the playback mix. Unknown (no mix loaded) leaves the comparisons out.
struct MixFacts {
    std::uint32_t sampleRate = 0;
    double durationSeconds = 0.0;
};

// Checks a decoded stem (`file`, null when decoding failed with `decodeError`) against the mix. The caller decodes;
// this reads the samples and, for `format`, the file's header. Diagnostics:
//   error    the file did not decode, or it holds non-finite samples
//   warning  it is all silence; its extent (offset + duration) differs from the mix's by
//            more than `toleranceSeconds`; it lies wholly outside the mix
//   info     its sample rate differs from the mix's (analysed at its own rate; nothing is resampled)
// Matching durations are NOT reported as alignment: alignment is whatever the offset says.
[[nodiscard]] StemReport checkStem(const StemSource& source, const AudioFile* file, const std::string& decodeError,
                                   const MixFacts& mix, double toleranceSeconds = 0.05);

// The file's own sample format ("u8", "s16", "s24", "s32", "f32") without decoding it, "" when unknown.
[[nodiscard]] std::string nativeSampleFormat(const std::filesystem::path& path);

} // namespace avgen::audio
