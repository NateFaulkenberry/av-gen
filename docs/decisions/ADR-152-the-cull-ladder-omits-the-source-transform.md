# ADR-152: The GPU cull ladder sizes a procedural by its raw mesh, not by the object

**Status:** Accepted (defect recorded; the fix is scoped, not applied here)
**Date:** 2026-09-13
**Found by:** the Phase C justification measurement (ADR-150), which had the same bug
**Affects:** ADR-029 (the GPU LOD ladder), ADR-126 / audit §4.9 (whose figures inherit it)

## What is wrong

`shaders/cull.wgsl` computes an instance's projected radius as

```
radius = limits.z * max(|record.scale|) * limits.w
```

where `limits.z` is `cullRadius` and `limits.w` is `objectScale`, the largest column length of the
object matrix. Both are supplied by `procedural_renderer.cpp`:

- `cullRadius = max(mesh->radius, groupRadius[i])`, and `mesh->radius` is `cached.radius` — half the
  diagonal of the **uploaded** mesh's bounds. The uploaded mesh is `makeLodMesh(object.source, …)`,
  which is the source mesh before any of the object's own transforms.
- `objectScale` comes from `objectMatrices[i]`, and `scene_renderer.cpp` passes
  `const std::vector<glm::mat4> identity(scene.procedurals.size(), glm::mat4(1.0f))` — so it is
  always exactly 1.

**Neither term contains `sourceTransform`.** And `sourceTransform` is where a scatter layer's size
lives: `composition.cpp` puts the "make this thing 0.45 m tall" normalisation there on purpose,
because `distributionTransform` would scale the placements along with the mesh. The shader applies it
at draw time (`u.sourceMatrix`), and the wind code goes out of its way to travel the bounds through
it — "the height profile is measured in the space the deformer stack sees, which is after the source
transform … so the bounds have to travel through the same matrix". The cull path does not.

So the ladder is not comparing the projected radius it says it is comparing.

## How wrong, per layer

Measured, committed, re-runnable — `tests/unit/test_representation_band_analysis.cpp` prints it:

| layer | rung-0 radius | `sourceTransform.scale` | the ladder is wrong by |
|---|---|---|---|
| `valley_grass` | 0.727 | 0.525 | **1.9× too large** |
| `valley_flowers` | 0.987 | 0.268 | **3.7× too large** |
| `valley_bushes` | 0.944 | 0.695 | 1.4× too large |
| `valley_ferns` | 1.422 | 1.666 | 1.67× too small |
| `valley_pebbles` | 0.258 | 2.971 | **3.0× too small** |
| `valley_beacons` | 0.439 | 3.236 | **3.2× too small** |
| `elder-crown` | 1.000 | (8.2, 2.0, 7.4) | **8.2× too small** |
| `visitor` | 231.429 | 0.035 | **29× too large** |

Only three of Glowmere's thirty procedurals have a unit source scale.

## What it does to the image

Two things, and they are opposite for different layers:

- **A layer the ladder thinks is larger than it is** stays on a finer rung further away and is culled
  later than `minScreenRadius` says. That is wasted triangles — `valley_grass`, 97,161 instances, is
  the worst case and it is the layer this whole phase's excess is concentrated in.
- **A layer the ladder thinks is smaller than it is** drops to a coarser rung *nearer the camera*
  than the author asked, and vanishes at `minScreenRadius` while it is still several pixels across.
  That is a visible quality loss with no diagnostic attached to it.

**Verified visually.** The ceiling probe's 40 px arm raises `minScreenRadius` to 40 on every culling
procedural. The elder mushroom's cap — `elder-crown`, about 330 px wide in the baseline capture, so
a true projected radius near 165 px — **disappears from that capture**. The ladder sized it at 1.0
world units instead of 8.2 and culled it. Nothing in the frame says it happened.

It does not bite in the shipped Glowmere frame because the authored `minScreenRadius` is ~1.5 px and
`elder-crown` has no ladder to descend. It bites the moment anybody raises either — which is exactly
what a representation phase does.

## Why this is recorded rather than fixed here

The fix is small and obvious: fold `sourceTransform`'s largest column length into `cullRadius`, and
into `groupRadius[lead]` per part, in `procedural_renderer.cpp`. Two lines.

Its *consequences* are not small. It moves every scatter layer's rung selection in a different
direction — grass and flowers coarsen, ferns and pebbles refine — so it changes the shipped image of
every world that scatters, changes the frame's triangle count, and invalidates the calibration of the
28 / 11 / 4 px thresholds those layers were tuned against. That is its own piece of work with its own
visual gate (§50) and its own measurement, and it belongs to whoever owns the ladder's calibration,
not to a measurement task that happened to trip over it.

## What it does to the numbers Phase C is justified by

ADR-126 and audit §4.9 walk the scatter the same way and inherit the same omission: coverage and
`pixelsPerTriangle` are both wrong by `sourceScale²`, in a different direction per layer.

Corrected (ADR-150's analysis now applies the whole transform chain), the **per-layer** picture moves
a great deal — the 2–8 px band goes from 447 drawables to 1,068 — and the **aggregate** barely moves:

| | ADR-126, as published | corrected |
|---|---|---|
| whole-frame estimated excess | 1.52× | **1.51×** |
| ideal-proxy ceiling on the C5+C6 bands | −10.17% | **−9.45%** |

The errors very nearly cancel across thirty layers. That is worth stating precisely because it could
not have been assumed, and because it means **§4.9's headline survives its own defect** while its
per-layer attribution does not. Anyone quoting a single layer's excess from ADR-126 is quoting a
number that is wrong by that layer's source scale squared.

## Verified vs assumed

**Verified:** every scale in the table, printed by a committed analysis. That `objectMatrices` is
identity, read in `scene_renderer.cpp`. That the cull path never multiplies by the source transform,
read in `procedural_renderer.cpp`. That `elder-crown` disappears from a 40 px arm, seen in the
capture at full size.

**Assumed:** that folding the source scale into `cullRadius` is the whole fix. The parts/fanout path
(ADR-108) shares one lead's bound across parts that may have different source transforms, and
whether the lead's bound still covers every part afterwards is not tested here.

## Correction, 2026-09-13: the prediction in this ADR is wrong

This document states that the defect "does not bite in the shipped Glowmere frame because the
authored `minScreenRadius` is ~1.5 px". That was an assumption, stated alongside measured claims,
and it has since been tested. **It is false.**

The fix was implemented as described here — fold `sourceTransform`'s largest column length into
`cullRadius` and into `groupRadius[lead]` — and the canonical frame captured on both arms, same
binary, fix stashed and restored, both runs under the GPU lock:

| | before | after |
|---|---|---|
| pixels differing by >2/255 | — | **89,019 (8.69%)**, max delta 226 |
| submitted triangles | 264,305 | **273,819 (+3.6%)** |
| visible instances | 2,161 | 2,020 |
| LOD distribution | 343 / 1,318 / 495 / 5 | **304 / 604 / 1,070 / 42** |

Both directions of the error appear at once, exactly as this ADR's per-layer table predicts:
instances demote to coarser rungs where the ladder had them too large (LOD1 1,318 → 604), while the
triangle count *rises* because layers it had too small — pebbles 3.0×, beacons 3.2×, elder-crown
8.2× — stop being culled while several pixels across and return to the frame. Visually the
foreground gains prominent pink blooms that were specks before: content the scene specifies and the
renderer was discarding.

**The fix is held on branch `fix/adr-152-cull-radius`, not merged.** It is a correctness fix, and the
reason for holding it is not doubt about its correctness: it changes the flagship scene's
appearance, and the scene may have been authored *against* the buggy behaviour — density tuned to
compensate for over-culling would now read as too dense. That is an art decision about Glowmere's
look rather than a rendering decision, and it needs the person who owns that look.

The reasoning in the section above for not fixing it inside a measurement task was sound. What was
unsound was predicting the visual consequence instead of capturing it.
