# ADR-1780: the beat trackers read a low-weighted log-band rise, and a tempo is scored on its whole bar

- Status: Accepted (eng/beat-clock; owner-approved scope, 2026-10-26)
- Code: `src/analysis/analyzer.{hpp,cpp}` (`AnalysisFrame::beatStrength`, `beatLowStrength`),
  `src/analysis/beat_tracker.cpp` (`scanTempo`, `estimateTempo`), `src/analysis/stem_analysis.cpp`.
- Tests: `tests/unit/test_beat_tracker.cpp` `[adr1780]` (a son clave reads 110, not 4:3); the lab's beat gates
  (ADR-1785).

## Problem

The Audio Response Lab measured both clocks on known music. The syncopated 110 BPM fixture read 144.6 BPM (4:3: a son
clave's dotted-eighth spacing is the strongest single autocorrelation peak). A four-on-the-floor with a backbeat read at
half time (the snare beats doubled the onset ratio, so the two-beat lag beat the one-beat lag). Both trackers read the
onset detector's ratio over an adaptive threshold, which is not level-free at -30 dB and lets a stacked snare dominate.

## Decision

- **The input.** `beatStrength`: the half-wave-rectified rise of each band's log power (dB / 10, floored at -80 dB),
  weighted bass 1, low-mid 0.5, mid 0.25, high-mid 0.15, treble 0.1. `beatLowStrength`: bass and low-mid only (1, 0.3).
  Both are compressed (square root) and smoothed (one hop) before correlation.
- **The score.** A candidate period L (60-200 BPM, quarter-hop steps) scores the mean of the autocorrelation at L, 2L,
  3L and 4L (Scheirer; Davies and Plumbley, the comb behind BTrack), times Ellis's log-Gaussian prior (120 BPM, one
  octave). The related periods (x2, x1/2, x3/2, x2/3, x4/3, x3/4) are candidates on the same score: a metrical error has to
  win on the whole bar of evidence.
- **The phase.** Where the low register is itself confident of the chosen period, it sets the phase (ADR-1781 live,
  ADR-1782 offline): a kick or a bass note is on the beat more often than a hat.

No dependency was added; nothing was downloaded.

## Consequences

- The syncopated fixture reads 110 BPM on both clocks; the backbeat fixtures read their tempo, not half of it.
- The low bands' rise peaks a few hops after an attack; the file grid snaps each beat to the whole-band peak within an
  eighth of a period (ADR-1782), and Rebuild's bar lines stay within 30 ms (ADR-896's test).
- `stem.<key>` frames carry `beatLowStrength` too, so a beat feed's stem (ADR-1581) tracks the same way.
