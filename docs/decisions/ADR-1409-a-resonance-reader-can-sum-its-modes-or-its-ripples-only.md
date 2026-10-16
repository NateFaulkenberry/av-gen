# ADR-1409: A resonance reader can sum its modes only, or its ripples only

- Status: Accepted (2026-10-16), proto/cymatic-split (Cymatic World light pass, Stage D: travelling illumination)
- Code: `ResonanceSpec::rows`, `ResonanceRows`, `resonanceRowHeard` (`src/spatial/resonance.*`); bits 2-3 of a
  reader's `gridBounds0.w` (`src/spatial/field.cpp`); `resonanceRowHeard` in `shaders/fields.wgsl`, checked in
  `resonanceSums` and `resonanceShadingAt`.
- Tests: `tests/unit/test_resonance_motion.cpp` ("A reader's rows select the standing modes or the ripples, and the
  two add up to all"); GPU parity in `tests/rendering/test_resonance_gpu.cpp` (a ripples-only displacement reader and
  a modes-only shading reader in the ADR-1403 case).

## Context

Onset ripples (ADR-1403) are rows of the mode table after the modes. Every reader sums them with the modes, which is
right for the geometry. The colour, lighting and optics brief asks for light that travels: a pulse that moves through
the figure and is gone, localised rather than a global ring. The ripples are already that motion, but a reader could
not separate them from the standing figure. A listening window does not help, because ripples sit on the same
dispersion axis as the modes (`rippleU`).

## Decision

A reader's `"rows"` is `"all"` (the default, ADR-1400), `"modes"` (the standing modes only) or `"ripples"` (the onset
ripples only). It is packed in bits 2-3 of `gridBounds0.w` (bit 0 `listenAlongAxis`, bit 1 `sandNormalized`). The CPU
and GPU samplers skip a row whose geometry (shape.w mod 4; 2 = ripple) the reader does not hear. The default is not
written to JSON, so existing scenes save byte-identically.

## Consequences

- A ripples-only reader is exactly 0 with no onset alive, so a light drawn from it is dark in silence and travels with
  each ring. A modes-only reader gives the standing figure without the rings' disturbance.
- `all = modes + ripples` for every linear channel (displacement, the signed figure); the quadratic channels (sand,
  amplitude) are of the selected rows' own sum.
- Cost: none when `all`; otherwise one extra table load per row.
