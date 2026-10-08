# ADR-1283: An authored cutout reaches a mesh whose asset has a material

- Status: Accepted (2026-10-08), proto/bio-valley (the VFX art pass, the 2.5D card experiment)
- Code: `src/scene/composition.cpp` (the procedural mesh path's `resolveMaterial`).
- Tests: `tests/unit/test_composition.cpp` (`[adr1283]`).

## Context

A procedural node with a mesh source takes each part's material from the asset. When the scene authors a
`material` block, the author's factors replace the asset's: base colour, opacity, emission, roughness and metallic.

Nothing else was carried over. An authored `alphaMode`/`alphaCutoff`, `doubleSided`, `unlit` and
`baseColorTexture` were parsed, validated and then dropped whenever the GLB carried any material. Every generated
GLB in the valley carries one.

The consequences:
- the 2.5D silhouette cards drew as opaque rectangles, with no texture;
- the valley's fronds, written with `alphaCutoff`, `alphaMode: mask` and `doubleSided`, were one-sided, uncut sheets;
- nothing logged it.

## Decision

When the material is authored:
- `doubleSided` and `unlit` are ORed with the asset's;
- an authored `alphaMode` other than opaque replaces the asset's, together with its `alphaCutoff`;
- an authored `baseColorTexture` replaces the asset's.

An author who sets none of them keeps the asset's own, as before.

## Consequences

- Scenes that authored these switches on mesh sources now get what they wrote. Every such scene that rendered
  before was rendering something its author did not ask for. The valley's fronds change (cut leaflets, both sides).
- Scenes that authored none of them render byte-identically.
