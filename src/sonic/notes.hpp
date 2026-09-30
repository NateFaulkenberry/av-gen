#pragma once

// Note events and the Musical Context read from them (ADR-1020, Sonic Garden POC).
//
// A `NoteTrack` is a sorted list of notes with absolute times. It is filled from a Standard MIDI File here (the
// offline path: the note events a DAW exports beside the audio bounce); live MIDI (Phase 7) would append to one
// incrementally. The event model borrows CLAP's addressing (channel, key, note id) and keeps velocity and pitch
// as floats, so MIDI 2.0's 16-bit velocity and per-note pitch fit without a change of shape.
//
// The Musical Context is a PURE FUNCTION OF TIME over the track: every quantity is an exponential-kernel sum over
// the notes that began before `t`, or a statistic of the notes sounding at `t`. There is no state, so a seek lands
// exactly where a play does and an offline render is deterministic by construction. Only the one-frame events
// (note on, note off, phrase start) need the previous frame's time, and they take it as an argument.
//
// It is independent of timbre on purpose (brief §7C): the same notes give the same context whatever they sound
// like. Durations are counted as held so far, never as the file's full length, so the numbers are the ones a
// live performance would give at the same instant.

#include "core/error.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace avgen::sonic {

struct NoteEvent {
    double start = 0.0;       // seconds
    double duration = 0.0;    // seconds (note-on to note-off)
    float velocity = 0.0f;    // 0..1 (MIDI 1.0: v / 127)
    float pitch = 0.0f;       // in semitones, MIDI numbering; fractional for per-note pitch (MIDI 2.0)
    std::uint8_t key = 0;     // 0..127
    std::uint8_t channel = 0; // 0..15
    std::uint32_t id = 0;     // unique per track, in start order

    [[nodiscard]] double end() const { return start + duration; }
};

struct NoteTrack {
    std::vector<NoteEvent> notes; // sorted by start, then key
    double longest = 0.0;         // the longest duration: how far back a note sounding now can have begun

    [[nodiscard]] bool empty() const { return notes.empty(); }
    [[nodiscard]] double endSeconds() const;
    // Sorts, numbers and measures `notes` after they have been filled.
    void finish();

    // A type 0 or type 1 Standard MIDI File with a PPQ division; tempo changes on any track apply to all.
    // SMPTE divisions are refused. A note-on with no note-off ends at the end of its track.
    static Result<NoteTrack> fromMidiBytes(std::span<const std::uint8_t> bytes);
    static Result<NoteTrack> fromMidiFile(const std::filesystem::path& path);
};

// What the notes are doing at an instant, in musical units (normalisation to 0..1 is the runtime's, with the
// divisors in `ContextSettings`).
struct MusicalContext {
    int active = 0;          // notes sounding
    float density = 0.0f;    // notes begun per second (kernel-weighted)
    float rhythm = 0.0f;     // distinct onsets per second: a chord counts once
    float velocity = 0.0f;   // 0..1, of the recent onsets (or the sounding notes)
    float pitch = 0.0f;      // pitch centre, MIDI note number, 0 when there has never been a note
    float lowest = 0.0f;     // the range's bounds (sounding notes plus the last `rangeSeconds` of onsets)
    float highest = 0.0f;
    float range = 0.0f;      // highest - lowest, semitones
    float motion = 0.0f;     // mean |interval| between successive onsets, semitones
    float direction = 0.0f;  // -1 falling .. +1 rising (signed motion over unsigned)
    float duration = 0.0f;   // mean held duration of recent notes, seconds
    float legato = 0.0f;     // share of successive onsets that overlap the one before, 0..1
    float regularity = 0.0f; // 1 - coefficient of variation of inter-onset intervals, 0..1 (0 with < 3 onsets)
    float chord = 0.0f;      // share of recent notes that began together with another, 0..1
    float tension = 0.0f;    // interval-class dissonance of the sounding notes, 0..1
    float repetition = 0.0f; // share of recent onsets that repeat a pitch from the few before, 0..1
    float phrase = 0.0f;     // share of the last `phraseSeconds` during which anything sounded, 0..1
};

struct ContextSettings {
    double tau = 1.0;              // the recency kernel for rates and means, seconds
    double window = 4.0;           // onsets older than this are ignored
    double chordSeconds = 0.03;    // onsets this close are one onset
    double rangeSeconds = 2.0;
    double phraseSeconds = 4.0;
    double phraseGapSeconds = 0.6; // an onset after this much silence starts a phrase
    int repeatLookback = 4;        // onsets compared for repetition
};

[[nodiscard]] MusicalContext contextAt(const NoteTrack& track, double seconds, const ContextSettings& settings = {});

struct NoteEvents {
    bool noteOn = false;
    float onVelocity = 0.0f; // the loudest note begun in the interval
    int onCount = 0;
    bool noteOff = false;
    bool phraseStart = false;
};
// The notes begun and ended in (from, to]. `from` >= `to` is an empty interval.
[[nodiscard]] NoteEvents eventsBetween(const NoteTrack& track, double from, double to,
                                       const ContextSettings& settings = {});

// "C3", "F#4" (MIDI 60 = C4). For the diagnostic view.
[[nodiscard]] std::string pitchName(float midi);

} // namespace avgen::sonic
