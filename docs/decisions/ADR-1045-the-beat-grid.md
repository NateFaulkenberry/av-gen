# ADR-1045: The beat grid: the author's bars and beats, and authored event envelopes

- Status: Accepted (2026-10-01), proto/liminal-space (All You Got art pass 2)
- A source kind (`beatgrid`) in `src/signals/beat_grid.*`, registered in `SourceRack::create`.
  Pure in time (ADR-901), so seek equals play with no new replay code.
- Tests: `tests/unit/test_beat_grid.cpp` (`[beatgrid][adr1045]`).

## Context

Art pass 2 (`docs/prototypes/liminal-space/02-art-pass-2.md`) is written in the owner's numbering: "verse 1,
bar 4, beat 4" is a BIG CLAP, "IS" lands on the and of 4 *before* bridge 2's bar 1, LET IT GO falls on
beats 1, 1.5 and 2. The owner's bar 1 is the analysis's bar 2 (a count-in), and the tempo steps from 109
to 111 BPM at the owner's bar 75. What existed:

- the analysis's beat phase (`SourceContext::beatPhase`, `musicalBeats`, ADR-896): its own numbering,
  not the owner's, and no notion of a section-relative position;
- event-mode timeline sources (ADR-900): one-frame events at seconds, which an envelope source turns
  into a stateful ADSR (not pure in time) and which the author must compute by hand;
- `seq::events` (tier-1 baked events): built for sequences, not reachable from a project's routes.

Every clap had to be hand-converted to seconds, and a different treatment per clap meant a different
hand-built timeline per clap.

## Decision

A `beatgrid` source carries an explicit tempo map (`tempo`: steps at bar downbeats), an origin (the
seconds of bar 1 beat 1) and a section table (name -> global bar). Positions are written
`section:bar:beat` or `bar:beat`, beats 1-based and fractional, bars may be 0 or negative.

It publishes, as pure functions of the clock:

- per division (bar, half, quarter, eighth, sixteenth): a decaying pulse (`grid.<n>.<div>`, 1 on the
  division and exactly 0 before the next; shape `sources/<n>/pulseDecay`), a phase saw (`.phase`) and a
  raised cosine (`.wave`, smooth, for breathing);
- per event channel: `grid.<n>.<channel>`, the strongest of that channel's envelopes. An envelope is an
  attack that *anticipates* the event (a smoothstep rise ending on the instant), a hold (or `until` a
  position), and a release (exp, linear or smooth, ending exactly at zero). Durations are in beats on the
  grid (so they stretch correctly across the tempo step) or in seconds. `repeat` expands one event.

A treatment per clap is a channel per clap: routes from `grid.song.clap1` drive a colour explosion, from
`clap2` a cut to black, and so on. A span (`until`) is a gate, usable as a route's `depthSource`.

## Consequences

- Seek equals play by construction: nothing is integrated, and a smoothing route after it is replayed by
  ADR-901 (tested: a 300 ms decay route lands bit-exact).
- An instant computed to land on a beat (k / fps) is read as on it: positions within 1e-7 s of a peak
  count as the peak, and the division phase is nudged by 1e-7 of a division, so frame round-off never
  drops a pulse to its tail.
- Envelopes are evaluated by scanning a channel's events each frame: a few hundred events cost
  microseconds. If a film ever carries tens of thousands, sort and bisect.
- Changing the settings after attach needs a re-attach for new channels (as for the control source).
