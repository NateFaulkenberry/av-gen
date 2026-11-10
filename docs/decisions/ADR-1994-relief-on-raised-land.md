# ADR-1994: Relief on raised land (`noise` on ridges and rises)

- Status: Accepted (Musical Geography CP-D r2, CPD-R3c, 2026-11-10), scene/musical-geography.
- Code: `TileRidge::noise`, `TileRise::noise` and `landPart` (`src/scene/tile_terrain.{hpp,cpp}`); the twin in
  `tileSample` (`shaders/generator.wgsl`); the packing in `packGeneratorTiles`.

## Context

Art (CPD-R3c, e378f2cf): raised land is a perfectly smooth blob. The hills' term is gated by `inland`, which is 0 in
the sea, and ADR-1993 keeps it the continent's.

## Decision

A per-feature `noise` (0..1, default 0) on ridges and rises:

`height += noise · hillHeight · k · heightScale · (hill − 0.5) · profile`

- The relief is the hills' own: the same shaped fBm (`hill`), with its warp, its region variation `k` and its crown
  flatten.
- It is centred on half, so it roughens the feature without raising or lowering it on average.
- It follows the feature's profile, so it fades out to the feature's edge.
- It is summed over the features at a point like their heights. It is not scaled by breathing.
- `noise` 0 is bit-for-bit the smooth feature.

## Consequences

- Two more uniform lanes per eight features (4 lanes in all).
- Tests:
  - `test_tile_terrain` [adr1994]: the relief varies by more than a metre inside a rise, and is bounded by
    `noise · hillHeight · heightScale`; it is nothing beyond the edge; `noise` 0 is exactly the smooth rise; reading,
    writing, hashing, and refusal outside 0..1.
  - `test_tile_terrain_gpu` [adr1992]: a noisy ridge and a noisy rise, CPU against GPU, still and moving.
