#pragma once

// Offline analysis stems (stems-multichannel, ADR-1520): what a project says about each stem, and what was found when
// the file was checked.
//
// A stem is an ANALYSIS-ONLY source. It is never played, never mixed into playback and never substituted for the mix:
// the project's stereo mix (assets.audio / assets.audioClips) is the only thing the audio device hears. That is why
// stems are their own list (`assets.stems`) and not arrangement clips, which ADR-103 mixes down.

#include "audio/source_role.hpp"
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
    // ADR-1630: what the stem is. Only `drums` runs the kick/snare/hat classifier (ADR-1060) and publishes the class
    // names; every role publishes onsets, levels and the onset register. Explicit only -- never inferred from the key.
    SourceRole role = SourceRole::Other;
    float gainDb = 0.0f;            // ADR-1582: the stem's trim before analysis, -24..+24 dB (`gainDb`)
    [[nodiscard]] bool hits() const { return classifiesHits(role); }
    [[nodiscard]] bool operator==(const StemSource&) const = default;
};

// `label` -> a key not in `taken`: lower case, every run of characters outside [a-z0-9] becomes one '_', trimmed;
// empty becomes "stem"; a key already taken gets "_2", "_3", ...
[[nodiscard]] std::string makeStemKey(std::string_view label, std::span<const std::string> taken);
// Whether `key` is a valid stem key ([a-z0-9_]+, not empty).
[[nodiscard]] bool validStemKey(std::string_view key);

// What reading a stem file found, gathered while it is analysed (the samples are not kept).
struct StemFileFacts {
    std::uint32_t sampleRate = 0;
    std::uint32_t channels = 0;
    std::uint64_t frames = 0;
    float peak = 0.0f;
    std::size_t nonFinite = 0; // NaN / Inf samples
    bool silent = true;        // every sample is exactly 0
    std::string format;        // the file's own sample format ("s16", "s24", "f32", ...), "" when unknown
    // ADR-1632: the stem's alignment probe (`StemProbeWriter`), for the stem-set check; empty when not taken.
    std::vector<float> probe;
};
// The facts of a decoded file (tests, and anything holding the samples already).
[[nodiscard]] StemFileFacts factsOf(const AudioFile& file);

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

// The report on one stem file from what reading it found (`facts`, null when decoding failed with `decodeError`).
// Diagnostics, each naming the file:
//   error    the file did not decode, or it holds non-finite samples
//   warning  every sample is silent
[[nodiscard]] StemReport checkStem(const StemSource& source, const StemFileFacts* facts, const std::string& decodeError);

// What a checked stem is against the playback mix -- recomputed whenever the mix or the offset changes, without the
// samples:
//   info     its sample rate differs from the mix's (analysed at its own rate; nothing is resampled)
//   warning  it lies wholly outside the mix; its span (offset .. offset + duration) differs from the mix's by more
//            than `toleranceSeconds` (outside its span it publishes nothing)
// Matching durations are NOT reported as alignment: alignment is whatever the offset says.
[[nodiscard]] std::vector<StemDiagnostic> mixDiagnostics(const StemSource& source, const StemReport& report,
                                                         const MixFacts& mix, double toleranceSeconds = 0.05);

// The file's own sample format ("u8", "s16", "s24", "s32", "f32") without decoding it, "" when unknown.
[[nodiscard]] std::string nativeSampleFormat(const std::filesystem::path& path);

// ---- the stem-set check (ADR-1632) -----------------------------------------------------------------------------
//
// Whether a project's stems line up with its mix, from a PROBE of each: the mono signal box-averaged down to about
// kStemProbeHz (an integer factor of the file's rate, the same for every file of one rate). The probe is linear, so
// the probe of a sum is the sum of the probes: a set that sums to the mix sums to the mix's probe to the same
// residual, and a stem off by d seconds is off by d in its probe. 210 s is 3.4 MB of probe per stem.
//
//   rate      a stem at another rate than the mix: info (analysed at its own rate); the set's sum leaves it out
//   channels  a stem with another channel count than the mix: info (analysed as mono; a mono stem's width reads 0)
//   length    mixDiagnostics' span check, unchanged
//   sum       the mix fitted as a weighted sum of the stems (one gain per stem: a mix has faders), at the stems'
//             offsets: the residual in dB below the mix. Info -- a set may be incomplete (no vocal stem) and that is
//             not misalignment
//   lag       per stem, the lag within +-kStemProbeMaxLagSeconds at which it best matches what the other stems leave
//             of the mix (normalised cross-correlation over its 16 loudest seconds: coarse at an eighth of the probe
//             rate, then refined at the probe rate). A warning when that is not 0, naming the offset that aligns it; a
//             warning too when it matches nothing (another song, or a lag beyond the search)
//
// Nothing is ever corrected: AV Gen does not guess an offset (ADR-1520). It says so, and the artist sets it.
inline constexpr double kStemProbeHz = 4000.0;
inline constexpr double kStemProbeMaxLagSeconds = 0.5;

// The probe's decimation factor for a file at `sampleRate` (>= 1).
[[nodiscard]] std::uint32_t stemProbeFactor(std::uint32_t sampleRate);

// Streams mono samples into a probe.
class StemProbeWriter {
public:
    explicit StemProbeWriter(std::uint32_t sampleRate);
    void push(std::span<const float> mono);
    [[nodiscard]] std::vector<float> take() { return std::move(probe_); }
    [[nodiscard]] double rate() const { return rate_; }

private:
    std::uint32_t factor_ = 1;
    double rate_ = 0.0;
    double sum_ = 0.0;
    std::uint32_t count_ = 0;
    std::vector<float> probe_;
};

// The probe of a whole mono signal.
[[nodiscard]] std::vector<float> stemProbe(std::span<const float> mono, std::uint32_t sampleRate);

struct StemSetMember {
    const StemSource* source = nullptr;
    const StemReport* report = nullptr; // what decoding found (rate, channels); a stem that did not decode is skipped
    std::span<const float> probe;       // empty: no probe (left out of the sum and the lag search)
};

struct MixProbe {
    std::uint32_t sampleRate = 0;
    std::uint32_t channels = 0;
    std::span<const float> probe;
};

struct StemSetCheck {
    std::vector<std::vector<StemDiagnostic>> perStem; // one list per member, in order
    std::vector<StemDiagnostic> set;                  // the set as a whole (the sum)
    double residualDb = 0.0;                          // the fitted sum's residual against the mix; 0 when not measured
    bool measured = false;                            // the sum was measured (the mix and >= 1 stem at its rate)
    std::vector<float> gains;                         // per member, the fitted gain (0 when left out)
    std::vector<double> lagSeconds;                   // per member, the best lag found (0 when aligned or not searched)
    std::vector<float> match;                         // per member, the normalised correlation at that lag (0..1)
};

[[nodiscard]] StemSetCheck checkStemSet(std::span<const StemSetMember> members, const MixProbe& mix);

} // namespace avgen::audio
