# ADR-156: A wide effect reads a low-frequency source, rather than sampling a fine one harder

**Status:** Accepted
**Date:** 2026-09-14
**Corrects:** ADR's absent predecessor — the tap-count fix committed in `01287ae`, which halved the
artefact's period without removing it

## The report

Post-processing over Glowmere's river printed two things that are not in the scene: evenly spaced
**dotted horizontal bands** across the whole frame, and small **square lattices of dots** in dark
parts of it, far from any water. Both scaled with the anamorphic tier and neither responded to
`post/anamorphic/ghosts`.

## What was actually wrong

Both are the same mistake made twice: a deliberately *wide* effect point-sampling a deliberately
*fine* texture.

The water writes a hard-thresholded sparkle field to the emission target (`water.wgsl`, §15 highs)
— by design a field of discrete points on a near-regular grid. The bloom pyramid keeps it. Then:

* **The streak** is a horizontal gaussian reaching `stretch * 8` output texels — at the authored
  stretch of 10, a seventh of the frame's width. Its tap count is bounded. Spread over the finest
  bloom level, consecutive taps landed several source texels apart, so each tap *copied* the sparkle
  rather than integrating it: one dot per tap, hence bands. `01287ae` raised the count from 8 a side
  to 48 and capped the step at one source texel — but the cap could not be honoured at 48 taps, so
  the comb survived at a quarter of its period. That is the version the user described as "bands of
  shimmering… looks bad compared to before".
* **The ghosts** are *magnified* reads (1.33x and 2.5x). Magnification enlarges whatever structure
  the source still holds, which is how a lattice ends up printed on an unrelated part of the frame.

## The decision

**Lower the source's frequency instead of raising the sampling rate.** `PostProcessor::run` now
walks down the bloom pyramid until one texel is at least as wide as the step `fs_wide` will take,
and hands *that* level to the pass; the level is recorded in `PostStats::anamorphicLevel`. The
streak keeps its reach, its sigma and its energy — only detail a seventh-of-a-frame blur discards
anyway is gone. The ghosts read the same level, which is what a defocused lens copy should be.

The alternative — enough taps to close the gap at full resolution — is hundreds of taps per pixel
for detail the effect immediately throws away.

Two details that are not incidental:

* The walk's tap budget (48 a side) has to equal the shader's clamp, or it stops one level short.
  The shader still derives its count from the source it was actually given, so the two cannot
  silently disagree: a missing coarse level raises the count instead of reinstating the comb.
* The ghosts' 3x3 supersample is weighted 1-2-1 per axis, not flat. A flat box over three taps a
  texel apart has a flat top and an abrupt edge, so a magnified point source comes out **square** —
  which is what the first attempt printed, trading a lattice of dots for a lattice of little blocks.
  That intermediate state is visible in the evidence below and is why the weights are there.

## Evidence

Glowmere, camera forced top-down over the river at (10, 60, 95), t = 20.0 s, 1280x720, anamorphic
on vs. off, difference amplified 10x — so the image *is* the tier's entire contribution.

| | streak | ghosts |
|---|---|---|
| before (`01287ae`) | dotted bands, period ~4 px | square 3x3 lattices of dots |
| after | one continuous smooth band | soft round blobs, no lattice |

The comparison is a controlled one: same project, same frame, same build except the change.

## What this does not fix

The sparkle field itself is still a near-regular grid of points, and anything that magnifies it will
show that. Nothing in the post chain magnifies it any more. If a future effect does, this is the
rule it has to follow.
