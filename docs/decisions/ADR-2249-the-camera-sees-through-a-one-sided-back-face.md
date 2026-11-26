# ADR-2249: a camera ray passes through a one-sided back face, as the raster culls it; light rays see both faces

- Status: Accepted (2026-11-26), eng/hybrid-rt (Hybrid RT H2; the coordinator's ruling on the H2 plan)
- Code: `pathtrace/path_tracer.cpp` (the camera ray's intersection loop), `pathtrace/coverage.cpp` (`doubleSided`)
- Tests: `tests/unit/test_pathtrace_materials.cpp` (`[adr2249]`)

## Decision

The raster draws a material that is not `doubleSided` with `CullMode::Back`. Front faces are CCW, which matches the
tracer's `backface`, taken from the triangle's own winding.

- **Camera rays.** A camera ray that hits such a back face continues past it (`tnear` beyond the hit, at most 64
  times). The traced frame then shows what the raster shows.
- **Every other ray.** Shadow, bounce and light rays still see both faces. Light transport through a culled face is
  not the raster's decision.
- **Recorded.** The coverage table lists this as a deliberate difference.
