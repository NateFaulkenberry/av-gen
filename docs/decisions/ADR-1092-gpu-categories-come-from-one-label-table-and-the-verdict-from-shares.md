# ADR-1092: GPU categories come from one label table; the critical path from the measured shares

**Status:** Accepted (live optimizer, Stage 1c/1e). **Date:** 2026-10-03

## Decision

- `gpuCategoryTable()` (`app/live_profile.cpp`) maps every `gpu::FrameTimeline` label to one of: geometry/opaque,
  shadows, lighting, SDF, particles/simulation, volumetrics, post: bloom, post: depth of field, post: motion blur,
  post: other, temporal, composite/tonemap, other. Exact names first, then prefixes (`volume.*`, `distort.*`,
  `post/*`); anything unknown goes to "other" and is shown, never guessed into a neighbour. Unit-tested, including
  that grouping loses and double-counts nothing.
- **The verdict** (GPU, CPU, sync/present) is read from the medians: live, a GPU span at 85% of the interval is GPU
  bound, main-thread work at 85% is CPU bound, over budget with neither is sync/present. Headless serialises CPU and
  GPU, so it names the larger of the two and says sync is not measured there.
- **CPU categories** are the phases that exist (probe2's engine.update split, the renderer's `CpuFrameBreakdown`,
  the live loop's UI, waits and outputs), each labelled measured or coarse; "physics/simulation" is reported as not
  separately measurable because it is inside the scene update.
- **Limits stated in every report:** frames overlap on the GPU (ADR-1085), Metal writes no timestamp for an empty
  pass, the UI and output copies are not GPU-timed, and the results are specific to the machine.
