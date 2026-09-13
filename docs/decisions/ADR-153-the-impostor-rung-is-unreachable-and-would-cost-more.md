# ADR-153: The impostor rung exists, imported meshes cannot reach it, and reaching it would cost more

**Status:** Accepted
**Date:** 2026-09-13
**Supports:** ADR-151's refusal of C5
**Corrects:** ADR-126's claim that Glowmere's scatters already draw billboards

## Two claims, and both were wrong

ADR-126 wrote: *"Glowmere's scatters already run the ADR-029 ladder at 28 / 11 / 4 px with a billboard
at LOD2 and a dot at LOD3, so §5.4's proposed impostor band substantially exists."* That is the
sentence C5 was going to be scoped around, and it is false in both directions at once.

**The billboard is not reached.** `makeLodMesh` documents rung 2 as "a camera-facing billboard quad
circumscribing the source's bounding sphere" and rung 3 as "a single point quad — a dot at distance".
But an imported mesh takes an earlier branch: `if (spec.kind == PrimitiveKind::Mesh && level > 0 &&
level <= 3 && spec.assetMesh)` returns a meshoptimizer simplification and never falls through to the
quad. Every Glowmere ecology layer sets `source.assetMesh`, so **not one instance in the scene has
ever drawn a billboard.** Printed by `[.analysis][bands]`:

| layer | rung 0 | rung 1 | rung 2 | rung 3 |
|---|---|---|---|---|
| `valley_ferns` | 260 | 91 | **31** | **9** |
| `valley_grass` | 79 | 28 | **9** | **8** |
| `valley_canopy_m1` | 545 | 187 | **65** | **21** |
| `valley_beacons` | 880 | 307 | **100** | **34** |

A billboard is 2 triangles. Rung 2 is between 9 and 100.

**And the renderer already believes otherwise.** `procedural_renderer.cpp` selects `pipelineNoCull`
for `level >= 2` on the stated grounds that "levels 2 and 3 are camera-facing billboards, so they are
never back-face culled". For an imported mesh they are neither camera-facing nor billboards — they
are closed-ish simplified meshes being rasterised two-sided, paying for back faces that would
otherwise be discarded. That is a small, real, unmeasured cost sitting on the far rungs of every
scatter in the engine, and it is recorded here rather than fixed because changing it changes the
image of the rung and belongs with whoever recalibrates the ladder.

## So make it reachable?

That was the obvious C5 — no bake pipeline, no atlas, just let an imported mesh fall through to the
quad the engine already builds. It was costed before it was written, and it is a **regression**.

`[.analysis][bands]` runs the frame with rungs 2 and 3 replaced by the quad, charged at the quad's
own coverage — `4 × projectedRadius²`, because a camera-facing quad circumscribing the bounding
sphere covers that whatever silhouette the mesh had:

| | triangles | coverage | weighted cost |
|---|---|---|---|
| as shipped | 207,727 | 3,530,872 px | — |
| rungs 2 and 3 as the engine's own impostor | 193,581 | **3,702,769 px** | **+3.25%** whole frame, **+16.1%** on the ecology |

**The billboard removes 14,146 triangles and adds 171,897 pixels of coverage.** A fern is a wispy
thing whose silhouette fills a fraction of its bounding disc; an opaque quad circumscribing that disc
fills all of it and then some. The frame is fragment-bound (§4.2) and coverage is what it pays for,
so trading 7% of the triangles for 5% more covered pixels is the wrong direction.

This is the whole reason a real impostor is an **alpha-tested, baked, view-dependent atlas** rather
than a quad — §5.5's `ImpostorAtlas`, and risk register row 11's "new asset-pipeline surface". The
cheap version is not a cheap version of the right thing; it is a different and worse thing.

## Decision

- **The impostor rung stays unreachable for imported-mesh sources.** Making it reachable is measured
  as a cost regression and would be a visible one as well: a solid quad where a fern was.
- **The two defects are recorded, not fixed:** the two-sided pipeline on far rungs that are not
  billboards, and the documentation in `procedural.hpp` and `procedural_renderer.cpp` that describes
  a rung the dominant source kind never gets. Both belong with the ladder's calibration.
- **A baked alpha impostor atlas is not built,** because ADR-151 measured its whole band's deletion
  at +1.8%. There is nothing for a correct impostor to win here either; it is the band that is
  empty, not the technique that is wrong.

## Rejected alternatives

- **Alpha-test the quad against the source's silhouette** without baking an atlas — one view, cut
  out. Rejected on the same measurement: the band it would serve is worth +1.8% deleted. A cheaper
  way to draw nothing valuable is still nothing valuable.
- **Shrink the quad below the bounding sphere** so it covers less. Rejected: it is then smaller than
  the object it stands in for, which is a silhouette that pops when the rung changes — trading a
  measured cost problem for an unmeasured quality one.

## Verified vs assumed

**Verified:** the branch in `makeLodMesh`, read. Every rung triangle count in the table, printed by a
committed analysis from the real meshes. The `pipelineNoCull` selection for `level >= 2`, read. The
coverage arithmetic of the impostor arm, which is committed and re-runnable.

**Assumed:** that a camera-facing quad covers `4 × projectedRadius²`. That is exact for a quad
circumscribing the bounding sphere and square-on to the camera, which is what `makePointQuad` builds
and how the Point path orients it; it ignores that the projected bounding *sphere* is a disc, so the
figure is if anything generous to the quad by 4/π on the corners it wastes. Also assumed: that the
two-sided pipeline on far rungs actually costs something. It is not measured, and it is small.
