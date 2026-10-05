# GPU World Architecture: research spike

Branch `research/gpu-world`, from main `ba0003de`, started 2026-10-04. The brief is
`docs/research/gpu-world-brief.md` (the owner's, verbatim). Machine: Apple M2 Max (38-core GPU, 64 GB),
macOS 26, Dawn/WebGPU on Metal. All GPU runs went through `tools/gpu-lock.sh`.

**Status:** Phase 0 complete. Gate 0: PASS (narrow; see below).

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
