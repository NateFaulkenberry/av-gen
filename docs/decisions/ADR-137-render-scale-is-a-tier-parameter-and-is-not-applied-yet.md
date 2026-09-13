# ADR-137: Render scale is a tier parameter, and the thing that stops it being applied is the tonemap binding

**Status:** Accepted
**Date:** 2026-09-13

## Problem

Scope §34 asks for a **fixed** render scale as a tier parameter, and the roadmap is explicit that
dynamic resolution is deferred *behind* it: "Dynamic resolution — justified when a fixed render-scale
tier system exists and is insufficient."

§5.8 also records the measurement that makes the deferral more than a matter of ordering: this
renderer's scene pass is only **44% resolution-dependent**. 640×400 is 0.26× the pixels of 1280×800
and 0.66× the scene time. A dynamic-resolution controller sized on the naive model would be wrong by
a factor of two about what it was buying, and would spend its authority on a lever with half the
travel it expects.

## Decision

**`QualitySettings::renderScale` exists, is set per tier, and is resolved in exactly one place:
`QualityPolicy::renderSize(outputWidth, outputHeight)`.** One function, so no caller invents its own
rounding; clamped to at least one pixel; a scale above 1 is allowed, because an offline render may
legitimately ask for supersampling. Offline forces 1.0 and `assertOfflineIsUncompromised()` checks
it. Unit-tested including the degenerate ends.

**It is not applied to the render targets, and this ADR exists to say what stops it rather than to
leave the parameter looking merely unfinished.**

`SceneRenderer::resize` sizes the HDR target and the whole post chain, and `tonemap.wgsl` reads that
target with `textureLoad` at a coordinate derived from `in.uv * textureDimensions(hdrTexture)`. So a
size mismatch between the HDR chain and the output already *works* — it is a nearest-neighbour
upscale. That is the problem. The tonemap binding declares
`wgpu::TextureSampleType::UnfilterableFloat` and has no sampler, so there is no filtered upsample
available to it without changing the bind group layout, the shader and every bind group built
against it.

**A render scale below 1 that upscales with nearest-neighbour is not a quality tier, it is a defect
with a knob.** §50 rejects an optimisation that saves time and visibly damages the image, and point
sampling a 0.75× frame up to 1× damages it in the most conspicuous way available — stair-stepped
edges on every silhouette, and a shimmer under any camera motion. So the parameter ships and the
application does not.

## What remains, precisely

1. `tonemap.wgsl`'s HDR binding becomes `Float` with a linear sampler (one extra binding, one
   `textureSampleLevel`). This is in the post/composition path and not in Phase D's file ownership.
2. `SceneRenderer::resize` scales its internal targets by `renderScale`, keeping the caller's size
   as the output size. The early-out comparison and `stats_.width/height` move with it — the stats
   should report the *scene* resolution, because that is what every per-pixel number in the harness
   is a number about.
3. The HDR readback path (`--capture`, and the offline render's frame grab) reads the scene target
   directly, so it would produce scaled images. Offline forcing `renderScale = 1.0` covers the
   deliverable case; `--capture` on a scaled preview would need to say so in its log line.

None of the three is hard. All three are outside the files this phase owns, and a partly-applied
render scale is worse than an unapplied one: it would be a tier parameter that changes the picture
in a way the tier table does not describe.
