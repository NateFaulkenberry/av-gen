# ADR-2106: Cloud detail erosion in metres; squared, jittered light taps; multiple scattering

- Status: Accepted (2026-11-19), avagent/neon-dreams (Neon Dreams, iteration 7)
- Extends: ADR-1289 (sculptural lobes), ADR-2100 (Cloud Environment v2), ADR-2105.
- Code: `shaders/clouds.wgsl` (`erosionAt`, `lobeGrid`, the key's light in `fs_clouds`), `src/scene/clouds.{hpp,cpp}`
  (fields, validation, JSON, live knobs), `src/rendering/cloud_renderer.cpp` (`erosion`, `erosion2` uniforms).
- Tests: `[adr2106]` in `avgen_tests` (read, validate, round-trip, live knobs) and `avgen_render_tests` (erosion
  scallops a lobe's round silhouette and never goes past it, the same from two fresh renderers; six squared light
  taps land within 0.8/255 of a 32-tap reference where two even taps miss by 20/255).

## Context

Neon Dreams iteration 6, as the Orchestrator judged it: (a) every formation read as a pile of smooth spheres. A
lobe's edge was a smoothstep over a share of its radius (`soft`), and the shape and detail noises only remapped the
density inside that edge. At extinction 0.15 the visible surface is a few metres deep, so the outline stayed the
ellipsoid's. (b) Hard-edged, near-black patches in lit cloud (2.8 s and 17 s). Cause, measured by changing nothing
but `lightSteps`: two even taps of 130 m toward a low key. A sample reads the lobe 65 m and 195 m away; whether a tap
lands inside a lobe or steps over it is all or nothing, so each lobe's silhouette was cast on the cloud below it as a
black stencil, and where the taps missed, the crowns read lighter than they should. At 8 even taps the patches were
gone.

## Decision

1. **Erosion in metres** (`erosion` 0..1, off by default). Per sample, up to three octaves of billow noise from the
   shared 64^3 volume: coarse (wavelength `erosionScale`, inverted Worley with some Perlin-Worley), mid (1/3.3) and
   fine (1/10). The coarse two are moved by a curl-like offset of `erosionCurl` metres for tatters. Each lobe's surface
   is eaten inward by `min(0.38 r, 0.6 L)(1 - c) + min(0.3 r, 0.18 L)(1 - m) d + min(0.2 r, 0.06 L)(1 - f) d`, where
   `r` is the lobe's smallest semi-axis, `L` the wavelength and `d` = `erosionDetail`. Each octave's depth follows the
   lobe's size, so a 15 m head is broken by the fine octaves and a 200 m tower by the coarse one. The erosion only
   goes inward, because the bins hold the lobe's own bounds.
2. **Height.** Over a lobe's top half the noise is used as is (rounded heads: cauliflower). Under its middle it is
   inverted (wisps), the lower half is squashed by `erosionFlatten` (flatter bases), and the edge is wider: from
   0.07 L at the crown to 0.28 L at the base. As `erosion` rises the edge replaces the authored `soft`.
3. **Level of detail.** The fine octave fades out as the larger of a pixel's footprint and half the march step
   approaches its wavelength, and the mid octave fades out more slowly. The edge is never narrower than that
   footprint. Without this the edges of far towers read as fur in a played render.
4. **Seek-safe motion.** Each octave is offset by a different fraction of the drift offset (0.35 for the coarse two,
   +0.25 for the mid, 0.6 for the fine), and the drift offset is a pure function of the timeline second. The billows
   evolve slowly instead of sliding as one sheet.
5. **The key's light.** With `lightSpacing` 2, tap j covers [D((j)/n)^2, D((j+1)/n)^2], sampled at a point jittered
   per frame (not per pixel, which left a screen-door pattern). Taps are dense near the sample, where a billow shades
   its neighbour, and sparse further out. Each tap reads the eroded field (two octaves). `multiScatter` adds two
   Wrenninge-style octaves (`0.5 m e^{-0.35 tau}` with a softer phase, `0.25 m e^{-0.12 tau}`), so a body in its own
   shadow is dim but not black.
6. Live knobs: `clouds/erosion`, `clouds/erosionScale`, `clouds/erosionDetail`, `clouds/erosionCurl` and
   `clouds/multiScatter`. `erosionOctaves` (1..3) is the offline-quality knob.

## Consequences

- Off by default. A scene without these keys draws as before, except that the light march now evaluates
  `densityBase` with no erosion, which gives the same result.
- Erosion shrinks a lobe by about half its amplitude on average. Lobes authored to touch may part, so the art
  re-checks hiding cloud (the Neon Dreams plug).
- Cost (Neon Dreams, 1080p offline, steady state, 60 frames at 15-17 s): 67 ms/frame before, 90 ms with erosion
  (3 octaves) and 6 squared light taps. At 49-51 s (inside the Veil): 70 to 90 ms.
