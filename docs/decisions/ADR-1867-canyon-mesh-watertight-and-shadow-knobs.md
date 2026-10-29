# ADR-1867: The canyon mesh is watertight and fine enough for a grazing sun; split scheme and shadow resolution become scene knobs

- Status: Proposed, scene/strange-attractor (canyon shadow pass 2, 2026-10-29).
- Code:
  - `src/scene/attractor_world.cpp`: the RTIN error propagation in level order.
  - `Environment::shadowSplitLambda` and `Environment::shadowResolution`, through the Composition (`environment.*`
    keys, saved only when set), `ShadowRenderer::update` and `SceneRenderer::updateLights`.
  - `examples/strange-attractor/strange-attractor-flythrough.scene.json`: `meshSpacing` 1, `meshError` 0.002,
    `meshMinError` 0.15.
- Tests:
  - `[adr1867]` in `tests/unit/test_attractor_world.cpp`: every interior edge is shared by exactly two triangles.
    Before: 269,303 open edges of 1.53M. After: 0.
  - The `[cascades]` and composition key tests cover the knobs.
- Evidence: `docs/prototypes/strange-attractor/10-canyon-shadows-2.md`.

## Context

The owner still saw stair-stepped shadow edges on the canyon walls ("jitter") after ADR-1866 fixed the cascades'
frame-to-frame scale. The coordinator's order was split scheme, then resolution, then a fifth cascade. Measuring each
step showed neither of the first two was the cause.
- **Split scheme** (lambda 0.6 and 0.5, range 2000 and 1500 m): edge flips and raggedness moved by a few percent either
  way, with no visible change.
- **Resolution 6144 and 8192:** the teeth on the wall are **pixel-identical** at 4096 and 8192.

A stair-step that does not change with the shadow map's resolution is not in the shadow map. It is in the caster:
- the silhouette of the terrain's triangles along a curved crest, sampled on a 2 m grid, cast through a 14° sun that
  stretches every facet about four times;
- plus hairline **cracks**: T-junctions the background showed through as dark-blue dashes that crawled along crests
  (2,965 such pixels in one frame).

The cracks came from the RTIN's error propagation, which was depth-first. A triangle's error includes its children's,
and a child's midpoint is shared with the neighbour across the child's hypotenuse. Depth-first reads it before the
neighbour's subtree has written it, so the parent under-reports and splits where its neighbour does not. The leaf level
also wrote errors at integer-divided cell centres (grid corners), polluting other triangles' errors. That is why the
mesh was 8.27M triangles at tolerances that bound it at 1.34M.

## Decision

- **Martini's level order.** Every triangle of a level is processed before any triangle of the level above. Triangles
  are addressed by heap id, so no triangle list is stored. Within a level they run in parallel, and shared midpoints
  are written with an atomic max. The finest level is the one whose midpoints lie on the grid.
- **The canyon's mesh:** 1 m sampling with tighter tolerances (0.002 per metre from the path, 0.15 m floor). That gives
  2.92M triangles: a third of the old buggy count, crack-free, and fine enough that a crest's silhouette is smooth at
  the grazing sun.
- **Two scene knobs**, kept because they are the right levers for other scenes even though they did not fix this one:
  - `shadowSplitLambda` (0 uniform .. 1 logarithmic; default keeps the engine's 0.85);
  - `shadowResolution` (raise only; 256..8192).

  The canyon sets neither.
- **No fifth cascade.** Step 3 was not needed.

## Consequences

- The canyon's mesh is watertight. Shadow teeth on crests are gone at the measured moments (`canyon-shadows2/`).
- Vertex and index memory: 238.6 → about 80 MiB. The bake takes about 5 s at 1 m sampling, up from 4.4 s.
- **The lattice changes.** The mesh wire draws these triangles, so its pattern changes with them: finer on near walls,
  coarser where the old leaf-level error inflation had over-refined. The lattice is on hold for the owner, so this is
  called out with frames, not tuned.
