---
id: rendering/volumetrics
title: Volumetrics and Fog
category: Rendering
summary: One fog, two ways of drawing it, and how to make light visible in the air.
order: 55
tags: fog, volumetrics, god rays, scattering, haze, atmosphere
keywords: how do i add fog; god rays; light shafts; atmosphere; volumetric; make the air glow
related: rendering/lights, rendering/emission-and-bloom, performance/what-costs-what
features: subsystem.rendering
parameters: scene/volumeDensity, scene/volumeMaxDistance, scene/volumeAnisotropy, scene/volumeSteps
---

# Volumetrics and Fog

There is **one** fog in AV Gen: `scene/volumeDensity` (0 – 2, **default 0**), a participating
medium whose extinction is `volumeDensity × volumeAbsorption` per metre. It is drawn two ways, and
they are the same air under the same law (ADR-705):

- **The volumetric march**, out to `scene/volumeMaxDistance`: light is scattered towards the camera
  along every ray, and objects cast shadows through it.
- **The surface fog**, beyond that distance: the same air integrated in closed form, fading
  surfaces towards `scene/fogColor`. It is nearly free.

Set `volumeMaxDistance` to **0** for distance fog with no march at all -- the whole ray is then the
cheap closed form. **With `volumeDensity` at zero, nothing is allocated and no pass runs.** Off is
free.

The old `scene/fogDensity` -- an exponential-squared fog with a density of its own, which agreed
with the march at one distance and nowhere else -- was removed by ADR-705.

| Parameter | Default | Range | Meaning |
|---|---|---|---|
| `scene/volumeDensity` | 0 | 0 – 2 | the amount of medium; the master switch |
| `scene/fogHeight` | 0 | | the height the medium's falloff is anchored to |
| `scene/fogHeightFalloff` | 0 | 0 – 10 | how fast it thins with altitude; 0 is uniform |
| `scene/volumeScattering` | 1.0 | 0 – 20 | how much light bounces towards the camera |
| `scene/volumeAbsorption` | 0.5 | 0 – 20 | how much is swallowed |
| `scene/volumeAnisotropy` | 0.3 | −0.95 – 0.95 | forward scattering; positive haloes the light source |
| `scene/volumeNoise` | 0 | 0 – 4 | breaks the medium up |
| `scene/volumeNoiseScale` | 0.1 | 0 – 10 | the size of the structure |
| `scene/volumeNoiseSpeed` | 0.1 | −10 – 10 | how fast it drifts |
| `scene/volumeEmission` | 0 | 0 – 20 | the medium glows on its own |
| `scene/volumeSteps` | 32 | 4 – 256 | march quality; the main cost knob |

The march runs at half resolution and is composited depth-aware, so the cost is roughly linear in
`volumeSteps` and quadratic in nothing.

## Making local lights visible in the air

By default only directional light is scattered. To see a point or spot light throw a shaft you need
two things:

1. **`volumeLocalLights`** above zero in the composition's `environment` block. This is a
   scene-file key, not a parameter, so it cannot be modulated.
2. The light's own **`volumetricStrength`** above zero. A hand-authored light defaults to 1.0; a
   light created by a lighting rig defaults to **0**, which is the usual reason a rig's practicals
   do not shaft.

`volumeLocalLights` is close to free in a thin medium and costs real time in a dense one.

## Turning anisotropy up

`volumeAnisotropy` is the single control that most changes the *look*. At 0 the medium scatters
evenly and reads as haze. Towards 0.8 it scatters forward, so looking towards a light gives a bright
halo and looking away gives almost nothing — which is what reads as a shaft.

## Cost

`--disable volume` is the A/B. On a terrain scene at 1440×900 the volumetric pass measured about
1.05 ms and turning it off saved about 0.89 ms of frame time; the two agree, which is how you know
the pass timer is telling the truth. Raising `volumeSteps` eightfold raised both by the same six and
a half milliseconds.
