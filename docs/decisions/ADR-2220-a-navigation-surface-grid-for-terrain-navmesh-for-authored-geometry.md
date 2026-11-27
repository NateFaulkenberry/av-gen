# ADR-2220: one navigation surface interface — the grid for terrain, a navmesh for authored geometry

- **Status:** Accepted (2026-11-26), on `eng/115-crowds` (115 programme, Agent 3).
  - The navmesh backend's library was approved by the owner on 2026-11-26. It lands separately as ADR-2221. Until then the backend is not in the build.
- **Code:** `src/nav/nav_surface.hpp` (`INavSurface`, `NavBackend`, `NavStatus`, `NavPath`) and `src/nav/grid_surface.{hpp,cpp}` (`GridNavSurface`).
- **Tests:** `tests/unit/test_crowd_world.cpp` (the Crowd Lab cases run on the grid surface).

## Context

ADR-268 rejected a navmesh (2026-09) because "there is exactly one walkable surface per XZ point". Its §6 named the condition that would change that: authored interiors and bridges. Neon Dreams meets it:
- its decks span y = 94–223 m;
- 95 of its figures stand on roofs;
- the 115 Stage A spike voxelised its real geometry and counted 48,034 cells (at 0.3 m) with more than one walkable layer in the 400 m core alone.

`NavCell` holds one ground height per cell, so the grid cannot represent this world. The grid is still right for heightfield worlds, for the reasons ADR-093 and ADR-268 give.

## Decision

1. The crowd asks a navigation surface six questions through `INavSurface`, and nothing else:
   - `findPath`, which returns a status as a *reason* (the `PathStatus` vocabulary);
   - `ground`;
   - `staticObstacles` (discs and edge segments, in a fixed order);
   - `lineClear`;
   - `valid`;
   - `contentKey`.
2. **`GridNavSurface` presents `entity::NavGrid` and `Navigator` unchanged.** It is the only place the crowd touches `entity/` (115 decision O7):
   - solids come from the obstacle field as exact discs;
   - the edges of cells whose *terrain* is not standable become segments;
   - cells blocked only by a solid emit no edge, because a blocky edge round a disc would close gaps a body fits through.
3. **`NavBackend::NavMesh` names the second backend:** a tiled voxel navmesh over authored geometry.
   - Until ADR-2221 lands, it is not built: no CPM entry, no fetch, no stub that pretends to answer.
   - A scene with no terrain node therefore gets no surface. Its crowd avoids itself and its authored walls and walks straight lines, and the composition logs that once.
4. Authored walls (ADR-2222's first part, carried here) are added to the obstacle field as overlapping chains of solids, so the grid plans round them. Avoidance sees the same walls as segments.

## Consequences

- Terrain worlds get crowds today with no second planner.
- Neon Dreams' crowd waits for the navmesh backend.
- The 4 m grid is too coarse for a 2 m doorway. The lab environments set `navCellSize` to 0.5, and a scene with doorways must do the same.
