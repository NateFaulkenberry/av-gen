# ADR-1781: the live tracker acquires in seconds, holds a lock, and gates silence

- Status: Accepted (eng/beat-clock)
- Code: `src/analysis/beat_tracker.{hpp,cpp}` (`BeatTracker`, `BeatTrackerConfig`, `BeatState::silent`);
  `push(strength, lowStrength, onset, rms)` from `AnalysisRunner`, `ChannelAnalysis` and `avgen_audio_lab_replay`.
- Tests: `tests/unit/test_beat_tracker.cpp` `[adr1781]` and the existing live cases; the lab's live gates (ADR-1785).

## Problem

The lab's causal replay: the live tracker locked 15-25 s after the music started (the dynamic fixture at half time
first); in a 6 s silence its confidence took 4.3 s to fall under 0.1 and its clock pulsed 11 times into the silence;
in sub-3 s silences confidence held 0.6-0.8. The owner asked for a lock within about 4-8 s, a beat that stops and a
confidence that falls within about half a second of silence, and a short break that does not destroy a good lock.

## Decision

- **Acquisition.** A first estimate from 2 s of history (four picked onsets), re-estimated every 0.25 s until locked,
  every 0.5 s after, from all the history up to 8 s. A lock is confirmed only by an estimate over at least 4 s (a shorter
  window cannot see a bar).
- **Hysteresis (the context state).** A locked period is kept while its own peak (within 8%) scores 65% of the best;
  another tempo must win twice running. A tempo change is also read from the newest 5 s alone when they are confident
  (0.6) and the held period scores under half their best, unless the new tempo is a metrical relative of the held one.
- **The silence gate.** Input RMS under -60 dBFS: once it has lasted 0.15 s, the next beat that falls due is held for 8%
  of a period; no sound closes the gate (and the beat is skipped). A silence of a beat and a bit (at least 0.4 s) closes
  it whatever the beat, so a click track's gaps never do. Gated: no beat, `tempoBpm` 0 (the engine's live clock stops),
  confidence falls with a 40 ms time constant, the phase runs on inside. Sound back within 2 s resumes the lock and its
  confidence; after a longer silence the first two onsets are checked against the held phase (a miss restarts
  acquisition); after 8 s the lock is forgotten.

## Consequences

- Causal replay, before and after, in `docs/prototypes/beat-clock/02-report.md`: lock 1.5-2.7 s from the music's start
  (was 12-25 s), 4.4 s after the 120 -> 96 step (was 3.7 s: the hysteresis that keeps a syncopated lock costs a little
  here); confidence under 0.1 0.36-0.74 s into a silence (was 4.3 s or never), no pulse in it.
- `beat.bpm` reads 0 in a gated silence on the live path: a scene sees the clock stop instead of guessing.
