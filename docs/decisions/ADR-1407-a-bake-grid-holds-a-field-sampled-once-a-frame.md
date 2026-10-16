# ADR-1407: A bake grid holds a field sampled once a frame

- Status: Accepted (2026-10-16), proto/cymatic-split (Cymatic World's shape-language overhaul, Stage 7)
- Code:
  - `spatial::GridMode::Bake` (`src/spatial/grid_field.*`: name `bake`, one component, a plane, needs `injectField`, the CPU step).
  - `cs_bake` (`shaders/simulate.wgsl`).
  - The scheduler in `src/rendering/simulation.cpp`: one bake a frame, its field block packed at the frame's own clock, no catch-up, no checkpoints.
- Tests:
  - `tests/unit/test_grid_field.cpp` ("A bake grid is a plane that names its field ...").
  - `tests/rendering/test_simulation_gpu.cpp` ("A bake grid holds its source field at the frame's clock, and a seek needs no catch-up").

## Context

Cymatic World's ground is shaped by three resonance readers, one per spatial scale (ADR-1405). Each is a loop over up to 34 mode and ripple rows. Every vertex evaluated all three, three times for its finite-difference normal, and then again in the depth prepass.

Fixing the ground mesh's anisotropic cells meant 2-4× the vertices. That cost 82-116 ms at High against a 33 ms budget, almost all of it per-vertex field evaluation.

The renderer already has a shared grid table that every field consumer samples (`FieldKind::Grid`, ADR-032). What it lacked was a grid whose content is simply a field sampled now.

## Decision

`mode: "bake"`: a plane grid [x, 1, z] whose cells hold its `injectField`'s scalar value at their centres.
- It is computed once a frame by `cs_bake`, with the field block packed at the frame's clock (not at a quantised sub-step).
- It has no state: no stepping backlog, no catch-up after a seek, no checkpoints. A seek lands exactly where a play would.
- The CPU `step` does the same, for CPU readers and tests.
- A scene samples it through a `grid` field like any simulated grid.

## Consequences

- Cymatic World bakes the summed relief (a compound of the three scale readers, their heights as strengths) and the three values its material reads (sand ridge, crest line, excitation). That is four 768² grids, 2.4M floats of the 4M table, 0.6-0.7 ms a frame.
- The ground's single deformer samples the relief grid, and its normal step is one cell (0.17 m), so the differences span cells rather than one bilinear facet.
- Per-vertex field cost is gone, and mesh density is now bounded by the rasterizer and the material, not by the field.
- The bake samples the field at one height (the plane's). A field with vertical structure loses it: Cymatic World's raised point sources and layered lattice are read on the plate, which is what they draw.
- Resolution caps detail at the cell size (here 0.17 m over ±64 m). Finer detail aliases into the grid, so the scene keeps its crests a few cells wide.
