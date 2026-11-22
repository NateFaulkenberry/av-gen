# ADR-2122: 64 material programs on the GPU

- Status: Accepted (2026-11-22), avagent/neon-dreams (Neon Dreams, iteration 12; found by the art agent's
  `avgen_cast_trace` on owner msg 170's build: 36 `program-not-uploaded` warnings)
- Code: `kMaxGpuMaterialPrograms` (`src/rendering/material_programs.hpp`), `MAT_MAX_PROGRAMS` and the block's array
  (`shaders/material.wgsl`), `kGpuProgramSlots` (`src/scene/route_liveness.cpp`).
- Tests: `tests/rendering/test_material_gpu.cpp` (`[adr1675]`: the 33rd and 64th now render, the 65th falls back),
  `tests/unit/test_route_liveness.cpp`, `tests/unit/test_glowmere_tree_fireflies.cpp`.

## Context

Neon Dreams reached 35 material programs (the stage and the Lone Lamp's added roles). Programs 33-35 -- the town's
eave strips (`ndStrip_eave`) and the landmark crowns and sign strips (`ndStrip_sign_a`, `ndStrip_sign_c`) -- were never
uploaded: those surfaces drew with their plain material and every route and track on them (the kick ring, the Moog's
pulse, the crowns on the bass) was dead. The liveness check said so; nothing failed.

## Decision

The table holds 64 programs (was 32). The block grows to 16 + 64 x 2,872 bytes (~184 KB, a read-only storage
buffer); only the header and the programs in use are uploaded each change (ADR-1675), so a scene with few programs
pays nothing more.

## Consequences

- The 65th program still falls back to its material with one log line and a `program-not-uploaded` liveness verdict.
