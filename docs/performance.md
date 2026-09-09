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

Superseded for the post chain by the ADR-037/ADR-039 reordering: see
`docs/performance/image-formation.md` for 1080p and 4K measurements of exposure, bloom, halation,
anamorphic and depth of field on and off (bloom 0.3 ms at 1080p; depth of field is now the
expensive stage).

## Procedural geometry (ADR-023)

Instanced procedural objects with the GPU deformer stack: 1k/10k-instance probe in
`docs/performance/procedural-geometry.md` (10k instances of a 24-segment cylinder, 2.4 M
triangles, one draw: 3.6 ms undeformed, 4.5-5.6 ms with one to three deformers at 720p).

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

## Milestone 1.0 offline rendering numbers (Apple M2 Max, Release)

| Metric | Value |
|---|---|
| Six-node composition + HDRI + timeline, 1920x1080 PNG sequence, 8 encoder threads | 50 fps (60 frames in 1.2 s incl. readback and PNG encode) |
| Orb scene + sparks, 1280x720 PNG sequence | 102 fps (300 frames in 2.9 s) |
| Readback | synchronous per frame; the GPU idles while the CPU maps (a staging ring is the next step) |

## Budget and revisit triggers

- Analysis: switch FFT backend (pffft/vDSP) if hop time exceeds 10% of the hop period.
- Render: MSAA and post-processing will add cost; the frame graph in 0.6 introduces a transient
  pool so intermediate targets are not reallocated.
- Offline: replace the synchronous readback with a 3-deep staging ring when the render-job runner
  arrives (1.0).
- Memory: `AnalysisTrack` ≈ 0.8 MB per second of audio; whole-file decode ≈ 0.4 MB per second
  of stereo 48 kHz.

## Where a world frame goes (2026-09-09)

Measured on an M2 Max, 200 headless frames at 1440x900, Glowmere with terrain and eleven scatter
layers. **The built-in per-pass GPU timers are not trustworthy on this setup and were actively
misleading**: `volumeMs` reported a constant 14.5 ms at every resolution from 720x450 to 2880x1800
*and* at every step count from 18 down to 6. A pass whose measured cost is invariant to both its
pixel count and its loop count is not being measured. `gpuFrameMs` is flat across the same 16x pixel
range for the same reason. Everything below is wall-clock difference between scene variants, which
is the only instrument here that has held up.

| variant | ms/frame | delta |
|---|---|---|
| full (terrain + ecology + shadows + volumetrics) | 41.5 | |
| volumetrics off | 40.0 | volumetrics = 1.5 ms |
| shadows off | 34.4 | shadows = 6.5 ms *with* ecology |
| ecology off | 23.5 | **ecology = 18 ms** |
| neither ecology nor shadows | 22.2 | shadows = 1.3 ms *without* ecology |

Then the surprise. The ecology cost is **linear in the number of scatter objects and independent of
what they draw**:

| scatter layers | ms/frame |
|---|---|
| 0 | 23.5 |
| 1 | 28.8 |
| 3 | 32.0 |
| 6 | 35.1 |
| 11 | 39.8 |

About **1.5 ms per procedural object per frame**, fixed. Things that did *not* change it:

- **Resolution.** Flat from 720x450 to 2880x1800. The frame is not fragment-bound.
- **Triangles.** Switching on the LOD ladder (it defaults to one level, so every surviving instance
  was drawing its full-resolution mesh) saved 1.1 ms.
- **Instances drawn.** Per-layer view distances, cutting grass from 520 m to 70 m, saved nothing.
- **Terrain chunk count.** 256 chunks or 25, no difference.
- **Volumetric steps or reach.** 18 steps over 320 m or 6 over 120, no difference.

Culling itself is working: 6,400 instances survive and 31,200 are culled each frame.

So the ceiling on a world is not its instance count, its triangle count or its resolution -- it is
**how many distinct scatter layers it has**. The cull work is already batched into a single compute
pass, so the overhead is in the draw path: each object issues up to four indirect draws in each of
the depth prepass, the main pass and every shadow cascade -- around 264 indirect draws for eleven
layers -- and those indirect buffers were written by a compute pass in the same command buffer.
That is the shape of a compute-to-indirect-draw hazard serialising the render passes, and it is
consistent with the CPU sample, which found 73% of the main thread blocked in `waitForQueue`.

Confirming that needs a Metal frame capture rather than the in-engine timers. The two fixes it
points at, in order: draw a layer's LOD levels through fewer indirect draws (or skip empty levels
without submitting), and cull per shadow cascade instead of reusing the camera's visible set --
cascade 0 covers about forty metres and is currently drawing everything the camera can see.
