# ADR-122: A drawable's importance is measured in pixels per triangle

**Status:** Accepted
**Date:** 2026-09-13

## Problem

Phase C of the renderer upgrade adds "an explicit, measured decision about how much a given object
is worth per frame" (target architecture §5.1). Nothing in the engine could state that. The GPU
ladder (ADR-029) knows an instance's projected radius; `Scene` knows a mesh's bounds; neither knows
what a drawable costs.

And the measurement says projected radius is the wrong number. Audit §4.5 held coverage constant at
one full screen and swept only tessellation:

| triangles | px/triangle | scene pass |
|---|---|---|
| 2 | 512,000 | 3.02 ms |
| 2,048 | 500 | **1.57 ms** |
| 131,072 | 7.8 | 3.21 ms |
| 2,097,152 | 0.49 | 7.73 ms |

Identical pixels throughout. **Cost is a function of triangle size, not of how much screen an object
covers.** An evaluator that reports projected radius is reporting the variable that was held
constant.

## Alternatives

1. **Projected radius alone**, as §5.4 sketched. Rejected by the table above: a fern and a terrain
   chunk at the same radius differ in tessellation by four orders of magnitude and therefore in
   fragment cost by the factor that table measures.
2. **One scalar importance score** combining size, distance, hero and motion. Rejected: the two
   consumers order objects differently. Representation wants px/triangle; a material tier wants
   pixels. Collapsing them is how quality settings become the single unusable slider §5.3 warns
   about, and the collapse is irreversible — a consumer cannot recover the term it needed.
3. **Measure px/triangle on the GPU** and read it back. Rejected: a readback stalls the frame being
   measured, which is the same objection ADR-077 records against reading indirect instance counts.
4. **A triangle count and a surface area, projected on the CPU.** Chosen.

## Decision

`scene::MeshMetrics` carries a mesh's triangle count, world-space surface area and bounding sphere,
cached per `(Scene::identity, Scene::meshVersion)`. `rendering::ImportanceEvaluator` turns a scene
and a camera into one `ImportanceRecord` per drawable: distance, projected radius, projected area,
**pixels per triangle**, screen velocity, hero. Pure arithmetic — no device, no traversal, no state.

Pixels per triangle is estimated, and the estimate is a geometric result rather than a tuned
constant:

- **Cauchy's formula.** The mean projected area of a closed convex surface over all orientations is
  `A/4`. Back-facing triangles are culled, so that quarter is the whole of what the front-facing
  half projects.
- **Half a closed mesh faces the camera.**

So mean front-facing projected area per triangle is `A / (2N)` in world units, times the square of
the pixels per world unit at that distance. `A` is measured from the mesh; only the two factors are
assumed, and both are named where they are used.

Projected radius uses **exactly** `shaders/cull.wgsl`'s arithmetic — `radius / distance × height /
(2 tan(fovY/2))` — rather than an exact sphere projection. An entity and a scattered copy of the
same asset standing beside it must not disagree about how big they are; that disagreement is a seam.

## Consequences

- The evaluator is a pure function of (scene, camera), so its output can be asserted exactly rather
  than eyeballed. `tests/unit/test_representation.cpp` does.
- A mesh whose area was never measured falls back to the **bounding disc**, which is never smaller
  than the true silhouette. Over-stating px/triangle keeps triangles, so the failure mode of an
  unknown mesh is too much geometry rather than too little.
- Two assumptions are wrong in known directions and are documented rather than corrected. A leaf
  card is not closed, so its true px/triangle is about half the estimate — the estimate is optimistic
  for exactly the aggregate geometry Epic warns about. A terrain chunk seen at a grazing angle
  projects far less than a quarter of its area, so the estimate is optimistic there too. Both make
  §4.5's diagnosis *more* likely to be understated, not less.
- No occlusion, no history, no budget. Occlusion is deferred with its evidence (§6); history belongs
  to the selector, where the frame-independence trade is made (ADR-125); a budget is policy.

## Rejected alternatives

- **Caching metrics on `Scene` itself**, beside `meshBoundsCache_`. Rejected to keep the derived
  copy out of the authoritative type: `MeshMetricsCache` is a separate object with a stated
  invalidation rule, which is what the forensics history (nine of eleven defects at an ownership
  boundary) argues for.
- **Counting triangles only, without area.** A triangle count without an area cannot say how large a
  triangle is, which is the only thing §4.5 says matters.

## Revisit when

A GPU counter for fragment invocations becomes available on this stack, or the overdraw counting
pass (ADR-115) is extended to the procedural path — at which point the estimate can be checked
against a measurement instead of against its own assumptions.
