# GPU Procedural Systems: Productionization

Branch `gpu/productionization`, from main `e94ac0b8`, started 2026-10-05. The brief is
`docs/research/gpu-productionization-brief.md` (the owner's, verbatim). This document belongs to the
engine agent (brief Phases 0-3). Phase 4, the flagship LIVE scene and its art, belongs to a separate art
agent, and the handoff to it is at the end of this file. Machine: Apple M2 Max (38-core GPU, 64 GB),
macOS 26, Dawn/WebGPU on Metal. Every GPU run goes through `tools/gpu-lock.sh`.

Read with: `docs/research/gpu-world-architecture-spike.md` (the spike, final decision GREEN, narrowly
scoped), `docs/development/gpu-sim-seek-investigation.md`, ADR-1114 and ADR-1115.

## PROGRESS (resume from here)

| Phase | State | Where |
|---|---|---|
| 0. Reality check | done | §Phase 0 |
| 1. CPU/GPU bridge research | in progress | §Phase 1 |
| 2. Architecture decision | not started | §Phase 2 |
| 3. Production hardening | not started | §Phase 3 |

---

## Phase 0: Repository Reality Check

Read-only. I read the spike document in full, the seek investigation, ADR-1114 and ADR-1115. I also read
the production code paths named below, and three read-only code surveys mapped the audio-to-GPU path,
the editor, CLI, picking and profile path, and the stateful-GPU and seek path. File and line references
are at main `e94ac0b8`.

**The coordinator's account is correct.** None of the spike's Phase 5 proposal is implemented in
production. The spike's merge into main brought documents, data and four prototypes behind an
OFF-by-default CMake option. Its only shared-file change is that option. The production code changes
merged in the same window (ADR-1114, ADR-1115 and the live-optimizer stream) are separate work.
`git grep -i gpuworld` over `src/`, `shaders/`, `tests/`, `examples/` and `tools/` finds nothing. No
production system depends on the research code.

### IMPLEMENTED (in production, on main)

| What | Where | Note |
|---|---|---|
| GPU-resident instance records, uploaded only on structural change | `rendering/procedural_renderer.cpp:1748` | ADR-023/025 |
| Effector compute pass: base records → live records, ≤8 effectors over ≤16 fields | `shaders/points.wgsl` `cs_effectors` | ADR-025. Stateless, recomputed from the base records every frame |
| Deterministic GPU cull + LOD: classify, then a stable prefix-scan compaction (no atomics), `drawIndexedIndirect` per rung from one shared args buffer | `shaders/cull.wgsl`, ADR-029/051/108/287 | This is the compaction the spike said a generator must reuse |
| Fields on the GPU: one 5,904 B uniform `FieldBlock`, repacked per frame at `renderTime` | `rendering/field_uniforms.hpp`, `shaders/fields.wgsl` | Consumers: effectors, procedural draw (deformers, emissive field), particles, simulation, volumes, SDF, PBR materials |
| Simulated grids (ADR-032), with the shared 8 MB grid table at group 0 binding 15 of every `fields.wgsl` consumer | `rendering/simulation.{hpp,cpp}` | Fixed step at `simRate`, gather-only kernels, bit-identical run to run |
| **ADR-1114:** a seek runs a grid's whole backlog (≤108,000 steps, chunked 256 per submit); `resetTemporalHistory()` reaches the grids | `simulation.cpp:313-517`, `scene_renderer.cpp:2009` | Exact for **time-invariant** inputs only: the kernels sample fields at the *arriving frame's* time for every replayed step (`simulation.hpp:17-24`) |
| **ADR-1115:** the particle warm-up never rolls before t = 0 | `core/pre_roll.cpp:42-79` | |
| GPU particles: emit, simulate, stable scan, indirect draw; they sample grids through fields | `rendering/particle_renderer.cpp` | Seek relaxed by ADR-360: scrub may differ from play |
| Asynchronous stats and timestamp readbacks; nothing in the frame loop waits on the GPU | ADR-029, `gpu::FrameTimeline` | Blocking `read*` helpers exist for tests and tools only |
| Resource accounting: Dawn's own memory estimate plus pipeline counters | ADR-1091, `gpu/resource_stats.cpp:54-98`, `live_profile.cpp:894-932` | Reads Dawn totals; no per-subsystem lines |
| CPU entity checkpoints for exact seek, with `checkpointInputKey` invalidation | ADR-700, `entity/entity.cpp:1569-1720` | The only checkpoint system. No GPU state is in any checkpoint |
| Tests | `tests/rendering/test_gpu_sim_seek_gpu.cpp` | `[gpu][simulation][seek]` passes; `[.investigate]` particle seek fails by design; `[.perf]` seek cost |

### PARTIALLY IMPLEMENTED

| What | State | Where |
|---|---|---|
| Per-element seed for effectors | `InstanceRecord::random` is written at rebuild and uploaded, but **no GPU code reads it** | `shaders/points.wgsl` (declared, unread) |
| Spectrum on the GPU | One row only (the newest analysis frame, 1,025 bins, RGBA16F texture). Bound only to fullscreen user shader layers; no history | `scene_renderer.cpp:2198-2231` |
| Onset/event history | ADR-906 field triggers give a field the age of its **latest** event only. `TriggerClock::lastTriggers` can return the last N times (pure in t). Band onsets are reachable only as lazily derived Signal triggers; `TriggerClock::onsets_` is broadband only | `world/effects/effect_trigger.cpp:165-243`, `scene/field_params.cpp:228` |
| Live analysis | A lock-free triple buffer delivers only the **newest** frame to the render thread, so hops between two render frames are lost (events survive through the latch) | `analysis/analysis_runner.cpp:98-160` |
| Grid seek | Exact but linear in distance (no checkpoints); inputs sampled at frame time, not step time | ADR-1114 §4 |
| Per-subsystem memory | `ProceduralStats::instanceBufferBytes` exists but reaches only the Control panel; `SimulationStats` has no bytes; `ProfileCapture::instanceBufferBytes` is written to JSON as `instanceBytes` but never assigned (always 0, a side finding) | `procedural_renderer.cpp:2171`, `profile_capture.cpp:110` |
| Procedural picking | Per **object** only: a 2-bit space tag + 14-bit index in the R32Uint identifier target. No instance id | `scene_types.hpp:132-152`, `application.cpp:4206` |
| Distribution kind in the editor | A raw `SliderInt 0..7` with no labels; every kind's fields are shown whatever the kind | `procedural.cpp:3430`, `param_widget.cpp:26` |

### RESEARCH ONLY (isolated, off by default)

| What | Where |
|---|---|
| Four prototype executables behind `-DAVGEN_GPUWORLD_PROTOTYPE=ON`: the population bench (Phases 1-2), the art sketchbook with Echo Field, Endless Meadow and Mycelium (Phase 3), the compact-vs-expanded representation bench (Phase 4), and the flatten probe | `prototypes/gpu-world/` |
| Production-control and Phase 4 conventional scenes (stress swarm, meadows at 250 m-2 km) | `prototypes/gpu-world/production-control/`, `phase4-conventional/` |
| Measurements, stills and the seek-test data | `docs/research/gpu-world/` |
| The Phase 5 architecture proposal | `gpu-world-architecture-spike.md`, "Phase 5" |

The prototypes use their own renderer, an atomic-append compaction (order is nondeterministic), real
audio analysed once per run, and a hand-written C++ mirror only for the meadow terrain. None of that
is production code and none of it should be ported as is.

### MISSING (none of the Phase 5 proposal exists)

- Per-element audio inputs for `cs_effectors`: seed, spectrogram history, onset history,
  delay-by-distance. No audio data of any kind reaches a compute pass.
- `DistributionKind::Generator`. The enum is `Single … Scatter` (`procedural.hpp:274`), and
  `ProceduralGeometry` is unchanged since before the spike.
- Any CPU mirror of GPU-generated content, any region query, any picking below the object.
- Bake-to-scatter. There is no bake action for geometry anywhere in the editor. A `Scatter`
  distribution's cloud is runtime-only and never serialised, so there is nothing to bake *into* yet.
- GPU checkpoints of any kind. No code snapshots GPU state. Particle state buffers lack `CopySrc`.
- Population → grid deposits. No particle or agent writes a grid; grids inject only from analytic
  fields.
- Per-step simulation inputs.
- Regression scenes for Echo Field, Endless Meadow or Mycelium on production systems.
- Tests of any of the above. No new shader or compute infrastructure.

### RISKY / NEEDS REVIEW

1. **Grid replay is exact only for time-invariant inputs.** Any audio-driven or animated grid input
   makes scrub ≠ play today, and checkpoints alone would not fix it: per-step inputs are needed
   first.
2. **Particles cannot carry an exact-seek promise** (ADR-360, measured in the seek investigation).
   Anything that deposits from *particles* inherits that, so a stateful population that must seek
   exactly cannot be a particle system as built.
3. **Live analysis drops hops.** A spectrogram ring fed from the triple buffer has duplicated or
   missing rows at render rates below 94 Hz. That is acceptable for the visual ring if documented, but
   not for anything claiming exactness.
4. **Live input has its own clock** (input-stream seconds, not transport seconds). History indexing
   must not assume `renderTime` is the analysis clock.
5. **Adding a storage binding to every `fields.wgsl` consumer** includes the PBR fragment stage,
   which counts against `maxStorageBuffersPerShaderStage`. The grid table already set the pattern of
   one shared binding; a second one is a real cost.
6. **The 14-bit pick index and 16-bit object id** cannot address generated elements, so per-element
   picking has to go through a CPU query, not the identifier target.
7. Prototype hazards already met, which must not be repeated: `atan2(0,0)` is NaN on Metal; `f32(i)`
   loses angle precision at i ≈ 1M; counters are not evidence of an image.
8. The spike's Phase 5 left "the checkpoint half for today's particles and grids" to the seek
   investigation. That investigation (ADR-1114) implemented option A (replay) and recommended B
   (checkpoints) "the moment a real grid lands in a song". This work is that moment.

---

## Phase 1: CPU/GPU Bridge Research

(in progress)
