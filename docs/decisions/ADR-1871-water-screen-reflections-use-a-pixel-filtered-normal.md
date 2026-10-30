# ADR-1871: The water's screen-space reflections use a pixel-filtered normal

- Status: Accepted by the owner (2026-10-30), scene/strange-attractor (canyon water pass 2, 2026-10-29).
- Code: `shaders/water.wgsl`, the reflection block.
  - `rippleKeep` scales the ripples' tilt down until the reflected ray swings by at most 1.5× the flat mirror's own
    spread from one pixel to the next.
  - `nScreen` is the normal with that tilt. The SSR march's rays and the sky mirror reflect about it.
  - The cube lookup and the cirrus lookup are unchanged (ADR-1865/1868 already filter the cirrus).
- Test: `[adr1871]` in `tests/rendering/test_water_cirrus_gpu.cpp`.
  - A dark wall standing on rippled water under a bright sky, rendered over a sub-pixel glide.
  - It counts luminance sign reversals against the same shader reflecting about the unfiltered normal. The control is
    derived from the live file.
  - Measured 1,060 → 651. Near the camera the ripples are many pixels long and the reflection rightly wobbles with
    them, in both arms.
- Evidence: `docs/prototypes/strange-attractor/11-canyon-water-2.md`.

## Context

The owner's main complaint about the canyon's opening (0.3–1.5 s) was a jagged, spiky fringe on the reflected bank in
the water: saw-teeth or icicles hanging off the reflected rock silhouette, redrawn every frame.

**Bisect** on the opening (frames 12–14):

| Variant | Teeth |
|---|---|
| SSR off | Gone, along with the bank's reflection |
| Ripples off | Gone, with a clean mirrored edge |
| Sky mirror off | No change |
| Edge fade 0 | No change |

So the teeth are the SSR march's reflection, bent by the ripples.

**Cause.** The march reads the frame itself, a picture whose pixels are the reflection's own. Seen from high above,
the ripples are a few pixels long, but each one tilts the normal enough to swing the reflected ray several reflected
pixels from its neighbour. Across a reflected silhouette, neighbouring water pixels then alternately find the rock and
the sky behind it, and the hit/miss pattern forms the teeth. The ripples move and the camera moves, so the teeth are
redrawn each frame.

ADR-1868 blurred the hit's colour by the reflected ray's spread, but not the hit/miss decision itself.

## Decision

The march and the sky mirror reflect about a normal whose ripple tilt is limited to what the reflection can resolve:

- `rippleKeep = clamp(0.5 · flatSpread / (spread − flatSpread), 0, 1)`;
- `nScreen = normalize(mix(up, n, rippleKeep))`.

Here `up` is the surface's own up (`cascadeUp` on a falls, set inside the cascade markers), and both spreads are ADR-1868's `fwidth` of the reflected
direction. Where the ripples are resolvable, `rippleKeep` is 1 and nothing changes. Where they would alias, the tilt
taken out survives as ADR-1868's blur of the hit. Below a pixel, that blur is what the ripples do to the picture.

## Consequences

- On the opening (0–2 s, 1080p, frames rendered from the scene), the non-blue teeth area inside the smoothed water
  edge falls from 207 to 97 px per frame, and its frame-to-frame toggles from 406 to 186. Crops show the saw-teeth gone
  and the reflected edge gently wavy.
- Near the camera, where the ripples are many pixels long, the reflection still wobbles with them.
- Two `fwidth`-derived scalars that already existed, and one `mix`: no measurable cost.
