# ADR-297: A door that closes is a rectangle of the graph, and the labels are the whole of it

**Status:** Accepted
**Date:** 2026-09-18

`docs/character-ai-research.md` §A.4, and §P7 item 3 of the plan:

> **Dynamic obstacles.** `ObstacleField` is built once at load. A door that closes, a craft that
> lands, a fallen tree — none can be added at runtime, and the grid would need a partial rebuild.
> This is the one Recast/TileCache feature with a real analogue here, and it is a rebuild-a-rect
> problem on a 23,716-cell grid, not a library problem.

It is, and this is the rect. `NavGrid::rebuildRect(nav, lo, hi)`.

---

## 1. What is partial and what is not, and why

Three things happen per cell in a build, and exactly one of them is local.

**Sampling is local.** A cell's contents are a function of the world at its centre and the solids
within half its diagonal, so re-sampling the cells a rectangle covers — grown by one cell, because a
solid just outside the rectangle changes a cell just inside it — is the whole of the update.

**Edge marking is nearly local**, one ring further out: a cell *outside* the rectangle is an edge
cell because of a cell inside it.

**Region labelling is not local at all, and must not be treated as one.** A wall dropped across a
corridor divides a region that reaches the far side of the world. A flood fill that stopped at the
rectangle would report two halves of one island as connected, and that is the single error a
reachability structure exists to make impossible. So `buildRegions` and `buildTerrainRoom` run over
the whole grid, and the totals are **counted rather than accumulated** — twenty-four thousand
increments is 0.02 ms, and four delta counters is four chances to leak one.

Measured on Glowmere, minima of 3, load average ~20:

```
  8 m rect     1.449 ms
 24 m rect     2.054 ms        a whole build is 287.1 ms
 80 m rect     6.333 ms
```

The floor is the two whole-grid passes, which is why a small rectangle is a millisecond and not a
microsecond. That is the honest shape of the cost and it is the right trade: the alternative buys
tens of microseconds and risks the one wrong answer that matters.

## 2. One cell, written once

`build` and `rebuildRect` share `fillCell`. They did not, for the ten minutes it took to write this,
and that is exactly how two walkable sets come to exist that agree until somebody edits one of them.
`fillCell` clears the cell first: a rebuild that inherited `NavEdge` or `NavBlocked` from what used
to be there would leave a graph that remembers a door that is now open.

## 3. What it does not do, stated rather than discovered

* **The trust verdict is not re-taken** (ADR-295). It is a statement about this world's *terrain*,
  and a solid that lands on it is not terrain — `NavTerrain` is read off the terrain rules, which no
  obstacle can reach. Re-running it would cost more than the rebuild and could only return the same
  answer.
* **Interest points are not re-extracted.** `shorePoints` and `vistaPoints` are handed out as spans
  and `EntityWorld` copies them at load; moving them here would change nothing a character reads and
  would invalidate a span somebody else holds. A shore point under a newly landed craft stays a shore
  point. This is a limitation, not a decision, and it is the next thing to do here.
* **Nothing authors a dynamic obstacle.** This is the graph half. The field half is
  `spatial::ObstacleField::add` followed by `build()` — the index is invalidated by `add` and the
  grid reads the field, so the order is not optional — and there is no scene key, no action verb and
  no tool that does it yet. The test is the only caller.

## 4. The measurement, with its control

`tests/unit/test_nav_grid_trust.cpp`: two walls with a 12 m doorway, the only way across a flat
world.

```
open:    1 region,  2,577 walkable, route west->east is ok
closed:  2 regions, 2,573 walkable, 1,169 stranded, route is Unreachable with 0 waypoints
         rebuilt in 0.137 ms against a full build of 1.165 ms
```

The control is the same rectangle rebuilt with **nothing added**: every cell's flags and obstruction
byte identical, the region count unchanged, the route still `ok`. Without it the section above proves
only that `rebuildRect` breaks things. A second control checks that a route that never needed the
doorway is untouched, so this is a door and not a wall across the world.

And the two answers agree: `connected` (a label lookup) and `path` (a search) both say there is no
way through, which is the property the flood fill is labelled with A*'s own step rule to guarantee.
