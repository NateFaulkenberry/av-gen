# ADR-481: The mip chain paid `std::pow` for a transfer function with 256 answers

**Status:** Accepted
**Date:** 2026-09-20
**Related:** ADR-480 (the interactive-performance pass this came out of), ADR-182

## Problem

The §4 load waterfall on `examples/world/glowmere-valley-2-multicam.json`:

    project load: 3236 ms total -- Reading the project 5 ms; Loading audio 508 ms;
    Building the scene 2219 ms; Layers and parameters 16 ms; Timeline and automation 489 ms
    ... texUp=2145.02ms/58  meshUp=14.91ms/1pass/869buf  env=18.74

**2.1 seconds uploading 58 textures — 37 ms each — against 15 ms for 869 mesh buffers.** It is the
largest single component of the load, and it is what a person waits through before the first usable
frame (§29).

It is not the GPU. `gpu::uploadTexture` builds the whole mip chain on the CPU, one level at a time,
and `downsample8` filters sRGB data in linear light — correctly, which is the point — by calling
`srgbToLinear` on every source channel of every level and `linearToSrgb` on every destination
channel. Both are `std::pow`. On a 512×512 image the chain is about 1.3 million `pow` calls, and
three quarters of them are the decode.

The previous investigation left this open in as many words: *"Why a single texture upload costs
~25 ms. Mipmap generation is the obvious candidate and it is one level deeper than this work went."*
It is the candidate, and this is that level.

## Decision

**The decode becomes a 256-entry table.**

Its input is a `std::uint8_t`. There are 256 possible answers. A table of them is not an
approximation of the function — it is the function's complete output, so **not one bit of any mip
level changes**, and that claim is checkable exhaustively rather than argued about.
`tests/rendering/test_texture_upload.cpp` checks all 256 against the formula written out again, so
a future edit to the curve in one place fails instead of diverging silently.

**The encode keeps its `std::pow`.** Its input is a float with no small domain to tabulate. An
interpolated table there would be an approximation — a different mip chain, for a saving a quarter
the size. §50 says an optimisation that changes the image has to be argued for; this one does not
have to be, and that is the whole reason the two halves are treated differently.

## Consequences

`gpu::srgbToLinear8` is now public, for the test. It is the only reason it is public and the header
says so.

**What is left undone, named rather than implied.** The mip chain is still built on the CPU, still
level by level, still with a `std::vector` allocated and returned per level, and still with a full
copy of level 0 to start. Generating it on the GPU with a compute pass, or caching the chain beside
the asset, would take the rest — and both are a different size of change than one lookup table.
This ADR is the part that is free and exact.
