---
id: rendering/shadows
title: Shadows
category: Rendering
summary: Cascades, the settings that change them, and what shadow stability actually fixed.
order: 54
tags: shadows, cascades, pcss, contact, bias, stability, popping
keywords: shadows are flickering; shadow popping; shadow bias; cascade; why is my shadow blocky; contact shadows
related: rendering/lights, performance/what-costs-what, troubleshooting/rendering
features: subsystem.rendering
---

# Shadows

## Cascades

A directional light's shadow is split into up to four **cascades**: near geometry gets a small,
detailed map and distant geometry a large, coarse one. The splits follow a practical scheme
weighted towards the camera, and each cascade fits the bounding sphere of its slice of the frustum
— a sphere rather than a box, so the fit does not change as the camera turns.

The number of cascades comes from the quality tier (2 for preview, 3 for realtime, 4 for high and
offline) and can be overridden per composition in the `environment` block's `shadowCascades`.

## Stability

Two things used to make shadows crawl, and both are fixed. It is worth knowing what they were,
because the symptoms are the ones people usually reach for `shadowBias` to cure:

**Shadow texels used to swim as the camera moved.** The window a cascade covers is now snapped to
its own texel grid, anchored to a rotation-only basis at the world origin. Measured over 1 mm
camera steps, drift fell from 0.68 texels a frame to 0.04, and a receiver stayed on its texel in
229 frames out of 239 where before it had managed four.

**Cascades used to switch, not blend.** Two cascades are now mixed over the last 12% of each one's
depth extent, so the boundary is a gradient instead of a line that sweeps across the ground as you
walk. This costs about half a millisecond on a dense scene at the editor's canvas, and the cost
lands in the scene pass rather than the shadow pass. It is a shader constant, not a setting, and it
does not apply to spot shadows.

## Contact shadows

Separate from shadow maps and controlled separately. `contactShadow` is **on by default** on every
light, including the types that get no map at all. It is a short screen-space march that catches
the small occlusions a 2048-pixel cascade cannot resolve — the join where an object meets the
ground.

Contact shadows do not survive being computed at reduced resolution. They are exactly the signal
that does not, which is why the shadow mask covers the map term and leaves the contact march at
full resolution.

## The shadow mask

At the `preview` and `realtime` tiers the directional shadow term is computed in its own
half-resolution pass and sampled by the lit pass, rather than being computed per pixel. On a dense
scene at the editor's canvas this takes about a quarter off the scene pass. It covers at most three
leading directional lights. `high` and `offline` compute it at full resolution.

`--disable shadowmask` turns it off, which is the A/B to run if you suspect it.

## What to change, and when

| Symptom | Reach for |
|---|---|
| Shadows detach from their objects | `shadowBias` **down**, or contact shadows on |
| Acne — stripes across a lit surface | `shadowBias` up, a little |
| Shadows too hard | `softness` up |
| Shadows too soft or muddy | `softness` down, or a higher tier |
| A shadow boundary sweeping across the ground | already fixed; if you still see it, check the cascade count |
| No shadow at all from a point or area light | expected — see [Lights](help://rendering/lights) |
| A wide scene of small objects casts almost nothing | a known limitation; see [Rendering troubles](help://troubleshooting/rendering) |
