# ADR-038: Procedural composition, visual hierarchy and negative space

- Status: Accepted (2026-09-09)
- Research: `docs/research/procedural-composition.md`

## Decision
- **Composition data** on the scene: a list of `FocalPoint` (position, radius, weight, optional
  bound object) and `DepthLayer` bands (near, mid, far, with density, contrast, saturation and
  detail multipliers), plus `ExclusionRegion` volumes.
- These are exposed as **fields**, which is what makes them integrate for free: a focal clearance
  field (a radial falloff around each focal point) multiplies generator density through the existing
  density filter; an exclusion field does the same with a hard edge; a weight field drives emission
  and scale through ordinary effectors.
- **Visual weight** becomes a point-cloud attribute (`weight`) that lighting, emission and scale
  effectors read, so a hero object is one attribute away rather than a separate code path.
- **Camera framing**: a composition target names a focal point and a screen position (thirds,
  centre, or explicit normalised coordinates); the camera behaviour solves for the offset that puts
  the point there, which is a two-dimensional nudge on top of whatever behaviour is driving it.
- **Scale hierarchy** is authored, not automatic: worlds are expected to carry objects at three or
  more scales, and the stress and showcase scenes demonstrate it. The engine supports it by making
  distance-based density, detail and LOD per object rather than global.

## Consequences
- Positive: procedural generation gains intent; density, contrast and clutter respond to what
  matters; negative space becomes an instruction.
- Negative: composition fields add per-object sampling cost; a scene that sets no composition data
  behaves exactly as today, which is the intended default.
