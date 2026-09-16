# ADR-012: Time model

- Status: Accepted (2026-09-08)
- Research: `docs/research/offline-rendering.md` §12, `docs/research/audio-analysis.md` §1.3-1.4,
  `docs/research/audiovisual-systems.md` lesson 2, 9

## Problem

The same scene must render in real time and, later, as a deterministic frame sequence at an
arbitrary resolution and frame rate. Audio analysis must line up with the timeline in both modes.

## Decision

- **`FrameClock` is the single source of time.** It produces a `FrameTime { renderTime,
  deltaTime, frameIndex }` per frame. `RealtimeClock` advances by measured wall-clock delta,
  clamped to a maximum step; `FixedStepClock` sets `renderTime = frameIndex / fps`. No engine
  code outside `RealtimeClock` reads a system clock.
- **Audio position is sample-indexed.** `AudioPlayer::positionSeconds()` derives from the
  play-head frame count; the analyzer stamps every `AnalysisFrame` with the PCM frame index at
  its window centre. Live mode: the render thread consumes the most recent frame. Offline mode
  (later): `AnalysisTrack::at(time)` returns the frame whose centre is nearest `renderTime`,
  computed by running the identical analyzer over the decoded buffer ahead of rendering.
- **Timeline transport.** `Transport` owns play/pause/seek and the mapping between
  `renderTime` and audio position. In 0.1, real-time playback drives `renderTime` from the audio
  play-head when playing (audio is the master clock) and from the frame clock when paused, so
  visuals stay phase-locked to sound.
- **Randomness is seeded.** Any stochastic element takes a `Rng` (PCG32) seeded from
  `(projectSeed, frameIndex, systemId)`; `std::random_device` and `rand()` are banned.
- **Stateful passes expose `resetTemporalState()`** so offline renders can warm up.
- **Render jobs are declarative** (`RenderJob { fps, frameRange, resolution, seed, warmupFrames,
  output }`) from the first version of the offline runner (1.0); the struct is defined now.

## Rationale

- Injecting time is the single decision that makes offline rendering a mode rather than a
  rewrite (`offline-rendering.md` §12.9).
- Audio-as-master-clock during playback is what keeps visuals locked to sound; sample indices are
  the only representation that survives seeking and offline evaluation.

## Consequences

- Every subsystem update signature takes `const FrameTime&`.
- Modulation smoothing uses `deltaTime` and time constants, so a 24 fps offline render and a
  120 fps live run converge to the same envelope shape (not bit-identical; bit-identity applies
  within a mode at a fixed fps).
- Tests can drive the whole pipeline with a `FixedStepClock` and synthetic audio.
