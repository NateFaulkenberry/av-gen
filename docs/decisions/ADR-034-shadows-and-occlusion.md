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

## Consequences
- Positive: contact, separation and form. The single largest visual gain available.
- Negative: a depth-only pass per shadow-casting light per cascade; AO needs the auxiliary targets
  from ADR-035; PCSS is expensive enough to reserve for one light.
