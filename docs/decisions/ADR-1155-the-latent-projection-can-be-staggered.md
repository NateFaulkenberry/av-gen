# ADR-1155: The latent projection can be staggered

**Status:** Accepted (proto/astral-forge, the production path of THE ASTRAL FORGE, iteration 4, step 8)
**Date:** 2026-10-06
**Resolves:** ADR-1140's revisit trigger "the simulation dominates a frame with a latent ... port the staggered
projection, which needs a pool record wider than 64 bytes" -- without widening the record.
**Implemented by:** `ParticleLatent::stagger` and its validation in `src/scene/particles.{hpp,cpp}`; the
`latent.stagger` key in `src/scene/particle_io.cpp`; `cs_latent_staggered`, `latentOctEncode`/`latentOctDecode` and
`storedLatentNormal` in `shaders/particles.wgsl` (and their use in `vs_flake` and `vs_shard`); the staggered
pipelines, interpreted and compiled (ADR-1145 builds both entries of a variant), and `latentInfo.z` in
`src/rendering/particle_renderer.{hpp,cpp}`.
**Tests:** `tests/unit/test_astral_port.cpp` ("stagger round-trips and is refused by name out of range or with
tendons"); `tests/rendering/test_astral_port_gpu.cpp` ("stagger: a projection refreshed every third step binds the
matter as every step does, and stores it", `[gpu][particles][latent][stagger]`); the cost in `[.perf][astral4]`.

## Context

The latent force costs four SDF evaluations per bound particle per step. The prototype refreshes the projection
on one step in three per particle (staggered by block) and springs toward a stored target in between, paying
about a third. ADR-1140 did not port it because the target needs storage and the 64-byte pool record is full.

## Decision

**`"latent": {..., "stagger": 3}`** (1..4; 1, the default, is the force as it always was). A staggered system runs
`cs_latent_staggered`: a particle projects on the steps where `(slot / 64 + frame) % stagger == 0` (by 64-slot block,
so a SIMD group takes one branch), on any step it is released, and while it has nothing stored; otherwise it
springs toward the point it stored. Everything after the projection is `cs_latent`'s arithmetic.

**The store is the record's `home` lane**: the surface point (world) in `xyz`, and the latent normal octahedral-
packed into 2 x 11 bits in `w` as `-(1 + bits)`. That keeps `w` below 0.5, which the attractor reads as "not an
anchored home", and `w > -0.5` means "nothing stored yet" (a fresh particle's 0). A flake system reads its plate
normal back through `storedLatentNormal` when the system is staggered (ADR-1153's normal moves from `home.xyz` to
the packed `home.w`). A staggered latent is refused with tendons (which do not project) and with a scatter anchor
(whose crown is the `home` lane).

`cs_latent` is not edited: an unstaggered latent runs exactly the entry it ran.

## Consequences

- [TBD measured]
- A stored point is up to `stagger - 1` steps old: under a fast-moving latent the matter lags the anatomy by that
  much (the prototype's trade too), and a changed tree is seen by a third of the matter per step.
