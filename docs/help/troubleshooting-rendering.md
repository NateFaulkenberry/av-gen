---
id: troubleshooting/rendering
title: Rendering Troubles
category: Troubleshooting
summary: Symptoms in the picture, and what each one actually is.
order: 81
tags: troubleshooting, rendering, shadows, glow, missing, black
keywords: objects disappearing; scene is black; shadows missing; metal is black; glow does not illuminate; flickering
related: rendering/lights, rendering/shadows, rendering/emission-and-bloom, rendering/environment, performance/what-costs-what
---

# Rendering Troubles

## Something glows but nothing near it is lit

Working as designed. Emission is a property of a surface, not a light source. Put a real light
there. See [Emission, bloom and glow](help://rendering/emission-and-bloom).

## A point, sphere or area light casts no shadow

Working as designed. Only directional and spot lights get shadow maps, and only one directional
light is cascaded per frame. Everything else has contact shadows and ambient occlusion. Replace it
with a spot if the shadow matters. See [Lights](help://rendering/lights).

## Metal is black, or reflections are missing

Reflections come entirely from the environment. With no HDR map and the procedural sky's background
off, there may still be *lighting* but nothing recognisable to reflect. There are no screen-space
reflections in AV Gen. See [Environment and sky](help://rendering/environment).

## A wide scene of small objects has almost no shadows

A known limitation. The shadow far plane is derived from the scene's bounding radius, so a large
ground plane pushes the cascade splits far beyond the geometry and a city of small buildings comes
out flat — a 260 m scene of around 220 nodes reports two shadow draws. Small scenes are fine, and
objects near the camera still cast.

## Instances flicker at the edge of the frustum, or an LOD snaps

There is **no hysteresis and no LOD crossfade** anywhere in the renderer. Every screen-radius and
LOD threshold is a hard comparison, so an instance sitting near a threshold flickers as the camera
breathes and an LOD swap is instantaneous. Known, and not currently configurable.

Separately, culling uses the source bounds through the instance transform. An object with a large
world-space deformer can pop at the frustum edge because its bounds do not account for the
deformation — which is one reason culling is opt-in per object.

## Volumetrics or the shadow mask vanished for a frame

Both report themselves: *"volumetrics disabled this frame"* and *"shadow mask disabled this frame"*,
with a reason, in the log. Usually a resource that could not be allocated at a new size.

## Some objects are missing after a scene grew

Two hard caps, both of which log:

- **256 visible SDF objects.** *"more than 256 visible sdf objects; extra objects skipped"*.
- **64 skinned rigs.** *"N skinned rigs in the scene; only the first 64 get a joint palette"* —
  the rest draw unskinned.

There is also a cap of **256 lights** in total.

## A pass reports 0.00 ms, or an absurd number

See [The profiler](help://performance/instruments). A pass under about 0.066 ms reads zero; an
empty render pass gets no timestamp at all and its cost folds into the pass after it.

## The whole application exits partway through

Check the log for `GPU device lost`. AV Gen exits with status 3 when the device is lost. A run that
finishes but reports GPU errors exits with status 5, and every uncaptured error is logged as it
happens — worth reading even when the picture looked right.

## A render does not match a previous one

Three known reasons, all deliberate:

1. **The quality tier** is whatever the process was started with, not `offline`.
2. **Temporal passes** — ambient occlusion, exposure adaptation — have seen a different number of
   frames at a different frame rate. Frame 0 is identical; later frames are not.
3. **Warm-up.** The first frames drawn with freshly compiled pipelines differ by a
   least-significant bit. AV Gen renders two throwaway frames first unless `AVGEN_NO_WARMUP` is set.
