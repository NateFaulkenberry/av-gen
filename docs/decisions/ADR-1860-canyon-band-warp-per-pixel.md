# ADR-1860: The canyon mesh carries the unwarped strata coordinate; the program warps it per pixel

- Status: Proposed, scene/strange-attractor (canyon Phase 1c, 2026-10-28).
- Code:
  - `src/scene/attractor_world.{hpp,cpp}`: uv.x is now `(height + strata offset) / band`.
  - `examples/strange-attractor/build_rock_program.py`: writes the `canyonRock` program, which applies `bandWarp`
    with a `palette` op (two sines) before quantising the bands.
- Tests: `[adr1860]` in `tests/unit/test_attractor_world.cpp`.
- Evidence: `docs/prototypes/strange-attractor/06-canyon-phase1c.md`.

## Context

The owner saw vertical streaks on the near steep face in the opening shot. Debug renders traced the cause. These were
ruled out:
- Shadows: the streaks are still there with shadows off.
- Vertex normals: rendered as colour, they are smooth.
- The baked occlusion: smooth.
- Triangle size: capping the longest triangle edge at 4 m did not remove them.

The cause is the band coordinate. Rendered as colour, its band boundaries on that face are a sawtooth.
- uv.x carried the *warped* coordinate, `bandWarp(h + offset) / band`.
- The warp is a sum of two sines in the strata coordinate, so it bends within a triangle that drops several bands down
  a cliff.
- Linear interpolation of the warped value then zigzags the band lines from triangle to triangle. The colour, the
  bedding planes and the ledge normals all follow that zigzag.

## Decision

- **uv.x carries the coordinate before the warp.** On a wall this is close to linear in height, so interpolating it
  is close to exact.
- **The program applies the warp per pixel.** The `palette` op gives `0.32 sin(S/2.3 + 0.7)` and
  `0.18 sin(S/0.93 + 1.7)` in two lanes, and the program sums them. The band thickness cancels, so the program needs no
  constant per seed.
- **The RTIN's attribute error term now measures the unwarped coordinate.** The triangle count barely moves, from
  8.27M to 8.27M.

## Consequences

- The unit test checks every triangle taller than four bands. At its centroid, it compares the warp of what the
  rasteriser interpolates with the warp of the true coordinate there (from the baked offset). Mean error is 0.035
  bands now, against 0.170 for the old warped attribute.
- The streaks are gone from the opening (`canyon-p1c/01-*`).
- The ray-march path never read uv, so it is unaffected. It is removed in Phase 2 anyway.
