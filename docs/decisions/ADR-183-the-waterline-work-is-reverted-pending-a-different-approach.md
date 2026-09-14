# ADR-183: The waterline work is reverted, pending a different approach

**Status:** Accepted
**Supersedes:** the shipped behaviour of ADR-157, and the shader change it corrected
**Date:** 2026-09-14

## What is reverted

`shaders/water.wgsl` returns to its state before today's water work. Two changes go together:

* **The waterline reading the scene's depth** (`bf52530`). The shoreline fade, the foam band and the
  depth colour derived their vertical depth from the depth buffer instead of a per-vertex baked
  value, which fixed a stair-stepped bank on a coarse channel.
* **Its correction** (ADR-157): the axis-to-ray cosine, and the hand-written bilinear on the depth
  read.

## Why both, when only the filter was asked for

The filter exists *because of* the first change. `bedDepthAt`'s bilinear and the cosine were both
added to fix a forensics failure the depth-derived waterline introduced — the same patch of river
reading 5.7x differently when the camera turned. Reverting the filter alone leaves a state whose own
test suite fails, and a known-failing assertion is not a baseline anybody can think against.

Reverting both returns the water shader to a state that is green: 76 cases, 137,350 assertions, 0
failures.

## The reasoning being preserved

The judgement here is not that the waterline work was wrong. It is that **the problem may not be in
the water shader at all.** The reported artifact is shimmer interacting with ghosting across the
whole output, and post-processing that reacts to a shimmering surface is a different mechanism from a
surface that shimmers — with a different fix, or none, since it may simply be what that post chain
does to that content.

Two pieces of evidence from today support taking that seriously rather than treating this as a
retreat:

* **The largest artifact in this area was never the water's.** The dotted bands and lattices were an
  undersampled gaussian in `fs_wide` (ADR-159), localised by impulse response after three plausible
  water-adjacent fixes each changed the picture without removing the artifact.
* **The water's own most-blamed term is inert at the reported camera.** Zeroing `sparkle` produces a
  byte-identical frame at 960x540 *and* 1920x1080, because it is band-passed in screen space.

## What returns with the revert

The stair-stepped bank. The shoreline fade, foam and depth colour are again quantised to the water
mesh's tessellation, so a river on a coarse channel gets hard rectangular notches where quads end.
That was a real report and it is now un-fixed, deliberately, and should be picked up again once the
approach is decided — the diagnosis in `bf52530`'s message still stands even though its fix does not.

## What survives

Everything measured on the way is unaffected and still true: the anamorphic comb fix (ADR-159), the
attribution that water is 57% of Glowmere's flicker with ripple normals the largest identified share,
and that supersampling is refuted as a remedy. None of those depends on the reverted shader — the
attribution arms act on scene parameters, and the comb was in the post chain.
