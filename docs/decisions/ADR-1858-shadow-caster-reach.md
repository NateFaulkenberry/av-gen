# ADR-1858: A cascade's caster reach toward a low sun

- Status: Proposed, scene/strange-attractor (canyon Phase 1b, 2026-10-27).
- Code:
  - `src/scene/scene_types.hpp`: `Environment::shadowCasterReach` (default 0).
  - `src/scene/composition.cpp`: the `environment.shadowCasterReach` key. It is parsed, and saved only when above 0.
  - `src/rendering/shadow_math.{hpp,cpp}`: the `minPullBack` argument of `fitDirectionalCascade`.
  - `src/rendering/shadow_renderer.{hpp,cpp}` and `src/rendering/scene_renderer.cpp`: the argument is passed through.
- Tests: `[cascades]` in `tests/unit/test_shadow_cascades.cpp`. The new case checks two things:
  - The default 0 gives the old fit, bit for bit.
  - The reach brings a caster 3 km up-sun inside the near cascade's depth range, and it is capped at the caster
    distance.
- Evidence: `docs/prototypes/strange-attractor/05-canyon-phase1b.md` (flicker trace).

## Context

`fitDirectionalCascade` places each cascade's light-space eye back along the sun direction by
`clamp(casterDistance, radius * 0.5, radius * 3)`, where `radius` is that cascade's bounding-sphere radius. A near
cascade is tens of metres across, so it only sees occluders within about a hundred metres up-sun.

Under a 9 degree sun in the Strange Attractor canyon, the shadow on a slab comes from a rim more than a kilometre away:
- In the far cascade, the slab is in shadow.
- When the camera approaches and the slab moves into a nearer cascade, the rim drops out of that cascade's volume and
  the slab turns lit in a single frame.

The trace found this was the cause of the 24.17 s spike in the flythrough (6.3x the frame-difference baseline), and of
those at 25.67 and 26.03 s. The spikes vanish with shadows off. Changing cascade count or bias does not move them, and
caster screen-size culling was already off.

## Decision

`Environment` gains `shadowCasterReach`, in metres. Every directional cascade reaches back toward the sun by at least
`min(shadowCasterReach, casterDistance)`. 0 is the default and changes nothing.

The cost is depth precision. A deeper volume spreads the same depth texture over more metres, so the scene that uses
the key raises its shadow bias to match. The Strange Attractor uses 4000 m with bias 8.

## Consequences

- Full 46 s flythrough at 960x540: frame-difference irregularity max went 5.34 -> 1.47, and p90 went 0.448 -> 0.289.
  What remained was the dotted checker that screen-space contact shadows draw on lit walls, a scene setting: with
  them off, max 0.27 and p90 0.042. (Shortening the river's SSR reach was tried alongside and turned out to make no
  difference. It is back at 1200 m, because the short reach lost the lit walls' reflections.)
- Nothing else sets the key, so every other scene's shadows are unchanged. The unit test pins this.
