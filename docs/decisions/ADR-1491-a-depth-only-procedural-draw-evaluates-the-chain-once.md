# ADR-1491: A depth-only procedural draw evaluates the deformer chain once

- Status: Accepted (perf/resonator-live)
- Amends: ADR-023 (procedural geometry), ADR-034 (instanced procedurals cast shadows through the depth pipeline),
  ADR-1371 (a program-cut procedural's prepass runs the cutout program).
- Code: `shaders/procedural.wgsl` (`procVertex`, `vs_proc`, `vs_proc_depth`),
  `src/rendering/procedural_renderer.cpp` (`createPipeline`).
- Tests: `The depth-only procedural vertex stage lands on the lit pass's depth (ADR-1491)` and the unchanged
  ADR-1371 cases (`tests/rendering/test_procedural_gpu.cpp`).
- Evidence: `docs/prototypes/resonator-quality/10-report-live-performance.md`.

## Problem

Every procedural vertex runs the whole deformer chain four times:

- `p0`, the position;
- `p1` and `p2`, two finite-difference points for the normal;
- `pPrev`, the chain at last frame's time, for the velocity target.

The depth prepass and the shadow passes drew through the same `vs_proc` as the lit pass (ADR-034's "the same vertex
stage, so the depth matches"). So they paid for a normal and a velocity that a depth-only pass never writes.

On the Resonator scenes the procedurals are large deformed meshes:

- Cymatic World's ground has 172k vertices;
- Waveform Dimension's four sheets have 112k vertices and five field deformers.

There, the depth prepass alone measured 4.9 ms and 8.4-9.2 ms a frame, constant across render scales, which made it
pure vertex work.

## Decision

One vertex function, `procVertex(in, instance, depthOnly)`, with two entries:

- `vs_proc` calls it with `false`;
- `vs_proc_depth` calls it with `true`. The procedural depth pipeline, used by the prepass and the shadow passes,
  draws through it.

`depthOnly` is a constant at each call site, so the compiler drops what a depth-only draw does not read:

- **`p1` and `p2`.** These are kept only when the object's material is masked (`object.flags.x == 1`), because
  `fs_proc_depth` runs a masked material's cutout program, which reads the normal (ADR-1371).
- **`pPrev`.** `prevClip` is set to `clip`.

`p0` is the same expression in both entries, and `clip` stays `@invariant`. So the lit pass's `LessEqual` test
against the prepass depth still passes on exactly the surfaces it drew before.

## Consequences

- **The picture is identical.** The GPU test renders a deformed, overlapping column field with and without a depth
  prepass, and the two images hash equal. A one-bit difference between the two entries' positions would make the lit
  pass fail its depth test and show the background. The Resonator scenes' live captures are pixel-identical before
  and after (the report's table).
- **Measured.** The headless A/B and the live matrix are in the report (§4).
- Masked procedurals keep the full normal in the prepass, so ADR-1371's cutouts are unchanged.
- The point-source, fibre and wire paths have their own vertex code and are unchanged.
