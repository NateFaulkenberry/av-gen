# ADR-126: Glowmere's quad-overdraw share, and the instrument that had to be built to find it

**Status:** Accepted
**Date:** 2026-09-13

## Problem

Task C1, and the number the rest of Phase C is worth. Audit §4.5 confirmed quad overdraw as the
*mechanism* behind the resolution-independent cost, on a synthetic plane, and said explicitly that
it did not establish Glowmere's own share. Since then LOD0 (ADR-110) cut Glowmere from 430,231 to
264,305 camera triangles, so whatever share existed has moved.

## The instrument this was supposed to use, and why it could not

ADR-115 added `Overdraw` and `FragmentDensity` views that count fragment invocations per pixel in a
dedicated pass. That pass draws **plain, non-skinned opaque entities only** — its own header says
"procedural instances, SDFs, particles and transparency are outside its scope".

Glowmere's geometry is overwhelmingly procedural scatter. So the view is **not blank, and is worse
than blank: it is confidently partial.** Captured at 1280×800 on the canonical camera, the `Overdraw`
view shows the terrain chunks and nothing else — a field of 1× with cyan/green/red only along chunk
silhouettes and slivers. Every tree, fern, grass clump and mushroom in the frame is absent. Read
without knowing the scope limit, it says "Glowmere barely overdraws at all", which is a confident
answer to a question it did not ask.

Two things follow, and both are recorded rather than fixed here because they are in files this work
does not own:

1. Extending the counting pass to the procedural path is the change that would make these views
   answer this question. It belongs to whoever owns `procedural_renderer.cpp`.
2. **The `FragmentDensity` view is saturated and therefore useless on this scene.** Its grayscale is
   `density / max(info.y, 1.0)` and `info.y` defaults to **1.0** for this view
   (`scene_renderer.cpp`), so any pixel with one fragment or more maps to white. The captured frame
   is a pure black-and-white silhouette of the terrain against the sky. It needs a default scale in
   the range of the counts it displays — the `Overdraw` view's own bands run past 5.

## Decision

The share is measured **from the CPU**, by a committed analysis:
`tests/unit/test_triangle_size_analysis.cpp`, `[.analysis][triangles]`. It loads the world, walks
every visible entity and every scatter instance at the LOD level `cullLodLevel` would pick for it,
estimates pixels per triangle per drawable (ADR-122), buckets triangles by that, and weights each
bucket by the screen area it covers against §4.5's own measured curve.

### The result, 1280×800, canonical camera, this revision

| | drawables | triangles | coverage | below 4 px/tri | estimated excess |
|---|---|---|---|---|---|
| **all submitted geometry** | 2,603 | 207,727 | 3.37 Mpx (3.29× the frame) | **45.2% of triangles, 2.1% of coverage** | **1.52×** |
| authored entities (terrain, heroes, props) | 74 | 36,286 | 2.59 Mpx | 0.0% / 0.0% | 1.37× |
| procedural scatter (the ecology) | 2,529 | 171,441 | 0.77 Mpx | **54.7% / 9.0%** | **2.02×** |

Distribution over the whole frame, by px/triangle:

| px/triangle | triangles | coverage |
|---|---|---|
| < 0.5 | 11.8% | 0.1% |
| 0.5–1 | 8.0% | 0.2% |
| 1–2 | 11.8% | 0.5% |
| 2–4 | 13.6% | 1.2% |
| 4–8 | 15.8% | 2.7% |
| 8–64 | 23.4% | 15.2% |
| 64–512 | 14.5% | 57.6% |
| 512–4096 | 1.1% | 22.4% |

Worst layers by estimated excess, with their coverage: `valley_grass` 2.19× over 169 kpx,
`valley_ferns` 2.24× over 97 kpx, `valley_pines_m1` 3.34× over 26 kpx, `elder-filaments` 5.04× over
2.3 kpx, `elder-crown` 5.05× over 1.3 kpx.

### What this says

**Quad overdraw is real in Glowmere and is concentrated entirely in the ecology, but it is a smaller
prize than the triangle share suggests.** Nearly half of Glowmere's triangles are below the quad
threshold — and they cover 2% of the frame. A representation system that removed *all* of them would
recover about a third of the triangle-size-sensitive fragment cost, and most of that third comes from
the 8–64 px band rather than from the sub-pixel tail.

**The authored entities have no sub-pixel triangles at all** — 0.0% below the threshold. Terrain
after LOD0 is not the problem; the ecology is. That matches ADR-110's mechanism and it means the
remaining Phase C work has a well-defined target: the scatter layers, which are 23% of frame
coverage and carry a 2.02× estimated excess.

**A large part of the 2–8 px band is already handled.** Glowmere's scatters already run the ADR-029
ladder at 28 / 11 / 4 px with a billboard at LOD2 and a dot at LOD3, so §5.4's proposed impostor band
substantially exists. What is left is the 11–28 px band, where instances still draw a reduced *mesh*.

## What is estimated, and in which direction

Every one of these is a way this number could be wrong, stated rather than buried:

- **px/triangle is a CPU estimate**, resting on Cauchy's formula and a closed-mesh assumption
  (ADR-122). A leaf card is not closed and a grazing terrain chunk is not unbiased; both errors make
  the estimate **optimistic**, so the true excess is more likely above these figures than below.
- **Coverage is summed per drawable**, so a pixel is counted once per drawable over it — fragment
  invocations before hidden-surface removal, not pixels stored. The total is 3.29× the frame.
- **The gate curve is a shape, not milliseconds.** §4.5's shader is simpler than Glowmere's, so the
  multiplier applies to the triangle-size-sensitive component and not to the whole scene pass. It is
  never converted into a millisecond figure here.
- **The upper half of the curve rests on one point.** The analysis reports the excess split at 500
  px/triangle for exactly this reason. For Glowmere the unmeasured half contributes 1.01× of the
  1.52×, so the headline is carried by the measured half — but the split is printed every run so that
  can be checked rather than assumed. (ADR-124 names the four-arm sweep that would close the gap.)
- **The walk accounts for 207,727 of the renderer's 264,305 camera triangles**, plus 26,799 it
  reports as not modelled (bounding centre behind the camera, which the renderer's box test may still
  keep). The remaining ~11% is geometry drawn by paths this walk does not model — water surfaces,
  SDFs, splines. The shortfall is printed, not absorbed.

## Consequences

- Phase C remains the justified main line for the ecology specifically, and is **not** justified for
  the terrain, which after LOD0 has no sub-pixel triangles at all.
- §4.5's closing question — "if Phase C's first task shows a small delta, then quad overdraw is real
  but is not where Glowmere's time actually goes, and Phase B becomes primary" — is answered "not
  small, not enormous": 1.52× overall, 2.02× on the 23% of coverage that is ecology. **Phase B's
  per-pixel work remains at least as valuable as representation for this scene**, and that is a
  change from how the plan was ordered.

## Revisit when

The overdraw counting pass covers procedural instances, at which point this estimate can be checked
against a GPU counter rather than against its own assumptions — which is the only way to find out
whether the closed-mesh assumption is costing it a factor of two on the aggregate geometry.
