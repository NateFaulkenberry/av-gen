---
id: rendering/emission-and-bloom
title: Emission, Bloom and Glow
category: Rendering
summary: Why raising emission does not light anything nearby, and what to do instead.
order: 52
tags: emission, bloom, glow, emissive, light, halation
keywords: how do i make something glow; why doesnt my glow light anything; emissive does not illuminate; bloom; make it glow and light the room
related: rendering/lights, rendering/exposure-and-tonemapping, modulation/recipes, rendering/volumetrics
features: subsystem.rendering
parameters: post/bloom/intensity, post/bloom/threshold, post/bloom/knee, post/bloom/radius
---

# Emission, Bloom and Glow

> [!WARNING]
> **Raising a material's emission does not illuminate nearby objects.** Emission is an additive
> radiance term on the surface itself. Nothing in the lighting path reads a material's emissive
> value. A brighter emissive surface is a brighter *surface*, and the wall beside it stays exactly
> as dark as it was.

This is the single most common surprise in AV Gen, so it is worth stating what each mechanism
actually does.

## The three mechanisms

**Emission** makes a surface bright. `emissiveColor` and `emissiveIntensity` on a material; the
intensity defaults to **0**, so a material glows only when you ask it to. There is also a Fresnel
rim tinted by the emissive colour, which is what gives an emissive object a soft edge.

**Bloom** makes bright pixels spread. It is a post effect: it reads the finished HDR image, keeps
what is above a threshold, blurs it through a pyramid and adds it back. It does not light anything;
it is the camera's response to brightness, not the scene's.

**Lights** illuminate. A `Point`, `Spot`, `Rect`, `Disk`, `Tube` or `Sphere` light is the only
thing in AV Gen that puts energy onto other surfaces.

## Making something glow *and* light its surroundings

**Put a light there.** Add a point light at the emissive object's position, with a colour matching
the emissive and a range that covers what you want lit. This is the general answer and it is the
one that always works.

**For scattered glowing ecology**, AV Gen can do it for you. When a composition enables the ecology
light field, glow clusters are binned and reduced to real point lights automatically, so a valley
full of glowing plants lights its own ground without you placing hundreds of lights. It is **off by
default** and is enabled in the composition's `environment` block with `ecologyLight` (a gain) and
`ecologyLightRange`. These are scene-file keys, not parameters, so they cannot be modulated.

**To light the air rather than surfaces**, volumetrics have to be on and have to be told to
consider local lights. See [Volumetrics and fog](help://rendering/volumetrics).

**For glowing particles**, a particle system's `volumeGlow` reduces its live emissive particles to
one aggregate source the volumetric march can see.

## Bloom controls

| Parameter | Default | Range |
|---|---|---|
| `post/bloom/enabled` | on | |
| `post/bloom/intensity` | 0.2 | 0 – 10 |
| `post/bloom/threshold` | 1.0 | 0 – 20 |
| `post/bloom/knee` | 0.6 | 0 – 1 |
| `post/bloom/radius` | 1.0 | 0.25 – 3 |

Threshold is in **exposed HDR units**, not 0–1: a threshold of 1.0 keeps what is brighter than a
fully lit white surface. `knee` softens the cut so that a pixel just over the threshold does not
appear abruptly. `radius` controls how far the spread reaches.

The engine ships a route from `audio.rms` to `post/bloom/intensity`, which is why a default scene
already breathes a little.

`post/bloom/emissionWeight` (0 – 1) makes the bloom **selective**: at 0 every bright pixel blooms,
and at 1 a pixel blooms in proportion to how much of its radiance it emits rather than reflects. It
is what stops a white wall under a hard key glowing like a lamp. `env/skyBloom` rides on the same
mask and needs this above 0 to do anything.

## Halation and anamorphic

Two further spreads, both **off by default**: `post/halation/*` adds a warm, wide, tinted bloom of
the kind film gives around very bright highlights, and `post/anamorphic/*` adds a horizontal streak.
Each has its own `intensity`, `threshold` and `tint`.
