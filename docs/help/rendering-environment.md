---
id: rendering/environment
title: Environment and Sky
category: Rendering
summary: What an environment map does, the procedural sky that stands in for one, and the two intensities.
order: 56
tags: environment, hdr, ibl, sky, reflections, background
keywords: how do i load an hdr; environment map; sky; reflections; why is my metal black; background image
related: rendering/lights, rendering/emission-and-bloom, start/projects
features: command.file.open-environment, subsystem.rendering
parameters: env/intensity, env/rotation
---

# Environment and Sky

An **environment** does two jobs: it lights the scene, and it may also be the visible background.

## Loading one

**File ▸ Open Environment (HDR)...**, the `E` key, or dropping the file on the window.

The format is **equirectangular Radiance `.hdr`, 2:1**. Anything else is refused with *"is not an
HDR (Radiance .hdr) image"*. There is no EXR environment loader.

On load, AV Gen builds a cube map, an irradiance cube for diffuse lighting, a roughness-prefiltered
cube for reflections, and a BRDF lookup table. This blocks for tens of milliseconds — it is
intended for load time, not per frame.

## The two intensities

| Parameter | Scales |
|---|---|
| `env/intensity` | how much the environment **lights** the scene (0 – 20, default 1) |
| `env/sky/...` intensity | how bright the **drawn sky** is |

They are separate on purpose. A scene often wants a bright sky behind it and restrained
environmental light, or the reverse.

`env/rotation` turns the sky **and** its lighting together, so a rotation stays physically
consistent.

`env/skyboxBlur` (0 – 1) blurs only the drawn sky.

The visible sky samples the equirectangular map directly rather than the prefiltered cube, because
a star is one texel of an 8K map and does not survive being resampled onto a cube face.

## Reflections

Reflections come entirely from the environment, through split-sum image-based lighting: a
roughness-prefiltered cube plus a BRDF lookup.

> [!NOTE]
> **There are no screen-space reflections, no reflection probes and no planar reflections.** A
> mirror will reflect the sky and not the room. If a surface must reflect the scene, the answer in
> AV Gen today is to compose the shot so that it does not need to.

## The procedural sky

With no HDR loaded, an analytic sky feeds exactly the same chain, so metal always has something to
reflect. `env/sky/*` carries a zenith colour, a sun direction, size and intensity and so on.

Its `env/sky/background` (called `showBackground` in a scene file) defaults to **off**: the
procedural sky lights the scene but the visible background stays the flat background colour until
you turn it on. That is the usual reason a scene is lit as if there is a sky and shows none.

A composition can also set `lightFromEnvironment`, which aims the key light at the map's brightest
direction.
