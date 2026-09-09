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
| Test suite | 146 tests, ~5 s Debug |

## Milestone 0.2 numbers (Apple M2 Max, Release unless stated)

| Metric | Value |
|---|---|
| DamagedHelmet load (3.8 MB, five 2048² PNGs) | 161 ms Release, 835 ms Debug (stb PNG decode) |
| Environment preprocessing, 1k HDRI (cube 256 + 9 mips, irradiance 32, prefiltered 128 x 6, BRDF 128) | 18 ms Release, 127 ms Debug |
| Helmet + IBL + skybox, 2880x1800 window | 120 fps (vsync), CPU work 0.3 ms, GPU 1.0-1.2 ms |
| Helmet headless 1280x720 incl. readback | ~6 ms per frame |

## Milestone 0.5 numbers (Apple M2 Max)

| Metric | Value |
|---|---|
| Orb scene + 131k-capacity sparks, 2880x1800 window | 120 fps, GPU 0.79 ms (was 0.4 ms without particles), CPU work 1.2 ms (Debug) |
| One-million-particle pool, ~1M alive, 1280x720 headless (`[.perf]` probe) | 3.4 ms GPU per frame (emit + simulate + indirect draw) |
| Particle uniform update per system | one 288-byte write; no readback |

## Milestone 0.6 numbers (Apple M2 Max, Debug window 2880x1800)

| Metric | Value |
|---|---|
| Orb scene + sparks + default post chain (bloom 6 levels + composite + tone map) | 120 fps, GPU 3.3 ms (2.5 ms is the post chain at full 2880x1800) |
| Post chain passes at 1280x720 with DoF + motion blur + bloom | 13 passes, ~13 transient textures reused every frame |

Bloom at native Retina resolution dominates; starting the chain at quarter resolution is the
obvious optimisation when the budget tightens.

## Milestone 0.7 numbers (Apple M2 Max, Release)

| Metric | Value |
|---|---|
| Six-node composition (2x DamagedHelmet, orb, grid, BoxTextured, nested MetalRoughSpheres + particles, studio HDRI), 2880x1800 window | 120 fps (vsync), GPU 1.9 ms, CPU work 1.8-2.4 ms |
| Same composition headless 1280x720 | GPU 0.59 ms per frame |
| Scene file load (three glTF assets, one nested scene) | 163 ms Release, of which DamagedHelmet decode 153 ms; second helmet instance free (registry cache) |
| Composition rebuild (flatten) | structural changes only; per-frame cost is one TRS compose per entity |

## Milestone 0.8 numbers (Apple M2 Max, Release)

| Metric | Value |
|---|---|
| Orb scene + 3 timeline tracks + 2 cues, 2880x1800 window | 120 fps, GPU 1.4 ms, CPU work ~1 ms |
| Timeline evaluation | binary search per track per frame; negligible next to modulation |

## Milestone 1.0 numbers (Apple M2 Max, Release): deterministic particle compaction

| Metric | Value |
|---|---|
| One-million-particle pool, ~1M alive, 1280x720 headless (`[.perf]` probe), atomic dead/alive lists (before) | 2.29 ms GPU per frame |
| Same probe with stable prefix-sum compaction (after: emit + simulate + reduce + top scan + scatter + indirect draw) | 2.61-2.73 ms GPU per frame (+15%, two runs) |
| Orb scene headless 1280x720, 240 frames at 30 fps, run twice | 240/240 identical per-frame hashes, 0 GPU errors |

The extra cost is two passes over the 4 MB flag buffer plus a 4 KB block-sum scan; curl-noise
simulation still dominates. The earlier 3.4 ms figure above was a different build of the same
probe; the before/after pair here was measured back to back on the same binary configuration.

## Budget and revisit triggers

- Analysis: switch FFT backend (pffft/vDSP) if hop time exceeds 10% of the hop period.
- Render: MSAA and post-processing will add cost; the frame graph in 0.6 introduces a transient
  pool so intermediate targets are not reallocated.
- Offline: replace the synchronous readback with a 3-deep staging ring when the render-job runner
  arrives (1.0).
- Memory: `AnalysisTrack` ≈ 0.8 MB per second of audio; whole-file decode ≈ 0.4 MB per second
  of stereo 48 kHz.
