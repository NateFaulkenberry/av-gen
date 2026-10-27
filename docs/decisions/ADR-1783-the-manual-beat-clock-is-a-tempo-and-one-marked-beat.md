# ADR-1783: the manual beat clock is the tempo override and one marked beat

- Status: Accepted (eng/beat-clock). The transport bar control is not verified by eye.
- Code: `src/app/engine.{hpp,cpp}` (`setBeatAnchor`, `clearBeatAnchor`, `manualBeatClock`, the manual grid in
  `rebuildFeeds`, `beat.source` 4 in `advanceClock`, `transport.tempo.anchor`), `src/analysis/analysis_track.hpp`
  (`TrackedGrid::manual`), `src/ui/transport_bar.cpp` (Mark beat).
- Tests: `tests/integration/test_manual_beat_clock.cpp` `[adr1783]`.

## Problem

When the analysis gets a file wrong there was no way to set the beat clock by hand. The transport's tempo override
(ADR-394) set only the tempo number; the grid stayed the analysed one.

## Decision

- **The clock.** The tempo override plus one hand-marked beat (the anchor, seconds on the project clock) is the file's
  beat clock: beats at anchor + k x 60 / bpm, the anchor beat 1 of a bar (the meter's downbeat). It replaces the
  analysed grid in `rebuildFeeds`, so every consumer of the grid (`beat.*`, the meter, beat stepping, the timeline's beat
  markers, the music classifier's beats) follows it.
- **The format.** `transport.tempo` = `{"bpm": 112, "source": "user", "anchor": 3.42}`. No anchor: the override is the
  tempo number only, as before. Clearing the override clears the anchor.
- **The UI.** The transport bar, beside the tempo field: **Mark beat** marks the playhead (with no override, it first
  takes the tempo in force); marked, it shows `beat 1 @ 3.42 s`, a click re-marks, a right-click clears.
- **`beat.source` 4: manual.** Precedence for `beat.*`, highest first: MIDI clock (1, while selected and running),
  manual (4), the analysed file grid (2: the beat feed's stem, else the mix), the live tracker (3, live input; the manual
  clock is a file clock and does not apply), none (0). The tempo readout keeps ADR-394's order (the override outranks
  the MIDI clock for the displayed number); `beat.bpm` is the MIDI clock's while it runs.

## Consequences

- The Audio Response Lab's dial has no lamp for 4; its runs never set an override.
