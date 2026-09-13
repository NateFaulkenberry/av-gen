# ADR-121: framebuffer fetch is not available here, and the reason is the driver, not the language

Status: accepted

## Context

Scope item B3 asked for a feasibility report on framebuffer fetch: a fragment shader reading the
attachment value already in tile memory instead of round-tripping it through a texture. On Apple
silicon it is the mechanism behind single-pass deferred shading, and it would in principle let the
scene pass and the passes after it share tile memory instead of system memory.

The standing assumption in this repository was that WGSL's capability wall settles it -- no 64-bit
atomics, no texture atomics, no `[[early_fragment_tests]]` (ADR-115 was written against that last
one). That assumption is correct about the language and was never checked against the driver.

## What was measured

`gpu::Context` now asks the adapter, at creation, which of the tile and bandwidth features it has,
and logs both lists at debug level. On this machine -- Apple M2 Max, Dawn/Metal, the pinned
prebuilt Dawn in `.cache/cpm/dawn_prebuilt` -- the answer is:

```
present: TransientAttachments, DualSourceBlending, Subgroups, ShaderF16
absent:  FramebufferFetch, PixelLocalStorageCoherent, PixelLocalStorageNonCoherent,
         MSAARenderToSingleSampled
```

Dawn defines `FeatureName::FramebufferFetch` and both `PixelLocalStorage` variants. This adapter
advertises none of the three.

## Decision

**Framebuffer fetch is not available and no work is planned against it.** Not "WGSL cannot express
it" -- that is true and is the smaller half of the reason. The larger half is that the device does
not offer the feature, so there is nothing to express it *into*, and no amount of shader authoring
changes that. The same applies to pixel-local storage, which is the other way the same idea is
spelled.

This also retires a soft assumption that the wall is the language. Two of the four features asked
about *are* present, so "Dawn/WGSL will not let us" is not a general answer and should not be used
as one: the list is per-adapter, per-Dawn-build, and it is now printed rather than remembered.

## Consequences

* The multi-pass structure stands. Anything that wants a previous pass's value reads it as a
  texture, which is what the renderer already does.
* ADR-115's reasoning is unaffected and is now better supported: it rejected counting overdraw in
  the scene pass because a fragment shader writing to a buffer disables hidden surface removal and
  WGSL has no `early_fragment_tests` to recover it. Neither framebuffer fetch nor pixel-local
  storage would have offered a way round that either.
* Re-check after any Dawn upgrade. The probe is two log lines and costs nothing; the finding it
  produced is worth more than the assumption it replaced.
