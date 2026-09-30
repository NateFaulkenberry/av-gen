# ADR-1021: A deformed normal flips only for a mirror, never for the angle it has turned through

**Status:** Accepted (Sonic Garden POC, engineering follow-up to art pass 1).
**Date:** 2026-09-30

## Context

`vs_proc` (`shaders/procedural.wgsl`) rebuilds the normal of a deformed vertex by finite differences:
`nw = cross(p1 - p0, p2 - p0)`, where `p1` and `p2` are the vertex nudged along two source tangents with
`cross(t1, t2) = n`. It then flipped `nw` whenever `dot(nw, nRef) < 0`, where `nRef` is the source normal carried
through the instance and object transforms but **not** through the deformers.

That test confuses "the deformer turned the surface" with "the surface is inside out". A twist rotates the
surface's normals about its axis, so once the twist angle at a vertex passes 90 degrees the rotated normal faces
away from the undeformed one and was flipped. A twist with a `speed` always gets there: the art agent found a
sphere with twist speed 0.3 rendering a black equator from about five seconds on, and worked around it with
rotation routes.

## Decision

`cross(J t1, J t2) = det(J) J^-T n`. The rebuilt normal is the outward normal times the sign of the chain's Jacobian
determinant, and only that sign needs correcting. The deformers (bend, twist, sine, noise, displacement, field,
wind, the FX displacement) are continuous deformations from the identity and do not change it. A mirror does:

- the instance scale (`s.x * s.y * s.z < 0`);
- the object matrix (`determinant` of its linear part);
- a path deformer run backwards along its spline (`pathScale < 0`, with a valid spline and amount above 0.5).

The shader multiplies those signs and flips `nw` only when the product is negative. The degenerate fallback
(`nw = nRef` when the cross product vanishes) is unchanged.

## Consequences

- Twists of any angle shade correctly. `tests/rendering/test_procedural_gpu.cpp`, "A twist past a quarter turn keeps
  its normals outward" (`[adr1021]`), renders a sphere with a half-turn phase, a +-344 degree twist, and twist
  speed 0.3 at 10 s, against the untwisted sphere. Before the fix the three arms differed by 74, 37 and 72 (mean
  absolute, 8-bit) with 12-17% of the lit sphere at under half its brightness; after, all pass (mean under 3,
  under 1% darkened).
- **Folds.** Where a strong noise or displacement deformer folds a surface over, the old test forced the folded
  facets' normals back towards the undeformed side. Those facets' winding is also reversed on screen, so the
  fragment stage's `frontFacing` correction then turned them away from the viewer: dark streaks along the creases.
  Now the vertex normal agrees with the winding and the creases shade as the surface they are. This is the only
  change measured outside a twist past 90 degrees: in the Sonic Garden only the bass (the heavy world's fractured
  hero) changes, 0.1-0.2% of pixels at 12 and 20 s, and the pad, bell, perc, morph and context-pad are
  bit-identical at 1, 6, 12 and 20 s.
- Existing examples whose twists pass 90 degrees change where they did: Helix, Hyperspace, Worlds, Cathedral,
  Chamber (its world-space ring twist along a long tunnel), Lab. Temple changed in 0.01% of pixels at 1 s and not
  at all at 12 or 20 s. Frame diffs are in PROGRESS.md.
- Mirrored procedural instances were already not drawn correctly (with back-face culling, an instance with a
  negative scale renders as its far side); that is unchanged and outside this decision. For an undeformed or
  mildly deformed mirror the mirror term gives the same normal the old test did.
- Cost: one 3x3 determinant and a loop over at most eight deformer codes per vertex, only on the path that has a
  non-degenerate cross product.
