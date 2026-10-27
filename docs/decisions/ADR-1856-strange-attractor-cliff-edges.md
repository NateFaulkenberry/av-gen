# ADR-1856: Strange Attractor cliff edges: resolved terraces, a Lipschitz march, clean terminators

- Status: Accepted, scene/strange-attractor (Gate 2e, 2026-10-27).
- Scope: scene-local. `src/scene/attractor_world.cpp` (pass 1b' terraces; baked `slopeMax`), the renderer (an
  RGBA32F height texture), `shaders/attractor/world.wgsl`. No engine code changed.
- Evidence: `docs/prototypes/strange-attractor/gate2e-artifact-catalogue.md`.

## Decision

**Generator:**
- Terraces are applied in their own pass, with each riser at least 3 texels wide, and fade where they can't be
  resolved.
- A max-slope field (dilated over 5 texels) is baked into the height texture's b channel.

**March:** the step is `clearance × 0.9 / (1 + slopeMax × the ray's horizontal fraction)`, with up to 900 steps.

**Shadow:**
- Starts 25 m out, against the B-spline surface out to 1.5 km and the bilinear surface beyond.
- The start offset is 3.5 m along the normal and 2 m toward the light.

**Shading:**
- A rough-rock terminator: `((n.l + 0.18) / 1.18)^1.7`.
- Normals always from the B-spline gradient.
- No normal-map grain.
- Strata colour only on true faces.
- The ledge cut is soft, a third of its former depth, and centred.

## Consequences

- Offline cost rose from about 109 to about 123 ms per frame at 2× supersampling, because the slope-bounded march
  takes more steps beside cliffs.
- **The rough terminator is an art-direction change**: lit-to-shadow falloff on rock is softer.
