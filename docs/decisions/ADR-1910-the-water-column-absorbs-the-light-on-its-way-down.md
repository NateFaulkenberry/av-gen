# ADR-1910: The water column absorbs the light on its way down

**Status:** Accepted (Strange Attractor pass 3, Phase E)
**Date:** 2026-11-04
**Relates to:** ADR-1908 (spectral distance fog), 59-pass3-critic-and-renderer-audit.md §2

## Problem

ADR-1908 made the ocean lose red with distance *from the camera*. That is half of what water does. The other half is
the light's own path: sunlight and skylight reach a seabed 60 m down through 60 m of water, and a lamp's light crosses
water to reach a rock. Without it, a surface near the camera keeps its full dry colour. The pass-3 baseline's hero
still showed it: the seabed at the camera's feet read as dry sediment, and the only depth cue in the ocean was the
fog, so the brief's "terrain in front of a blue background" held at every distance.

## Decision

Two environment values: `waterSurface` (world y) and `waterDownwelling` (absorption per metre, by channel). Both are
parameters (`scene/waterSurface`, `scene/waterDownwelling`), so the timeline and audio can drive them.

- **Directional lights and the ambient** are multiplied by `exp(-k * max(surface - y, 0))`, the water between the
  surface and the point.
- **Local lights** are multiplied by `exp(-k * d * u)`, where d is the lamp-to-point distance and u is the share of
  that segment under the surface.
- **Emission is untouched.** A glowing thing is the light source.
- **Off by default.** With all-zero `k` both functions return 1 before reading anything else, and the scene file
  writes neither key. Every scene but a water column renders as it did, which is why it costs one frame vec4
  (`FrameUniforms::waterColumn`, placed before `portalClip`, which stays last).

It is applied in `directLighting` and the lit path's ambient, the paths the terrain, glTF meshes and material
programs share. The toon, unlit and fallback-tier paths are left alone: none is used under water.

## Consequences

- Depth becomes a lighting cue that does not depend on where the camera is. Ledges and the seabed darken and turn
  blue-green with depth, the base's foundations sit in a darker layer than its roof, and the floods' warm light dies
  within tens of metres instead of lighting the whole floor.
- The distance fog (ADR-1908) and this term multiply. Values are tuned together: the ocean's first pass uses
  k = (0.06, 0.022, 0.014) per metre, close to coastal water's (red is gone in about 15 m).
- Test: `a scene's water column loads, is a parameter, and is saved only when it absorbs (ADR-1910)`.
