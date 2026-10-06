# ADR-1150: A density surface is bisected, skips empty blocks and takes its normal from a smooth density

**Status:** Accepted (proto/astral-forge, the production path of THE ASTRAL FORGE, iteration 3, step 1)
**Date:** 2026-10-06
**Resolves:** the "density banding and voxel blockiness" of `docs/prototypes/astral-forge/06-iteration-2.md`
("What still looks generic" item 4) and ADR-1142's revisit trigger (the coarse occupancy grid).
**Implemented by:** `shaders/particle_density.wgsl` (`cs_density_coarse`), `src/rendering/particle_renderer.{hpp,cpp}`
(the coarse texture, its own layout and dispatch, `readDensityCoarse`, `ParticleDensityVolume::coarseView`),
`src/scene/particles.{hpp,cpp}` (`densityCoarseResolution`, the budget), `shaders/sdf_raymarch.wgsl`
(`kSdfDensityMode`, `sdfDensityMarch`, `sdfDensitySmoothAt`, `sdfDensityFieldOf`, `sdfDensityNormal`, binding 7),
`src/rendering/sdf_renderer.{hpp,cpp}` (binding 7, `density1.w` / `density2.w`, the variant splice, the
`setDensityOccupancySkipping` switch)
**Tests:** `tests/unit/test_astral_production_look.cpp` (`[adr1150]`: the coarse resolution),
`tests/unit/test_particle_latent.cpp` (the memory budget with the coarse grid);
`tests/rendering/test_astral_production_look_gpu.cpp` ("occupancy: the coarse grid holds each 8^3 block's
maximum, apron included", "occupancy: skipping empty blocks takes fewer steps and draws the same surface",
"density normals: a fully sharpened density surface shades like the tree's own surface"), `[gpu][adr1150]`.

## Context

The iteration-2 production frames (`iter2/production/latent-entity-t4.png`) showed two defects the prototype
does not have:

- **Contour banding** on every rounded form (the eye spheres most). ADR-1142's march was the SDF pass's
  relaxed sphere trace: it stops at the first step below `epsilon * t` and shades "one more step" further on.
  The density field is not a distance, so where that step lands relative to the iso level varies with the
  step pattern, and the scatter shows as rings that follow the depth contours.
- **Blocky reflections**: rectangles a few voxels across on every reflective patch. The normal was the
  tetrahedral gradient of the field at the object's `normalEpsilon` (0.004 in the example, a sixth of a
  0.025 cell). The density is sampled trilinearly, whose gradient is constant within a texel and jumps at
  its faces, so every texel showed as a facet; at sharpness 0.85 the density term's gradient was as large
  as the tree's and dominated the normal.

A third finding came out of the investigation: the rings of bright dots on the eye spheres in the same
frame are not the surface at all. They are the billboards of the bound matter, half buried in the surface
they form, z-fighting it along its depth contours. ADR-1153's flakes lift a plate resting on the surface
toward the eye, as the prototype's splat does (its depth test accepts a flake within `0.015 dist + 0.03`).

ADR-1142 also left empty-space skipping as a revisit trigger: the march stepped every empty block at a
fraction of a cell.

## Decision

Everything below is compiled into the **density variant only**. The `@@SDF_SURFACE@@` block now also holds
`const kSdfDensityMode: bool` (false in the default module, true in the variant ADR-1142 splices), and the
march and normal branch on that constant, so every other object's module keeps its arithmetic: the branch is
removed at compile time, never taken at run time. (Measured below: byte-identical.)

**The coarse max-occupancy grid.** One rgba16float texel per 8^3 block of the volume, `ceil(res / 8)^3`
(24^3 at 192). `cs_density_coarse` writes the maximum density of the block's texels plus a one-texel apron,
which is everything a linear lookup inside the block can read. It is its own dispatch after the resolve, in
the same compute pass, with its own layout (0 params, 4 the resolved volume as a sampled texture, 5 the coarse
grid): the resolve's group binds the volume as a storage target, and a texture cannot be both in one
dispatch. `ParticleDensityVolume::coarseView` hands it out; an SDF density object binds it at group-1 binding 7
(the 1x1x1 placeholder otherwise). Memory: `densityMemoryBytes` adds `cr^3 x 8` bytes (110 KiB at 192^3,
256 KiB at 256^3). The GPU grid equals the CPU maximum of the read-back volume exactly (a maximum of halves is
a half).

**The march (`sdfDensityMarch`)**, after the prototype's `fs_surface`:

1. The ray is clipped to the volume's box as well as the object's bounds: there is no matter outside it, so
   no surface.
2. A block whose maximum is below `0.3 iso` is skipped to its exit (plus 0.3 cell). Below `0.38 iso` both the
   density term and the dilated term of ADR-1142's field are positive, so no surface can be in it.
3. Otherwise the step is the field times the step scale, clamped to [0.65, 3] cells: no step crawls, none
   jumps a shell of matter.
4. The first sample below the hit threshold is refined by **7 bisections** on the field's sign between it and
   the previous sample: the hit is on the iso-surface to 1/128 of a step.

The lit pass and the depth prepass still call one function, so they still reach bit-identical `t`. The
refined `t` is both the depth and the shading point.

**The normal (`sdfDensityNormal`).** Tetrahedral differences of the same field, but with the density read as a
**cubic B-spline** of the texels (Sigg and Hadwiger 2005: eight linear fetches at offset positions, C2, so its
gradient is continuous) and a tap distance `mix(0.7 cell, normalEpsilon, sharpness)` (the prototype's). The
march and the bisection stay on the trilinear field. 4 taps x (8 fetches + one tree evaluation) per pixel.

**The example's sharpness is 1.** The prototype's formed moments (TEST 01 at 10.8 s, TEST 02) run at
S = 1 in the anatomy (`conductor.hpp`: `S = sstep(0.55, 1, C)`, C = 1). At S < 1 the density term is a share
of the surface and of its normal, and with production's 400 k particles (2 M in the prototype) it reads as
lumpy matter; that is the honest look of a partly formed entity, not a defect. The facets are gone at any S.

**`setDensityOccupancySkipping(bool)`** on `SdfRenderer` switches the skip off (it binds the placeholder and
`density2.w` = 0) for the tests and the cost measurement. It is not a scene parameter: skipping never changes
the surface, only its cost.

## Consequences

- Every object not in density mode draws with the module it had. Measured against the pre-change binary
  (`2eca1bd7`), 1280x720 at `--range 2:2`: `ferrofluid-crown.json` (two compiled trees), `stellar-nursery.json`,
  `particle-vfx-lab.scene.json`, `tempered-metal.scene.json`, `fungi.json`, `organic.json` and `hero.json`:
  **0 differing channels** each (with ADR-1151..1153 in the same binary). (The lab frame at 2 s is nearly
  black, mean 0.1 of 255, so it is weak evidence; the other six have means of 14..78.)
- The contour rings and the facets are gone (`~/Desktop/av-gen-review/37-astral-forge/iter3/production-vs-prototype/`,
  `side-latent-entity-iter2-vs-iter3.png`). The GPU test: a fully sharpened density sphere's shading differs
  from the tree's own sphere by a mean of 2.6 luminance codes over the lit pixels, against 55.5 for the
  unsharpened matter, and has less fine-detail energy than the tree's own render (2577 against 5770).
- Skipping: the test's density sphere takes **10.3 steps per sampled ray instead of 52.7** (0.20x), with the
  same surface (mean |dL| 0.62 codes).
- Measured cost (M2 Max, 1080p, under the GPU lock, shared with other agents' work; `[.perf][astral3]` and
  `[.perf][latent]`):

| Arm | SDF raymarch pass p50 | GPU frame p50 |
|---|---|---|
| frame-filling density sphere (200 k particles, 192^3), skipping off | 30.0 ms | 50.5 ms |
| the same, skipping on | **13.2 ms** | **23.2 ms** |
| the example's crude mask at 1080p, density mode, S 0.85 (ADR-1142's arm) | 9.6 ms (was 7.9) | 29.2 ms (was 24.7) |

  The density pass (splat + resolve + the new coarse dispatch) for 1 M particles is 0.66 / 1.18 / 2.36 ms at
  128^3 / 192^3 / 256^3 (ADR-1141 measured 0.59 / 0.92 / 1.57 ms without the coarse dispatch, on a quieter
  GPU). The mid-frame mask is slower than in ADR-1142 (+1.7 ms): the B-spline normal (32 fetches per pixel) and
  7 bisections cost more than skipping saves on a surface that covers a quarter of the frame; on a frame-filling
  surface skipping wins by 17 ms.

## Rejected alternatives

- **A uniform branch in `sdfMarch`.** ADR-1142 measured that a uniform branch in the shared field moved two
  compiled trees by 5,192 channels. A compile-time constant spliced into the variant is removed by the
  compiler from every other module.
- **The coarse max from the raw u32 grid in the resolve's group** (a 12^3 window: blur radius plus apron). It
  needs no second layout, but the raw maximum of shot noise is several times the blurred one, so sparse dust
  blocks would rarely be skipped.
- **Tricubic sampling in the march.** Eight fetches per step for hundreds of steps per pixel. The march only
  needs the sign change; the normal needs the smoothness.
- **A larger normal epsilon alone** (one cell or more). It blurs the facets but keeps them (taps still straddle
  texel faces) and rounds the tree's own features at full sharpness.
- **The prototype's local sharpness spread** (S falls off away from the anatomy's centre). It is authored per
  archetype in the prototype; production has one sharpness per object. A later need can add a falloff.

## Revisit triggers

- The B-spline normal is too expensive on a frame-filling surface: take it from a precomputed gradient
  (the volume's three spare channels could hold it, written by the resolve).
- A density surface needs a sharpness that varies over it (the prototype's `sharpSpread`).
