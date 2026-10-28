# ADR-1809: The headless benchmark (and `--ab`) can measure the live frame

- Status: Accepted (eng/render-improvements, 2026-10-28). Render audit recommendation R5 (gap G2b), approved by the
  owner.
- Code: `src/app/application.{hpp,cpp}` (`AppOptions::liveFrame`, `--live-frame`, `Application::installLiveRenderModes`,
  `runHeadless`), `src/app/live_profile_run.cpp`, `src/rendering/render_stats.{hpp,cpp}` (`BenchmarkConditions::frame`).
  Docs: `docs/help/reference-command-line.md`.

## Problem

`avgen --headless --ab <arm>` (ADR-051/113) measured the **offline** frame. `runHeadless` never installed the renderer
modes the live editor runs with (the comment at the old `application.cpp:4984` said so): ADR-233's deferred sky rebuild,
ADR-1070's capped live sky lighting, ADR-1492's velocity on demand, ADR-1493's compiled procedural materials and
ADR-1805's pipeline deferral. On Rainbow World, whose sky moves with the audio, the audit's `--ab` reported about 37 ms
of GPU and a 6.3 ms CPU sky rebuild every frame, against 18.3 ms and no rebuild under `--live-profile`, and its own null
A/B varied 18% between blocks. Live-facing A/B decisions on any scene with an audio-moved sky were unreliable.

The same five modes were set in two places already (`runLive`, and `runLiveProfileHeadless` since ADR-1600), copied by
hand.

## Decision

- **One function, `Application::installLiveRenderModes()`**, sets the five modes. The live editor, the live profiler and
  the new flag all call it, so they cannot drift apart.
- **`--live-frame`** makes the headless benchmark (an ordinary headless run and every block of `--ab`) call it, and also
  applies the live antialiasing floor (ADR-1024) unless `--live-aa` says otherwise, as the live profiler does. The quality
  tier, size and range are still whatever the command line says.
- **The default stays the offline frame.** `--headless` is the offline engine; an A/B for an offline decision (a render
  setting, an offline tier) must keep measuring what a render renders; and every existing script and every ADR that
  quotes an `--ab` number measured the offline frame, which a changed default would silently redefine. A headless A/B
  now **logs which frame it measures** and points to `--live-frame`, and the bench JSON records `"frame": "offline"` or
  `"live"`, so two records of different frames cannot be mistaken for one workload.
- With `--live-frame` the live sky schedule reads a wall clock, so frames are not bit-reproducible; the log says so.

## Consequences

- **Rainbow World**, `--ab none --ab-blocks 3`, 240 frames a block, 1080p, Feline from 60 s, one process each, back to
  back on 2026-10-28 (load average 3-4):

  | Frame | GPU per frame (baseline blocks) | CPU sky rebuild (`env=` in the update log) | Baseline block spread | Null A/B |
  |---|---|---|---|---|
  | offline (default) | 37.51 ms | 6.4-7.6 ms on most frames | 0.87% | -0.55 ms, inside the 3.1% floor |
  | `--live-frame` | **26.20 ms** | 0.0-0.8 ms | **2.70%** | +0.10 ms, inside the 2.7% floor |

  The audit's acceptance was "under 3% block spread and about 18 ms GPU". The spread is met. The sky rebuild is gone
  and the GPU frame fell by 11.3 ms, but it is 26 ms, not 18: the live profiler's 18.3 ms was measured with
  `--quality ultra`, the live ladder's Ultra level, while a headless benchmark renders the default quality tier
  (recorded as `qualityTier: default`). That difference is in the quality settings, not in the frame modes this ADR
  installs; it was not separately verified here. For a like-for-like live number use `--live-profile`; use
  `--ab ... --live-frame` for an interleaved live-mode A/B.
- The offline run's own block spread was 0.87% this time, not the audit's 18%: the machine was quieter (the audit's
  load average was up to 23), so the audit's 18% was partly the machine. The 6-7 ms CPU sky rebuild is reproduced. The
  11 ms GPU difference between the two frames is the five live modes together (the sky rebuild's passes, compiled
  procedural materials, velocity on demand); this run does not split it between them.
- The live profiler's and the editor's behaviour is unchanged (the same calls, in one place).
