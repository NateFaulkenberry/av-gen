# ADR-2241: procedural instance records are world space; the tracer does not apply `distributionTransform` again

- Status: Accepted (2026-11-26), eng/hybrid-rt (Hybrid RT H1; audit defect D2)
- Code: `pathtrace/snapshot.cpp` (procedural scatter)
- Tests: `tests/unit/test_pathtrace_environment.cpp` (`[adr2241]`)

## Context

Generation composes `distributionTransform` onto every placement (`generateCloud`; ADR-2138 moves the records the
same way). The raster draws the records under an identity object matrix (`scene_renderer.cpp`,
`procedurals_->update(..., identity, ...)`). The tracer multiplied the records by `distributionTransform` a second
time. Every unit test placed records by hand with an identity transform, and Glowmere's scatter has an identity
transform, so nothing noticed. In Neon Dreams every bobbing island, building and vehicle moved: the airship left the
frame, and 31% of the pixels both renderers covered disagreed in depth by more than 5% (2.5% after this change,
the rest being wires and particles).

## Decision

The instance transform is `T(position) * R(rotation) * S(scale)` of the record, exactly what `procedural.wgsl`
places under the identity model. The test generates a real grid with a translated and rotated
`distributionTransform` and requires every traced instance origin to equal the record the raster draws, with a
control that the records carry the transform.

## Consequences

A traced procedural is where the raster draws it, for every distribution the raster draws. Generators remain
unsupported (their records exist only on the GPU).
