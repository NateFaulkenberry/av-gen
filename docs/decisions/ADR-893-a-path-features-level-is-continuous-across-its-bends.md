# ADR-893: A path feature's level is continuous across its bends

**Status:** Accepted
**Date:** 2026-09-25
**Found by:** scouting Glowmere Valley 2's terrain for Glowmere Valley 3. The owner asked for the
defects to be fixed.
**Implemented by:** `blendedLevel` and `levelledHit` (`src/world/world_map.cpp`), used by
`WorldMap::heightUncached`, `waterSurface` and `waterTable`
**Tools:** `avgen_world_preview --seams` (`tools/world_preview.cpp`)
**Tests:** `tests/unit/test_path_level_continuity.cpp` (`[seams]`)

## Context

A path feature's *level* is the height the terrain is flattened toward, a river's water line and
bed, and a flat's water. It was the path's own height at the single nearest point on the path
(`closestOnPath`).

On the inside of a bend that nearest point is not unique. Past the bend's centre of curvature a
point is almost as near the path's other arm. Across the medial axis, where the two arms are equally
near, the nearest point jumps from one arm to the other. The level jumps with it, because the level
is the path's height *there*.

Glowmere Valley 2's `valley-corridor` reaches 300 m from its path, descends about 2 m per 60 m of
course, and flattens the ground toward that level at weight 0.55. The jumps became cliffs:
**1,455 cells in 42 runs, up to 2.35 m high**. They were four straight walls, each running from a
bend of the corridor out across the valley. On a hillshade map they show only as thin dark lines,
which is why nobody had called them walls.

## Decision

The level is an average of the path's level over every part of the path that is nearly as near as
the nearest point, weighted by a kernel of how much further it is:

    level(p) = ∫ L(s) K(d(s) − d₁) ds / ∫ K(d(s) − d₁) ds,     K(x) = smoothstep(1 − x / 10 m)

Every term is a continuous function of p, so the level is continuous wherever the nearest point
jumps.

- **The integral:** 5-point Gauss-Legendre per segment, over the stretch of the segment inside the
  band. For a straight run with a linearly falling level, it returns the nearest point's level.
- **The gate:** where the average agrees with the nearest-point level to 5 cm, the nearest-point
  level is returned *exactly*, fading to the average over the next 5 cm. Beside straight runs and
  outside bends, terrain that was never stepped does not move by a bit.
- **Consumers:** every consumer of a level goes through it: the height's flatten targets, river beds,
  water flats, the water surface and the water table.
- **Switch:** `AVGEN_LEVEL_BLEND=0` takes it out without a rebuild, the twin of
  `AVGEN_PATH_CUTOFF` and `AVGEN_HEIGHT_CACHE`.

**Two cheaper designs were built and measured first, and both failed for one reason.** Both
blended the *local minima* of distance along the path.

1. Faded by path length near a segment's end, they left 637 half-metre steps.
2. Weighted by persistence, they left steps up to 2.53 m.

Far inside a bend the distance along the path is nearly flat over a long stretch, so any rule that
picks points jumps as the flat stretch's deepest wiggle moves. Only an average over the whole
stretch is continuous. That is recorded in the code so that nobody "simplifies" it back.

## Consequences

- **Seams:** the scanner finds 0 on Glowmere Valley 2's world (from 1,455 cells).
- **Other terrain, measured against the multicam scene's 28 placed objects and a 48×48 grid:**
  - 12 objects stand on ground that moved at least 1 mm: the animals, which re-ground every frame,
    and four heroes by 0.3–3.9 cm;
  - 950 of 2,304 grid points moved at least 1 mm, 281 at least 5 cm, and 105 at least 20 cm;
  - the 20 cm moves are all in the bands around the old cliffs.
- **Placed objects in other scenes:** a scene placed on this world by hand can have an object whose
  ground moved by a few centimetres near a bend.
  - Scree's ground in the multicam scene fell 1.9 cm. That is inside the 12.6 cm float its
    centre-point seating already had on its slope.
  - The multicam project file is unchanged.
- **Cost:** height queries on the insides of bends integrate. The whole-map seam scan of 1.6 M
  heights and water levels went from 3.2 s to 5.6 s. The terrain cache (`HeightCacheScope`) keeps
  seek replays from paying it twice.
- **Tests:** each case has a control that fails without the fix:
  - the synthetic bend's old level jumps 2.65 m across the axis the test crosses;
  - every Glowmere transect crosses a place where the old level jumps more than a metre.

  With the fix switched off they fail at 1.08–2.65 m.
