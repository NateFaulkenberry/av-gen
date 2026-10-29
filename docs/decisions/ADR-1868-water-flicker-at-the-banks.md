# ADR-1868: Water flicker at the banks: the reflection is filtered to the pixel; the attractor's water block reads every key

- Status: Proposed, scene/strange-attractor (canyon water flicker, 2026-10-29).
- Code:
  - `shaders/water.wgsl`, the reflection block:
    - `reflectSpread`: the reflected ray's spread across a pixel;
    - `rippleAlias`: how much of that spread the ripples cause;
    - the cirrus lookup's normal flattens fully where `rippleAlias` is high;
    - `waterSsr` blurs its hit by the spread.
  - `src/scene/attractor_world.cpp`: the `water` block accepts every float `WaterSettings` key the scene reader knows
    and refuses unknown keys.
  - `examples/strange-attractor/strange-attractor-flythrough.scene.json`: `water.foam` 0.
- Tests:
  - `[adr1868]` in `tests/rendering/test_water_cirrus_gpu.cpp`: rippled water under cirrus with the camera gliding
    sub-pixel. Luminance sign reversals drop from 2,658 to 595 against the same shader with the filter removed (the
    control is derived from the live file).
  - `[adr1868]` in `tests/unit/test_attractor_world.cpp`: the water keys.
- Evidence: `docs/prototypes/strange-attractor/10-canyon-shadows-2.md` § water.

## Context

The owner saw water flicker along the banks.

**Measure.** Brighten-then-darken flips per frame among water pixels within 4 px of rock. The water mask comes from a
same-camera render with the river painted flat green.

**Worst moments:** 5.1, 14.2, 17.1, 20.5 and 21.2 s.

**Frozen camera.** Bank flips were 0.00–0.01%, so the flicker is entirely view-dependent: sampling, not noise.

**Floor.** The flat-green render itself flips 4.8% in the bank band, because the waterline is a geometric edge
stair-stepping as the camera moves. A 10× finer near plane did not move it, so it is not depth precision. The 2x
supersampled final reduces it.

**Bisect** (bank flips over the floor, on the worst windows):

| Variant | Result |
|---|---|
| Sky mirror off | No change |
| Specular off | No change |
| SSR off | Worse at 20.7 s: the SSR's rock reflection covers sky that would otherwise flicker |
| ADR-1865 water cirrus off | 16.6 s: 1.8 → 0 points; 20.7 s: 4.1 → 2.0 |
| Ripples off | Most of the rest |
| `foam` 0 | 16.6 s: 6.4 → 2.5%, **but only once the attractor's water block honoured the key** |

The foam result exposed a defect: the block read fifteen water keys and dropped every other one in silence. `foam`,
`reflectionRays` and similar keys did nothing, so the river carried the engine's default 0.55 foam band. That band is
0.42 m of depth, which far away is a one-pixel bright line hugging every bank and crawling in dashes.

## Decision

- **Our term, fixed at its cause.** ADR-1865's reflected cirrus looked the streaks up along a ray the ripples swing
  across a streak within a pixel. The water now measures the reflected ray's spread across the pixel (`fwidth` of the
  reflection direction) against what a flat mirror's spread would be. Where the ripples dominate it, the cirrus sees a
  flat mirror. The SSR hit is blurred by the same spread, so its colour is the average of what the pixel's rays see.
- **The silent water keys.** The attractor's water block now takes every float water key and refuses unknown ones.
- **The canyon's river has no foam.** Glassy water was always the direction (Gate 2d); the foam was never authored,
  only defaulted.
- **Tried and dropped:**
  - widening the shoreline fade and foam bands to two pixels of their depth gradient (no measurable change);
  - more reflection rays (4: worse at 20.7 s, and four times the SSR cost).

## Consequences

- Full 46 s flight, bank flips: 6.48% → 5.33%, against the 4.77% geometric floor. The excess over the floor fell by
  two thirds.
- Open-water flips: 2.85% → 2.17%.
- Cost: two `fwidth`s and a reflect per water pixel, plus one blur radius term. The measured GPU time is in the
  evidence.
- Every water under a cirrus sky gets the filter. Water elsewhere is unchanged apart from the SSR blur term, which is
  zero on a flat surface.
