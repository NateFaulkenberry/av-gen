# ADR-2250: a shadow ray aims from its offset origin at the target, so an emitter does not shadow itself

- Status: Accepted (2026-11-26), eng/hybrid-rt (Hybrid RT H2; found while validating ADR-2247)
- Code: `pathtrace/path_tracer.cpp` (`visible`)
- Tests: `tests/unit/test_pathtrace_materials.cpp` (`[adr2250]`)

## Context

This predates H2 (ADR-351 Phase 3).

- **The bug.** `visible` offset the origin along the normal but kept the direction from the un-offset hit. The ray
  then misses the target by the offset. On a target surface not perpendicular to the ray, it meets the target's own
  plane before `tfar`.
- **Why distance made it worse.** The offset is relative to the hit's distance from the camera, so the loss grew with
  distance and grazing angle.
- **Measured.**
  - From a floor point 5.7 m away at a grazing view, 5,718 of 20,000 shadow rays were blocked by the quad they aimed
    at.
  - Light sampling read 13% dark there, while BSDF sampling (no shadow ray) and a brute-force hemisphere integral
    agreed with each other.
  - Over a whole image of small emitters, light sampling and MIS read 29% dark.
- **Why the tests missed it.** The existing MIS tests used one large emitter close to the camera.

## Decision

Aim from the offset origin at the target point, at the target's distance less the same relative shortfall. The
regression test checks that light-only, BSDF-only and MIS agree within 2% at a point seen from above and at a grazing
5.7 m view.
