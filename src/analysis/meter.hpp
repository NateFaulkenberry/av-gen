#pragma once

// Musical time, defined once (ADR-896).
//
// A beat clock says "this is beat 212 and we are 0.3 of the way through it". Everything that turns
// that into a bar, a downbeat, a phrase or a section needs one more fact -- which of those beats is
// beat 1 of a bar -- and until this file every consumer supplied its own answer:
//
//   * the bus (`beat.bar`, `music.downbeat`) counted the first tracked beat as beat 1 of the count
//     and took `count % 4 == 0` as the downbeat, so on a track that starts on its downbeat every bar
//     began on beat 4;
//   * the effect trigger's Beat source counted from 0, so it and the bus disagreed by one beat;
//   * the sequencer's bar ruler, the Director's "bar 64 beat 3" and the bar snap took every fourth
//     tracked beat from the first, which is right for that track and wrong for any other phase;
//   * the transport's bars readout started counting at 0 s, a beat before "Rebuild"'s first bar.
//
// `Meter` is the one answer. It is four integers and a handful of pure functions, and every place a
// beat count becomes a bar, phrase or downbeat reads it: the engine resolves one per frame (the
// project's pinned values over the analysis's estimate) and hands the same value to the bus, the
// music runtime, the LFOs, the timeline, the shaders, the triggers, the sequencer and the Director.
//
// **Musical beats.** A beat clock counts `clockBeats` from 0 at its first beat (for an analysed track,
// the first tracked beat). `Meter::beats(clockBeats)` is the musical position: 0.0 exactly on beat 1
// of bar 1, negative in a pickup before it. Bars, phrases and sections are floor-divisions of that
// number, so they are well defined on both sides of zero.

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace avgen::analysis {

struct AnalysisFrame;

struct Meter {
    int beatsPerBar = 4;
    int phraseBars = 4;
    int sectionPhrases = 4;
    // The beat of the beat clock on which bar 1 begins -- and phrase 1 and section 1 with it --
    // counted from 0 at the clock's first beat. For an analysed track: the index into
    // `OfflineBeats::beatTimes` of the first downbeat.
    int downbeat = 0;

    [[nodiscard]] int beatsPerPhrase() const { return beatsPerBar * phraseBars; }
    [[nodiscard]] int beatsPerSection() const { return beatsPerPhrase() * sectionPhrases; }

    // A clock position as musical beats: 0 at beat 1 of bar 1.
    [[nodiscard]] double beats(double clockBeats) const { return clockBeats - static_cast<double>(downbeat); }
    [[nodiscard]] std::int64_t beat(std::int64_t clockBeat) const { return clockBeat - downbeat; }

    // Of a musical beat index (floor-divisions, so a pickup is bar -1 and its last beat is beat 3).
    [[nodiscard]] int beatInBar(std::int64_t musicalBeat) const;      // 0 = the downbeat
    [[nodiscard]] std::int64_t bar(std::int64_t musicalBeat) const;   // 0 = bar 1
    [[nodiscard]] std::int64_t phrase(std::int64_t musicalBeat) const;
    [[nodiscard]] std::int64_t section(std::int64_t musicalBeat) const;
    [[nodiscard]] bool isDownbeat(std::int64_t musicalBeat) const { return beatInBar(musicalBeat) == 0; }

    // Of a continuous musical position: how far through the current bar, phrase, section (0..1).
    [[nodiscard]] double barPhase(double musicalBeats) const;
    [[nodiscard]] double phrasePhase(double musicalBeats) const;
    [[nodiscard]] double sectionPhase(double musicalBeats) const;

    // The tracked beats that are downbeats (bar lines) from bar 1 on, in order: `beatTimes[downbeat]`,
    // then every `beatsPerBar`-th after it. What a bar ruler, a bar snap and a sequence's Bar trigger
    // draw from.
    [[nodiscard]] std::vector<double> barTimes(std::span<const double> beatTimes) const;

    // Clamps every field into its legal range (at least 1 of everything, downbeat unchanged).
    [[nodiscard]] Meter sanitized() const;

    friend bool operator==(const Meter&, const Meter&) = default;
};

// ---- the offline clock: musical time as a pure function of seconds ----------------------------

// The beat clock of an analysed grid at `seconds`: 0 at `beatTimes[0]`, `i + f` a fraction `f` of the
// way from beat i to beat i + 1, and extrapolated with `periodSeconds` before the first beat
// (negative) and after the last. Pure, so a seek lands where a play does by construction. Returns 0
// for an empty grid or a non-positive period with fewer than two beats.
[[nodiscard]] double clockBeatsAt(std::span<const double> beatTimes, double periodSeconds, double seconds);
// The inverse: the second at which the clock reads `clockBeats`.
[[nodiscard]] double secondsAtClockBeats(std::span<const double> beatTimes, double periodSeconds, double clockBeats);

// ---- the estimate --------------------------------------------------------------------------------

// What the analysis concluded about the meter of a track (ADR-896), and how sure it is. Beats per bar
// is not estimated: every consumer in the engine is written for 4/4 and a detected 3/4 that nothing
// could honour would be a setting that does nothing.
struct MeterEstimate {
    // The tracked beat (0 .. beatsPerBar - 1) that is beat 1 of bar 1. 0 when there is no grid.
    int downbeat = 0;
    // 0..1: how far the winning phase's score stands above the runner-up's (a margin of 0.3 in score
    // units reads 1). Low on material with no backbeat and no structural changes.
    float downbeatConfidence = 0.0f;
    // Each phase's score, for a panel that wants to show why. Index = candidate `downbeat`.
    std::array<float, 4> downbeatScores{};
    // Bars per phrase when the evidence is clear (4, 8 or 16), else 0 = "not estimated".
    int phraseBars = 0;
    // The ratio the phrase length was decided on: mean change at phrase starts over mean change
    // half a phrase later. 0 when not estimated.
    float phraseEvidence = 0.0f;
    bool valid = false; // a grid long enough to estimate from existed
};

// Estimates the downbeat phase and the phrase length from the analysis frames and the tracked beats.
//
// Two kinds of evidence, combined:
//   * **the backbeat** -- a snare or clap lands on beats 2 and 4, so the beats whose high band
//     (2-16 kHz) jumps hardest are beats 2 and 4. Decides the phase to within two beats.
//   * **where things change** -- a layer entering, a drop-out, a crash: arrangement changes land on
//     bar lines, so the strongest beat-to-beat spectral changes pick one of the two.
// Plus a small prior for the first tracked beat, which only decides a track with no evidence at all.
//
// The phrase length is the longest of 4, 8, 16 bars whose phrase starts carry at least twice the
// change the half-phrase points do. Pure; the same frames and beats give the same estimate.
// `binHz` is the analyzer's bin spacing (sample rate / window size).
[[nodiscard]] MeterEstimate estimateMeter(std::span<const AnalysisFrame> frames, std::span<const double> beatTimes,
                                          float binHz, int beatsPerBar = 4);

} // namespace avgen::analysis
