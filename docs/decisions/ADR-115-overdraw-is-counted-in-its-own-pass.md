# ADR-115: Overdraw is counted in its own pass, or not at all

**Status:** Accepted
**Date:** 2026-09-13

*Written after the fact. The change landed referencing this number and the file was never created —
a dangling reference in three places in `scene_renderer.hpp`. Recorded here from the implementation
and its tests.*

## Problem

The renderer is fragment-bound: the same 430k triangles cost 0.26 ms through a depth-only shader and
15.73 ms through the scene shader, and a tessellation sweep at constant coverage showed 4.9× the
fragment cost from triangle size alone. The diagnostic the engine most lacked was the one that shows
*where* fragments are being spent — how many times each pixel is shaded.

Apple's own counter for this is fragment-shader invocations ÷ pixels stored, available only through
Xcode. That tooling is out of scope for this repository, so the count has to come from inside.

## Alternatives

1. **Count in the existing scene pass.** One `atomicAdd` per fragment into a per-pixel buffer,
   always on, read when a view asks for it. Rejected: Apple documents that a fragment shader writing
   to a buffer **disables hidden surface removal**, because Metal must then execute every fragment
   even when occluded. Metal offers `[[early_fragment_tests]]` to recover some of it; **WGSL has no
   such attribute and Dawn emits none**. So this would make the renderer permanently slower and
   permanently unlike itself, to measure how fast it is.
2. **Infer overdraw from depth complexity.** Cheaper, and wrong: it counts geometry layers, not
   shaded fragments, and the two differ by exactly the quad overdraw this exists to find.
3. **A separate, opt-in counting pass.** Chosen.

## Decision

A dedicated pass (`shaders/overdraw_count.wgsl`) writing an `atomic<u32>` per pixel, **encoded only
when `AuxDebugView::Overdraw` or `::FragmentDensity` is selected**. Never on the normal frame path.
`Overdraw` shows the per-pixel count in colour bands; `FragmentDensity` is a 3×3 spatial average —
deliberately a different picture of the same data, since two views that hash alike are a diagnostic
that lies.

The pass draws plain, non-skinned opaque entities only, back-face culled to match the real opaque
pass, with `depthCompare = Always` because the depth attachment exists only to satisfy the render
pass. That scope limit is documented in the shader.

## Consequences

- The renderer's normal path keeps hidden surface removal. The measurement costs nothing when not
  asked for.
- The count is **not** the whole frame: procedural, skinned and blended geometry are not counted yet.
  On a scene whose visible content is entirely those — which RendererQA is — the view is legitimately
  blank, which is why these two views are deliberately **excluded from the `[views]` distinctness
  test**. Adding them there would have produced a confident, empty, meaningless picture.
- Verified rather than assumed: a test reads the counter buffer directly for a stacked-cube scene
  against a spread-cube scene. Overlap drives the count up (max ≥ 5) where spread does not
  (max == 1 exactly), the two views hash differently from each other, and the buffer is provably
  all-zero when an ordinary view is selected.

## Rejected

Counting in the scene pass. The cost is not the atomic — it is losing HSR on every frame forever,
with no WGSL mechanism to opt back in.
