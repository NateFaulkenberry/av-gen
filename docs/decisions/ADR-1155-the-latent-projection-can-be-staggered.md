# ADR-1155: The latent projection can be staggered

**Status:** Accepted (proto/astral-forge, the production path of THE ASTRAL FORGE, iteration 4, step 8)
**Date:** 2026-10-06
**Resolves:** ADR-1140's revisit trigger "the simulation dominates a frame with a latent ... port the staggered
projection, which needs a pool record wider than 64 bytes" -- without widening the record.
**Implemented by:** `ParticleLatent::stagger` and its validation in `src/scene/particles.{hpp,cpp}`; the
`latent.stagger` key in `src/scene/particle_io.cpp`; `cs_latent_project`, `cs_latent_spring`, `latentOctEncode`/`latentOctDecode` and
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
two entries instead of `cs_latent`:

- **`cs_latent_project`**, dispatched over only the 64-slot blocks whose turn it is (`(block + frame) % stagger == 0`,
  `ceil(blocks / stagger)` workgroups), does the four SDF taps and stores each bound particle's surface point and
  normal;
- **`cs_latent_spring`**, over every slot, springs each bound particle toward its stored point with `cs_latent`'s
  arithmetic (stiffness, damping, the tangential flow, the release impulse along the stored normal). It contains no
  SDF code. A particle with nothing stored yet waits for its block's turn (at most `stagger - 1` steps).

The split matters more than the stagger. The first version kept one kernel with the taps behind a per-block branch:
at the T01 scene it saved 0.9 ms of 9 (a compiled 90-node tree inlined four times sets the register allocation, and
so the occupancy, of every thread of the kernel, whether or not it takes the branch, and a pass of 2 M particles
then waits on memory). With the taps in their own kernel over a third of the matter, the spring runs at full
occupancy.

**The store is the record's `home` lane**: the surface point (world) in `xyz`, and the latent normal octahedral-
packed into 2 x 11 bits in `w` as `-(1 + bits)`. That keeps `w` below 0.5, which the attractor reads as "not an
anchored home", and `w > -0.5` means "nothing stored yet" (a fresh particle's 0). A flake system reads its plate
normal back through `storedLatentNormal` when the system is staggered (ADR-1153's normal moves from `home.xyz` to
the packed `home.w`). A staggered latent is refused with tendons (which do not project) and with a scatter anchor
(whose crown is the `home` lane). ADR-1145's compiled variant builds `cs_latent_project` from the tree as it builds
`cs_latent`; the spring is the base module's.

`cs_latent` is not edited: an unstaggered latent runs exactly the entry it ran.

## Consequences

- [TBD measured]
- A stored point is up to `stagger - 1` steps old: under a fast-moving latent the matter lags the anatomy by that
  much (the prototype's trade too), and a changed tree is seen by a third of the matter per step.
