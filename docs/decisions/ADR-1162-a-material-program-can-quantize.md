# ADR-1162: A material program can quantize

- Status: Accepted (proto/digital-mosh)
- Amends ADR-030 (material programs). It is an appended op, so the wire format of every other op is unchanged.
- Found by: DIGITAL MOSH. Its corruption has the grain of a codec, aligned square macroblocks (the research's
  G3), and nothing in a material program could make a square.

## Problem

Every pattern op in a program is round:

- `noise` and `triplanar` give blobs;
- `voronoi` and `voronoiEdge` give polygons (ADR-1069).

There was no `floor`. A program could not snap a position to a lattice, so it could not:

- sample a value that is constant over a square;
- give each square its own random, to fail cells one at a time against a mask.

## Decision

Add one op, `quantize`:

```text
c   = floor(a.xyz * f + k.xyz)           // f = cells per unit (the op's `value`), k = a lattice offset
out = vec4((c + 0.5 - k.xyz) / f,        // the centre of a's cell, in a's own units
           hash01(c, seed))              // a uniform random per cell (noise.hpp / noise.wgsl hash01)
```

When `f <= 0`, the op passes `a` through. It reads only `a`, like the other position ops.

What this gives a program:

- **A constant over a square:** quantize a world position, then run `noise` at the result.
- **A probabilistic block mask:** `step(out.w, mask)`, so cells fail one by one as the mask rises. That is a
  stain with a macroblock edge.
- **A block size that is a parameter:** `value`, routable like every op constant.

## Consequences

- DIGITAL MOSH's plain and fragments stain in square cells. The cells flip one at a time as the contagion
  field rises past their own random.
- Tests:
  - `quantize gives the cell centre and a per-cell random` (`[adr1162]`): the cell centres, the same cell giving
    the same random, the neighbour differing, the lattice offset, the pass-through, and uniformity over 4,096
    cells (mean 0.5 ± 0.03, a quarter below 0.25);
  - the GPU parity batch in `test_material_gpu.cpp` (the `quantize` probe) against the CPU twin.

## Rejected alternatives

- **A general `floor` / `fract` pair.** That is two ops and two registers to do what one op does, and it still
  needs a hash for the per-cell random, which no op exposes.
- **Quantising inside `field` sampling** (sampling a field at the cell centre). A `field` op samples at the
  fragment's world position by design: ADR-050 hoists every field sample out of the op loop, and that is what
  made field ops cheap. A per-cell random thresholded against the field gives the same macroblock edge.
