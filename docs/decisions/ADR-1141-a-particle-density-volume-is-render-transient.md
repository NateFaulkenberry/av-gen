# ADR-1141: A particle density volume is render-transient, and it is the engine's first 3-D texture

**Status:** Accepted (proto/astral-forge, the production path of THE ASTRAL FORGE, step 2)
**Date:** 2026-10-05
**Resolves:** `docs/prototypes/astral-forge/04-architecture.md` "Production path" item 2 (particle → density
splat as a render-only transient).
**Implemented by:** `shaders/particle_density.wgsl` (`cs_density_splat`, `cs_density_resolve`),
`shaders/particle_record.wgsl` (the pool record, now shared), `src/rendering/particle_renderer.{hpp,cpp}`
(`ParticleDensityVolume`, `densityVolume()`, `readDensity()`), `src/scene/particles.{hpp,cpp}`
(`ParticleDensity`, `densityMemoryBytes`, `density/weight`), `src/scene/particle_io.cpp`,
`src/scene/particle_latent.cpp` (`splatDensity`, `resolveDensity`: the CPU reference)
**Tests:** `tests/unit/test_particle_latent.cpp` ("the density splat conserves mass and the blur is a
normalised binomial", round trip and refusals); `tests/rendering/test_particle_latent_gpu.cpp` ("the GPU
volume of a known particle placement matches the CPU splat and blur", "a volume with no consumer changes no
pixel of the frame"); `[gpu][particles][density]`.

## Context

In the prototype, the visible surface is the iso-surface of the particles' own density. The latent only
moves matter and sharpens what matter has already made (approach E, `04-architecture.md`). Production had
no way to turn particles into a field.

ADR-1120 considered the obvious route, particles depositing into a `Simulation` grid, and rejected it.
Particles carry ADR-360's relaxed seek, and their dt follows the frame. A grid fed by particles would inherit
both, and would become the first grid whose scrub differs from play.

This is different in the one respect that matters. The volume is **not state**. It is rebuilt from scratch
every frame, from that frame's particles. Nothing reads last frame's volume, so nothing accumulates, and
there is no history for a seek to get wrong. The volume is exactly as seekable as the particles it pictures
(ADR-360's contract, no worse). It never enters a `Simulation`, so every grid keeps ADR-1114/1119's exact
seek. It is a render target that happens to be three-dimensional.

## Decision

**A particle system may carry a `density` block:**

```json
"density": {"boundsMin": [-2.4, -2.4, -2.4], "boundsMax": [2.4, 2.4, 2.4], "resolution": 192, "weight": 1.0}
```

The bounds are in world space and required. `resolution` defaults to 128 and `weight` to 1.

**Each frame**, after the system's compute pass (emit → [latent] → simulate → compaction), in a compute
pass of its own (`particle-density` on the frame timeline):

1. The `resolution³` u32 grid is cleared with a buffer clear.
2. `cs_density_splat` deposits every live particle into the grid. It uses trilinear cloud-in-cell with cell
   centres at `lo + (i + ½)·cell`, and adds `u32(w·1024 + 0.5)` per corner with `atomicAdd`. Integer
   addition is associative, so arrival order cannot change the sum: the grid is deterministic. ADR-1120's
   deposits take the same approach.
3. `cs_density_resolve` blurs the grid 3×3×3 with a binomial ((1 2 1)³/64, edges clamped). It reads a 6³
   apron tile from workgroup memory, as the prototype's iteration-2 resolve does. It writes
   `r = weight · particles per cell` into an **rgba16float 3-D texture**. g and b are 0 and a is 1.

**Its own layout.** ADR-1140 spent the particle compute layout's last storage buffer. The density passes
bind 0 params, 1 the pool (read-only), 2 the grid and 3 the storage texture. They share no group with the
particle passes.

**The first 3-D texture in the engine.** Before this, no `texture_3d` appeared in any shader and no
`TextureDimension::e3D` appeared in `src/` (checked: the only hit was the WGSL keyword list in
`src/shaders/shader_format.cpp`). The format is **rgba16float**, because it is the one format in core
WebGPU that is both:

- *filterable*, so a linear sampler can read it, which ADR-1142's raymarch needs. r32float is not
  filterable without the `float32-filterable` feature.
- *storage-writable*, so one compute pass writes what the raymarch samples. r16float is not a storage
  format in core.

Three of its four channels are unused. At 8 bytes a texel, that waste is the price of one portable format.

**Memory and the cap.** One volume costs `res³ × 12` bytes: a 4-byte grid cell plus an 8-byte texel
(`densityMemoryBytes`).

| resolution | memory |
|---|---|
| 128 | 24 MiB |
| 192 | 81 MiB |
| 256 | 192 MiB |

`resolution` is **refused above 256** at load, and the message states the 192 MiB a 256³ volume costs. It is
also refused below 4. Bounds must be finite with max > min on every axis, and weight must be in 0..10⁴. The
renderer's `ParticleStats::densityBytes` reports what the allocated volumes hold.

**Lifetime.** The volume belongs to the system's pool:

- It is created on the first frame that needs it, and re-created when the resolution changes.
- It is dropped with the pool: a capacity change, or a scene switch through `resetAll`, re-creates it on
  the next frame.
- A **disabled or distance-culled system's volume is resolved empty**, with no splat. A consumer then sees
  no matter rather than the last frame on which the system was on.
- Inside an ADR-360 warm-up the passes are skipped. Only the arriving frame's volume is ever read.

**Handed to other renderers** the way `glowBuffer()` is handed to the volume march:

- `ParticleRenderer::densityVolume(scene, name)` and `densityVolume(index)` return a `ParticleDensityVolume`:
  the view, the world bounds, the resolution and `valid`.
- A system with no volume, or none resolved yet, hands out a **1×1×1 zero placeholder** with `valid` false.
  That placeholder reads as "no matter anywhere".
- `readDensity()` (tests and tools) copies the r channel back.

**Parameters.** `particles/<name>/density/weight`, registered only when the block is present.

## Consequences

- A system without the block allocates nothing and dispatches nothing. A system with a volume and no
  consumer changes no pixel of the frame. This was measured with the matter drawn as billboards and an SDF
  object in the same frame: both images identical.
- The GPU volume matches the CPU reference (`splatDensity` + `resolveDensity`) for 4096 particles bound to a
  sphere at 48³. Every voxel agrees within the allowance of half a unit in the last place of an 11-bit
  mantissa, plus one fixed-point quantum per corner. One quantum is 1/1024 of a particle: a corner weight's
  `+0.5` rounding can fall the other way when Metal fuses the multiply-add. A first tolerance written as
  2·10⁻³ relative failed at 1.7·10⁻² on near-empty voxels, which is that quantum and not a disagreement.
- **Cost**, measured on the M2 Max with 1M particles, timing the particle renderer on its own timeline: the
  density pass p50 is **0.59 / 0.92 / 1.57 ms at 128³ / 192³ / 256³**. The full table, with the raymarch, is
  in ADR-1142. The resolve scales with cells and the splat with
  particles, as the prototype measured (0.9 / 2.2 / 4.6 ms at 128 / 192 / 256 on 2M particles).
- The volume is a picture, so a scrub sees the particles' relaxed seek (ADR-360) and nothing worse.

## Rejected alternatives

- **A `Simulation` grid fed by particles.** That is ADR-1120's rejection and stays rejected. It would make
  particle seek relaxation leak into the exactly seekable grids.
- **Sampling the grid buffer directly in the raymarch** (no texture). Trilinear filtering would cost 8
  storage reads per sample and need the blur on the fly. The march samples the field hundreds of times per
  pixel.
- **r32float plus a manual trilinear.** It is not filterable in core, so the march would do 8 loads per
  sample, and it saves only half the bytes of the format that works.
- **A max-occupancy coarse grid for empty-space skipping** (the prototype's 24³ "coarse" texture). It is
  worth up to a few ms on a frame-filling surface. It was left out of this step to keep the first 3-D
  texture alone; see ADR-1142's revisit trigger.
- **Splatting heat into g** as the prototype does. Production particles have no heat. A later need adds
  it, and the format already has the channel.

## Revisit triggers

- A consumer needs more than one channel: heat, velocity divergence, or a second species. The texel already
  has three spare channels. The splat grid would need a second u32 plane.
- A volume larger than 256³ is wanted. That needs a sparse or bricked layout, not a raised cap.
- Two consumers want one system's volume at different resolutions. Today it has exactly one resolution.
