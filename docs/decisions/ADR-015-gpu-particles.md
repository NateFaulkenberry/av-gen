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
