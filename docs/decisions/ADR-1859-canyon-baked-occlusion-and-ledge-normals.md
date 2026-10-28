# ADR-1859: The canyon mesh bakes its occlusion; the rock draws its ledges as detail normals

- Status: Proposed, scene/strange-attractor (canyon Phase 1b, 2026-10-27). It is accepted with ADR-1857.
- Code:
  - `src/scene/attractor_world.{hpp,cpp}`: `horizonOcclusion`, the uv.y packing, `kAttractorPlayaLevels` and
    `kAttractorOcclusionScale`.
  - `examples/strange-attractor/strange-attractor-flythrough.scene.json`: the `canyonRock` program's decode, its
    `normal` and `occlusion` outputs, and the look (see the evidence doc).
- Tests: `[adr1859]` in `tests/unit/test_attractor_world.cpp`.
- Evidence: `docs/prototypes/strange-attractor/05-canyon-phase1b.md`.

## Context

Phase 1 moved the canyon to engine geometry (ADR-1857). Compared with the gate2e ray-march, two things were lost.

**The crevices were not darkened.** GTAO draws fins along the RTIN's long triangles, so it stays off. Contact shadows
drew a dotted checker on lit walls, so they are off too (see ADR-1858's trace).

**The rock read as plastic close up.** The ray-march cut per-pixel ledges along the strata. A 2 m height grid cannot
resolve them, so the mesh carries only the smooth surface.

## Decision

**Baked occlusion.** Each mesh vertex gets a horizon search over the height grid:
- Eight directions, at distances that double from 2 m out to 256 m.
- Each direction counts only what rises above the slice's horizontal, or above the surface itself where that is
  higher. A wall therefore isn't darkened just for facing sideways (the sky term already accounts for that). A gorge,
  a slot or the foot of a cliff is darkened.
- The visibility is packed into uv.y beside the playa mask:
  - `uv.y = level + 0.05 * visibility`, where `level` is the playa mask quantised to 0..16.
  - The scale is kept small on purpose. The shader builds its tangent frame from the uv gradient, so uv.y must not
    out-vote the band coordinate in uv.x.
- The program recovers both values with `quantize`. The visibility feeds the program's `occlusion` output (ambient)
  and darkens the albedo slightly.
- Rock coverage no longer enters uv.y. The program only ever read the playa threshold of it.

**Ledges as detail normals.** The band coordinate is uv.x, so the cotangent frame's tangent runs up the strata. The
program writes a tangent-space normal `(s, 0, 1)`, where `s` is built from four parts:
- an under-lip that faces down at each band's foot;
- a lip that faces up at its head;
- five laminations per band (the `palette` op's cosine);
- a low-frequency triplanar relief.

The laminations and relief fade out between 280 and 700 m, so they never alias. The whole perturbation applies to
walls only.

There is no shared engine change. The bake is the attractor generator's, and the shading uses existing program
outputs.

## Consequences

- Bake time: the horizon search adds about 64 grid reads per mesh vertex, in parallel. The engine's material-program
  limit is 64 ops; the program now uses 62 of them.
- Full-flight flicker after the look change: irregularity max 0.23 and p90 0.054 at 960x540. That is unchanged from
  the flicker fix alone (max 0.27).
