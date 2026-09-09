# ADR-015: GPU particle systems

- Status: Accepted (2026-09-08)
- Research: `docs/research/particles.md` (§11 requirements), `docs/research/rendering-techniques.md`

## Problem

Milestone 0.5 needs particle systems that scale to millions of particles, run entirely on the GPU
(no readback in the hot path), react to audio through the parameter system, and integrate with
the HDR scene pass and depth.

## Alternatives considered

1. CPU simulation with instanced rendering.
2. GPU compute with a fixed pool, dead list and per-frame alive list, indirect draw (chosen).
3. GPU compute with stream compaction (prefix sums) for deterministic ordering.
4. Sorted transparency (GPU radix sort) from the start.

## Decision

- `scene::ParticleSystem` is plain data (emitter, forces, appearance, capacity, seed) and every
  modulatable field is registered as `particles/<name>/<field>` by `registerParticleParameters`;
  scene controllers copy finals back each frame (`applyParticleParameters`) with rest-relative
  scaling for lifetime, speed, size and extent.
- `rendering::ParticleRenderer` keeps one pool per system: `array<Particle>` (48 B AoS), a dead
  list stack with an atomic counter, an alive index list, and the indirect draw arguments whose
  `instanceCount` is the alive counter. Per frame in one compute pass: `cs_reset` (indirect args),
  `cs_emit` (pop dead slots for `floor(rate·dt + carry) + burst` particles, clamped to capacity),
  `cs_simulate` (age, kill → dead list; gravity, drag, curl-noise turbulence, attractor with
  orbit; append alive). The scene pass then issues one `DrawIndirect` of camera-facing quads per
  system (additive premultiplied or alpha blend, depth test, no depth write).
- Randomness is `pcg3d(slot, frameIndex + seed·7919, salt)`: identical for a given frame
  sequence, so offline renders reproduce emission exactly. Alive-list order depends on atomic
  scheduling, which only affects float summation order for additive blending.
- Turbulence uses curl of three hash-based value-noise potentials (Bridson 2007), so the field is
  divergence-free and particles do not clump.

## Rationale

- Meets the particle research's renderer requirements 1 to 12 and 14 (compute passes as peers,
  persistent storage buffers, indirect draw from GPU-written args, 32-bit atomics, no readback).
- The dead/alive-list design is the standard production layout (Wicked Engine, Niagara GPU
  emitters) and needs no sorting to look right for additive sparks and dust.
- Measured on the M2 Max: a 131k pool costs under 0.3 ms of the 0.8 ms frame at 2880x1800; a
  one-million pool with ~1M alive runs at a few milliseconds at 1280x720 (see performance.md).

## Consequences

- Alpha-blended (non-additive) systems are unsorted; use additive blending for volumetric looks.
- No collision, trails, or sub-frame emission jitter yet; a deterministic prefix-sum path
  (research §11.13) is not implemented, so alive ordering is not bit-stable across runs.
- Pools are re-created (particles lost) when `capacity` changes; all other settings are live.
- Soft particles (depth fade) are parameterised but not yet sampled from the depth buffer.

## Revision 2026-09-08: deterministic compaction

Milestone 1.0 (offline rendering) needs bit-identical frames from identical runs, and the
atomic dead-list pop / alive-list append above made slot assignment and draw order depend on
GPU scheduling: two headless orb runs diverged after ~100 frames. Alternative 3 (stream
compaction with prefix sums) is now implemented and replaces the atomic path entirely.

- **No atomics on any path.** `Counters` holds plain `deadCount`/`aliveCount`; the indirect
  `instanceCount` is a plain store from one thread of the scan pass.
- **Emission** is `cs_emit`: thread `i < min(emitCount, deadCount)` takes `deadList[i]` (lowest
  free slot first). `deadList` and `deadCount` are the previous frame's compaction output (or the
  CPU reset: `0..capacity-1`, `capacity`). Seeds stay `pcg3d(slot, frame + seed·7919, salt)`,
  and because the slot is now a function of the frame sequence the seeds are too. Lifetime is
  `lifeMin + (lifeMax - lifeMin)·r` (exact when min == max) instead of `mix`.
- **Simulation** writes `flags[slot] = 1/0` (alive after this step) instead of appending.
- **Stable compaction** in three dispatches over blocks of 1024 slots (256 threads x 4
  consecutive slots, Hillis-Steele scan in workgroup memory): `cs_scan_reduce` writes each
  block's alive count to `blockSums`; `cs_scan_top` (one workgroup, looping over chunks of 1024
  blocks, so any pool up to the 4M clamp works) scans `blockSums` in place and writes the counts
  and indirect args; `cs_scan_scatter` recomputes the local scan and writes `aliveList[rank] =
  slot` for alive slots and `deadList[slot - rank] = slot` for dead ones. One scan serves both
  lists because the dead rank of a slot is `slot - aliveRank`. Integer sums are associative, so
  nothing depends on scheduling. Both lists are in slot order, so the draw order is fixed.
- **Pass order** per system per frame, in one compute pass (dispatches are ordered and their
  storage writes visible to later dispatches, so no ping-pong is needed): emit -> simulate ->
  reduce -> top scan -> scatter -> indirect draw. Emit consumes last frame's dead list before
  scatter rewrites it.
- **API:** unchanged except one addition, `ParticleRenderer::readCounts(index)`, a blocking
  readback of a pool's counters for tests and tools. Parameters and the scene format are
  unchanged. `ParticleStats::emittedThisFrame` remains the requested count.
- **Cost:** two extra 4 MB passes over the flag buffer plus one 16 KB scan per 1M pool. The
  `[.perf]` probe (1M pool, ~1M alive, 1280x720, Release, M2 Max) measures 2.29 ms before and
  2.61-2.73 ms after (+15%); the simulate pass (curl noise) dominates.
- **Verification:** `tests/rendering/test_particles_gpu.cpp` runs the same 200-frame sequence on
  fresh renderers (and a fresh context after unrelated GPU work) and requires identical per-frame
  hashes; a second test checks the GPU alive/dead counts against a CPU model every frame
  (64 fps so `dt` and the 1 s life are exact in f32), including the free-slot clamp. The orb
  scene rendered headless twice for 240 frames gives 240 identical per-frame hashes
  (`--log debug` now prints every frame's hash).
- **Remaining limits:** determinism is per GPU family and driver (fast-math and FMA contraction
  are compiler decisions, see `docs/research/offline-rendering.md` §4); the alive order is slot
  order, not age or depth order, so alpha-blended systems are still unsorted.
