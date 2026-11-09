# ADR-1957: TileNav, A* over the tiles

- Status: Accepted (Musical Geography Phase 6, 2026-11-09), scene/musical-geography.
- Code: `entity::planTiles`, `tileWalkAllowed`, `tileJumpAllowed`, `TileNavRules` (`src/entity/tile_nav.*`).

## Decision

One node per tile, and heights come from a query (the ground at planning time).

- **Walk:** one of the four edge neighbours, with |dh| ≤ `stepHeight` (0.6 m), and not water. Sea slabs are hopped
  onto, never waded.
- **Jump:** 1 to `jumpTiles` (3) tiles away along one of the eight lattice directions, rising at most `jumpRise`
  (3 m) and dropping at most `jumpDrop` (8 m). A 1-tile jump is offered only where a walk is not allowed.
- **Cost:** a walk costs its length. A jump costs 1.3 × its length + `jumpCost` (1.5), so walking is preferred where
  it can be done.
- **Search:** Euclidean heuristic, limited to `window` (48) tiles from the start.

Distances are counted in tiles; heights are in metres.

## Consequences

- Tests: it walks a level run and hops a 2 m riser, never walking a step over `stepHeight`. It reaches a 2.5 m crown
  and refuses a 6 m pillar and a 4-tile moat. Every step on a real 4 m tiles ground obeys the rules.
