# ADR-1651: The live ladder thins the aurora ribbons' planes

- Status: Accepted (eng/aurora-borealis)
- Extends ADR-1641 (the aurora ribbons, `ribbonSteps`) and the live ladder (ADR-1083, ADR-1094..1105, ADR-1603).
- Asked for by Aurora Borealis (01-research-scene.md §4.6, §6 E2; 03-plan.md): the ribbons are the scene's hero and its
  largest cost (+4.4 / 5.1 / 5.4 / 6.6 ms for 16 / 24 / 32 / 48 planes), and the ladder had no lever on them; it could
  only lower the resolution.
- Code:
  - `src/rendering/render_quality.hpp`: `QualitySettings::auroraStepScale` (1 = as authored).
  - `src/world/atmospherics.{hpp,cpp}`: `applyAuroraStepScale(AuroraGpu&, scale)`: a ribbon slot's planes x scale,
    rounded, at least 8; a curtain slot, or any slot at scale 1, untouched.
  - `src/rendering/scene_renderer.cpp`: applied to each aurora slot as the frame uniforms are filled; the quality arms
    `auroraplanes75` and `auroraplaneshalf`.
  - `src/app/interactive_resolution.{hpp,cpp}`: `LiveQualityRung::auroraStepScale`, a ceiling (`applyLiveRung`,
    `applyCeiling`); the three ladders; the priority ladder's `volumes` group; the ceiling levers and JSON key.
  - `src/rendering/quality_policy.hpp`: part of "a render is exactly as authored".
- Tests:
  - `tests/unit/test_interactive_resolution.cpp`: "the ladders thin the aurora ribbons' planes from Medium (Balanced)".
  - `tests/unit/test_aurora_audio_response.cpp`: "the aurora step scale thins only a ribbon slot's planes (ADR-1651)".
  - `tests/rendering/test_aurora_pulse_gpu.cpp`: "the aurora step scale thins the ribbons' planes and nothing else
    (ADR-1651)": the default is every plane; the arms set it; at 0.5 the ribbons differ and are exactly the frame of an
    aurora authored with 24 planes, with total light within 10%; a curtain aurora is byte-identical; hidden
    `[measure]`: "aurora ribbon pulse and step scale: cost (measure)".

## Decision

| ladder | Ultra | High | Medium | Low | Emergency |
|---|---|---|---|---|---|
| Balanced | 1 | 1 | **0.75** (48 -> 36) | **0.5** (24) | 0.5 |
| Resolution first | 1 | 1 | 1 | 0.75 | 0.5 |
| Effects first | 1 | 0.75 | 0.5 | 0.5 | 0.5 |

- A ceiling like every other rung field: never more planes than the base asked for.
- In the priority ladder it goes with `volumes` (a little: 0.75; fully: 0.5): the plane march is a sky volume.
- Applied in the renderer to the packed slot, so offline (scale 1) is untouched, and a scene without ribbons cannot
  tell it exists.
- **Not counted in `effectChanges`** (the controller's expected gain per step), for ADR-1640's reason: it is a gain
  only where a scene has ribbons. Counting it would promise every other scene's controller a saving it cannot have and
  change their ladders' behaviour. The research proposed counting it; the "no-op without ribbons" requirement wins.

## Evidence (M2 Max, AC power, Release)

Hidden `[measure]`, 1080p, mostly-sky frame, ribbons at 48 planes, the scene label over 8 interleaved reps of 100
frames. The runs were bimodal (clock states), so both the median and the fastest rep are given:

| | median (ms) | fastest (ms) |
|---|---|---|
| 48 planes | ~12.3 | 11.25 |
| 36 planes (0.75) | ~10.6 | 9.43 |
| 24 planes (0.5) | ~11.8 (bimodal) | 7.58 |

About -1.7 ms at 0.75 and up to -3.7 ms at 0.5 on a mostly-sky frame, before the resolution the rung also drops.

## Consequences

- The scene keeps 40-48 planes at Ultra and High and the ladder does the thinning; the upscaler hides the grain at
  Medium and below.
- A scene that authored few planes (16) is floored at 8 by Low.

## Revisit triggers

- Grain crawling at Medium in motion (01 §6 risk 4): keep 0.75 at Low on Balanced, or add E5's temporal jitter.
