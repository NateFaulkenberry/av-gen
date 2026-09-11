# ADR-080: Keeping a directed camera out of the scenery

Status: accepted
Date: 2026-09-10

## Context

A directed camera is baked from shot geometry — orbit points around a subject at a distance in
radii. That geometry knows nothing about what is in the way, so it will put the eye inside a
hillside, inside a canopy, or inside the hero it is looking at.

Measured on Glowmere: **21 of 32 baked camera keys** were inside terrain, canopy or a hero.

## Decision

An obstacle model assembled from what the world can cheaply answer, rather than a collision mesh:

- **Terrain** is exact and analytic. `WorldMap::height` is closed-form, so "how high is the ground
  here" costs a few noise samples and no memory.
- **Vegetation is not enumerable but it is predictable.** There are a hundred thousand instances and
  they live on the GPU — but a scatter layer states its height and the biomes it grows in, and the
  map states each biome's weight at a point. The tallest layer that grows here is therefore known
  without touching a single instance. This is a *statistical canopy*: it says "trees about fourteen
  metres tall grow around here", not "there is a trunk at exactly this spot". That is the right
  resolution for a camera, which wants to be above the canopy or in a clearing — not threading
  between trunks.
- **Heroes** are a handful of capsules and are exact. A capsule rather than a bounding sphere: a
  sphere around a twenty-metre subject is enormous and would push the camera out of shots meant to
  be close, while the actual obstruction is a trunk of roughly `radius` running up its height.
- **Clearances** (ADR-067) are where the composer decided nothing grows, so the canopy is absent
  there. This is what lets a directed camera fly low down the negative-space corridor that was cut
  for exactly that purpose — without it, the clearance system and the camera system would work
  against each other.

### Corrections are vertical and minimal

Pushing a camera sideways changes which way the shot faces and what is in frame. Lifting it changes
the shot least, and a camera that rises slightly to clear a canopy reads as choosing its altitude
rather than as being shoved. A camera that is already clear is not moved at all, and horizontal
position is never touched.

Biome presence is thresholded at 0.25. Biome weights blend, so almost everywhere has a trace of
almost everything; treating any non-zero weight as "trees grow here" would lift the camera to canopy
height over open meadow.

### Smoothing must not undo the clearance

Correcting each key independently puts a kink at every corrected one — a camera that steps up for
one key and back down for the next reads worse than one that never dipped. So the corrections are
smoothed, but **clamped to the floor each point needed**. A plain average is the obvious way to
write this and is exactly wrong: it drags a corrected point back into the thing it was lifted out
of. The smoothing removes the kink, not the clearance.

## Consequences

- Glowmere's directed camera now travels between 7.2 m and 16.4 m, varying smoothly.
- The bind that made this visible also surfaced a second defect: the bake emits a
  `camera/focus/emphasis` track for hero spotlighting and **nothing registers that parameter**, so
  it bound to nothing. Tracks naming parameters this build does not have are now left out and named
  once, rather than sitting unbound in a saved project for somebody to puzzle over later.

## What this does not do

It is not occlusion: the camera is kept out of solid things, not prevented from having a tree
between it and its subject. Nor does it move a camera that is merely *close* to something — only one
that is inside it. Both are reasonable next steps and neither is implemented.
