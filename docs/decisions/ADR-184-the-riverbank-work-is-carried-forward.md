# ADR-184: The riverbank work is carried forward; only the shimmer chase was reverted

**Status:** Accepted
**Supersedes:** ADR-183
**Date:** 2026-09-14

## The correction

ADR-183 reverted `shaders/water.wgsl` wholesale. That was too much, and the reason is a
misclassification worth recording: **the riverbank work and the shimmer chase were never the same
work**, and reverting by file rather than by intent took out the wrong one.

* The **shimmer / ghosting chase** was entirely in `shaders/post.wgsl` — the luminance-weighted bloom
  prefilter, the streak tap count, the coarse-source selection. All three were reverted at `c232776`
  and replaced by the properly localised fix in ADR-159.
* The **riverbank work** was in `water.wgsl` and was a different report with a different cause: the
  shoreline fade, the foam band and the depth colour derived their vertical depth from a per-vertex
  baked value, so every shoreline effect was quantised to the water mesh's tessellation and a river
  on a coarse channel got hard rectangular notches where quads ended.

ADR-157's cosine and hand-written bilinear are part of the *second* of those, not the first. The
bilinear is specifically what stopped one texel of depth quantisation reading as a step in how much
bed shows through — it is the smoother bank, not a shimmer remedy. The cosine is a correctness fix
without which the same patch of river reads 5.7x differently when the camera turns.

## What is restored

All of it: the depth-derived waterline, the axis-to-ray cosine, and the bilinear read. 76 cases,
137,350 assertions, 0 failures — the same green as before the revert.

## What stays reverted

Nothing in `water.wgsl`. The shimmer chase's three post-processing changes remain reverted from
`c232776` and are not coming back; ADR-159's fix stands in their place, and the open question about
how ghosting across the whole output reacts to a shimmering surface stays open by choice.

## The lesson

**Revert by intent, not by file.** A file can carry two unrelated pieces of work, and "revert the
water shader" is a sentence about a file while "revert the shimmer chase" is a sentence about a
change. When those disagree, the file wins by accident and takes a fix nobody asked to lose — which
is what happened, and was visible at the time only because the revert's own message listed the
stair-stepped bank as a consequence.

That listing is the part to keep doing. A revert that names what it un-fixes can be corrected in one
message; one that does not is found months later by whoever reports the bug again.
