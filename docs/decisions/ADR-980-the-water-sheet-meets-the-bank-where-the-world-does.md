# ADR-980: The water sheet meets the bank where the world does

**Status:** Accepted
**Date:** 2026-09-28
**Found by:** the GV3 targeted art pass, item 1 (`docs/glowmere-valley-3/art-pass/00-brief.md`): the owner's two
screenshots of the north head's falls, "a stepped, vertical wall" and "a slab with staircase edges"
**Implemented by:** `world::buildChunkWater` (the dry corner's level), `world::kWaterChunkMaxLod` and
`Composition`'s per-frame terrain LOD pick; the measuring tool `tools/water_probe.cpp` (`avgen_water_probe`)
**Tests:** `tests/unit/test_water.cpp` and `tests/unit/test_composition.cpp` (`[adr980]`)
**Data change beside it:** `tools/gv3/world.py` `JUNCTION_ALONG` (Glowmere Valley 3's falls meet the river on the
valley floor); recorded in `docs/glowmere-valley-3/art-pass/PROGRESS.md`

## Context

Water is one mesh per terrain chunk (`buildChunkWater`), built on the ground's own 1.2 m grid, with a quad
wherever any corner is wet. The shoreline is not in the mesh: the sheet runs one cell past it and the ground
cuts it off by depth. That makes two things decide where a viewer sees the water end: the height the mesh gives
a DRY corner, and the ground mesh actually drawn beside it.

Nothing measured either. `avgen_world_preview --seams` finds a step inside the ground or inside the water surface;
a sheet standing proud of its bank is a step in neither. So a probe was built first (`avgen_water_probe`): it
rebuilds every chunk's water mesh and its ground at every LOD exactly as a load does and compares them on a fine
grid with `WorldMap::height` and `WorldMap::waterSurface`.

On Glowmere Valley 3 (r7b), the falls down the north head:

- **The dry corner took the HIGHEST of its wet neighbours' levels.** Right on a lake, where they are all one
  level. On the falls, which drop about a metre per 1.2 m cell, the highest neighbour is the one upstream, so
  every edge corner stood a cell's descent above its own row's water: the sheet's edge rose up to 1.6 m clear of
  the bank, stepping row by row. 41 m2 of false water on the falls at LOD 0, 24 m2 of it more than 30 cm proud;
  the drawn surface up to 1.54 m off the world's; drawn slopes up to 74 degrees. That is the "slab with staircase
  edges".
- **The ground at LOD 3 (9.6 m cells) against water at 1.2 m.** The drawn bank is a chord through the real one,
  so the shoreline moves by up to a cell and the coarse bank shows through the river: 143 m2 along the whole river,
  up to 1.8 m deep. The 4K final turns terrain LOD off; previews and the editor do not.

The "vertical wall" is data, and is fixed in data: the river began at the falls' foot, and a river reaches its
full width past its first point, so its 26 m cap reached up the head's face, where the blend by weight
(ADR-894) dragged the falls' water 1.6-3.8 m below the falls' own level. On the steepest pitch that left it 1 m
deep and 8 m wide -- half transparent under the 1.6 m shore fade -- between the falls above and a basin below.

## Decision

1. **A dry corner's level is the plane the water around it lies in**, evaluated at the corner: the wet points of
   its 5x5 window, weighted by 1/d^2 and fitted by least squares, with a small ridge on the two slopes (a row of
   neighbours that says nothing about one axis leaves that axis level). It is held to at most the highest wet
   neighbour -- the old answer, so no corner rises above where it was -- and to at least as far below the lowest
   as the window's own spread. On a lake the fit is the one level all round: bit for bit the old answer.
2. **The water grid is sampled two cells past the chunk**, so a border corner is fitted to the same neighbourhood
   by both chunks that share it (the old rule saw only its own chunk's half). Only the chunk's own grid decides
   whether the chunk has any water.
3. **A chunk that carries water never draws its ground coarser than LOD 1** (`kWaterChunkMaxLod`).

## Consequences

Glowmere Valley 3 with the falls' junction moved (`world.py` `JUNCTION_ALONG`):

| | r7b | after |
|---|---:|---:|
| falls, LOD 0: false water / over 30 cm / worst | 41.1 m2 / 24.2 m2 / 1.61 m | 1.0 m2 / 0 / 0.11 m |
| falls: drawn surface against the world's, worst | 1.54 m | 0.11 m |
| falls: steepest drawn water | 74 deg | 43.6 deg (the falls' own gradient) |
| whole river, LOD 0-1: false water, worst | 1.61 m | 0.16 m |
| whole river, LOD 2: false dry, worst | 0.41 m | 0.35 m (in transparent shallows) |
| whole river, LOD 3 (now never drawn under water) | 143 m2, 1.83 m | -- |

- Every scene's water changes where its surface descends; a flat body is unchanged. A scene whose river runs down
  a slope loses the lifted rim along its banks.
- Far water chunks draw ground at LOD 1 instead of 2-3: a few thousand triangles on a wide. Cost measured in the
  art pass's A/B (PROGRESS.md).
- The probe stays: `avgen_water_probe <scene> <prefix> --region x0 z0 x1 z1 --step s` answers "does the water
  meet the land" for any terrain without a render.

## Rejected alternatives

- **The analytic continuation (`WorldMap::waterTable`) for a dry corner.** It is the nearest feature's level, not
  the blended one the wet corners carry: at the falls' junction it lifted edge corners 3 m above their wet
  neighbours, the defect made worse.
- **The mean of the wet neighbours.** Right on a straight bank, but a corner whose wet neighbours are all upstream
  of it keeps the old lift; the plane fit reads the slope from two rows of them.
- **Water meshes per LOD.** Consistent at every level, but a new mesh per level per chunk, a second set of chunk
  ids to select, and a coarse faceted sheet on a steep course, against a one-line cap costing a few thousand
  triangles.
- **Deepening the falls' bed to hide the neck.** It keeps the sag and widens the upper falls; the junction was the
  cause.

## Revisit triggers

- A world whose water chunks are most of its terrain (a lake world), where the LOD cap's triangles would matter.
- A water body steeper than the 5x5 window can describe (a cliff-face fall): it wants a sheet of its own.
