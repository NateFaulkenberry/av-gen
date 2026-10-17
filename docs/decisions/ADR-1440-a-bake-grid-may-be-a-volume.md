# ADR-1440: A bake grid may be a volume

- Status: Accepted (2026-10-17), proto/waveform-dimension (Waveform Dimension, Stage B)
- Code: `spatial::GridField::validate` (`src/spatial/grid_field.cpp`). A bake grid is a plane `[x, 1, z]` with
  x, z <= 1024 (ADR-1407), **or a volume** with every axis <= `kMaxGridResolution` (128). The header comment in
  `grid_field.hpp` says so too. No shader changed: `cs_bake`, the CPU `step`, `simCellCenter` and `gridTrilinear`
  were already three-dimensional.
- Tests:
  - `tests/unit/test_grid_field.cpp`, "A volume bake grid holds a 3D field at every cell centre" (the limits, and the
    CPU bake of a field that varies in y).
  - `tests/rendering/test_simulation_gpu.cpp`, "A volume bake grid holds a 3D field at every cell, as the CPU bakes
    it" (the GPU's 32 x 24 x 16 bake equals the CPU's to 1e-3, and its layers differ).

## Context

Waveform Dimension's sculpture is a set of nested sheets displaced along their normals by a three-dimensional resonance
field (ADR-1400). Its four readers sum low-mid plane waves heard in three turned frames, plus the bass lobes. On
256k vertices, the Field deformer evaluated every audible mode of every reader:
- once for the position;
- three more times for the finite-difference normal;
- again in the depth prepass.

That cost 16.6 ms per geometry pass at 1080p on the M2 Max. The material's per-pixel excitation reader added 9 ms more.

ADR-1407's bake already turns a field into a grid once a frame. But it refused anything but a plane, and Cymatic World's
ground needed only a plane. A plane bake of this field would flatten its vertical structure. That structure is the
whole point of a 3D field.

The Stage A sphere-traced prototype measured the same thing more sharply. Evaluating the field at every march step
cost 550 ms a frame. With the field baked to two 128 x 64 x 128 volumes, the march dropped to 209 ms and then 165 ms
(the march itself stayed too costly for that prototype, which was not chosen).

## Decision

`mode: "bake"` accepts a volume. The plane rule and its larger limit stay for planes.

## Consequences

- Cost of one 128 x 64 x 128 volume (1,048,576 cells), M2 Max:
  - The bake: about 1.05 ms a frame for a four-reader compound. It was 2.1 ms for two such grids in the Stage A
    profile (the `sim` pass).
  - Memory: 4 MiB of the 16 MiB grid table (`kMaxGridTableFloats`). Two volumes plus a coarse 96 x 48 x 96 one fit
    with room to spare.
  - Bandwidth: 4 MiB written a frame. A reader then pays a trilinear gather (8 floats) per sample instead of a loop
    over the modes.
- Everything that samples the grid is stateless and exact under seek, as ADR-1407 bakes are. One bake a frame at the
  frame's own clock, with no catch-up.
- Detail is capped at the cell (about 2 m over the sculpture's 260 x 160 x 260 m). The field's shortest geometric
  wavelength is 7 m, so the finest family is three or four cells a wavelength. Normals taken across a cell are smooth
  (trilinear) but carry faint cell-scale facets on close, flat surfaces.
- Plane bakes and their tests are unchanged, except that the unit test's "not a plane" refusal now refuses a grid
  that is neither a plane nor a volume within the limits.
