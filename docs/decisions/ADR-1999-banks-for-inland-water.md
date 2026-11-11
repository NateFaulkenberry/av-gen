# ADR-1999: Banks for inland water: an inland shore, a lip and a step

- Status: Accepted (Musical Geography CP-D r3, request CPD-R4, 2026-11-10), scene/musical-geography.
- Code:
  - `TileTerrainSpec::bankWidth`, `TileRiver::lip` / `step`, `TilePond::lip` / `step`, `TileSample::bank` / `coast`,
    `bankShore`, `bankLip`, `applyFeatures` (`src/scene/tile_terrain.{hpp,cpp}`);
  - the kernel's twin (`tileBankShore`, `tileBankLip`, `tileSample`, `shaders/generator.wgsl`) and the packing
    (`riverLevel.w`, `pondB.zw`, `land0.z`, the new `riverStep` lane; `src/rendering/procedural_renderer.*`);
  - `GroundSample::bank`, the hop's `avoidBanks` (`src/world/ground_source.hpp`, `src/entity/behaviors.cpp`);
  - the flight's `snap: "shore"` reads the coast alone (`src/scene/flight.cpp`).

## Context

The owner on draft4: the aliens "appear to be standing in the water feature areas ... they don't really read like
water features ... could we try giving water features a shore/banks". The shore channel (ADR-1993) covered the sea's
coast only; ponds and rivers set `water` with no shore, flush in the land: a different colour on the same flat tiles.

## Decision

1. **An inland shore.** Within `bankWidth` metres (tiles spec, default 8, 0 = none) of river or pond water, the shore
   channel ramps from 1 at the waterline to 0, by the smoothstep; inside the water it ramps over the same width but at
   most to a river's middle (or a pond's centre), so a narrow stream keeps its own colour down its middle. The tile's
   shore is the larger of the coast's and the bank's, so the material's existing shallows term and a bank colour ring
   inland water as they ring the sea. `TileSample` also carries the two parts alone: `bank` and `coast`.
2. **A bank shape** per river and pond, metres, default 0 (flush, as before):
   - `lip` raises the land over the first 4 m beyond the waterline (one 4 m tile), easing out by 6 m;
   - `step` sets the water's surface that far below the bank (the water's channel share of it).
   The level (set or `"auto"`) is the bank's; the water is `level - step`.
3. **No level moves.** The `"auto"` levels read the land without lips and steps, so a bank never moves a pond's or a
   river's level. Checked on art's scene: the 3 river sources (river -0.2500, westStream 13.8617, dawnStream 10.9773)
   and the 2 ponds (low1 -0.4945, low2 -0.3339) are identical flush, ringed, and banked (lip 0.6, step 0.8); the ring
   alone moves no height (0 m within 80 m of each pond).
4. **Walkers (cheap, so built):** the hop's `avoidBanks` (0..1, default 0 = off) treats a tile whose bank channel is at
   least that as water: no goal, step, landing or rest on it, so a body keeps a bank's width off a pond or a river.
5. **The flight's shore snap reads the coast alone**, so a pond or a river near a `snap: "shore"` waypoint does not
   pull the flight inland.

## Consequences

- Every scene with ponds or rivers gets the ring at its default 8 m (no height change); art can set `bankWidth` 0.
- The kernel's uniform block grows by one lane (16 bytes).
- Tests:
  - `test_tile_terrain` [adr1999]: the ring (1 at the line, 0.5 at half a bank, 0 a bank out, none mid-river, not the
    coast, off at 0); the lip and the step on a pond and a river with the levels unchanged; JSON, hash and refusals;
  - `[.][measure]`: every level of a real scene's water, flush, ringed and banked;
  - `test_tile_terrain_gpu` [adr1999]: the kernel's heights and channels (the shore included) against the CPU on a
    world with a lipped and stepped stream, a lipped and stepped pond and a flush one;
  - `test_tile_characters` [adr1999]: with `avoidBanks` a body never stands on a bank; without it, it does.
