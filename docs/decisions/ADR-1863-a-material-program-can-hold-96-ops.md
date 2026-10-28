# ADR-1863: A material program can hold 96 ops

- Status: Proposed, scene/strange-attractor (canyon Phase 2, 2026-10-28). Extends ADR-1661 (48 to 64) and ADR-036.
- Code:
  - `src/scene/material_program.hpp`: `kMaxMaterialOps` 64 → 96. `MaterialProgramGpu` grows from 7,536 to 11,120
    bytes; its `static_assert` is parametric.
  - `shaders/material.wgsl`: `ops: array<MaterialOpGpu, 96>` and `MAT_MAX_OPS` 96.
  - `docs/procedural-materials.md`: the budget.
- Tests: `[adr1863]` in `tests/unit/test_material_program.cpp` checks that 96 ops validate, 97 are refused, and the
  size is right. ADR-1661's golden 48-op pack, with its zero tail, still holds. The layout guard re-derives the WGSL
  twin.

## Context

The canyon's rock program (`examples/strange-attractor/build_rock_program.py`) needed 63 ops in Phase 1c:
- band warp per pixel (ADR-1860);
- band colour;
- baked occlusion (ADR-1859);
- ledge, lamination and relief normals.

Phase 2's contour tracing (Mode A) and wall glow add 22 more. The engine already compiles programs straight-line per
scene (ADR-1493), so a program costs its own ops, not the ceiling. The ceiling is a validation limit plus the length
of the packed array.

## Decision

Raise the shared budget to 96. No op, register or packing rule changes. Existing programs pack to the bytes they did,
followed by zeroed slots.

## Consequences

- The 32-slot program block grows by about 114 KB of storage on the GPU.
- At 85 ops the canyon program costs about 10 ms more in the scene pass than at 63 (45.0 against 35.2 ms at 1080p
  ultra). The cost is the ops themselves, not the ceiling. The Phase 2 notes list how to recover it: a gated layer, or
  contours only within the fade distance.

## Rejected

- **Contours in a second material on the same chunks:** a second draw of 8M triangles costs more than the ops.
- **Contours as mesh-wire segments:** iso-lines are not edges. Each triangle would have to emit a variable number of
  segments.
