# ADR-1156: An alpha flake system covers

**Status:** Accepted (proto/astral-forge, the production path of THE ASTRAL FORGE, iteration 4, step 9)
**Date:** 2026-10-06
**Resolves:** ADR-1153's "a dark flake in an additive system adds nothing rather than occluding" for the case that
wants occlusion: the prototype's flakes are splatted coverage-averaged, so dark plates hide what is behind them
and a collapse is a dark, glinting spray, not a white cloud.
**Implemented by:** the alpha branch at the end of `vs_flake` (`shaders/particles.wgsl`).
**Tests:** the comparison renders (`~/Desktop/av-gen-review/37-astral-forge/iter4/`); byte-identity of every
additive flake system (ADR-1145's table).

## Context

`vs_flake` folds everything that should thin a plate -- fusing into the surface it forms (`fuse`), the near fade of
a plate larger than its focus, and the hand-off to a shard (ADR-1147) -- into its colour. For an additive system
that is the same thing as thinning it. For an alpha system it is not: a fused plate became a black, fully opaque
plate in front of the surface it was supposed to dissolve into, which made `"blend": "alpha"` unusable for flakes
(no scene used it).

## Decision

**An alpha flake system draws the plate's own colour** -- its band reflection, its sparkle and its heat (ADR-1148) --
with `fuse x nearFade x (1 - shardTaken)` multiplied into its **alpha** instead of its colour: the prototype's
coverage-weighted average. A dark plate now occludes, a glinting one shows its glint, and a fused one lets the
surface show through. An additive system is unchanged: the same expressions, in the same order, feed its colour.

## Consequences

- No existing scene draws alpha flakes, and additive ones are byte-identical (the expression that fed their colour
  is untouched: the reflected term is now named, and multiplied by `fuse` in the same order).
- In alpha mode the plates are blended in slot order, not depth order (ADR-015's draw order). For plates a pixel or
  two across this is invisible; for large ones it would show.
