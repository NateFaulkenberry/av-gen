---
id: rendering/overview
title: How a Frame Is Built
category: Rendering
summary: The order the renderer works in, where HDR ends, and which pass does what.
order: 50
tags: rendering, frame, passes, hdr, pipeline
keywords: how does rendering work; what are the passes; render order; hdr; what does tonemapping do; what does this rendering setting do; what do the render settings mean; quality settings
related: rendering/exposure-and-tonemapping, rendering/emission-and-bloom, rendering/lights, performance/what-costs-what
features: subsystem.rendering
---

# How a Frame Is Built

Every frame runs the same passes in the same order. Knowing the order is what makes the rest of the
rendering documentation make sense — particularly which controls act before tone mapping and which
act after.

## The order

1. **Background shader layers** — any user shader you added as a background.
2. **Cluster build** — the lighting grid.
3. **Simulation, particles, procedural culling** — compute work.
4. **Shadow passes** — one per shadow view.
5. **Background** — clears the HDR target and draws background layers into it.
6. **Depth prepass** and **linear depth**.
7. **Ambient occlusion** and its temporal filter.
8. **Shadow mask** — the screen-space shadow term, at reduced resolution.
9. **Scene** — the lit pass. Writes five targets: HDR colour, normal and roughness, velocity,
   emission, and object ids.
10. **SDF raymarch**, then the scene pass resumes.
11. **Volumetrics** — marched at half resolution, then composited.
12. **Debug draw.**
13. **Post shader layers** — any user shader you added as a post effect.
14. **The built-in post chain** — exposure, depth of field, motion blur, lens, bloom, halation,
    anamorphic, composite, FXAA, sharpen.
15. **Tone map** — and with it exposure multiply, vignette, grain and the sRGB encode.
16. **The 2D composition** — text and shape layers over the finished frame.

Everything from step 5 to step 14 is **scene-linear HDR**, in a 16-bit float target. Tone mapping
is the last thing before the overlay.

Two consequences worth holding on to:

- **Anything in the post chain works in exposed HDR units**, not in 0–1. A bloom threshold of 1.0
  means "brighter than a fully lit white surface", not "at the top of the slider".
- **An EXR render is written before tone mapping**, which is why composition layers do not appear
  in one.

## Quality tiers

`--tier preview|realtime|high|offline` scales shadow resolution, cascade count, shadow filter taps,
soft shadows, contact-shadow steps, the shadow mask's resolution, ambient occlusion and SDF shadow
steps together. The default is `realtime`.

| | shadow map | cascades | soft shadows | shadow mask |
|---|---|---|---|---|
| preview | 1024 | 2 | off | half resolution |
| realtime | 2048 | 3 | on | half resolution |
| high | 2048 | 4 | on | full resolution |
| offline | 4096 | 4 | on | full resolution |

> [!WARNING]
> An offline render does **not** raise the tier on its own. A render started from the Render panel
> or with `--render` uses whatever tier the run was started with, which is `realtime` unless you
> passed `--tier offline`.

## Turning phases off

`--disable shadows,ao,volume,post,shadowmask` switches phases off for cost attribution. The run
logs which arm it is in, so two halves of an A/B can never be confused afterwards. See
[Diagnosing performance](help://performance/diagnosis).
