# ADR-1801: A seek replays the control layer for an integrating route or a trigger-counting source

- Status: Accepted (eng/qa-pass-2, 2026-10-27)
- Code: `src/app/engine.cpp` (`Engine::controlReplayNeeded`).
- Tests: `tests/integration/test_integrate_seek.cpp` `[adr1801]`; `tests/integration/test_control_seek.cpp` (the cost
  case, updated).

## Problem

A seek with an analysed track (an offline render that starts at `--start`, every scrub in the editor) rebuilds the
route chains in two ways:

- **The signal replay** (ADR-870/901) replays a route whose signal it can rebuild, on its own fixed **60 Hz** grid.
- **The control replay** (ADR-1168) replays the sources, the state machine and every route on **the play's own grid**
  (the render's fps). It ran only for scene states, conditioned signals (ADR-1602) and routes into a source
  (ADR-1573).

An integrating route (ADR-1041, `chain.integrate`) keeps a running total for the whole film. Summed over 60 Hz steps it
is not the total a 30 fps play sums, and a route on a signal the signal replay does not rebuild (a stateful source)
restarted at 0. A sample-and-hold (`random`) or envelope source has the same shape: its value is the k-th of its
triggers since a reset.

Measured on the shipped projects with an integrating route and no scene states (Feline Footwear, 30 fps, a seek to
60 s against a play to 60 s, every parameter's final compared):

| project | parameters that differed, before | after |
|---|---:|---:|
| Neon Vector World | 60 (the noise fields' drift: 978 m played, 964 m seeked) | 0 |
| Glitch Signal | 24 (rain spawn rate off by 317) | 0 |
| Chromatic Topography | 32 | 0 |
| Cymatic World | 24 | 0 |
| Waveform Dimension | 18 | 0 |
| Resonant Silk | 9 | 0 |
| Datascape | 7 | 0 |
| Infinite Color Geometry | 6 | 0 |
| Phase Hall | 5 | 0 |
| Stellar Nursery | 2 | 0 |
| Sacred Geometry Flight | 69 | 1 |
| 8-Bit Ocean | 38 | 1 |
| Chorus Field - Trench God | 12 | 12 |

The one left in Sacred Geometry Flight and 8-Bit Ocean is a particle `burst` event on the landing frame, and Trench
God's twelve are its camera and face fields; neither comes from a route chain, and both are the same with and without
this change (reported, not fixed here).

## Decision

`controlReplayNeeded` is also true when an enabled route integrates, or a `random` or `envelope` source exists. The
control replay then runs, on the play's grid, and the seek lands where the play does.

## Consequences

- **Seek output changes** for the 13 shipped projects in the table (the 12 live projects only when they are rendered
  or scrubbed offline with a file; live input never seeks): Sacred Geometry Flight, Neon Vector World, Infinite Color
  Geometry, Chromatic Topography, Glitch Signal, 8-Bit Ocean, Datascape, Stellar Nursery, Cymatic World, Resonant
  Silk, Waveform Dimension, Phase Hall, and Chorus Field - Trench God (no change measured). A render that starts at 0
  is unchanged; one that starts mid-track now matches the play.
- **Cost:** a seek to 3:00 in the same harness went from 127 to 164 ms (Sacred Geometry Flight), 104 to 132 ms (Neon
  Vector World) and 257 to 292 ms (8-Bit Ocean), measured while another suite ran. Projects without an integrating
  route or such a source pay nothing new.
- A follower (attack/decay) on a signal the signal replay rebuilds is still replayed on the 60 Hz grid; it forgets in
  its decay time, so a seek's landing differs by a fraction of one release. Moving the signal replay onto the play's
  grid would change entity seeks (ADR-700's 60 Hz grid), Glowmere's among them, so it is not done here.
