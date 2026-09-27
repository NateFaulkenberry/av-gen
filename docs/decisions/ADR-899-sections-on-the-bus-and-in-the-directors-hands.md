# ADR-899: Sections on the bus, and in the director's hands

**Status:** Accepted
**Date:** 2026-09-26
**Found by:** the GV3 revision audit (reports/modulation.md §1 and §7C, director.md §3-§4): neither
the detected structure nor the authored section timeline reached the signal bus, and the Director
Agent saw each section as `{type, occurrence, start, end}`.
**Follows:** ADR-247 (the authored section timeline and `song::SectionCue`), ADR-249 (Song Mode's
`SongPlan`), ADR-755 (the Director's `MusicalContext`), ADR-870 (the replayed bus), ADR-897
(the level-free measures)
**Implemented by:** `section.*` in `declareFrameSignals` and `Engine::advanceClock`
(`src/app/engine.cpp`); `SectionCue::audio` and `cueSheet(..., track)` (`src/song/section_cue.*`);
`SongPlanSection::audio` (`src/app/song_plan.*`); `SectionRun::energy/density/audio` and
`MusicalContext::downbeat/phraseBars` (`src/directing/time_ref.*`); `director.inspect_scene`
(`src/ai/director_tools.cpp`)
**Tests:** `tests/integration/test_section_signals.cpp`

## Context

`beat.section` and `music.section` count 16-bar blocks of the beat clock. What a film is cut to --
the timeline in the Sequence panel, which GV3 authored as 13 sections with an energy each -- never
reached a route: a world could not brighten in the drop because it was the drop. And a director,
human or LLM, was handed a section's name and span and nothing about what it sounds like, so "the
break is darker and emptier than the plateau" was not a fact it could read.

## Decision

**On the bus**, from `sequence().sectionTimeline` at the transport position:

| signal | value |
|---|---|
| `section.index` | the section the playhead is in, from 0; -1 when none |
| `section.progress` | how far through it, 0..1 |
| `section.energy` | the section's own `energy` -- as authored, or as detected |
| `section.change` | an event, strength 1, on the frame the index changes |

Declared in `declareFrameSignals` and published in `advanceClock`, so the seek replay rebuilds them
at every step, and the replay key includes the timeline's boundaries and energies. A jump is not a
change: the first frame after a seek has no previous frame to have crossed from.

**In every director's hands**, the measured, level-free profile of the audio under each section
(`analysis::SpanProfile`, ADR-897): energy composite, onsets per second (all percussion, and kick /
snare / hat apart), brightness in Hz, each band's mean level in dB and on the 0..1 level scale, and
stereo width:

- `song::SectionCue::audio` -- `cueSheet(timeline, language, track)` measures it; without a track it
  is `measured() == false`.
- `app::SongPlanSection::audio` -- carried from the cue, or measured for a detected structure;
  written to and read from a song plan's JSON as `"audio"`.
- `directing::SectionRun::audio`, plus the run's own `energy` and `density` (duration-weighted over
  merged entries), and `MusicalContext::downbeat` / `phraseBars` (ADR-896) --
  `musicalContextFrom(..., track, meter)`.
- `director.inspect_scene` returns each section's `energy`, `density` and `audio`, and a `meter`
  block (`beatsPerBar`, `phraseBars`, `downbeatBeat`, `firstDownbeatSeconds`).

The section's own `energy` and the measured `audio.energy` are kept apart on purpose: the first may be
a person's word (GV3's timeline carries the owner's composite), the second is what the track says.

### Where an artist finds them

- In the route-source picker: `section.index - section number (Sequence section timeline, -1 =
  none)`, `section.progress - progress through the current section`, `section.energy - energy of
  the current section`, `section.change - section change (a new section begins)`.
- In the Sequence panel's section inspector (click a section): the section's `section.energy` value
  and the measured profile of the audio under it ("measured: energy 0.56, 5.5 hits/s (kick 2.2),
  brightness 2673 Hz"). Read-only: the song model holds a section's energy as the timeline's
  (detected, or typed into the project); making it a person-settable field is a model change left
  to whoever needs authored per-section depth.
- The Director panel's section tooltip already showed the section's energy and density.

### The fields, for the Director stream

| field | unit | meaning |
|---|---|---|
| `energy` | 0..1 | the level-free composite over the span (not rescaled to the track) |
| `onsetRate` | onsets/s | all percussive band onsets, one per 30 ms |
| `kickRate`, `snareRate`, `hatRate` | onsets/s | the low, mid, high band onsets |
| `brightnessHz` | Hz | mean spectral centroid over audible frames |
| `bandsDb.<band>` | dB | mean band power, sine-amplitude units (0 dB = full-scale sine) |
| `bandLevels.<band>` | 0..1 | the same on the long-term level scale (0 = -60 dB) |
| `width` | ratio | mean side/mid RMS; absent for mono |
| `frames` | count | analysis frames measured; 0 = nothing measured |

Bands are `bass` (20-150 Hz), `lowMid` (150-400), `mid` (400-2000), `highMid` (2000-6000),
`treble` (6000-16000).

## Consequences

- A route can key a section: `section.change` into an envelope for a boundary flare,
  `section.energy` as a Multiply depth (once the route chain can take depth from a signal), and
  `section.progress` for an arc across a section.
- **Measured on "Rebuild"** (01-music.md's 13 segments, re-measured 2026-09-26): the break measures
  901 Hz and a treble level of -46 dB against the plateau's 2591 Hz and -27 dB; the pull-back 0.54
  onsets/s against the arrival's 5.48; kicks 2.1-2.3/s in every groove and 0 in the pull-back.
- **Changed behaviour:** none in existing scenes -- `section.*` are new signals, and the profile is
  new data. `director.inspect_scene`'s output gains fields; `SongPlan` JSON gains an optional
  `"audio"` per section.
- **Not done here:** Song Mode does not yet *use* the profile to choose shot durations; that is the
  Director stream's (musical shot durations and a reactivity planner). The route chain's depth-by-signal
  (reports/modulation.md §7C, second half) is the route stream's.
