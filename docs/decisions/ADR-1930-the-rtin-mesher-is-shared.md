# ADR-1930: The RTIN terrain mesher is a shared module

- Status: Accepted (CHRONOS Gate 1, 2026-10-31), scene/chronos. The owner chose this to land first, ahead of the rest
  of Gate 1, so Strange Attractor Art Pass 2 can rebase onto it.
- Code:
  - `src/scene/terrain_rtin.{hpp,cpp}` (`scene::rtin`):
    - `Grid` and `gridFor`;
    - `propagateErrors`, a template over the caller's normalised local error;
    - `extractTriangles`, `chunkTriangles`, `windUpward`;
    - `parallelRows`.
  - `src/scene/attractor_world.cpp`: `buildTerrainMesh` calls these instead of its own copies. Its surface sampling,
    tolerance field, forced portal splits, attributes, baked occlusion and river stay where they were.
- Tests:
  - `[terrain-rtin][adr1930]` in `tests/unit/test_terrain_rtin.cpp`:
    - the grid size;
    - a plane meshes to two triangles;
    - a bump refines and stays watertight (every interior edge used twice);
    - the result is deterministic;
    - every triangle lands in one chunk;
    - the winding.
  - `[attractor-world][adr1930]` pins the canyon mesh's hash (positions, normals, uvs, indices, river), with and
    without the portal approach. The values were taken from the in-file implementation before the move.

## Context

CHRONOS (`docs/prototypes/chronos/`) needs a height-field canyon. Strange Attractor's mesher is the part of that scene
which earned its keep:
- error-bounded, with a tolerance that varies in space;
- crack-free by level-order error propagation (ADR-1857, ADR-1867).

A second copy would drift from the first the way the depth-first propagation once did.

## Decision

Move the scene-independent core into `scene::rtin`, unchanged in behaviour:
- the grid;
- the level-order propagation with its atomic max;
- the bintree extraction;
- the centroid chunk partition;
- the upward winding.

The caller keeps everything that is about its surface:
- **What it supplies:** `localError(ax, ay, bx, by, mx, my)`, already divided by its tolerance. That is where the
  attribute-aware band error and the portal's forced splits live.
- **What it builds:** the chunk contents (vertices, normals, attributes).

The propagation is a header template, so the canyon's local error is still inlined at roughly 2^29 calls at 1 m
sampling, with no `std::function` overhead.

## Consequences

- **No picture change.** The pinned mesh hashes match bit for bit. Strange Attractor's flythrough, rendered before
  and after (0-32 s, 960 frames, 960x540), is compared in `docs/prototypes/chronos/04-gate1.md`.
- `attractor_world.cpp` loses about 70 lines. Art Pass 2's edits to the canyon's sampling, tolerance or attributes
  are untouched by this move. Only edits inside the old propagation or extraction loops would conflict.
