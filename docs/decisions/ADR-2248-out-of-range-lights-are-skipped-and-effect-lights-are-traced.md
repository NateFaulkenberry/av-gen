# ADR-2248: out-of-range lights are skipped without changing a pixel, and the effect-light pool is traced

- Status: Accepted (2026-11-26), eng/hybrid-rt (Hybrid RT H2; audit defect D12 part; the effect-light row of D6)
- Code: `pathtrace/lights.{hpp,cpp}` (`lightOutOfRange`), `pathtrace/path_tracer.cpp`, `pathtrace/snapshot.cpp`
- Tests: `tests/unit/test_pathtrace_materials.cpp` (`[adr2248]`)

## Context

The plan proposed power-based selection of one light per shading point. A profile of the H1 build at Neon Dreams
t = 45 put the per-light loop at about 25–30% of busy CPU (`sampleLight` 21%), with Embree at about 40%. One-light
selection could save at most that share, and it adds variance. The 2x the plan claimed cannot come from it. Most of
the 222 point lights are range-limited, so most are out of range at any given point.

## Decision

1. **`lightOutOfRange` skips a point or spot light whose range window is zero at the point.**
   - `sampleLight` would have returned exactly that light invalid. A property test of 200,000 random and boundary
     cases holds this.
   - The light's random numbers are still drawn, so no other sample moves and the image is unchanged.
   - Many-light importance sampling is deferred until a measurement asks for it.
2. **The effect-light pool (ADR-703) is appended as `SceneRenderer::updateLights` appends it.**
   - Unshadowed point lights, range floored at 0.01, after the scene's lights.
   - Appended only while the raster's 256-light list has room.
   - The tracer links no renderer, so the 256 cap is mirrored, and a test `static_assert`s it against
     `rendering::kMaxSceneLights`.

## Consequences

Neon Dreams at 960x540, 8 spp renders in 0.3–2.8 s, down from 0.4–3.6 s with H1. Glow spill lights now light the
trace.
