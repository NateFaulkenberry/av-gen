# ADR-1650: The water reflects the sky the frame drew

- Status: Accepted (eng/aurora-borealis)
- Extends ADR-1286 (the cinematic water path and its frame copy) and ADR-1603 (the refraction copy and the ladder's
  `waterSsrScale`). Follows ADR-916's, ADR-1642's and ADR-1643's rule: optional code is compiled out of every pipeline
  that does not draw it.
- Asked for by Aurora Borealis (docs/prototypes/aurora-borealis/, 01-research-scene.md §3.3, §4.3, §6 E1; 03-plan.md):
  the river must reflect the aurora overhead. Neither existing reflection path can:
  - the SSR marches against linear depth, and the sky's depth is 1e7, so no step ever lands behind it;
  - the environment cube is the procedural sky, with no aurora, comet or anything else the sky layer draws.
- Code:
  - `src/scene/water_surface.{hpp,cpp}`: `WaterSettings::skyMirror` (0..1, default 0), validated; `cinematic()`
    includes it.
  - `src/rendering/water_renderer.{hpp,cpp}`:
    - `WaterUniforms::mirror` (416 bytes, was 400, within the 512-byte stride);
    - a fifth pipeline, `water-cinematic-mirror`, built from the full source; the four existing pipelines compile the
      sky-mirror blocks out;
    - per-slot selection, `mirror_[i]` (with or without streaks: their code is gated on their amount).
  - `src/scene/composition.{hpp,cpp}`: the `water.skyMirror` key, written only when authored; the routable parameter
    `nodes/<terrain>/water/skyMirror`.
  - `shaders/water.wgsl`, in `sky mirror (ADR-1650)` blocks: the uniform and the lookup.
  - `docs/water.md`, "Sky mirror".
- Tests:
  - `tests/rendering/test_water_sky_mirror_gpu.cpp` (`[adr1650]`):
    - "the sky mirror packs into its own lane, untouched by the SSR lever";
    - "with the sky mirror at 0 the water is byte-identical to the shader with no sky-mirror code" (plain, cinematic
      and streaks paths; the control: on, it is not);
    - "the sky mirror puts the aurora overhead in the river, with the water's SSR on and off": ribbons overhead, the
      river's added light with the mirror is over 4% of what the aurora adds to the sky, over 20x what it adds without
      the mirror (measured: exactly 0), and in the aurora's colour (each channel's share within 0.12 of the sky's), with
      SSR 16x1 and with the ladder's `waterSsrScale` 0;
    - "the sky mirror adds nothing where its lookups land off screen" (camera pitched down: no pixel moves by 1e-3);
    - hidden `[measure]`: "water sky mirror: cost (measure)".
  - `tests/unit/test_water_cinematic.cpp`: defaults, cinematic switch, round-trip, written only when authored, range.
  - `tests/unit/test_water_parameters.cpp`: the parameter reaches the surface.

## Decision

**Read the frame at the reflected direction's vanishing point.** The sky is at infinity, so what a reflected ray `r`
sees of it is drawn where `r` itself projects: `viewProj * vec4(r, 0)`. The copy of the frame the cinematic water
already takes (after the sky and the atmospheric layer, before the water) holds the rendered aurora there. One bilinear
tap, whatever the surface's height. Far ranges come out right too: at distance D the angular error is about 2h/D.

**Trusted only where it is right:**
- on screen, faded over the last 6% of each edge, so a ripple that throws the point off the frame eases back to the
  environment reflection instead of smearing the edge texel (the coordinate is also clamped);
- the ray points up (faded over r.y 0..0.04);
- what the copy shows there is far: the linear depth beyond 0.35-0.5 of `ssrDistance` (a near bank at the vanishing
  point is not what the ray sees; the march finds those);
- not from under the water.

**Placed before the march, at the march's gain.** The mirror replaces the environment reflection where trusted, at
`ssrGain` (the brightness of the SSR's own reads of the same copy). The march's hits win over it, and its far fade hands
over to it, so the two meet without a seam.

**Independent of the SSR.** It is not inside `waterSsr` (which returns at `ssr` 0): the ladder's Emergency rung
(`waterSsrScale` 0) keeps the aurora in the river.

**A fifth pipeline.** The sky-mirror code is in its own marked blocks, stripped from the plain, torn, cinematic and
streaks pipelines, so every existing water surface runs exactly the shader it ran. The full source draws a surface with
the mirror; streaks in it are gated by their amount, so one pipeline covers mirror with and without streaks.

## Evidence (M2 Max, AC power, Release)

**Unchanged by default:** the identity test, hash for hash, on all three paths.

**The aurora in the river:** a dark river under green-teal ribbons (`AVGEN_WATER_DUMP`): with the mirror, the river
takes teal light broken up by its ripples, in the sky's own hue; without, none at all.

| QA river, ribbons overhead, level camera, 320x200 | aurora's added light, mean RGB |
|---|---|
| in the sky | 0.279, 1.631, 1.389 |
| in the river, mirror, SSR 16x1 | 0.020, 0.114, 0.099 |
| in the river, mirror, SSR off (Emergency) | 0.022, 0.128, 0.111 |
| in the river, no mirror | 0, 0, 0 |

The river returns about 7% of the sky's light here: the QA water's fresnel and alpha, not the lookup.

**Cost** (hidden `[measure]`: the QA river at 1080p, level camera so about half the frame is water, ribbons overhead,
the scene label's median over 6 interleaved reps of 60 frames):

| | scene (ms) |
|---|---|
| SSR 16x1, mirror off | 14.84-14.88 |
| SSR 16x1, mirror on | 14.91-14.99 |
| SSR off (Emergency), mirror off | 14.44-14.49 |
| SSR off, mirror on | 14.52-14.57 |

**+0.06-0.1 ms** at 1080p over half a frame of water: one copy tap and one depth load per water pixel.

## Consequences

- The scene sets `skyMirror` on its river and gets the rendered aurora in the water, coherent by construction (same
  shapes, colours and frame), broken up by the ripples it already has.
- Framing matters: a water pixel's mirror point is as far above the horizon as the pixel is below it. With the horizon
  at or above the frame's centre, every water pixel's mirror is on screen. Pitched down, mirror points fall off the top
  and those pixels keep the cube.
- The mirror cannot reflect what is off screen above the frame, and a near bank in front of the sky at the vanishing
  point blocks it (the depth test hands those pixels to the cube or the march).
- `tintPatches` recolour the reflection after the mirror, so they tint the aurora's reflection too.

## Rejected

- **Twins plus a mirror flag on the sky layers** (01 §4.3 fallback): a second aurora march over the water pixels, about
  2.5-3.5 ms (estimate), and it loses the engine water's ripples, streaks and glints.
- **Making the march hit the sky** (a finite sky depth): every existing cinematic surface would change, and the march
  would still stop at its reach.
- **Blurring by roughness** (the 7-tap disc `waterSceneAt` offers): the cost target is one tap; the ripples already
  perturb `r`.

## Revisit triggers

- Visible smearing near an edge in a shot: widen the fade, or lower the trust where the ripple's normal varies fast.
- A scene wants the mirror on rough water: take the disc blur behind a roughness threshold.
