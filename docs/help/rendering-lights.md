---
id: rendering/lights
title: Lights
category: Rendering
summary: The seven light types, what each one costs, and which of them can cast a shadow map.
order: 53
tags: lights, point, spot, directional, area, rect, sphere, intensity, temperature
keywords: how do i add a light; light types; area light; how many lights can i have; why does my point light not cast a shadow
related: rendering/shadows, rendering/emission-and-bloom, rendering/environment, performance/what-costs-what
features: subsystem.rendering
---

# Lights

## The types

| Type | Shape | Units |
|---|---|---|
| `Directional` | infinitely far, parallel rays | lux |
| `Point` | a position | candela |
| `Spot` | a position and a cone | candela |
| `Rect` | a rectangle | nits over the emitter |
| `Disk` | a disc | nits |
| `Tube` | a capsule | nits |
| `Sphere` | a sphere | nits |

The four area types are integrated analytically, which is why they are more expensive than a point
light of the same brightness.

## Properties

| Field | Default | Meaning |
|---|---|---|
| `intensity` | 1.0 | in the units above |
| `color` | white | linear |
| `temperature` | 6500 K | 6500 leaves `color` unchanged; resolved on the CPU |
| `tint` | 0 | −1 green to +1 magenta |
| `range` | 0 | 0 means infinite |
| `innerConeAngle`, `outerConeAngle` | 0, 45° | spot only |
| `width`, `height` | 1, 1 | rect |
| `radius` | 0.25 | disk, sphere, tube |
| `castsShadow` | **false** | allocates a shadow map |
| `contactShadow` | **true** | screen-space march; independent of `castsShadow` |
| `shadowStrength` | 1.0 | |
| `shadowBias` | 0.0015 | normal-offset scale, world units |
| `softness` | 1.0 | penumbra width multiplier |
| `volumetricStrength` | 1.0 | how much this light lights the air; a light from a rig defaults to 0 |
| `diffuseOnly`, `specularOnly` | false | |

A light also carries a **role** — `Key`, `Fill`, `Rim`, `Back`, `Ambient`, `Practical` — which is
authoring metadata. Shading does not read it.

## Limits

**256 lights** per scene. Beyond that the list is truncated.

Local lights are assigned to a clustered froxel grid — 16 × 8 × 24 cells with up to 32 lights each,
sliced exponentially in depth — so the cost of a pixel scales with the lights that actually reach
it rather than with the scene total. Directional lights bypass the grid entirely and are always
shaded.

## Which lights cast shadow maps

> [!WARNING]
> **Only directional and spot lights get a shadow map, and only one directional light is
> cascaded per frame.** Point, rect, disk, tube and sphere lights have `contactShadow` and ambient
> occlusion and nothing else. A glowing sphere in a doorway will not cast the shape of the doorway.

This is the shape of the implementation, not a setting, and it is worth planning a scene around.
If a point light needs to throw a shadow, replace it with a spot.

Up to eight shadow views exist in total, shared between the directional cascades and the spot
lights.

## Temperature and colour

`temperature` is applied on top of `color`, so a warm practical is usually `color` white with a
temperature around 2700 K rather than a hand-picked orange. `tint` moves the result along the
green–magenta axis, which is what corrects a light that reads too green after a temperature change.
