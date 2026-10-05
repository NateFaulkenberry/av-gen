# ADR-1119: A simulation step reads its own second, and seeks from GPU checkpoints

- Status: Accepted (gpu/productionization)
- Builds on ADR-032 (simulated grids) and ADR-1114 (a seek runs the whole backlog). It implements that
  investigation's options B (checkpoints) and C (per-step inputs) "the moment a real grid lands in a
  song". ADR-700 (CPU entity checkpoints and their input key) is the model.
- Amends ADR-1116: the furthest a spectrum field hears is 12.0 s (`kAudioMaxDelayRows`), shorter than
  the 16.4 s ring.

## Problem

ADR-1114 made a seek replay a grid's whole backlog. That replay was exact only for **time-invariant**
inputs, because every sub-step of a frame, and every replayed sub-step, sampled the field block
packed at the *frame's* second. So:

- a grid fed by anything that moves (a wave, animated noise, an audio field) drew a different state
  after a seek than it did in play;
- replay is linear in distance: a 60 s seek on a 128³ grid was 9.2 s of GPU work (ADR-1114's
  measurement).

A stateful system that exists to answer the music (ADR-1120) needs both fixed.

## Decision

1. **Per-step inputs.** `Simulation` owns 512 field-block slots (6,400 B each, 3.3 MB), bound at
   `simulate.wgsl` binding 4 with a dynamic offset. Sub-step *k* (1-based) reads a block packed by
   `FieldUniforms::pack` at second *k / simRate*. That block carries field animation (tau, wave
   time), the onset ages, the newest audio row, and the step index (`pad0`). Played and replayed steps
   therefore see identical inputs.
2. **Long replays move the audio ring.** A replay runs in chunks of at most 256 steps and at most 4 s.
   Before each chunk, `FieldUniforms::holdAudioRows` positions the ring at the chunk's last row; after
   the replay it goes back to the frame's row. A spectrum field's audible delay is bounded at 12.0 s,
   so every step of a chunk finds all its rows.
3. **GPU checkpoints.**
   - **Capture.** After any step (played or replayed) that lands on a multiple of
     `GridField::checkpointInterval` (default 5 s), the grid's cells (and agents) are copied GPU to GPU
     into a checkpoint buffer, between compute passes. Nothing is read back.
   - **Restore.** A seek, a backward jump or a first frame restores the newest checkpoint at or before
     the target, when that is better than the current state, and replays at most the spacing. With no
     checkpoint it replays from the initial state: slower, never inexact.
   - **Key.** A checkpoint matches when the grid's settings, the layout and the scene's
     `FieldSet::inputKey` match. The engine computes the input key every frame as the hash of every
     parameter base and the audio revision, which is ADR-700's rule. An edit changes a base and drops
     the checkpoints. Modulation changes finals, not bases, and replays exactly, so it keeps them.
   - **Budget.** `Simulation::setCheckpointBudget` (default 512 MB, CLI `--sim-checkpoint-mb`). When
     a grid would overflow it, that grid's spacing doubles and the checkpoints off the new spacing are
     dropped.
   - **Live input.** A grid fed by live-input audio never restores. That audio has no past to replay.

## Consequences

Measured in `tests/rendering/test_sim_checkpoints_gpu.cpp`, comparing raw GPU buffers byte for byte:

- An agents grid of 20,000 agents, whose deposits follow an onset field and an element-band spectrum
  field and are steered by moving curl noise:
  - played at 30 fps to 7 s;
  - replayed fresh from 0, 420 steps in one seek;
  - restored from the 6 s checkpoint after playing to 11 s, then replaying 60 steps.

  All three give **identical cells and agents**. The controls hold: one frame later differs in more
  than 10% of the cells and more than half the agents.
- A scalar grid injected by a travelling wave, played to 5 s and replayed fresh, gives identical bytes.
  This is the case ADR-1114 §4 said was inexact.
- Two runs of the same agents grid are bit-identical. A budget of three checkpoints holds at or under
  three checkpoints' bytes.
- The existing grid seek test (`[gpu][simulation][seek]`, grid-catchup-lab) still passes.

Still frame-sampled, and documented in `simulation.hpp`:

- a node transform the engine animates (the frame of a field fed to a grid);
- a triggered field's age (ADR-906);
- a Grid field read by another grid's kernels, which sees the previous frame's table.

Cost: packing one field block per sub-step on the CPU (16 fields), plus 6.2 KB of upload a step. A 60 s
seek uploads 22 MB of blocks in 15 bounded submissions.

## Rejected alternatives

- **Checkpoints alone, without per-step inputs.** They make the seek fast but not exact. Any moving
  input would still differ.
- **Checkpoints in CPU memory.** A 46 MB readback costs 5.6 ms; the GPU copy costs 0.32 ms
  (`[.perf][gpu-bridge]`).
- **Keying on the field values themselves.** Modulation moves them every frame, so the checkpoints
  would never be used. The parameter bases are what a person edits.
