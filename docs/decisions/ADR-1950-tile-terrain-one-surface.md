# ADR-1950: Musical Geography's ground is one surface, written twice

- Status: Accepted (Musical Geography Phase 2, 2026-11-08), scene/musical-geography.
- Builds on: ADR-1117 (the generator distribution and its CPU mirror).
- Code:
  - `scene::TileTerrainSpec`, `tileSample`, `tileHeightAt` and the JSON, in `src/scene/tile_terrain.{hpp,cpp}`;
  - the "tiles" kernel: `GeneratorSpec::tiles`, `generatorTile`, in `src/scene/generator.{hpp,cpp}`;
  - `cs_generate_tiles` and `tileSample` in `shaders/generator.wgsl`;
  - `packGeneratorTiles` and the third generator binding, in `src/rendering/procedural_renderer.{hpp,cpp}`.
- Tests:
  - `[geography][tiles]` in `tests/unit/test_tile_terrain.cpp`;
  - `[gpu][generator][geography][adr1950]` in `tests/rendering/test_tile_terrain_gpu.cpp`.

## Context

The brief asks for an enormous, continuous landscape of square tiles that rendering, the characters and the camera all
read as the same ground (§5: "character navigation and collision use the same effective terrain surface as
rendering"). The owner's decisions of 2026-11-08 narrow the landforms: rolling land with no cliffs, mesas or canyon.

## Decision

1. **A second generator kernel, "tiles".** Every cell of the window around the camera holds its tile, at the cell's
   centre, unrotated. The records then go through the existing effector pass, cull, LOD and draw. The two kernels
   share the window, the record layout and the bind group; the name picks the entry point. This is the "second
   kernel" ADR-1117 deferred its registry to, and that registry is still only the name check.
2. **One height per tile.** A tile's top is flat at `tileSample(centre).height`, so "how high is the ground here" is
   the tile under the point, exactly.
3. **One rule, two halves.** `tileSample` in C++ and in WGSL are the same operations in the same order, on
   `generatorHash`'s integer hashes and the same value noise. The GPU test reads the records back and compares them
   with the mirror, cell for cell: identity exactly, height within 1 mm, channels within 1e-4.
4. **Formations are designed functions.**
   - the sea and the shore come from a slow continent field and a threshold;
   - rolling hills are domain-warped fBm, shaped by a smoothstep and raised gradually inland;
   - crowns flatten hill tops above a threshold;
   - the path is a raised polyline band;
   - a slow region field varies the hills' wavelength and height across the world.
5. **The material channels ride in the record.** `random` = (water, shore, flat, path); `color.a` = the tile's
   lattice hash (request 1978).

## Consequences

- A CPU test holds the rolling-land promise on a 600 m window at 1 m tiles: the worst neighbour step is under 0.6 m
  and the 99th percentile under 0.3 m. The terrain is not flat: its relief across the window is over 5 m.
- The window is integer cells, so a tile never slides and keeps its identity as the camera travels. Hashing is
  integer, so this holds at any distance. Positions are f32, as for every generator (ADR-1117).
- Dynamic terms (waves, breathing, events, landing springs) are Phase 3. They add to `dyn` in both halves, and the
  parity test grows with them.

## Amendment (2026-11-08, CP-A): the river valley, and the height scale

The owner-approved r2 mock-up has a wide, smooth valley with a river. The formation set gains one:
- `tiles.river` is a polyline of up to 16 points.
- Within `valleyWidth / 2` of it, the land eases down to the river bed (the sea floor's level) on a smoothstep
  profile: smooth sides, no walls.
- Within `riverWidth / 2`, the tiles are water (water channel 1).
- The same polyline, like `path`, is what the flight (Phase 5) and the characters (Phase 6) follow.

`tiles.heightScale` multiplies the land's relief above the shore, so the art can raise how tall the world stands
without retuning the shapes. Both halves change together. Tests:
- the CPU cases hold the channel at the bed and the land far from the river untouched;
- heightScale doubles the relief above `shoreRise` and leaves the sea alone;
- the GPU parity case runs with a river.
