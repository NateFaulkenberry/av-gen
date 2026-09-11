# ADR-082: Making the LOD decision stable

Status: accepted
Date: 2026-09-10

## Context

Renderer 2.0 §10, §11 and §13 ask for screen-space LOD, hysteresis and a LOD crossfade, and call
screen-space LOD mandatory. The Phase 0 audit found that **screen-space LOD already existed** on
both sides — `procedural_renderer.cpp` computes `radius / distance * projScale` and selects by it,
and `shaders/cull.wgsl` does the same with frustum planes and a minimum screen radius. What did
not exist was any hysteresis or any transition: every threshold was a hard binary comparison,
evaluated fresh from the current camera, with no memory of what the instance decided last frame.

That produces two distinct artefacts, and it is worth separating them because they have different
causes and different fixes:

1. **An instance sitting near a threshold flips every time the camera breathes**, because the
   comparison has no dead zone.
2. **Every instance at a given radius crosses its threshold on the same frame**, so a whole band
   of the world changes mesh at once.

Measured, on a wall of 400 coincident instances walked through a 50 m threshold: the worst
single-frame change was **400 of 400** — the entire population, in one frame. And with the camera
breathing ±0.2 m around a 50 m threshold, the population changed level on **11 of 12 frames**.

## Decision

Two settings, deliberately separate, because they differ in kind.

### `lodSpread` — on by default (0.12)

Each instance gets its own threshold, offset by a hash of its index:

```wgsl
let spread = 1.0 + (ladderHash(i) - 0.5) * spreadAmount;
```

This is the crossfade, and it is the answer to artefact 2. No instance is ever half-way between
two meshes — the *population* is. It needs no second draw, no dithered alpha and no temporal
blend.

`ladderHash` is deliberately decorrelated from the `instanceHash` that density thinning already
uses. If one hash drove both, the instances that swap early would be exactly the ones thinning had
already removed, and the spread would do nothing where it was needed.

**It is a pure function of the instance index, so it is exactly as deterministic as the hard
comparison it replaces.** That is why it can be on by default.

Measured: worst single-frame change **400 → 31 of 400**.

### `lodHysteresis` — off by default (0.0)

A dead zone around each threshold. An instance keeps its level until the metric crosses by that
fraction, which is the answer to artefact 1.

Measured: level changes under camera jitter **11 → 0**.

This one reads the previous frame, and that is why it is off by default. **With it on, what you
see depends on how the camera arrived rather than only on where it is.** The divergence is bounded
— only instances within the dead zone of a threshold can differ, and those are by definition at a
size where the two levels are near indistinguishable — but it is a divergence, and this engine
promises that frame *N* looks the same however it was reached. Offering it and saying what it
costs is honest; turning it on silently would not be.

### No new buffer

Neither needed one. `lodIndex` is grow-only and is written by the classify pass and no other, so
last frame's level is still in it when this frame starts. The one requirement is that a freshly
allocated buffer is **zeroed** — classification now reads it before writing, and driver garbage
would read as "this instance was already at level four billion" and drop the object to its
coarsest mesh for a frame after every grow.

## Consequences

- Glowmere's LOD distribution is unchanged in aggregate (476/1484/374/5 against 478/1482/375/5).
  The spread changes *which* instances are on each side, not how many.
- Cost is below the noise floor. On a contended machine (two other agents running), best-of-three
  interleaved 150-frame runs put spread-on 0.13 ms *ahead* of spread-off, which means the true
  cost is smaller than the measurement error rather than that it is free. The cull pass is 0.33 ms
  of a 25 ms frame, so the ceiling on this is low regardless.
- Two existing tests that compare GPU classification against a hard-threshold CPU reference now
  pin `lodSpread = 0`. They are testing threshold semantics, which are unchanged; the spread has
  its own case.
- `lod/spread` and `lod/hysteresis` are ordinary parameters, so both are modulatable and both
  round-trip through the project file.

## What is not done

- No hysteresis on the shadow cascades' own LOD selection, which is a separate path.
- The dead zone is a fraction of each threshold, not a function of how fast the camera is moving.
  A camera that is genuinely travelling would be better served by a narrower zone, since a swap it
  passes through quickly is not seen.
