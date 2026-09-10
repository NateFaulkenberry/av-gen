# ADR-069: Placing assets by hand

Status: accepted
Date: 2026-09-10

## Context

Generate World composes a whole world from a recipe. There was no way to put one thing exactly where
you wanted it. Both are needed and neither replaces the other: a generated world is a starting point
somebody then adjusts, and an adjustment you cannot make by pointing at the ground is an adjustment
you make by typing coordinates into an inspector.

## Decision

The layout maths -- where a click's objects go -- is separate from the UI and the engine, for the
same reason the camera gestures are (ADR-068). "Does a brush respect its own spacing", "is a cluster
reproducible from its seed" and "does a brush painted on a hillside land on the hillside" are
questions with answers, and they should not require a window to ask.

`planPlacements` returns positions, yaws and scales. Turning those into nodes is the caller's job and
goes through `Engine::addNode`, the same entry point Generate World and any future agent use.

Four modes: Single, Brush, Cluster, Landmark.

- **Brush** uses dart throwing against a spacing radius rather than a uniform scatter. A uniform
  scatter over a disc clumps, and clumping is the one thing a brush must not do -- the user is
  already deciding where the density goes by moving the mouse, and a brush that clumps overrules
  them.
- **Cluster** distributes by `sqrt(random)` so the disc fills evenly. Without it a cluster is dense
  in the middle and thins toward its edge, which reads as a target rather than as a patch of
  something growing.
- **Landmark** multiplies on top of the scale jitter rather than replacing it, because a landmark is
  a different decision from the population, not a large member of it.

Both brush and cluster lay out on the **surface**, using a basis built from the picked normal. On a
45-degree hillside, laying out on the XZ plane puts half the brush underground and half in the air.

Placement is deterministic in a seed, so a stroke can be undone and redone and a test can assert
about one.

### Sink follows the surface, orientation follows the recipe

These are two different vectors and the first version conflated them, which a test caught: an object
placed on a wall sank *downward*, leaving it hanging in the air in front of the wall rather than
bedded into it. `surface` is which way is out of the ground; `orientation` is which way the object
stands. They coincide only when "lie along the slope" is on.

### Arming is explicit

An empty armed asset means a click selects. A viewport that places something every time you click is
a viewport you cannot look around in, so "none" is the resting state and the way back to it is
always on screen.

## Consequences

- The World Builder panel gains an asset list, a mode and its settings, and caches the library it
  already loaded rather than reading a second copy -- two independently loaded copies are two
  libraries the moment somebody edits the manifest between them.
- Placement needs a surface normal, which the identifier target does not carry. It is estimated from
  three depth samples (`pickNormalAt`) rather than by decoding the octahedral normal target, which
  would couple placement to the encoding in `pbr_shade.wgsl` for a value only the optional
  "lie along the slope" setting uses. On a silhouette edge the estimate is poor, and that is
  reported as no normal rather than as a wrong one.
- The application copies the armed asset and settings from the panel once per frame rather than
  reaching into UI state from the event handler, which also means a scripted caller can arm a
  placement with no panel in existence.
