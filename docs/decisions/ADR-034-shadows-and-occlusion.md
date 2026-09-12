# ADR-034: Cascaded shadows, contact shadows, ground-truth ambient occlusion

- Status: Accepted (2026-09-09)
- Research: `docs/research/shadows-and-occlusion.md`

## Decision
- **Cascaded shadow maps** for directional lights: 3 cascades by default (4 at high tier), stabilised
  by snapping the light-space origin to texel increments, fitted to the visible depth range.
  Filtering is PCF with a rotated Poisson disc; the key light additionally uses percentage-closer
  soft shadows (blocker search then variable-width filter) so an area light reads as soft.
  Normal-offset plus slope-scaled bias.
- **Spot lights** get a single perspective shadow map when `castsShadow` is set; point lights rely
  on contact shadows and occlusion rather than cube maps in the first implementation.
- **Screen-space contact shadows**: a 12-step depth march toward each shadow-casting light, combined
  with the shadow map by minimum. This is what makes small parts sit against each other.
- **Ambient occlusion**: GTAO from the depth and normal targets with a bent normal, temporally
  reprojected with neighbourhood clamping and a deterministic per-frame sample rotation, applied to
  ambient diffuse and (via the bent normal) to specular.
- Shadow-casting geometry includes instanced procedural objects (the same instance buffer, drawn
  depth-only) and meshed SDFs; raymarched SDFs cast through their bounding box in the depth-only
  pass at reduced steps, and receive normally.
- Budgets: shadow atlas 2048 square per cascade at the realtime tier, 4096 at high; AO at half
  resolution with a bilateral upsample.

## Point and area lights, added 2026-09-12

The original decision gave directional lights cascades and spot lights one perspective map, and left
point and area lights to contact shadows and ambient occlusion. That showed: a lamp hung over a scene
lit the floor straight through whatever was between them.

**Six faces in the same atlas.** A light with no single direction is six ordinary views — +X, −X,
+Y, −Y, +Z, −Z, each a 90-degree perspective from the light's own position — and the shading pass
picks one per fragment by the dominant axis of the direction from the light. No cube texture, no cube
sampler, and the depth passes the encoder already writes per view are unchanged; `ShadowRenderer`
gained a fit function and a flag, and nothing else in the pipeline learned about cube maps.

Three details that are not obvious:

- **The faces overlap slightly** (90° × 1.04). Tiled exactly, a PCF kernel at a face's edge samples
  outside it, `shadowLookup` reports that as invalid, and the shading pass reads invalid as
  *unshadowed* — a bright seam along every face boundary. The overlap costs nothing, because the
  face is still chosen by the dominant axis.
- **Every area shape gets the cube too.** A Rect or a Disk emits into a hemisphere, so three of its
  six faces are wasted — but a single perspective map cannot cover 180°, and one rule that is right
  for every shape is worth more than a special case per shape that is nearly right.
- **Six of the eight views.** `kMaxShadowViews` is 8 and is not raised: the atlas is 16 MB a layer at
  2048². So one shadowed point light is most of the frame's budget, it is opt-in through the light's
  own `castsShadow` (default false), and a light that does not fit the remaining budget goes without
  a shadow rather than getting a partial cube.

The face order is shared between `rendering::pointShadowFace` and the shader's `cubeFace` by
construction and by nothing else, so a unit test walks 400 directions over a sphere and asserts that
whatever face a direction picks, that face's own projection actually contains the point.

## Consequences
- Positive: contact, separation and form. The single largest visual gain available.
- Negative: a depth-only pass per shadow-casting light per cascade; AO needs the auxiliary targets
  from ADR-035; PCSS is expensive enough to reserve for one light.
