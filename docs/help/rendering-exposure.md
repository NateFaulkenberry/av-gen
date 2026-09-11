---
id: rendering/exposure-and-tonemapping
title: Exposure and Tone Mapping
category: Rendering
summary: Two different brightness controls at two different points in the chain, and the five tone-map operators.
order: 51
tags: exposure, tonemap, aces, agx, brightness, iso, aperture, vignette, grain
keywords: how do i make it brighter; image is blown out; tonemapping; aces or agx; exposure; too dark
related: rendering/overview, rendering/emission-and-bloom, troubleshooting/rendering
features: subsystem.rendering
parameters: scene/brightness, camera/exposure/iso, camera/exposure/compensation, post/tonemap/operator
---

# Exposure and Tone Mapping

AV Gen has **two** brightness controls and they act at different points. Confusing them is the most
common reason an image will not behave.

## Photographic exposure — acts first

`camera/exposure/*` is a physical camera model, applied at the **start** of the post chain, before
bloom. Because it runs first, bloom thresholds are in exposed units and stay meaningful as you
change exposure.

| Parameter | Default | Range |
|---|---|---|
| `camera/exposure/mode` | 0 (manual) | 0 manual, 1 automatic |
| `camera/exposure/aperture` | 5.6 | 0.7 – 45 |
| `camera/exposure/shutterSeconds` | 1/50 | 1/8000 – 4 |
| `camera/exposure/iso` | 400 | 25 – 204800 |
| `camera/exposure/compensation` | 0 | −8 – +8 EV, both modes |
| `camera/exposure/minEv`, `maxEv` | −4, 16 | the automatic mode's clamp |
| `camera/exposure/speedUp`, `speedDown` | 3, 1 | EV per second when adapting |
| `camera/exposure/meterCenterWeight` | 0.6 | 0 flat average, 1 centre-weighted |

In automatic mode the meter reads the image *before* exposure is applied and consumes the previous
frame's readback, so it lags by a frame — which is what makes it smooth.

`compensation` is the control to reach for first. It works in both modes and is in stops.

## `scene/brightness` — acts last

A plain multiplier applied **inside the tone-map shader**, after the entire post chain. Default 1.0,
slider 0 – 3, hard limit 8.

Use it for a quick overall lift, for fades (the sequencer bakes fades onto it), and for
audio-reactive brightness. Use the camera exposure when you care about how bloom, depth of field
and the highlight roll-off respond.

## Tone-map operators

`post/tonemap/operator` is an integer:

| Value | Operator |
|---|---|
| 0 | ACES fitted |
| 1 | **AgX** (the default) |
| 2 | Reinhard extended |
| 3 | Khronos PBR Neutral |
| 4 | clamp |

AgX holds saturation into the highlights longer than ACES and rolls off more gently; ACES is
contrastier. `clamp` is not a look, it is a diagnostic — it shows you exactly where values exceed 1.

## Chroma retention

`post/tonemap/chroma-retention`, 0 – 1, restates the source hue at the brightness the operator
chose. It is ramped in only for genuinely bright pixels, so it affects compressed highlights and
leaves midtones alone. Use it when a saturated emissive turns white as it brightens.

> [!NOTE]
> Note the spelling: `post/tonemap/chroma-retention` is hyphenated, while every other post path is
> camelCase.

## Vignette and grain

`post/output/vignette` and `post/output/grain`, both 0 – 1, both applied in the tone-map pass.
Grain is seeded from the frame index, so it is deterministic — the same frame renders the same
grain every time.
