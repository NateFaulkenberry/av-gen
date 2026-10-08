# ADR-1281: An emitter is reflected in still water

- Status: Accepted (2026-10-08), proto/bio-valley (the Bioluminescent Valley, CP3)
- Code: `shaders/ecosystem.wgsl` (`mirrorSplat`), `src/scene/ecosystem.{hpp,cpp}` (`mirror`, `mirrorLevel`,
  `mirrorStretch`), `src/rendering/ecosystem_renderer.cpp` (packed into the layer's `motion.yzw`).
- Tests: `tests/rendering/test_ecosystem_gpu.cpp` (`[adr1281]`), `tests/unit/test_ecosystem.cpp` (`[adr1281]`).

## Context

The reference's riverbank (panel 1) is lit mostly by its water: black water carrying broken vertical columns of the
bank's light (violet, cyan, green bokeh). In AV Gen, terrain water (ADR-099) reflects only the environment map, and
the ecosystem's emitters (ADR-1200) are accumulated after the lit pass. Nothing could make a glowing stalk on a bank
appear in the stream beside it.

## Decision

An emitter layer can draw each point's reflection in still water at a level:
- `mirror`: the strength (0 = off, the default; not written when off);
- `mirrorLevel`: the water's height in metres;
- `mirrorStretch`: the half length of the ripple streak, in metres;
- `mirrorSlope`: `[dlevel/dx, dlevel/dz]`, for a river that descends along its course (added at CP4: the valley's
  main river drops 1.2 m per km). The level at a point is `mirrorLevel + slope · (x, z)`.

In `cs_emit`, a point above the level is mirrored through the plane. The ray to the mirrored point crosses the
water at q. The reflection is drawn only if the surface the camera sees at q's pixel is that plane: the prepass's
linear depth must match q's view depth, within 0.3% of the distance + 0.3 m (tightened from 4% at CP4, when 4% let reflections land on the banks at range). A reflection therefore never lands on a bank, a rock or
the sky.

Ripples smear it into six splats spread over ±`mirrorStretch` vertically, with a small time-dependent wobble, so
light on rippled water becomes a short pillar. It is accumulated like any splat (order-free atomics, deterministic),
and it is done whether or not the point itself is occluded. A reflection is seen from below the bank's lip.

`mirror` and `mirrorStretch` are live parameters (`ecosystem/<layer>/mirror`, `.../mirrorStretch`). The level is
layout.

## Alternatives

- **Screen-space reflections in the water shader.** The water draws in the lit pass, before the ecosystem
  accumulates, so it cannot see the emitters. Reordering the passes, or a second water pass, is a renderer change for
  one effect.
- **Mirrored copies of the host geometry.** The cost would double, and only bodies would be reflected, not the light.
- **A planar reflection render.** It costs a whole second scene pass.

## Consequences

- One plane (possibly sloped) per layer. A scene with water at several levels splits its layers, or accepts the depth test's tolerance
  (a reflection is rejected where the visible surface is far from the plane).
- Cost: six extra splats per mirrored point above the plane. In the valley this is only the bank layers (a few
  hundred thousand points), well under a millisecond.
- A layer with `mirror` 0 runs the same code as before, bit for bit (the branch is not taken).
