# ADR-020: Offline rendering — render jobs, sequences, native video, queue, determinism

- Status: Accepted (2026-09-08)
- Research: `docs/research/offline-rendering.md` (time model §1, GPU determinism §4, headless
  §5, readback §6, PNG §7, video licences §8, how Unreal/Blender/Notch/TouchDesigner do it §9,
  queues §11), ADR-012 (time), ADR-015 revision (deterministic particle compaction)

## Problem

Milestone 1.0 must turn a project into frames: exact-time offline rendering that is bit-identical
run to run, written as an image sequence or a video with the audio muxed, at any resolution and
frame rate, in the background of the live app or headless, and in batches.

## Alternatives considered

1. Re-use the live loop with a fixed clock and `--capture` per frame (0.1 headless mode).
2. Link libav* for video (LGPL/GPL obligations, patent exposure).
3. A `RenderJob` with its own offline engine and renderer, PNG encoders on threads, native
   AVFoundation video, optional external ffmpeg process, JSON render queue (chosen).

## Decision

- `app::RenderSettings` (size, fps, range, output kind and path, pattern, codec, backend,
  quality, audio mux, threads) lives in the project (`"render"`) and is overridable from the CLI.
- `app::RenderJob` owns an **Offline** `Engine` loaded from the project file (so a render is a
  pure function of the project) and a `SceneRenderer`; frame f is rendered at
  `start + f / fps` via `FixedStepClock`, read back (`Image8`), hashed (per-frame and sequence
  hash), and queued to encoder threads: PNG frames in parallel, video frames in order to one
  writer thread. `step()` bounds work so the live app renders in the background with a progress
  bar; `run()` loops headless. Cancellation keeps partial output (video finalised).
- Video: `assets::VideoWriter` with a native macOS backend (AVFoundation: ProRes 4444/422,
  H.264, HEVC, audio muxed from the project's file) and an external `ffmpeg` backend (raw RGBA
  piped to a user-supplied binary; nothing linked or shipped). `auto` prefers native.
- Queue: `avgen --queue jobs.json` runs `{project, render overrides}` entries sequentially; the
  UI keeps an in-app queue of (project, settings).
- Determinism: GPU particles switched to stable stream compaction (ADR-015 revision) so headless
  sequences are bit-identical with particles on; render logs print the sequence hash, and the
  GPU tests render the same job twice and compare.

## Rationale

A separate offline engine per job is what makes renders reproducible and lets a render run
while the live engine keeps playing; loading from the project file (not from the live state)
turns "render what I see" into "render what is saved", which is also what a queue needs. PNG
sequences are the lossless interchange every compositor takes; native AVFoundation covers the
ProRes/H.264 deliveries the live-visual world uses without a GPL dependency (research §8);
piping to a user's ffmpeg covers everything else at zero licensing cost to the project.
Threaded encoding keeps the GPU busy: readback is synchronous, encoding is not.

## Consequences

- Positive: bit-identical sequences, background renders in the app, queues, ProRes/H.264 out of
  the box on macOS, audio muxed, no new third-party dependency.
- Negative: synchronous readback (a ring of staging buffers would overlap GPU and CPU work); no
  EXR/16-bit output yet; motion blur and DoF are the real-time approximations (no temporal
  supersampling); native video only on macOS.
- Follow-ups: staging ring, EXR, temporal supersampling for motion blur, per-job GPU timing in
  the log, render presets in the UI.
