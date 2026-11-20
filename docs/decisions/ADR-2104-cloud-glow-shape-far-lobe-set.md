# ADR-2104: Cloud glows that light the cloud's structure; a far lobe set on its own grid; 24 glows

- Status: Accepted (2026-11-19), avagent/neon-dreams (Neon Dreams, iteration 5)
- Extends: ADR-2100 (Cloud Environment v2), ADR-2101 (bins with column extents).
- Code: `src/scene/clouds.{hpp,cpp}` (`glowBreakup`, `glowShadow`, `glowBreakupScale`, `glowShadowDistance`,
  `farLobes`, `kMaxCloudGlows` 16 -> 24), `src/rendering/cloud_renderer.{hpp,cpp}` (uniforms `glowShape`, `farGrid`,
  `farGrid2`, `heroSpan`; the far grid's words after the hero grid's in one buffer), `shaders/clouds.wgsl` (`lobeGrid`,
  `skipGrid`, the glow loop), `shaders/clouds_resolve.wgsl` (the uniform block).
- Tests: `tests/unit/test_clouds.cpp` (`[adr2104]`: read, validate, round-trip, live knobs),
  `tests/rendering/test_clouds_v2_gpu.cpp` (`[adr2104]`: a formation given as the far set draws as it does in the hero
  set).

## Context

Two Neon Dreams defects and one limit. (a) Round the Mooring the city glows lit whole lobes evenly: a glow's light is
spherical around its point, so every lobe it reached read as a soft pink ball. (b) The Veil passage's inside was one
flat colour. (c) The art's far towers (638 lobes, 2-10 km out) in the same lobe set as the hero formations stretched
the bin grid's cells (64 along the longer side) to 223 m, and the hero cells overflowed the 64 lobes a cell holds.

## Decision

1. **Glow shape.** Each sample remembers the glow that lights it most. `glowBreakup` (0..1) multiplies the glow by a
   pocket term of the cloud's own coarse noise at `glowBreakupScale` cycles/m (0.12 in the gaps, up to 2 in the
   pockets): light gathers in pockets inside the cloud instead of filling a sphere. `glowShadow` (0..4) darkens the
   glow by one density tap `glowShadowDistance` metres toward that glow: billows facing it are lit, their far sides
   fall dark, so the glow draws the cloud's relief. Both 0 by default (unchanged); both are live parameters
   (`clouds/glowBreakup`, `clouds/glowShadow`). Cost: one noise tap and one `densityBase` per lit sample.
2. **Far lobe set.** `clouds.farLobes` (inline array) is binned on its own grid (same rule, its own cell size and
   height range) and its lobes follow the hero lobes in the lobe buffer. `lobeBase` takes the max of the two grids;
   the empty-air skip consults both. The renderer box is their union; the hero grid's column heights are quantised over
   the hero set's own range (`heroSpan`).
3. **24 glows** (was 16): the Veil's six inner pockets.

## Consequences

- Neon Dreams: the far towers no longer coarsen the hero grid (hero overflow from 18 cells to 6, from the hero
  formations themselves); the Mooring's cloud is lit at its edges and pockets, not as balls.
- A scene with neither key renders exactly as before.
