# ADR-1040: The liminal vocabulary: stairs, screw, warp and shell SDF nodes

- Status: Accepted (2026-09-30), proto/liminal-space
- Extends ADR-027 and ADR-1001. Implemented in `src/spatial/sdf.*`, `shaders/sdf.wgsl` (interpreter and the
  ADR-1003 compiler's helpers) and `src/scene/sdf_object.cpp`; the building blocks in `tools/liminal_sdf.py`.
- Tests: `tests/unit/test_sdf_liminal.cpp` (`[sdf][liminal]`), the parity list and a compiled case in
  `tests/rendering/test_sdf_gpu.cpp`.

## Context

The Liminal brief (§2, and the owner's addendum) wants plain architecture -- rooms, corridors, stairways,
landings, doorways, platforms -- that repeats, recurs and continues forever, and that changes
continuously rather than switching. ADR-027/1001 had boxes, booleans, axis repetition, folds and
recursion, but no stair, no way to chain a cell of architecture into the next one along a diagonal or
round a turn, and no deformation that bends walls without melting them (`displaceNoise` offsets the
distance, which turns planes into blobs).

## Decision

Four node kinds, appended to `SdfNodeKind` so the GPU numbers of every existing kind are unchanged (the
range predicates special-case them):

- **`stairs`** (primitive): `count` steps of `size.x` run and `size.y` rise climbing +X from x = 0, half
  width `size.z`, solid to y = 0 (`height` 0) or a floating flight with a sloped underside `height`
  thick. The profile is the signed distance to the infinite zig-zag of risers and treads (three steps
  round the point's diagonal coordinate) intersected with the flight's extent and underside, extruded
  in z: exact at the treads, a 1-Lipschitz bound elsewhere.
- **`screw`** (domain op): the cell repeated by a screw S. `count` 0: a translation `translation` (slab
  cells, `k = rnd(p.T/|T|^2)`); `count` n: a helix of n cells per turn about +Y, each turned 360/n
  degrees (the atan2(z, x) direction) and raised `translation.y`, the cell chosen by angle and then by the
  winding nearest the point's height. `offset` > 0 is a **seam guard**: the distance is capped at the
  distance to the cell boundary plus `offset`, so a march steps onto the boundary instead of jumping
  into the next cell's content, which only the next cell evaluates.
- **`warp`** (domain op): `p + amount * size * (vector value noise * 2 - 1)` at `p * frequency +
  translation`. `size` is a per-axis gain -- (1, 0, 1) bows walls and leaves floors flat -- and
  `translation` is the phase, a parameter rather than `speed * time`, so its speed can be integrated from
  the music (ADR-1041) and stops in silence.
- **`shell`** (unary): `|d| - offset/2`, an exact hollow of `offset` thickness: a room is one box.

## Consequences

- A room, a corridor, a stair and a bridge are a few nodes each; the whole example cell is 40.
- **The seam margin must exceed `epsilon x maxDistance`** (0.24 for the example's 0.0012 and 200 m):
  the march takes `d < epsilon * t` as a hit, and at a seam the guard reports `offset`, so a smaller
  margin draws the seam planes as surfaces in the distance (seen: dense stripes through a window).
  `tools/liminal_sdf.py` defaults to 0.3.
- **A CSG difference is only a bound near the cut's faces.** An eye passing through a shallow doorway
  cut reads the field as touching a surface that is not there; the library cuts doorways and open ends
  1-1.5 m past both wall faces, which only removes that room's shell.
- **`morph` between two rooms is not a continuous room.** A convex mix of fields whose surfaces are far
  apart has |grad d| < 1 and false near-zero sheets; rendered, the far wall filled with moire stripes.
  Grow a room by keying its box (`shell` makes that one parameter plus its centre). Morph stays for
  structures whose surfaces are close.
- The warp costs three value-noise lookups per evaluation; one per world at low frequency.
