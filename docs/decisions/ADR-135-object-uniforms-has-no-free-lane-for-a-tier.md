# ADR-135: `ObjectUniforms` has no free lane, so the material tier is frame-global for now

**Status:** Accepted
**Date:** 2026-09-13

## Problem

A material tier (ADR-133) must reach the shader as a value that is **uniform across a draw**.
ADR-118 measured why: a lane-varying branch around a twelve-iteration loop with a dependent texture
load in it ran **4.4% slower** than the loop it was skipping, in 7 of 8 interleaved pairs. In a
fragment shader, saving work per lane saves nothing unless the whole wave skips. So a per-fragment
tier is a pessimisation and a per-draw tier is free.

Per-draw means `ObjectUniforms`, which every scene-pass shader already has bound at group 1. It
looked as though it had a spare lane. It does not.

## What is actually in there

```
baseColor  rgb, a = opacity
emissive   rgb, w = intensity
material   x roughness, y metallic, z normalScale, w occlusionStrength
flags      x alpha mode, y alpha cutoff, z unlit, w texture mask
ids        x object id (tagged pick id), y material id, z bloom weight, w = ???
```

The header comment on `ids` said `w = 0`. **It is not zero: it is the skinned joint count**, written
by `makeItem` from `SkinningRenderer::Slice::jointCount` and read by `pbr_skinned.wgsl` to find the
previous frame's half of its palette slice. The comment two lines above the assignment says so; the
struct's own comment did not, and both are in `scene_renderer.hpp`.

This is worth recording rather than just fixing, because of *when* it was caught. The first
implementation read `object.ids.w` as the tier. Every unskinned draw would have read 0 and behaved,
and every **skinned** draw would have read its joint count — typically well above 2 — and shaded at
the flat tier. Not in the arm. In the **baseline**. An A/B whose baseline is quietly wrong reports a
difference that is real, reproducible, interleaved, and about nothing. The struct comment is now
corrected.

## Decision

**The tier is carried in the frame uniform (`FrameUniforms::materialTier.x`), not per draw, and it
is therefore frame-global.** That is sufficient for what Phase D has to do first — the two A/B arms
that price each rung need exactly one global value — and insufficient for what Phase D wants next:
importance-driven assignment needs a *different* tier per drawable.

**Adding the lane is not done here.** It is one `glm::vec4` in `ObjectUniforms`, which is 272 bytes
inside a 512-byte slot, so it costs no memory and no bandwidth — the slot is already padded to the
dynamic-offset alignment. It is nonetheless the object layout, which ADR-128 to ADR-130 have just
finished reworking and which Phase E owns, and adding a field to a struct another agent is holding
open is how a merge produces a defect neither agent wrote.

## Consequences

* `MaterialTierSelector` (ADR-133) is complete, tested and **not wired**. It has no consumer until
  the lane exists. This is stated in its header rather than left for the next reader to discover.
* The wiring, when it happens, is four lines: a `glm::vec4 tiering` in `ObjectUniforms` with
  `x = tier`; `makeItem` and `ProceduralRenderer`'s object-slot block each writing it; and
  `materialTierOf()` becoming `max(object.tiering.x, frame.materialTier.x)` so a forced tier stays a
  floor and never silently un-reduces a draw.
* **Procedural scatter needs a decision the entity path does not.** Its object slot is per *object*
  — one slot for a scatter of 2,161 trees — but it draws once per LOD level, and the LOD level is
  the representation selector's own answer to the same size question. So the natural tier source for
  scatter is `ProceduralUniforms` per level, not `ObjectUniforms` per object: LOD 0/1 Full, the
  billboard rungs Flat. That reuses the existing importance notion rather than adding a second one,
  which is what §5.3 asks for, and it is a `procedural_renderer.cpp` change rather than a layout one.
