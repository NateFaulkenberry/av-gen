# ADR-2121: 24 cloud beam arms

- Status: Accepted (2026-11-21), avagent/neon-dreams (Neon Dreams, iteration 12; owner msg 149: a band stage with
  spotlights whose beams show in the air)
- Code: `kMaxCloudBeamArms` (`src/scene/clouds.hpp`); `beams` in `shaders/clouds.wgsl` and `shaders/clouds_resolve.wgsl`;
  the per-arm air-scatter arrays in `fs_clouds`.
- Tests: `tests/unit/test_clouds.cpp` (`[adr2102]`'s refusals now at 25 arms).

## Decision

The cloud block holds 24 beam arms (was 8): Neon Dreams' three lighthouses (6 arms), the stage's 8 floor spots as short
wide beams and its 4 halo searchlights (18 in all). The uniform grows by 48 vec4s; the resolve shader's copy of the
struct grows with it (its later fields' offsets depend on the array).

## Consequences

- Each arm adds a closest-approach air term per pixel and a cone test per lit sample; arms past their length or off
  the ray cost a few instructions. Off-stage arms are cheap.
