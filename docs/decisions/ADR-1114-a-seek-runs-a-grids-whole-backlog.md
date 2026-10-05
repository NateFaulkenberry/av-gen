# ADR-1114: A seek runs a simulated grid's whole backlog

- Status: Proposed (implemented on `fix/gpu-sim-seek`, not merged; the owner decides)
- Builds on ADR-032 (simulated grids), ADR-581 (the 240-step catch-up, recorded and not fixed),
  ADR-360 (scrub may differ from play for particles only), ADR-397 (pre-roll).
- Investigation: `docs/development/gpu-sim-seek-investigation.md`.
- Follow-up: the particle warm-up's missing clamp at t = 0 that this investigation found
  (fix option P0) is fixed in ADR-1115.

## Problem

`rendering::Simulation` capped a grid's catch-up at `kCatchUpSteps = 240` sub-steps (4 s at 60 Hz)
on the first frame after a reset, and skipped the rest. ADR-581 measured what that does to a
`--range T:T` render. There was a second, unrecorded half: **`SceneRenderer::resetTemporalHistory()`
never reached the grids.** `Simulation::reset()` had no callers. So in the editor:

- a **forward** scrub kept the grid at the old position and repaid the gap at `maxSubSteps` a frame,
  so the landing frame showed the grid as it was before the scrub;
- a **backward** scrub reset the grid (time went backwards) and granted 240 steps.

Measured through `app::Engine` + `SceneRenderer` at the Offline tier on
`examples/labs/grid-catchup-lab.scene.json`, 320x180. The numbers are the mean absolute RGB difference
against the frame played from 0, in 8-bit levels, and the share of pixels more than 2 levels off:

| arm | 2 s | 10 s | 30 s | 60 s |
|---|---|---|---|---|
| fresh seek (`--range T:T`), before | 0.000 / 0% | 95.96 / 93.8% | 104.72 / 100% | 105.45 / 100% |
| forward scrub, before | 8.38 / 21.1% | 148.27 / 100% | 156.49 / 100% | 156.67 / 100% |
| backward scrub 60 → 10 s, before | | 95.96 / 93.8% | | |
| **every arm, after** | **0 / 0%** | **0 / 0%** | **0 / 0%** | **0 / 0%** |

"After" means byte-identical frames in every arm.

## Decision

1. On the first frame after a reset, and on the frame after `markDiscontinuity()`, a grid runs its
   **whole** backlog, `floor(T * simRate) - stepsTaken`. Continuous playback keeps `maxSubSteps` as
   its stall guard.
2. `SceneRenderer::resetTemporalHistory()`, which is the transport's seek hook, calls
   `Simulation::markDiscontinuity()`. A forward seek keeps the state and runs the gap. A backward
   seek resets, as before, and replays from 0.
3. The backlog runs in command buffers of `kStepsPerSubmit = 256` sub-steps, submitted ahead of
   the frame, so a long replay is many bounded submissions rather than one huge one.
4. `kMaxCatchUpSteps` (30 minutes at 60 Hz) is a ceiling against a runaway, not a budget. Beyond
   it the grid skips ahead with a warning, as it used to at 240.
5. `SimulationStats::catchUpSteps` reports how many steps a frame owed to a seek.

## Costs

This is a hidden probe, `avgen_render_tests "What a seek costs a simulated grid"`, one run on an M2
Max under the lock. It measures the wall time of the one frame that lands on T, for a fresh grid:

| grid | ms per step | seek to 10 s | 60 s | 300 s |
|---|---|---|---|---|
| 48³ scalar (the lab) | 0.057 | 36 ms | 210 ms | 1.03 s |
| 128³ scalar + 4 Jacobi sweeps | ~2.5 | 1.5 s | 9.2 s | 15.2 s (sic; one sample, see below) |

It needs no memory. The cost is **latency on a scrub** that is linear in song position (backward)
or in the gap (forward). That is acceptable for the lab's size and for an offline render's one-time
start. It is not acceptable for a large grid in the editor. That is GPU checkpoints' job, the
follow-up named in the investigation (§5). The 300 s figure at 128³ is lower per step than the 60 s
figure. GPU clock ramping is the likely cause, but it was not verified. Treat the large-grid
column as an order of magnitude.

## What this does not make exact

- **Fields that vary in time.** Every sub-step of one frame samples the field block evaluated at
  that frame's `renderTime`, in play and in replay alike. A replay therefore samples T for all of
  its steps, while a play sampled each frame's own second. The lab's fields are static, so it is
  exact there. A grid fed by an animated, routed or audio-driven field is not exact, and the same
  fact makes play itself frame-rate dependent. Fixing it means per-step field evaluation (the
  spike's per-step uniform block), and that is a broader change.
- **Particles.** ADR-360 relaxed scrub == play for particles on the owner's decision. Nothing here
  changes that.

## Revisit when

- A scene authors a grid larger than about 64³, or a song longer than a few minutes uses one. Then
  the editor's scrub stall wants checkpoints.
- A grid is fed by a field that moves in time. Then per-step inputs are what keep it exact.
