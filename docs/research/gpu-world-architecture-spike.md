# GPU World Architecture: research spike

Branch `research/gpu-world`, from main `ba0003de`, started 2026-10-04. The brief is
`docs/research/gpu-world-brief.md` (the owner's, verbatim). Machine: Apple M2 Max (38-core GPU, 64 GB),
macOS 26, Dawn/WebGPU on Metal. All GPU runs went through `tools/gpu-lock.sh`.

**Status:** Phase 1 complete. Gate 0: PASS (narrow). Gate 1: CONDITIONAL PASS.

## Hypothesis (from the brief)

> AV Gen may be able to represent certain audiovisual worlds as compact procedural descriptions whose
> expensive population, animation, culling, and simulation happen primarily on the GPU, reducing CPU
> scene/entity overhead while enabling visual systems that are impractical with conventional
> CPU-managed entities.

Scope note (coordinator, mid-spike): Phase 3 (new-art experiment) is handed to a separate art agent.
This spike runs Phases 0-2 at most, then writes a Phase 3 handoff.

---

## Phase 0: Architecture Reconnaissance

Read-only. No production code changed. Sources: the code paths cited below, `docs/gpu-culling-lod.md`,
`docs/gpu-fields.md`, `docs/performance/procedural-geometry.md`, `docs/particle-lab/README.md`,
`docs/live-optimizer/01-research.md`, ADR-097, ADR-273, ADR-700, and the live-optimizer stream's live
profiles of the three real scenes (`~/Desktop/av-gen-review/31-live-optimizer/data/raw/A2-*`, measured
2026-10-03 on this machine).

### 1. The current execution path

```
audio/MIDI/OSC -> signal bus -> Modulator::applyRoutes (per route, per frame)        [CPU]
timeline -> Composition (node tree) --flatten on structural change--> scene::Scene   [CPU]
  per frame: entities (EntityWorld: behaviours, character AI, reactions -> ModRoutes) [CPU]
             floaters / terrain LOD / ecology lights / camera                          [CPU]
SceneRenderer::render
  entities & meshes: CPU frustum cull, per-draw uniforms, one draw per part           [CPU encode]
  ProceduralRenderer::update (scatter layers, cities, imported-asset scatters):
    instance records uploaded ONLY when structureVersion changes                      [CPU -> GPU, rare]
    effector compute pass (fields move/scale records)                                 [GPU]
    cull + LOD compute pass (classify, scan, scatter, indirect args, no atomics)      [GPU]
    drawIndexedIndirect per LOD level, deformers (wind, twist, field) in vertex stage [GPU]
    ADR-056 vegetation sim: CPU integrates only a budgeted active set near camera      [CPU, bounded]
  ParticleRenderer: emit / simulate / scan / indirect draw, all compute               [GPU]
  SDF raymarch, volumes, post                                                          [GPU]
```

### 2. Where CPU work grows with scene complexity

| Cost | Grows with | Measured where | Size today |
|---|---|---|---|
| Composition flatten (`Composition::rebuild`) | node count, terrain, scatter generation | `docs/application-performance.md` | 620-942 ms cold; **0 flattens per frame during playback or scrub** (measured, 97 seeks) |
| Entity update (`EntityWorld`) | bodies x behaviours x reactions | live profile "scene update" | Glowmere (96 entities, 22 characters): **1.55 ms**; Liminal 1.09; Sonic 1.01 |
| Entity seek re-simulation | bodies x replayed steps | ADR-267/273, ADR-700 | was 2.4 s per click (161 s at 100 characters); **fixed by ADR-700 checkpoints** (replays at most 1 s) |
| Modulation routes | routes | live profile | Glowmere 0.45 ms |
| Procedural instances per frame | nothing (records are GPU-resident) | `docs/performance/procedural-geometry.md` | microseconds; 0.19 ms submission in Glowmere for 62k instances |
| Floaters (`Composition::updateFloaters`) | **instances, every frame**: CPU evaluates, rewrites 96 B records, bumps `structureVersion` -> full re-upload + bounds rebuild | code read | **used by no example scene** |

**The most important Phase 0 fact, and the strongest evidence against the hypothesis:** all three
shipped live scenes are GPU-bound, not CPU-bound. Live mode, 1080p projection, auto 60 (A2 runs):

| Scene | CPU work p50 | of which scene update | GPU p50 | Critical path |
|---|---|---|---|---|
| Glowmere valley 2 (22 characters) | 4.91 ms | 1.55 ms | 11.27 ms | GPU (130% of interval) |
| Liminal (all-you-got) | 3.14 ms | 1.09 ms | 11.47 ms | GPU |
| Sonic live | 3.72 ms | 1.01 ms | 13.17 ms | GPU |

Glowmere draws 716 of 61,869 procedural instances; the other 61k are culled on the GPU already.
The renderer forensics/upgrade and live-optimizer work found GPU per-pixel cost (scene shading, SDF,
volumes, motion blur) dominant: Glowmere 86% per-pixel, Liminal 97%. Removing *all* CPU scene work
from these scenes would not raise their frame rate.

### 3. Where data crosses CPU/GPU boundaries

- Per frame, small: object uniforms (768 B per procedural LOD level), cull uniforms (256 B), the
  `FieldBlock` (5,136 B), per-entity draw uniforms, light and cluster data.
- On structural change: instance records (96 B per instance), meshes, textures (an unconditional
  `textureVersion` bump re-uploads all 44 textures on a hero star: 1.1 s, `av-gen-ui-responsiveness`).
- Readbacks: cull stats and timestamps, asynchronous, a readout never an input (ADR-029).
- The one per-frame O(N) upload: floaters (`structureVersion` bumped every frame).

### 4. GPU-driven techniques already in production

This is the second fact that reshapes the spike: **most of the brief's experimental arm B already
exists in AV Gen and ships.**

| Brief's list | In AV Gen | Where |
|---|---|---|
| compute shaders, storage buffers | yes | effectors, cull, particles, simulation, volumes |
| GPU-generated instance data | partial: records are CPU-generated once, GPU-modified by effectors per frame | `points.wgsl` `cs_effectors` |
| indirect drawing | yes | `cull.wgsl` -> `drawIndexedIndirect` per LOD; particles |
| GPU-side animation | yes, in the vertex stage (wind, twist, bend, field deformers) and effector pass | `procedural.wgsl`, `points.wgsl` |
| GPU-side culling + LOD | yes, deterministic prefix-sum compaction, no atomics | ADR-029 |
| GPU-resident simulation state | yes for particles; vegetation dynamics are CPU-integrated (bounded set) and uploaded | ADR-015, ADR-056 |
| procedural generation | CPU at flatten (scatter, terrain, trees, mushrooms) | `scene/procedural.cpp`, `world/*` |

Measured costs already on record (M2 Max): cull pass 0.06 ms / 100k, 0.35-0.38 ms / 1M; effector
pass 1.1-1.4 ms / 1M records (analytic field), 4.6 ms with curl noise; 1M point billboards 3.1 ms
draw at 1080p; CPU microseconds per frame.

### 5. Candidate workloads

| Candidate | Plausible advantage? | Why |
|---|---|---|
| Static scatter (grass, fungi, rocks) | **No** | already GPU-resident, GPU-culled, indirect-drawn |
| Character entities (AI, navigation, perception) | **No** | stateful, branching, few (22-250); belong on the CPU; their seek cost is fixed (ADR-700) |
| Flatten cost | **No** for playback | it does not run per frame; it is an editing-latency problem, not a frame-time one |
| **CPU-animated instance populations** (floaters; any "N things each doing their own motion with per-instance state") | **Yes, plausibly** | the only CPU path that is O(N) per frame plus an O(N) re-upload; a GPU arm is O(1) CPU |
| **Per-instance audio-reactive populations** | **Yes, plausibly** | audio reaches populations today only as per-object uniforms or through ≤16 fields modulated per route; per-instance divergent response (own band, own phase, travelling kick wave) has no production path other than one entity per element, which costs a ModRoute per reaction per frame |
| Million-element fields, generated-around-camera worlds | maybe (Phase 3/4 questions) | particles cover part of it already |

### 6. Control cases

- `Composition::updateFloaters` is the production embodiment of Control A (CPU evaluates a pure
  function of time per instance, packs records, re-uploads every frame). The prototype's CPU arm
  replicates that shape, with a CPU frustum pre-cull added so the control is not a straw man.
- `ProceduralRenderer` (records GPU-resident, GPU cull, indirect draw) is what Experimental B must
  beat or match; the prototype's GPU arm is a minimal version of it plus per-frame GPU animation.

### Gate 0 decision: **PASS (narrow)**

There is at least one workload with a plausible, measurable advantage: a population whose every
element animates (and later reacts to audio) per frame. On the CPU that is O(N) evaluation plus an
O(N) upload every frame; on the GPU it is a fixed-size uniform write. Phase 1 measures whether that
difference is material.

Two qualifications written down now, before any prototype exists, so the result cannot drift past
them:

1. The real scenes are GPU-bound with 1-1.5 ms of scene update. A CPU win in the prototype will not
   speed up any shipped scene; it can only make *new* content affordable.
2. The GPU path the hypothesis describes is largely already built. The live question is narrower than
   the brief's framing: is *per-frame GPU animation of per-instance state* worth adding, given the
   static GPU population path already exists?

---

## Phase 1: Controlled GPU Population Benchmark

### Experiment design

Prototype: `prototypes/gpu-world/gpu_world_bench.cpp` (~900 lines, one file), built only with
`-DAVGEN_GPUWORLD_PROTOTYPE=ON` (target `avgen_gpu_world_bench`). It links `avgen_gpu` read-only for the
Dawn context, `gpu::FrameTimeline` (the production GPU timestamp instrument), texture readback and the
production mushroom generator. No production file changed apart from the three-line CMake option.

One population of a simple asset. Every element animates every frame: a bob, a two-axis sway, a slow
yaw, a scale pulse and an emission pulse, each with a per-instance phase. The animation is a pure
function of (rest record, time), like `evaluateFloaters`. Four arms share the draw shader, the mesh,
the animation maths (C++ and WGSL written operation for operation), the cull rule, the camera and the
1920x1080 target. Only where the per-frame instance data comes from differs.

| Arm | Per frame on the CPU | Per frame on the GPU |
|---|---|---|
| `cpu` (Control A) | per instance: conservative pre-cull on the rest position, animate, exact frustum and distance cull, pack a 48 B record; one `WriteBuffer` of the survivors | one `DrawIndexed` |
| `cpumt` (Control A, strongest) | the same split across a persistent 8-thread pool | same |
| `cpugrid` (Control A, world) | a uniform XZ grid built once; visit only cells whose box reaches the frustum and the max distance (ADR-056's idea) | same |
| `gpu` (Experimental B) | write one 304 B uniform block and the 20 B indirect args | one compute dispatch: animate, cull and append survivors (workgroup-aggregated atomics) into a compact buffer, then one `DrawIndexedIndirect` |

Scenarios:

- **field** (the population is the shot): a 200 m square of N elements seen from above at an angle,
  so ~82% are on screen at every N. Asset: an 8-triangle "firefly" octahedron. It is cheap to draw, so
  the population cost is visible rather than buried under raster cost.
- **world** (a big world, a nearby camera): the population is spread at constant density over a square
  of side √N metres. The camera stands at eye height and culls at 120 m, so ~10k are visible whatever
  N is. Asset: the production procedural mushroom (`organism::buildMushroom` at the schema's
  midpoint, 6,010 triangles, flattened to one mesh with per-part colour and an emissive mask). This is
  the realistic case: heavy asset, most of the world off screen.

Protocol: Release build. 60 warm-up frames, then 300 measured frames, with two frames in flight (the
CPU waits on the frame before last, as a live loop does). Each (N, arm) ran twice, with the arm order
reversed on the second repeat. Every run went through `tools/gpu-lock.sh`, and every run waited for
the 1-minute load average to fall under 4. **The first full matrix was discarded:** another agent was
compiling (load 18-58, seven clang processes at 90%+). It is kept in `data/contended/` for transparency.
Cells show the p50 of the two runs' per-run p50s, with the range of the two in brackets.

Instrument limits: GPU times come from `gpu::FrameTimeline`, whose resolution here is about 0.066 ms.
A "0.000" GPU compute value means "under one tick", not zero. "CPU frame" is the CPU work for the
frame: update, upload, encode and submit, excluding the wait for the GPU. "Frame interval" is the
wall-clock time between frame starts, so it is the end-to-end cost.

### Results: field (firefly, 8 triangles), everything animated and ~82% visible

Data: `docs/research/gpu-world/data/p1-firefly-field.{jsonl,table.md}`.

| N | visible | CPU frame ms: cpu / cpumt(8) / gpu | upload B/frame: CPU arms / gpu | GPU frame ms: cpu / gpu (compute part) | frame interval ms: cpu / cpumt / gpu |
|---|---|---|---|---|---|
| 1,000 | 857 | 0.080 / 0.093 / 0.051 | 41 KB / 324 B | 0.07 / 0.13 (<0.07) | 0.43 / 0.45 / 0.54 |
| 10,000 | 8,259 | 0.53 / 0.18 / 0.053 | 396 KB / 324 B | 0.26 / 0.26 (<0.07) | 0.63 / 0.64 / 0.68 |
| 100,000 | 82,498 | 4.94 / 0.98 / 0.059 | 3.96 MB / 324 B | 0.79 / 0.46 (<0.07) | 4.94 / 1.08 / 0.81 |
| 1,000,000 | 819,299 | 50.4 / 11.4 / 0.33 | 39.3 MB / 324 B | 4.85 / 2.29 (0.20) | 50.4 / 11.4 / **2.71** |
| 4,000,000 | 3,275,701 | 202 / 43.3 / 0.64 | 157 MB / 324 B | 16.3 / 8.78 (0.72) | 202 / 43.3 / **9.24** |

(`cpugrid` tracks `cpu` within 5% here; when everything is on screen a grid has nothing to skip. The
full table is in the data folder.)

![field, 100k, CPU arm](gpu-world/p1-field-100k-cpu.jpg) ![field, 100k, GPU arm](gpu-world/p1-field-100k-gpu.jpg)

### Results: world (mushroom, 6,010 triangles), ~10k visible whatever N is

Data: `docs/research/gpu-world/data/p1-mushroom-world.{jsonl,table.md}`.

| N | visible | CPU frame ms: cpu / cpumt / cpugrid / gpu | GPU frame ms (all arms) | frame interval ms: best CPU arm / gpu |
|---|---|---|---|---|
| 1,000 | 235 | 0.086 / 0.18 / 0.13 / 0.071 | 0.59-0.79 | 0.93 / 1.00 |
| 10,000 | 2,278 | 0.71 / 0.51 / 0.87 / 0.21 | 2.29-2.36 | 2.70 / 2.69 |
| 100,000 | 10,283 | 2.57 / 1.96 / 2.30 / 0.31 | 7.67-7.86 | 8.12 / 8.34 |
| 1,000,000 | 10,246 | 3.95 / 2.54 / 2.39 / 0.45 | 7.80-7.90 | 8.22 / 8.37 |

![world, 10k mushrooms, GPU arm](gpu-world/p1-world-10k-mushroom-gpu.jpg)

### Correctness and equivalence

- **The visible counts are identical** in every arm at every N (for example 819,299 at 1M field and
  10,246 at 1M world), so all arms draw the same set.
- **Image comparison**, at the same frame (t = 10.5 s) and the same camera, using 8-bit sRGB output:
  - field 100k, CPU vs GPU: 12 of 2,073,600 pixels differ by more than 2/255 (max 66, mean 0.0001);
  - world 10k mushrooms: 187 pixels (max 182, mean 0.007).
  These are silhouette pixels. GPU `sin`/`exp` and libm differ in the last bits, and that moves an
  edge by under a pixel. The images are equivalent, but not bit-identical.
- **GPU run-to-run**: two fresh runs gave byte-identical images, even though the append order of the
  workgroup atomics is not guaranteed. The depth test hides the order for opaque geometry. Translucent
  or additive geometry drawn in that order would not be stable (see the Bad-Idea list).

### Profiling observations

- **The CPU arms scale linearly with N.** That is about 50 ns per element on one core (animate, cull
  and pack), plus about 3.1 ms of `WriteBuffer` per million visible elements (39 MB). Eight threads
  give 4.4-4.7x on the update, but the upload does not parallelise: at 1M it is 3.5 ms of the 11.4.
- **The GPU arm's CPU cost is flat:** 0.05-0.06 ms up to 100k. At 1M-4M it is 0.3-0.6 ms, all of it
  encode and submit. That is Dawn's per-submit overhead, not population work.
- **The GPU compute pass is cheap:** 0.20 ms at 1M and 0.72 ms at 4M (animate, cull, append), against
  a draw of 2.1 and 8.1 ms. These agree with the production effector pass (1.1-1.4 ms per 1M records
  for an analytic field), which moves twice the bytes per record.
- **Surprise:** the CPU arms' *GPU* frame is longer than the GPU arm's (4.85 vs 2.29 ms at 1M, 16.3
  vs 8.8 at 4M), even though they draw the same instances. The likely cause is the 39-157 MB per-frame
  staging copy landing in the same timeline. It was not isolated further; it does not change the
  conclusion.
- **Memory:** the CPU arms hold a rest array plus a full staging array: 76 MB at 1M, 305 MB at 4M.
  The GPU arm holds the rest array on the GPU (32 B/element) plus the output buffer (48 B/element),
  30.5 MB of CPU-side rest copy at 1M (which could be dropped after upload), and uploads 324 B a
  frame against 39 MB. No arm grew its footprint over 300 frames.
- **The world scenario is GPU-bound in every arm** (7.8 ms of mushroom raster for ~10k visible). The
  GPU arm cuts CPU work 5.3x at 1M (2.39 → 0.45 ms against the best CPU control), and the frame is
  *0.15 ms slower* (8.37 vs 8.22). That is the Phase 0 finding reproduced in miniature: when the GPU
  is the bottleneck, freeing the CPU buys nothing.

### Gate 1 decision: **CONDITIONAL PASS**

Against the brief's targets:

| Target | Result |
|---|---|
| ≥2x less CPU work at meaningful sizes | **Met.** 16x at 100k against the 8-thread control (0.98 → 0.06 ms), 35x at 1M (11.4 → 0.33), 68x at 4M; 5.3x in the GPU-bound world at 1M |
| dramatically better scaling | **Met.** The CPU is flat in N, against linear for every CPU arm |
| substantially less CPU/GPU traffic | **Met.** 324 B/frame against 4 MB (100k), 39 MB (1M) and 157 MB (4M) |
| materially larger feasible population | **Met for cheap elements.** At a 60 fps budget (16.7 ms frame) the 8-thread CPU arm tops out around 1.4M animated elements, using every performance core. The GPU arm draws 4M in 9.2 ms end to end and is raster-bound, not population-bound |
| end-to-end frame | **Better only when the population is CPU-bound:** 4.2x at 1M (11.4 → 2.7 ms), 4.7x at 4M, 1.3x at 100k. **No change** (−2%) in the GPU-bound mushroom world and at ≤10k |

It is a pass because the CPU, scaling and transfer advantages are large, not marginal. It is
*conditional* for two reasons, both measured:

1. **The win is workload-specific.** It pays only for populations of hundreds of thousands or more
   of cheap, individually animated elements. A world of heavy assets is raster-bound, and so are all
   three shipped scenes (Phase 0). There the GPU arm changes nothing end to end.
2. **Control A is not what AV Gen does at scale.** Large populations in production already use
   GPU-resident records, the GPU effector pass and GPU cull (Phase 0 §4). Production's CPU cost per
   frame is already microseconds, so the GPU arm's CPU advantage over *production* is close to zero.
   The only production path shaped like Control A is floaters, and no example scene uses it. What
   Phase 1 shows is that "per-frame CPU animation of large populations" is a bad idea, which is
   already the engine's design. It does not show that the engine needs a new architecture.

Why continue to Phase 2 anyway: the condition the brief sets is "continue only if the workload is
representative of an important AV Gen use case". Large populations of individually *audio-reactive*
elements are that use case if anything is. They are also the one thing the existing effector path may
not express, because effectors and fields respond to audio per object or per field (≤16 fields), not
per element. Phase 2 therefore has a sharper question than the brief's generic one: **does per-element
audio response need anything production's GPU effector path does not already give, and is the CPU
alternative actually unaffordable?** Phase 2 adds production's own path as a control.

---

## Reasons This Might Be A Bad Idea (live)

- **The shipped scenes are GPU-bound.** CPU scene work is 1-1.5 ms of a 3-5 ms CPU frame against
  11-13 ms of GPU. Moving CPU work to the GPU makes the bottleneck worse in every scene measured.
- **The architecture largely exists already.** GPU-resident records, GPU effectors, deterministic GPU
  cull/LOD, indirect draws and GPU particles all ship. "GPU World" risks being a rename of
  `ProceduralRenderer` plus `ParticleRenderer`.
- **The expensive CPU things are not population-shaped.** Character AI, navigation and perception are
  stateful and branching, number in the tens, and the scrub problem they caused is already fixed
  with checkpoints (ADR-700).
- **The flatten is not per frame.** It is an editing-latency cost; GPU population does not remove it
  (the scatter generation would just move).
- **Determinism is a project rule** (ADR-091, ADR-360: a scrubbed frame must equal a played one).
  GPU compaction with atomics is order-nondeterministic; production avoids atomics on purpose. Any
  GPU state that integrates over frames needs checkpointing like ADR-700 to seek, which is much
  harder on the GPU.
- **Nobody uses the CPU-animated population path today** (no example uses floaters), which suggests
  the demand for the workload this spike can win on is unproven.
- **(Phase 1) The end-to-end win needs CPU-bound content.** In a raster-bound world of 6k-triangle
  mushrooms the GPU arm cut CPU work 5x and made the frame 2% *slower*. Every shipped scene is
  raster-bound.
- **(Phase 1) The GPU arm's advantage over *production* is near zero for stateless animation.**
  Production's records are already GPU-resident and its effectors already animate them on the GPU, at
  microseconds of CPU.
- **(Phase 1) Append order with atomics is nondeterministic.** It was invisible for opaque geometry in
  this test, where two runs were byte-identical. It is a real hazard for translucent or additive
  populations, and for anything that reads back "instance k". Production avoids atomics on purpose
  (ADR-015, ADR-029). Matching that costs a prefix-scan compaction (3 more dispatches; production
  already has the code).
- **(Phase 1) CPU and GPU maths diverge in the last bits** (12-187 silhouette pixels per frame here).
  An offline/CPU fallback for a GPU population cannot be bit-identical, so the golden-image tests would
  need tolerances.
