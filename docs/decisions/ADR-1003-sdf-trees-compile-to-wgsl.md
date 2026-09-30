# ADR-1003: SDF trees compile to WGSL (ADR-1000's fallback, taken)

- Status: Accepted (2026-09-30), proto/procedural-space
- Implemented in `src/spatial/sdf.cpp` (`sdfCompileWgsl`, `sdfCompileTable`, `sdfCompileKey`),
  `src/rendering/sdf_renderer.cpp` (pipeline variants) and `shaders/sdf_raymarch.wgsl` (the
  `sdfField` hook).
- Tests: "SDF compile: ..." in `tests/unit/test_sdf_space.cpp`, and "SDF compiled trees render like
  the interpreter" in `tests/rendering/test_sdf_gpu.cpp`.

## Context: the measurement ADR-1000 asked for

The example's hall state is one full-screen SDF object with 54 nodes and about 35 executed records
per evaluation. On this Mac at 960x540 (render scale 0.5 of 1080p) the **interpreted** frame was
140 ms: the `sdf` pass took 107 ms and the depth prepass 32 ms. The march itself was healthy, at
20.5 average steps and a 99.9 % hit rate, so the cost was per evaluation. The interpreter keeps
dynamically indexed private stacks and dispatches on each record's kind. ADR-1000's revisit trigger
("cannot hold an interactive frame at render scale 0.5") fired.

## Decision

- An `SdfObject` with `compile: true` is drawn by a pipeline whose module replaces the pass's
  `sdfField` hook with the tree compiled to straight-line WGSL. The compiled code is kind-specialised:
  - the exact primitive helper for each node;
  - each combination inlined as `min`, `max` or `sdfSmin`;
  - per-kind warp and displacement functions, split out of the interpreter's dispatchers, which now
    call them, so both paths share the formulas;
  - `recurse` as a bounded `for` loop;
  - a `morph` whose children are each emitted once behind a branch on the amount.
- **Parameters stay live.** Every node's values are read from a per-node table in the same storage
  buffer, repacked every frame. `sdfCompileKey` hashes only the kinds, the child structure and the
  enabled flags. A route, a state or the timeline moving an amount, a count or a size never
  recompiles; toggling `enabled` or editing the structure does.
- Variants are cached by key. A compile failure logs once and falls back to the interpreter. A
  shader reload clears the cache.
- The raymarch pass evaluates the field through a few call sites, because Metal inlines a compiled
  field at every one of them: the march (shared by all entries), and loops for the normal, AO, edge
  and shadow.

## Results (960x540, hall, same march: 20.5 average and 160 maximum steps)

| | frame (GPU p50) | `sdf` pass | depth prepass |
|---|---|---|---|
| interpreted | 140.3 ms | 107.2 ms | 32.0 ms |
| compiled | 8.1 ms | 6.5 ms | 1.05 ms |

The one-time compile for the example is about 3 s the first time a structure is seen (Metal's
compiler, in the first frame).

## A defect this found, fixed here

The first attempt emitted generic `sdfPrimitive`, `sdfWarp` and `sdfFinishUnary` calls. Metal then
inlined every kind's body, fbm and voronoi included, at every node and every call site. The compile
did not finish in 10 minutes and grew to 2.9 GB. Kind specialisation is required, not optional.

Separately, ADR-1002's step-statistics loop gave the lit pass its own march. The lit and depth-prepass
entries then compiled to different float code, and about a third of the surface failed the lit pass's
LessEqual depth test by an ulp, which showed as dark speckle. Every entry now calls one `sdfMarch`
function, and the lit pass writes its depth 4 ulps nearer (`sdfLitDepth`).

## Consequences

- The interpreter remains the default: it is the reference, and it needs no compile. `compile` is
  opt-in per object. The example turns it on.
- A structural change hitches for the compile. That is acceptable for authoring and invisible to a
  render, whose structure is fixed.
