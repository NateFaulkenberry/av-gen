# ADR-2243: the path tracer accounts for every scene member and material field, and reports what it drops

- Status: Accepted (2026-11-26), eng/hybrid-rt (Hybrid RT H1; audit defects D6, D19)
- Code: `pathtrace/coverage.{hpp,cpp}` (new), `pathtrace/snapshot.cpp`
- Tests: `tests/unit/test_pathtrace_coverage.cpp` (`[coverage]`)

## Context

The capability report listed what somebody had remembered. Neon Dreams' clouds (the floor and horizon), 35
material programs (every window), Stars, Shockwave pulses and the effect-light pool were not on it, so the trace
dropped them without a word.

## Decision

1. **A coverage table.** One row per `scene::Scene` data member and per `scene::Material` field: traced, degraded,
   unsupported or not applicable, with the reason.
2. **A test that reads the source.** It reads `src/scene/scene.hpp` and `src/scene/scene_types.hpp` from disk and
   fails when a member or field has no row, or a row names nothing. It has controls that the parser found the
   members it must.
3. **Reported rows.** `buildSnapshot` reports, with counts, every unsupported feature the frame contains:
   - clouds;
   - the ecosystem and astral environments;
   - mesh wire, corruption, portals;
   - temporal effects, waves, atmospherics, distortion;
   - Shockwave pulses, effect lights, entity effect lanes;
   - ribbons, Stars, shells;
   - material programs, transparency and alpha masks, normal maps;
   - toon, wire and refractive materials, and surface regions.
4. **A renderer caveat** for the pinhole camera (no near plane, depth of field or motion blur) and scene-linear output.

## Consequences

A new scene feature cannot reach the tracer undecided. The report is the input the hybrid compositor (H3) and the
API's warnings (H6) are built on.
