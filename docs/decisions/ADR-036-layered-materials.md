# ADR-036: Layered materials, geometric inputs, triplanar mapping, decals

- Status: Accepted (2026-09-09)
- Research: `docs/research/layered-materials.md`

## Decision
- The existing interpreted material program gains **layers**: a program is a base plus up to four
  layers, each with its own ops and a mask expression, composited bottom-up with height-aware
  blending (`blend by max(height + mask, base height)`) rather than a linear mix, and each layer
  able to write any subset of base colour, roughness, metallic, normal, emission and occlusion.
- New **inputs**: curvature (from screen-space normal derivatives), convexity and concavity,
  cavity, ambient occlusion (from the GTAO target), height, normal variance, world position,
  object position, triplanar coordinates, distance to camera, and the identifiers.
- New **ops**: triplanar sample, world and object projection, height blend, detail normal combine
  (reoriented normal mapping), curvature mask, edge wear, decal box projection, anisotropy, and a
  roughness filter driven by normal variance for specular anti-aliasing.
- **Multi-scale detail** is a convention rather than a mechanism: macro masks from large noise and
  curvature, meso from patterns and decals, micro from high-frequency roughness and normal noise
  faded by screen-space footprint so it never aliases.
- Op count per program rises from 16 to 48, and the program block becomes a storage buffer rather
  than a uniform to accommodate it.

## Consequences
- Positive: surfaces stop reading as primitives; metal, stone and glass become genuinely different;
  wear and grime follow geometry instead of being painted uniformly.
- Negative: longer programs cost more per fragment (measured: about 1.3 ms per full frame of
  covered pixels for 16 ops, so 48 ops needs masking by material and quality tier); curvature from
  derivatives is approximate on instanced geometry.
