# ADR-1992: Ridges and rises, raised land that can stand in the sea; 8 valleys

- Status: Accepted (Musical Geography CP-D r2, request CPD-R3, 2026-11-10), scene/musical-geography.
- Code:
  - `scene::TileRidge`, `TileRise` and the raised land at the end of `landPart` (`src/scene/tile_terrain.{hpp,cpp}`);
  - its twin in `tileSample` (`shaders/generator.wgsl`);
  - the packing (`packGeneratorTiles`, `src/rendering/procedural_renderer.cpp`).

## Context

The owner: "I don't wanna see long endless horizons". The art agent raised the land with data, but data cannot put
land where the continent field has none:
- over the open bay (30–50 s), the far shore stays a low strip;
- the dawn peninsula (185–210 s) is low land between two seas;
- the valley budget (4 valleys, 32 points) is spent.

See `02-requests.md`, CPD-R3.

## Decision

Two additive terms on the land's height, applied at the end of the land part: after the hills and the breathing,
before the dry valleys, rivers and ponds carve it.

- **`ridges`** (at most 8; each 2..32 points, 64 together): `height` (metres) × the smoothstep profile
  `1 - smoothstep(0, width / 2, distance to the polyline)`.
- **`rises`** (at most 8): `height` × `1 - smoothstep(radius, radius + edge, distance to the centre)`, so the height is
  full within `radius` and eases out over `edge`. A negative height is a hollow.
- **Their cores are land.** Wherever a profile passes about a half (`smoothstep(0.35, 0.65, w)`), the land channel is
  lifted to at least that. A ridge across the sea is land under its spine (a headland or a causeway), and a rise in
  the sea is an island. The shore channel is not moved; it still marks the continent's coast.
- The heights are metres, **not scaled by `heightScale`**, so a ridge stays the height it was authored at when the
  hills are retuned.
- They are part of the tiles' one rule. Rendering, the characters' ground, the flight's clearance and the "auto" water
  levels all read them: a pond on a ridge takes the ridge's rim, and a stream sourced on a ridge starts at its height.
  Seek equals play.

**The valley budget** rises from 4 valleys and 32 points to 8 valleys and 64 points.

## Consequences

- The kernel's uniform block grows by 84 lanes: 59 for the raised land, 25 for the larger valley budget. Each ridge
  is skipped outside its bounding box, widened by half its width.
- The cost (generators pass, live profile at 1080p ultra) is in `02-requests.md` (CPD-R3, engineering).
- Tests:
  - `test_tile_terrain` [adr1992]:
    - an island in open sea: land, its added height falling outward, the world untouched beyond `radius + edge`;
    - a ridge from the high land out to sea: height added on land, land under its core in the sea, untouched beyond
      its half width;
    - the carving after it: a valley cuts the ridge, a pond's auto level sits on it;
    - reading, writing and hashing; the caps (8 valleys of 8 points pass; a 9th valley or a 65th point is refused; a
      9th rise is refused).
  - `test_tile_terrain_gpu` [adr1992]: the kernel against the CPU (heights, channels, class) with a ridge from land
    across the sea, an island, a hollow and 8 valleys, still and moving.
