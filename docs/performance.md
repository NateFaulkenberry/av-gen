# Performance

Measurement infrastructure exists from day one; nothing is optimised yet.

## What is measured

| Metric | Where | How |
|---|---|---|
| CPU frame time, FPS | `Application::runLive` → `ui::FrameStats` | steady_clock around the frame; FPS averaged every 0.5 s |
| GPU frame time | `gpu::GpuTimer` | timestamp queries at the start of the scene pass and end of the tone-map pass, 4-slot mapped ring, no stalls |
| Draw calls, triangles, entities | `rendering::RenderStats` | counted per frame |
| Analysis time per hop | `AnalysisRunner::averageHopMicros` | steady_clock per hop, EMA 0.1 |
| Modulation + scene update time | `EngineStats::modulationMicros` | steady_clock around `Engine::update` |

All of these are displayed in the Control window and logged every 120 frames at debug level.

## Milestone 0.1 numbers (Apple M2 Max, macOS 26.6, Debug build unless stated)

| Metric | Value |
|---|---|
| GPU frame (scene + tone map) at 1280x720, headless | 0.07-0.13 ms |
| GPU frame at 2880x1800 window (scene + tone map, UI excluded from the timer) | 0.39-0.59 ms |
| Live window, Release | 120 fps (ProMotion vsync), CPU work 1.4-1.8 ms/frame |
| Live window, Debug | 60 fps (vsync), CPU work ~3 ms/frame (the earlier 16.6 ms figure included the vsync wait) |
| Analysis per hop (N=2048, H=512) | ~100 µs Debug, ~17 µs Release (0.16% of the hop period) |
| Offline frame incl. synchronous readback, 1280x720 | ~6 ms Release (250 frames in 1.53 s wall), ~43 ms Debug |
| Decode 24 s stereo 48 kHz WAV | ~100-150 ms |
| Test suite | 132 tests, ~4 s Debug |

## Budget and revisit triggers

- Analysis: switch FFT backend (pffft/vDSP) if hop time exceeds 10% of the hop period.
- Render: MSAA and post-processing will add cost; the frame graph in 0.6 introduces a transient
  pool so intermediate targets are not reallocated.
- Offline: replace the synchronous readback with a 3-deep staging ring when the render-job runner
  arrives (1.0).
- Memory: `AnalysisTrack` ≈ 0.8 MB per second of audio; whole-file decode ≈ 0.4 MB per second
  of stereo 48 kHz.
