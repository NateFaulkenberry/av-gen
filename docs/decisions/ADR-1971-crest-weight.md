# ADR-1971: The crest channel and its weight per train

- Status: Accepted (Musical Geography Phase 3, 2026-11-09). This is the art agent's request 1971, built by engineering
  within ADR-1953.

## Decision

Each wave train contributes `crest · u^(1 + 7·sharpness)` to the crest channel, where `u` is the train's phase mapped
to 0..1. Each ring contributes `crest · envelope`.

The channel is the sum, clamped to 0..1. With weights under 1, it brightens where trains cross: two trains at 0.5 reach
1 together and 0.5 alone. It reaches a material program through the `tileContour` op's y output, or through
`instanceEmissive.w` when no contours are on.

Parameters: `env/tiles/waveN/crest`.
