# ADR-1147: Near bound flakes resolve as lit shards

**Status:** Accepted (proto/astral-forge, the production path of THE ASTRAL FORGE, iteration 4, step 4)
**Date:** 2026-10-06
**Resolves:** "shards in the particle renderer" (`docs/prototypes/astral-forge/07-iteration-3.md`, retirement item
3; ADR-1153's "what is not ported").
**Implemented by:** `ParticleShards` and `particles/<n>/shards/{fraction, pixels}` in
`src/scene/particles.{hpp,cpp}`; the `shards` block in `src/scene/particle_io.cpp`; `shardWeight`, `shardCorner`,
`vs_shard`, `fs_shard` and the hand-off in `vs_flake` (`shaders/particles.wgsl`); the shard pipeline, its draw,
`ParticleUniforms::shard0/shard1` and the render layout's linear depth in the vertex stage
(`src/rendering/particle_renderer.{hpp,cpp}`); `ParticleFrameContext::pixelAngle` and the depth prepass a shard
system asks for (`src/rendering/scene_renderer.cpp`).
**Tests:** `tests/unit/test_astral_port.cpp` (`[shards]`: round trip, refusals by name, flake systems only, the
parameters); `tests/rendering/test_astral_port_gpu.cpp` ("shards: near bound plates become lit geometry; a zero
fraction draws exactly the flakes").

## Context

In the prototype a flake near enough to resolve is not a dot: `shards.wgsl` draws it as an irregular four-cornered
sliver in the plate's plane, with micro-grooves across it and a bevelled rim that catches the bands, opaque and
depth-tested, resting on the surface. That is the micro scale's proof that the surface is made of matter. The
prototype selects them in its compute splat and draws them with an indirect, compacted list; production particles
are billboards drawn from the alive list, and the compute stage has no storage buffer left for a compacted list
(ADR-1140).

## Decision

**`"shards": {"pixels": 1.6, "fraction": 0.25, "size": 1.0, "grooves": 40, "bevel": 0.15}`** on a flake system
(refused on any other) draws its near bound plates as shards.

**Candidates without compaction.** Every 4th slot is a candidate (the prototype keeps a quarter): the shard draw is
`capacity / 4` instances of 12 vertices, each reading `particles[4 i]` directly, so no list and no buffer is
added. A candidate is a shard when it is alive and bound (`b > 0.3`), its footprint `size / (distance x pixel
angle)` exceeds `pixels`, its own hash is under `fraction x min(1, (2 pixels / footprint)^2)` (fewer as they grow),
and it rests within `0.15 z + 0.1` of the opaque surface's view depth at its pixel (the linear depth, now visible to
the vertex stage; a scene with shards asks for the depth prepass that writes it). Its weight fades in over one pixel
of footprint and out between 60 and 80; the fragment keeps or drops the whole shard by its hash against the weight.

**The plate** is the prototype's: four hashed corners stretched along one axis in the plane of the stored latent
normal (ADR-1153's `home` lane; facing the eye when there is none), `size` particle sizes across. `fs_shard` cuts
`grooves` wavy micro-grooves across it, turns the outer `bevel` of the radius outward, and shades it as a flake is
shaded: the bands' strips (not the soft box) in three taps across the grooves, a Schlick Fresnel on the flake's
metal tinted by its temper film, `0.45 + 0.55 b` bright, plus the heat of ADR-1148. Opaque, depth written, drawn
before the system's flakes. `vs_flake` multiplies the same plate's radiance by `1 - weight` (and drops it at 1), so
a plate is a shard or a flake, never both.

## Consequences

- No other system changes; a flake system without the block draws exactly as before (the shard draw is not
  issued, and `vs_flake`'s hand-off multiplies by 1). With `fraction` 0 the image is byte-identical to no shards
  (the GPU test).
- The vertex cost is `3 x capacity` invocations whether or not any shard is near (a candidate that fails is culled
  in the vertex stage). [TBD measured]
- Shards need the linear depth: a scene with a shard system now encodes the depth prepass.
