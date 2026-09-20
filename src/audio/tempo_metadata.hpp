#pragma once

// Embedded Tempo: tempo/BPM carried *in the file* by its container's metadata, read once at
// import (ADR-394).
//
// This is Tempo Metadata, not Tempo Estimation. Nothing in this header looks at a single audio
// sample. `analysis::estimateTempo` and `analysis::trackBeatsOffline` are the estimation path and
// they answer a different question: this file says what number the producer wrote down, they say
// where the beats actually fall. A file can have both, one, or neither, and the two never merge
// into an undifferentiated "tempo" -- `AudioTempo::source` is the whole point.
//
// This is the ONE extraction layer. No ID3 frame, Vorbis comment or container chunk is parsed
// anywhere else in the engine; importer, sequencer and UI all read `AudioTempo`.
//
// What an embedded BPM gives you: seconds per beat, and beat-relative durations.
// What it does NOT give you: beat phase, downbeat, bar lines, time signature, swing, or any
// tempo change. Those come from analysis or from the user. See ADR-394.

#include "core/error.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::audio {

// Where a tempo value came from. Never collapse two of these into one: a producer's TBPM and a
// tempogram peak are not the same kind of fact, and the user's own number outranks both.
//
// Named Provenance, not Source, on purpose. `app::TempoSource{Analysis, MidiClock}`
// (src/app/engine.hpp) already exists and answers a *different* question: which live clock drives
// the beat signals. This answers how a number came to be known. Two enums called TempoSource, one
// selecting a clock and one recording an origin, is the same trap as this codebase's two
// unrelated "shot" types, so the second one gets a different word.
enum class TempoProvenance {
    None,             // nothing knows the tempo
    EmbeddedMetadata, // read from the container's metadata by readEmbeddedTempo()
    Detected,         // estimated from the waveform by the analysis path
    UserOverride,     // the artist typed it; outranks everything and is never overwritten
};

[[nodiscard]] std::string_view tempoProvenanceName(TempoProvenance source);      // "Embedded Metadata"
[[nodiscard]] std::string_view tempoProvenanceToken(TempoProvenance source);     // "embedded" (for JSON)
[[nodiscard]] TempoProvenance tempoProvenanceFromToken(std::string_view token);  // unknown -> None

struct AudioTempo {
    bool available = false;
    double bpm = 0.0;
    TempoProvenance source = TempoProvenance::None;
    // Diagnostics: which field of which format supplied it. Empty unless source is
    // EmbeddedMetadata. e.g. metadataKey "TBPM", metadataFormat "ID3v2.4".
    std::string metadataKey;
    std::string metadataFormat;
    // 1.0 for embedded metadata (the file asserts it); the tracker's own confidence for Detected.
    double confidence = 0.0;
    // True when the *format* declares the field an integer (ID3 TBPM is "a numerical string" of
    // an integer). The parsed value is still kept exactly as written -- real taggers write
    // "127.5" into TBPM regardless -- but this records what the spec promises, so a round-trip
    // and the UI can tell 128 from 128.00.
    bool integerSemantics = false;

    [[nodiscard]] bool isEmbedded() const { return available && source == TempoProvenance::EmbeddedMetadata; }
    // A known BPM gives exactly this and nothing else. 0 when unavailable.
    [[nodiscard]] double secondsPerBeat() const { return available && bpm > 0.0 ? 60.0 / bpm : 0.0; }
    [[nodiscard]] double beatsPerSecond() const { return available && bpm > 0.0 ? bpm / 60.0 : 0.0; }
};

// A tempo at a point in time. One embedded BPM is one event at 0.0; the type exists so that the
// day a real tempo map arrives (ID3 SYTC, a DAW export, the user drawing one) it is a longer
// vector rather than a rewrite. Nothing today produces more than one event.
struct TempoEvent {
    double timeSeconds = 0.0;
    double bpm = 0.0;
};

struct TempoMap {
    std::vector<TempoEvent> events; // ascending by timeSeconds; empty means "tempo unknown"

    [[nodiscard]] bool empty() const { return events.empty(); }
    // The tempo in force at `seconds`: the last event at or before it. 0 if empty.
    [[nodiscard]] double bpmAt(double seconds) const;
    [[nodiscard]] double secondsPerBeatAt(double seconds) const;
    // True while there is at most one event, i.e. the tempo is constant. Callers that cannot
    // handle a changing tempo should assert this rather than silently reading event 0.
    [[nodiscard]] bool isConstant() const { return events.size() <= 1; }

    static TempoMap constant(double bpm);
    static TempoMap fromAudioTempo(const AudioTempo& tempo); // empty when !available
};

// ---- validation -----------------------------------------------------------------------------

// The window a BPM must fall in to be believed. Outside it the value is ignored with a log line
// and the file is treated as carrying no tempo -- an absurd tag is not an import failure.
inline constexpr double kMinPlausibleBpm = 1.0;
inline constexpr double kMaxPlausibleBpm = 999.0;

// Parses a metadata BPM string. Rejects empty, whitespace-only, non-numeric ("banana"), trailing
// junk ("128bpm"), NaN, infinity, zero, negative and out-of-range values. Accepts fractional BPM
// ("127.5", "128.25") and does not round it. Always parses '.' as the decimal point regardless of
// the process locale. Returns the value, or nullopt if it is not a usable BPM.
[[nodiscard]] std::optional<double> parseBpmString(std::string_view text);

// ---- extraction -----------------------------------------------------------------------------

// Reads embedded tempo from a file on disk. Never decodes audio and never reads the whole file.
//
// Capability-driven: containers this build can actually *open* are the only ones tried, because a
// reader for a container the decoder rejects can never run (ADR-394 records which, and why MP4
// `tmpo` and Ogg/Opus Vorbis comments are deliberately absent).
//   MP3   ID3v2.2/2.3/2.4 TBPM (TBP in v2.2)
//   WAV   RIFF 'id3 '/'ID3 ' chunk -> the same ID3v2 reader
//   AIFF  'ID3 ' chunk -> the same ID3v2 reader
//   FLAC  VORBIS_COMMENT block, BPM or TEMPO (either, case-insensitive)
//
// Missing tempo is NOT an error: a file with no BPM returns an AudioTempo with available=false.
// The Result is reserved for the file being unreadable.
[[nodiscard]] Result<AudioTempo> readEmbeddedTempo(const std::filesystem::path& path);

// Same, over bytes already in memory. The extraction proper; readEmbeddedTempo() is this plus a
// read of the file's head. `name` is used only in log lines.
[[nodiscard]] AudioTempo readEmbeddedTempo(std::span<const std::uint8_t> bytes, std::string_view name);

// How many bytes from the front of a file readEmbeddedTempo() needs. Tags live at the head in
// every format above; nothing here scans a whole track.
inline constexpr std::size_t kTempoMetadataProbeBytes = 1u << 20; // 1 MiB

} // namespace avgen::audio
