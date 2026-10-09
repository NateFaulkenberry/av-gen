# ADR-1290: Sixteen material program slots

- Status: Accepted (2026-10-09), proto/bio-valley (spec 07 Deliverable C)
- Code: `src/rendering/material_programs.hpp` (`kMaxGpuMaterialPrograms`), `shaders/material.wgsl`
  (`MaterialProgramBlock`, `MAT_MAX_PROGRAMS`), `src/scene/route_liveness.cpp` (`kGpuProgramSlots`).
- Tests: `tests/unit/test_route_liveness.cpp` (`program-not-uploaded` now fires at the seventeenth program),
  `tests/unit/test_glowmere_tree_fireflies.cpp`, and the GPU material and layout suites.

## Context

The valley's world uses eight surface programs: rock, crown, cap, falls, moss, ground, bulb and frond. The moon's
phase shading is a ninth. The GPU table held eight, so the ninth program was dropped with one log line. The moon
rendered as a flat disc, with nothing to say why in the picture.

## Decision

The table holds sixteen programs. The block is a read-only storage buffer (ADR-036), so its size of
16 + 16 × 5,728 bytes is well inside every backend's storage limit. The select buffer gains eight 256-byte regions.

## Consequences

- Scenes with eight or fewer programs are unchanged: the same slots and the same indices.
- The liveness rule `program-not-uploaded` now fires at the seventeenth program.
