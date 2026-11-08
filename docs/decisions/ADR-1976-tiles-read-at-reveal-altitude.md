# ADR-1976: The squares read at reveal altitude

- Status: Accepted (Musical Geography Phase 2, 2026-11-08), scene/musical-geography.
  - This is request B of `docs/prototypes/musical-geography/02-requests.md`, owner-reviewed.
  - It replaces the first 1976 (fade the gap with distance) and the continuous far mesh of plan §6.1.
- Code:
  - the window snap (`align`), the ring (`finePitch`, `hole`), `generatorTileChildren` and `generatorTileHole`, in
    `src/scene/generator.{hpp,cpp}` and `shaders/generator.wgsl`;
  - the `tileBorder` material op: `MaterialOpKind::TileBorder`, `MaterialContext::localFootprint`, `matKTileBorder`,
    in `src/scene/material_program.*`, `src/scene/material_compiler.cpp`, `shaders/material.wgsl` and
    `shaders/pbr_shade.wgsl`.
- Tests:
  - `[geography][tiles][adr1976]` and `[material][geography][adr1976]` in `tests/unit/`;
  - `[gpu][generator][geography][adr1976]` in `tests/rendering/test_tile_terrain_gpu.cpp`.

## Context

The owner's review requires the squares to read 70–100 tiles up, with a 35–50 mm lens, across all the land in frame.
A gap of 8% of a tile is a sub-pixel line there. Sampled by the raster, it aliases into moiré, and the Phase 2 reveal
stills showed exactly that. Fading the gap, or replacing far tiles with a smooth mesh, would lose the squares where
they must read.

## Decision

1. **Rings of coarser tiles.** A "tiles" generator may be a ring:
   - its cells are 2× or 4× the fine pitch (`finePitch`);
   - each cell stands at the mean of the fine tiles it covers;
   - the finer ring's window, computed from the same camera, is a hole in it.

   Windows snap to whole blocks (`align`), so the rings tile the ground without overlap and in whole lattice blocks.
   The far field stays tiles: fewer, bigger ones, at the same screen density.
2. **A border that never thins below N pixels.** The `tileBorder` op returns border coverage on a top face. Per axis,
   the band is `max(authored width, N × the pixel's local footprint on that axis)`, antialiased over one pixel. Up
   close it is the authored line. Far away it is N pixels, so every tile keeps an outline instead of aliasing.
   `localFootprint` is `fwidth` of the local position, taken where the shader already takes the material's
   derivatives, in uniform control flow.

## Consequences

- The ring geometry is exact by test: the hole is the fine window to the metre, at three cameras including one
  1 km out. A coarse cell's height is the mean of its 16 children.
- The border width is the art agent's control (`value`, in pixels). The colour and strength are its program's.

## Amendment (2026-11-08, after CP-A): the seam band

The CP-A stills showed a straight line where the 1× ring meets the 2× ring. A 2× block stands at its children's mean,
so it steps against the fine tiles beside it.

A ring may now carry `blend: {cells, ratio}`. Within `cells` cells of its window edge, a tile's height and channels
ease toward the mean of the `ratio` × `ratio` block it belongs to, reaching it on the edge cell. That mean is exactly
the coarser ring's height there.

It is computed in both halves: `generatorTileInWindow` on the CPU, `tileCell` and the band in `cs_generate_tiles` on
the GPU. Tests:
- the CPU test checks that edge tiles equal their 2 m block within 1e-4, and that tiles deep inside are untouched;
- the GPU parity test runs with a band toward 4 m blocks.

The ring's `align` must be a multiple of `ratio`.
