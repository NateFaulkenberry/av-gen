# ADR-1571: The headless live profile writes a per-frame trace

- Status: Accepted (art/rainbow-world-2)
- Amends ADR-1090 / ADR-1362 (`--live-profile`, `--trace`). Additive: `--trace` was live-only and ignored headless;
  nothing else changes, and a run without `--trace` is the same run.
- Code: `src/app/live_profile.{hpp,cpp}` (`headlessTraceHeader`, `headlessTraceRow`, `HeadlessTraceCounts`, the usage
  text), `src/app/live_profile_run.cpp` (the warm-up and measure loops of `runLiveProfileHeadless` write a row per
  frame; the file is written after the measurement).
- Tests: `tests/unit/test_live_profile.cpp` ("a headless trace row...", `[adr1571]`).

## Problem

Rainbow World's owner reported slow frames at the rim crossings and arch holds and "unexplained 50-70 ms spikes in
long runs". The headless profile reports percentiles and the five worst frames, with a wall-clock `atSeconds` that is
not the piece time, so a slow frame could not be placed on the ride, the pass that grew could not be named, and a
pipeline variant built mid-run (the perf agent's hypothesis for the spikes) could not be seen at the frame it landed.

## Decision

With `--mode headless --trace <csv>`, one row per warm-up and measured frame:

`phase,pieceT,frameMs,gpuMs,cpuWorkMs,sceneMs,depthMs,shadowMs,cullMs,otherMs,triangles,drawCalls,shadowDraws,
visibleInstances,compiles,compiledMaterials`

- `pieceT` is the fixed-step clock's render time: exact, so two runs can be compared frame by frame.
- The passes are the frame timeline's intervals summed by label (`shadow*` together, the rest in `otherMs`).
- `compiles` is `gpu::pipelineCounters().compiles()` since the run began: a step means a pipeline or shader module
  was created at that frame. `compiledMaterials` is whether that frame's lit draws used the ADR-1493 variant.

## Consequences

- Frame-by-frame comparison of two runs of the same window separates content cost (the same in both) from contention
  or clock effects (uncorrelated), which is how Rainbow World's spikes were diagnosed (rainbow-world/14).

## Addendum: the sonic trace's columns

`avgen --sonic-trace` (the offline signal dump `tools/check_response_range.py` reads) also writes the `stem.*`,
`beat.*` and `env.*` columns. Events are cleared at the end of `Engine::update`, so an event is traced through an
envelope source triggered by it (`env.<name>`). This let Rainbow World's stem analysis compare the engine's stem
events with the grid (rainbow-world/14 §5). Code: `src/app/sonic_trace_cli.cpp`; test:
`tests/integration/test_sonic_trace_stems.cpp` (`[adr1571]`). Existing readers select columns by name, so the wider
file changes nothing for them.

Since ADR-1601 (2026-10-22) an `env.<name>` column rises on the event's own frame, at 1.0 for an instant attack. It used
to rise one frame after the event and peak at 1 - dt/release, so `env.*` comparisons recorded before then carry that
frame. ADR-1602 adds the `signal.*` columns and `--trace-finals` (every route target's final value).
