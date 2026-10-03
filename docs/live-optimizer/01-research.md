# Live optimizer: what already exists (research, 2026-10-03)

*By the coordinator, from a code inventory of `live/quality` (`5fcd7ad4`) and from the two investigations in `../av-gen-live/docs/live-quality/`. File and line references are to that branch. "The Cinematic Critic" in the brief is the existing **Creative Critic** (no other critic exists).*

**The headline: AV Gen already has most of the measuring and much of the scaling. What it lacks:**
- a single profiling command that runs the scene the way a live performance does;
- byte and memory accounting;
- a pre-warm;
- a handful of missing quality levers (LOD bias, shadow-caster limits, post-FX resolution, particle LOD, hero wiring);
- the human and agent surfaces on top.

## Phase 1: profiling

| need | state | where / what |
|---|---|---|
| reproducible headless benchmark | **EXISTS** | `--headless --bench-json --range a: --frames n` (`application.cpp` ~6030-6440). The record is `rendering::BenchmarkRecord` (`render_stats.hpp` ~446), schema `avgen.benchmark/1`. A/B: `--ab`, `--ab-blocks`, `--quality-arm`, `--disable`, `compareArms` with drift and noise floors |
| frame-time statistics | **EXISTS** (headless) | `Distribution`: min, max, mean, p10, p50, p90, p95, p99, low 1%, low 0.1%, stddev. **Missing:** deadline misses and percent under a target budget |
| live-editor timing | **EXISTS** | `--profile-cpu` / `--profile-csv` (`core::PhaseProfiler`, about 50 phases including engine.update splits, render-record splits, acquire and present waits, outputs, allocations) |
| GPU pass timing | **EXISTS** | `gpu::FrameTimeline` gives end-timestamp intervals that partition the frame, available on Metal. Labels: clusters, shadow, background, depth, fogsky, scene, sdf, ao, shadowmask, temporal, distort.*, volume.march / composite, particles, sim, effectors, cull, post/{meter, exposure, look, look-atmos, dof, motionblur, lens, bloom, halation, anamorphic, composite, fxaa, sharpen}, tonemap, composition, shaderlayer. **Caveats:** consecutive frames can overlap on the GPU, so the span over-reads (ADR-1085 caps it by the frame interval); Metal skips the end timestamp of an empty pass; the UI and output copies are not timed |
| resource counters | **PARTIAL** | `RenderStats` / `BenchmarkCounters`: draws, shadow draws, triangles (logical and submitted), instances visible/culled, LOD rungs, entity LOD, casters, lights, particles (capacity, emitted), SDF stats (steps, hit ratio), passes, state changes. **Missing:** any **byte count** (textures, render targets, buffers; `TransientPool` has only `size()`), distinct pipeline and shader-variant counts, a per-texture list |
| machine conditions | **PARTIAL** | `BenchmarkConditions`: adapter name, git, build, backend, size, tier, warm-up frames, session. **Missing:** host and model, CPU, OS version, refresh rate, window mode, audio and MIDI state, seed |
| warm-up / pre-warm | **PARTIAL** | Bench warm-up is a fixed 12 frames, not configurable. `--particle-warmup` steps particles only. **No shader or pipeline pre-warm and no pipeline cache.** SDF compiled variants build synchronously on the main thread at first use (`sdf_renderer.cpp` 407-445): **3.9 s** mid-run in Liminal |
| live (windowed, presented) profiling | **PARTIAL** | Exists only through the investigation's probes (`AVGEN_LIVE_FRAME_CSV`, not on main). The headless bench has no present, Fifo or UI, so it cannot predict a live frame's interval or deadline misses |
| optimization candidates | **MISSING** as a tool. The raw material exists: quality arms (shadowrange, contact, pcss, maskfull, shadowatlas1k, mat*, volumefull/preview/quarter/steps, scale85/71/58/50, liveaa, cosmic*), `--disable` arms, and the measured per-pass scaling laws in `evidence-live-projection-2026-10-02.md` §D |
| critic integration pattern | **EXISTS** | `director.evaluate` → `directing_evaluate.cpp`: render a clip, run the adapter, then `critic submit --wait --json`, calling an HTTP daemon at 127.0.0.1:8765 that returns `critic.report/1`. A template for an external-tool loop |
| image-difference metrics | **EXISTS** | `tools/quality-lab` (`avgen_quality analyze/compare`): spatial and temporal metrics, VMAF, artifact masks, `quality-report.v1.json` |

## Phase 2: scalability

| need | state | where / what |
|---|---|---|
| quality settings | **EXISTS** | `QualitySettings` (`render_quality.hpp` 77-434), about 35 fields, with tiers Preview, Realtime, High and Offline. `QualityPolicy::assertOfflineIsUncompromised` |
| resolution scale | **EXISTS** | `renderScale` sets the internal target, and post runs at that size; the tonemap upsamples to the output (~0.1 ms) |
| volumetric quality | **EXISTS** | `volumeResolutionScale`, `volumeStepScale`, `volumeStepFloor` |
| shadow quality | **EXISTS** | `shadowResolution`, `cascadeCount`, `shadowTexelTarget` (the range), soft/PCSS, contact shadows and steps, `shadowMaskScale`, `sdfShadowSteps` |
| shadow distance / caster culling | **PARTIAL** | Casters come from the frustum and shadow views only. **No per-caster distance or projected-size limit** |
| LOD | **PARTIAL** | `RepresentationPolicy` with `ImportanceEvaluator` and `ViewContext` for entity mesh LOD chains; the procedural `LodSettings` (distance and screen size, GPU cull); `DetailLimits`. **No global `lodBias`.** Proxy, impostor and cull radii are disabled (-1) |
| hero / importance | **PARTIAL** | `Composition::heroes()` (`HeroPoint` with importance and focal weight) is used by the director. `ImportanceInput::hero` and `RepresentationPolicy::heroFloor` **exist but `in.hero` is never set**. No per-node render-importance field |
| particles | **PARTIAL** | `particleSpawnScale` exists (capacity unchanged). **No culling, no simulation-rate or LOD control** (one step per frame) |
| post FX | **PARTIAL** | Per-effect enables are authored parameters. Quality gates exist only for motion blur and DoF (ADR-1083). **No per-effect resolution scale** (DoF and motion blur run at the full internal size; the bloom pyramid levels are artistic) |
| draw distance | **PARTIAL** | Procedural `maxDistance` and entity `cullDistance` are authored per object, gated by `DetailLimits`. **No global multiplier** |
| simulation quality | **PARTIAL** | Entity update bands (`fullDetailDistance`, `coarseInterval`, `cullDistance`) and rig rates (`updateHz`, `nearDistance`, `farHz`, `cullDistance`) exist, authored per object. No global setting |
| profiles | **PARTIAL** | The tiers, plus the live ladder (Ultra to Emergency × resolution_first / balanced / effects_first, ADR-1083/1084). No user-facing named profile that is not the controller's |

## Phase 3: live performance panel

| need | state | where / what |
|---|---|---|
| performance panel | **EXISTS** | `ControlPanel::drawPerformanceDashboard` (`control_panel.cpp` ~2525-2712, panel "Performance"): fps/median/p95/worst; `PlotLines` of frame and GPU history; per-frame GPU passes and CPU phases; contents; "Take a subsystem away" (the forensic arms) |
| live quality readout and controls | **EXISTS** (ADR-1087) | The Live panel's "LIVE QUALITY": pin or Auto, target, level, scale, internal → output, GPU against the budget, CPU, CPU-bound and at-bottom warnings |
| budget line, CPU/GPU/frame toggle, grouped GPU categories, resource inspector, Optimize review, saving a live profile | **MISSING** | — |
| panel registration | **EXISTS** | `kPanels` in `src/ui/editor_layout.cpp`; the `panel(id, …)` lambda; ImPlot is linked but unused for performance |

## Phase 4: runtime adaptation (largely delivered by `live/quality`)

| need | state |
|---|---|
| target FPS, budget with headroom, Auto/Manual (pinned), hysteresis, median window, sustained raise, learned step ratios, CPU-hold, no oscillation | **EXISTS** (ADR-1080/1083/1085) |
| configurable degradation priority | **PARTIAL**: three fixed strategies per project (ADR-1084), not a user-ordered list |
| per-project hard minimum and a "target unsustainable" status | **PARTIAL**: there's a global lowest scale and an "at bottom" warning. No per-project minimum level |
| recovery: restore one, measure, revert if unstable | **PARTIAL**: a sustained predicted-fit raise (120 frames), but no measured revert after a raise |
| no shader compiles during performance | **MISSING**: SDF variants compile on first use (3.9 s in Liminal) |
| no large allocations during transitions | **MEASURED OK**: a transition costs ≤0.9 ms over the 100-frame maximum (ADR-1086) |
| a slow output can't stall the loop | **EXISTS** (`PresentPacer`, ADR-1089) |
| open tuning question to the owner | a 60 target settles one level low (raise margin 0.8): raise the margin to ~0.9, or cap the frame rate? |

## Phase 5: perceptual

| need | state |
|---|---|
| screen-space importance per entity | **PARTIAL**: `ImportanceEvaluator` computes projected size for LOD; nothing reports it |
| hero preservation | **PARTIAL**: heroes exist (director); not wired to rendering |
| A/B image comparison | **EXISTS** as tools (quality-lab, Creative Critic) |
| optimization search | **MISSING** (the A/B bench machinery is the base) |
| agent integration | **PARTIAL**: `performance.get_stats` reads a snapshot. No tool profiles, reads or sets quality, or optimizes |

## Measured facts to design around (from the two investigations, M2 Max)

- **GPU cost per scene,** at a 1080p projection with the old controller off:
  - Sonic: 55 fps, about half fixed cost (shadows and vertex work).
  - Glowmere: 22-25 fps, 86% per-pixel; its CPU was about 7.4 ms fixed before ADR-1081/1082.
  - Liminal: 13-14 fps, 97% per-pixel.
- **The ADR-1081/1082 CPU fix on real Glowmere** (22 characters, 2026-10-03, interleaved pairs, load 6-16):
  - the diagnostic records went from 2.1-3.9 ms to 0.01 ms;
  - main-thread work fell 10.9 → 6.3 ms on the most comparable pair, and 19.9 → 9.0 and 13.6 → 7.8 on the others.
- **Pass costs at 1080p:**
  - Glowmere: scene 23.6, volume 8.2, motion blur 4.3, DoF 1.3 ms;
  - Liminal: SDF 34.6, volume 13.4, motion blur 5.1 ms;
  - Sonic: scene 12.2, shadow 1.2-2.4, shadow mask 0.9, DoF 0.8 ms.
- **The live ladder at 1080p:** every scene ≥72 fps at a 60 target and 104-118 fps at 90/120 (`30-live-quality/REPORT.md`).
