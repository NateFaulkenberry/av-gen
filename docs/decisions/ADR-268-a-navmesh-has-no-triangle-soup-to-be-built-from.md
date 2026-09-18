# ADR-268: A navmesh has no triangle soup here to be built from, and one walkable surface per point is a fact about this world

**Status:** Accepted
**Date:** 2026-09-17

The brief names Recast and Detour as references and asks for a real conclusion about adopting them.
§48 requires a full dependency analysis before any third-party library is proposed. The conclusion
is no, and four of the five arguments are properties of this repository rather than opinions about
the library.

ADR-093 already rejected a navmesh on taste-free grounds when it built `NavGrid`. This ADR exists
because "we already decided" is not an answer to a brief that asks the question again with a
different library named, and because the intervening year of work has produced measurements ADR-093
did not have.

---

## 1. What this world is

* **Terrain is analytic.** `WorldMap::sample` returns height, normal, slope, water surface and
  submersion from one set of noise evaluations, anywhere, exactly. It is a function of a seed, and
  an editor moves it.
* **There is exactly one walkable surface per XZ point.** `NavCell` carries a single `ground` float
  (`entity/nav_grid.hpp:60`). No overhangs, no bridges, no interiors, no second storey.
* **There is no triangle soup.** Obstacles are cylinders: `spatial::NavigationObstacle` is a centre,
  a radius, a base and a height, classified by `ObstacleType` and by `Traversal`. They come from the
  ecology scatter pass and from heroes. Measured on `glowmere-valley-2`: **1,238 solids**.
* **Canopy is statistical.** ADR-080: "nine-metre trees grow around here", never "there is a trunk
  at this spot".

## 2. What exists, measured

`tools/charai_probe.cpp`, minima of 3, load average 3.75:

```
154 x 154 cells at 4.00 m = 23,716 cells; build 243.9 ms
walkable 19,584   water 1,200   blocked 2,479   regions 28, largest 9,938

Navigator::requestPath  (A* across the world)   24.969 µs
Navigator::pathValid    (re-check a held route) 74.481 µs
Navigator::steer                                59.135 µs
Navigator::clearanceAt  (grid lookup)            0.024 µs
Navigator::obstructed   (grid lookup)            0.011 µs
```

## 3. The four decisive arguments

**Recast's input does not exist.** Recast rasterises a triangle soup into a heightfield, filters
walkable spans, builds regions and triangulates. There is no authored collidable geometry here to
hand it. Adoption means *generating* a triangle soup — tessellating the analytic terrain and
extruding 1,238 cylinders into meshes — in order to recover, approximately, the walkability the
world already answers exactly. The first step of the pipeline would be fabricating its own input.

**The one structural advantage of a navmesh is one this world cannot use.** A navmesh expresses
several walkable surfaces over one XZ point. This world has one, by construction. Everything else a
navmesh offers — polygon-accurate boundaries, funnel smoothing — is a quality improvement over a 4 m
grid whose local metre is already handled by `Navigator::steer`.

**A bake goes stale, and this world moves.** The grid rebuilds in 243.9 ms and is correct by
construction. A navmesh would be a build step somebody forgets to run, in a repository whose whole
position is that world data is generated rather than stored.

**Determinism is a hard requirement and not a documented Recast property.** ADR-267 needs the same
request to give the same waypoints on any machine, and `NavGrid`'s A* guarantees it by breaking ties
on cell index. Detour publishes no equivalent guarantee; its node pool and its floating-point portal
funnel would have to be audited before its output could be trusted under a scrub. **This was not
tested** — adopting the library to test it is building the thing — so it is recorded as an unretired
risk rather than as a measured defect, and the recommendation does not rest on it.

## 4. The dependency analysis, for completeness

recastnavigation is zlib-licensed, CMake-built, roughly 30 kLOC across Recast, Detour, DetourCrowd
and DetourTileCache. It runs on Apple Silicon in shipping titles. It is written in a C++98/03 idiom:
it compiles under C++23 but will not pass this repository's warning settings without being wrapped
as a system include, and `avgen_set_warnings` is applied per target, so that is a change to the
build rather than a flag. It adds a build-time asset pipeline stage and a binary navmesh artefact.

None of that is prohibitive on its own. It is cost paid for capability this world does not have.

## 5. The one component worth wanting, and why it cannot be taken alone

`DetourCrowd`'s velocity-obstacle avoidance is genuinely better than a positional push. It operates
on a `dtNavMesh`, so it cannot be adopted without everything above.

The existing `crowdSeparation` is a positional push, and ADR-240 §5 already found and fixed its
worst defect: the caller clamped the push to a full walking step, so the correction was
frame-rate-dependent and enormous. rook's worst backwards step was 0.0933 m — *exactly its authored
walking step, to four decimal places* — and became 0.0317 m once separation was made a speed
integrated against real dt. If crowd avoidance needs to be better than that, the work is a
velocity-space solver over the existing `spatial::ObstacleField`: roughly 300 lines, deterministic
by our own rules, and no dependency.

## 6. What would change the answer

**Authored interiors.** A building with two floors, a bridge over a path, a cave — anywhere one
walkable surface per XZ point is structurally wrong. If that is ever on the roadmap, revisit this,
and revisit it *before* the grid grows a second layer by accretion, because a grid that has learned
to carry two heights per cell is a navmesh that nobody decided to write.

## 7. What navigation still owes, which is not a library problem

* **Dynamic obstacles.** `ObstacleField` is built once at load. A door that closes, a craft that
  lands, a felled tree — none can be added. This is the one place TileCache has a real analogue, and
  it is a partial rebuild over an XZ rect on a 23,716-cell grid.
* **Off-mesh links.** `NavSettings::jumpOver` and `NavCell::vault` (ADR-196) exist, are priced into
  A*'s cost term, and are **zero in every scene**: nothing sets `jumpOver`, so every cell's vault
  column is 0 and the cost term it feeds is exactly 0. This is not the same thing as
  `explore/jumpRange` (ADR-194), which *is* authored — `ember` carries 7.0 — so a character can hop
  and the planner still cannot route it across a gap. The vocabulary for crossing a gap by jumping
  is built on the planner side and nothing uses it.
* **Reachability reporting.** 28 regions, largest 9,938 of 19,584 walkable cells — half the walkable
  ground is not reachable from the other half — and nothing tells an author. `NavGridStats::regions`
  is computed at build and read by no UI. `PathStatus::Unreachable` exists precisely for this and
  nothing surfaces it either.
* **Route re-validation is three times the cost of planning a new route.** `pathValid` (74.481 µs)
  samples the analytic world along every remaining leg at 2.5 m; `requestPath` (24.969 µs) is A*
  over a grid that is already built. `Explore`'s `repathSeconds` throttle is tuned as though the
  check were the cheap half.
