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

Two kinds of source. The first is primary and technical material: the W3C WebGPU and WGSL
specifications, the Metal Shading Language specification (2026-06 revision), Dawn's source
(`BufferMTL.mm`, `ShaderModuleMTL.mm`), Epic's Niagara and LWC documentation, the Assassin's Creed
Unity GPU-driven pipeline (Haar and Aaltonen, SIGGRAPH 2015), Unity's BatchRendererGroup, Frostbite's
FrameGraph (O'Donnell, GDC 2017), Houdini DOP caching, Blender simulation zones, GGPO, EA SEED's
PB-MPM, TouchDesigner's docs and arXiv 2408.05148. The second is measurement on this machine
(`tests/rendering/test_gpu_bridge_probe_gpu.cpp`, `[.perf][gpu-bridge]`, under the lock). The probe ran
while other agents were compiling (load 11-25), so its timings are orders of magnitude, not a
benchmark.

### What the established systems do, and what AV Gen takes from them

| System | Pattern | Taken / rejected |
|---|---|---|
| AC Unity GPU-driven pipeline | Persistent GPU instance buffers; GPU cull and compaction; multi-draw-indirect. The authors list "non-deterministic cluster order" as a cost of append | **Taken, already shipped:** ADR-029's prefix-scan compaction is the deterministic alternative to append. A generator must feed it, not its own atomic append (the spike's prototypes did that) |
| Nanite / editor hit proxies | The editor identifies an object by reading back the ID under the cursor, one pixel, asynchronously | **Taken in part.** AV Gen's identifier target already resolves the *object*. The *element* is re-derived on the CPU from an integer cell id: no instance id on the GPU, no readback of a population |
| Unity BRG / GPU Resident Drawer | The CPU is authoritative for every instance and the GPU buffer is its upload | **Rejected for generated content**: it is the second-copy-of-the-population model the brief forbids. Its ownership contract (one writer per persistent buffer) is kept |
| Niagara GPU sims | Determinism is an RNG setting "as long as the delta time is not variable", not a statement about the simulation. Sequencer scrub replays from the start (Desired Age); Sim Cache stores whole frames, and grids only as whole frames | Fixed tick: **taken**. Replay from t = 0 on every backward scrub: **rejected** (that is the 90 s re-simulation per click this project already measured). Exact seek needs checkpoints |
| Houdini DOPs, Blender bake, GGPO | Interval checkpoints with a memory budget and a trail; a parameter change invalidates downstream; the RNG state and tick counter are in the saved state; GGPO's synctest (roll back and compare every frame) | **Taken**: checkpoints every K s under a budget, spacing doubled on overflow, invalidated by a structural or input change, step counter inside; a play-vs-seek test on the device |
| PB-MPM, Houdini OpenCL | Particle → grid deposits through fixed-point integer atomics; no float atomics | **Taken.** WGSL has no float atomics; u32 adds are order-independent (they wrap, so contributions are clamped) |
| Frostbite FrameGraph | Transient resources live one frame and may alias; imported (history) resources are never aliased | **Taken as a rule**: generator windows and scan scratch are per-frame; simulation state, checkpoints and the audio ring are persistent and never aliased |
| TouchDesigner | Wall-clock time, frames dropped to keep realtime, reset rather than seek | This is AV Gen's LIVE mode. A LIVE simulation must still step on a fixed tick, or a dropped frame changes the result |
| UE5 LWC | Integer cells plus a local float offset; camera-relative as early as possible | **Taken for hashing** (integer cell ids). Camera-relative *rendering* is not taken: it touches every renderer (see limitations) |

### Measured on this machine (Apple M2 Max, Dawn on Metal)

| Quantity | Value | Consequence |
|---|---|---|
| `maxStorageBufferBindingSize`, `maxBufferSize` | 4 GiB each (the WebGPU default is 128 / 256 MiB) | A population or checkpoint store is bounded by memory, not binding size, *on this machine*; on other adapters it may not be |
| `maxStorageBuffersPerShaderStage` | 10 (default 8) | Adding storage bindings to every field consumer (PBR fragment included) spends a scarce limit. ADR-1116 adds none |
| `maxComputeWorkgroupsPerDimension` | 65,535 | 2-D dispatch for windows over 4.2M cells |
| Upload, `queue.WriteBuffer` | 1 MB: 0.03 ms. 64 MB: 6.7 ms to return, 8.1 ms to complete (~7.7 GB/s) | Per-frame uploads must stay at KB scale. The audio ring writes ~400 B a frame; a full refill (384 KB) is about 0.05 ms |
| GPU→GPU copy (`CopyBufferToBuffer`) | 8 MB: 0.11 ms. 46 MB: 0.32 ms. 128 MB: 0.80 ms (~150 GB/s) | A checkpoint save or restore is cheap enough to do inside a frame |
| Blocking readback (copy + map + wait) | 16 B: 0.27 ms. 1 MB: 0.28 ms. 46 MB: 5.6 ms | **A readback is never in the frame loop.** Even a 16-byte one costs a quarter-millisecond stall plus a GPU drain |
| Empty submit + wait | 0.027 ms | The floor of any synchronisation |
| Timestamp quantum | ~0.066 ms (the spike's measurement; Dawn quantizes by default) | Sub-tick passes are reported as "<1 tick", never as zero |

### The answers

**Ownership.**

| Class | What | Where it lives | Recoverable from the scene file? |
|---|---|---|---|
| CPU-authored | Scene JSON: the generator description, seeds, densities, field parameters, audio bindings, simulation parameters, checkpoint budget | `Composition`, parameters (the routable layer) | It *is* the scene file |
| GPU-derived | A generator's records for this frame; effector live records; cull lists and indirect args; the audio ring | Per-object buffers, field table | **Yes, every frame**: a pure function of (authored state, t, camera). Losing one costs one frame |
| GPU-simulated | Grid cells, agent positions and headings | `Simulation` | Yes, **by replay** from the initial state; checkpoints make that fast. They are a cache, never project data |
| GPU-render-only | Identifier, depth, velocity, HDR targets | Render targets | Not needed |

Rule (kept from the spike): **if losing a GPU buffer loses something the author made, the design is
wrong.** Nothing in this work violates it. The CPU must always be able to regenerate any generated
element from (description, cell id), and any simulated state from (initial state, inputs, step).

**Synchronisation.** In normal playback, CPU data crosses into GPU state only through
`queue.WriteBuffer`, once per frame:

- uniforms (the field block, now 6,192 B; per-object effector, cull and generator blocks of a few
  hundred bytes);
- the audio ring's new rows (~400 B);
- simulation per-step uniforms (256 B a step).

Nothing is read back in the frame loop. Every stall the code can reach:

| Potential stall | Where | Status |
|---|---|---|
| `readInstanceRecords` / `readCullCounts` / `readVisibleIndices` / `readLodLevels` / `readIndirectArgs` / `Simulation::readGrid` | renderers | Tests and tools only; documented as blocking. No product path calls them in the frame loop (checked) |
| Cull stats readback | `ProceduralRenderer` | Asynchronous ring of three; never waited on (ADR-029) |
| Particle `readCounts` etc. | `ParticleRenderer` | Tests only |
| Catch-up of a seek | `Simulation` | Submits in 256-step chunks and does not wait. A 60 s seek on a large grid can occupy the GPU for a while, but the CPU never blocks on it |
| Pipeline compile on first use | Dawn | 90-180 ms each (live optimizer); pre-warm is the mitigation (ADR-1102). New pipelines in this work are created at renderer init |
| `Context::waitFor` / `waitForQueue` | gpu | Tests, readbacks and shutdown only |

**Lifetime.**

- Storage, indirect and simulation buffers belong to the renderer that uses them (`ProceduralRenderer`
  per object, keyed by name, dropped after `cacheFrames_` frames unused; `Simulation` per scene layout).
- Generator windows are sized by (window radius / cell size)² at first use and grow only. A
  structural edit (cell size, window) resizes them on the next frame; an editor change to anything else
  is a uniform write.
- Checkpoints belong to `Simulation`. They are dropped on a structural change (layout hash), on a
  change of the simulation's input key, or when the scene changes.
- Scene unload drops the renderer's object map and the simulation's buffers with the scene pointer
  change (`resetTemporalHistory`).
- WebGPU `destroy()` is safe on in-flight buffers; memory returns when the submitted work completes.

**CPU mirror.** It represents **the rule, not the population**: the generator's parameters plus pure
functions that answer one question at a time:

- the element in this cell;
- the elements in this region (with a cap);
- the element a ray hits;
- how many elements are present in this window.

It holds no records. Its memory is the size of the description. It is exact for *identity*: presence
is decided by integer hashing only, so the CPU and GPU agree on which cells hold an element. It is
within last-bit float error for *position*. Simulations have no CPU mirror: their state is the
simulation, and the CPU can only regenerate it by replay. The editor treats a simulation as one
selectable unit (the spike's conclusion).

**Picking and inspection.** The identifier target resolves the object (14-bit tagged index), as
today. For a generator object the click then becomes a ray. The mirror walks the window's cells along
that ray and returns the nearest element whose bound the ray enters. The inspector shows:

- generator, version and seed;
- cell id (ix, iz);
- world position, size, rotation and random lanes;
- the derived state the GPU applies to it: every field sample at its position and with its element
  random (audio included). This is the CPU reference of exactly what the effector pass computes.

No GPU readback happens, and no population is materialised.

**Timeline.**

| System | Kind | play | pause | seek / reverse / loop / frame step | FPS change | offline |
|---|---|---|---|---|---|---|
| Generators | stateless | f(t, camera) | same frame | exact, free | none | identical |
| Audio fields (file) | stateless | f(t) | same | exact, free | none | identical |
| Audio fields (live input) | live | input clock | holds | not seekable (live input has no past to seek to) | none | n/a |
| Effectors, cull | stateless | f(t) | | exact | none | identical |
| Grids and agents | **stateful** | fixed 60 Hz steps, per-step inputs | no steps | **checkpoint + replay ≤ K s**, exact | none (fixed step) | identical |
| Particles | stateful | ADR-360 relaxed | | reset (+ optional warm-up) | dt-dependent | renders repeat; scrub ≠ play |

**Determinism.** What it means here, honestly (WGSL §15.7 allows reassociation and fused operations,
and gives transcendentals error bounds rather than exact results; Metal allows round-to-nearest-even
*or* round-toward-zero; Dawn compiles with `math_mode(relaxed)` and fast transcendentals on macOS 15+):

| Level | Promised? | Evidence |
|---|---|---|
| Same device, same Dawn build, same shaders, two runs or two processes | **Bit-exact** for race-free kernels and integer accumulation: generators, audio fields, grids, agents | the spike's Mycelium hash across processes; this work's play-vs-seek tests |
| Play vs seek on that device | **Bit-exact** for generators, audio fields (file), grids and agents through checkpoints | `[gpu][seek]` tests |
| Across Apple GPU families, OS updates, Dawn updates | **Not promised.** Expect last-bit drift; integer identity (which cell holds an element, which onset fired) is still exact | spec text above |
| CPU mirror vs GPU | Identity exact; floats within ~1e-4 relative | parity tests |
| Across backends (Vulkan, D3D12) | Not promised | |
| Live input | Visually equivalent only | ADR-1116 |

Golden images keep tolerances. Persisted caches, if ever added, must be fingerprinted (GPU family, OS,
Dawn revision, shader hash).

**Offline rendering.** `RenderJob` drives the same renderers at the same timeline instants, so:

- generators, audio fields and simulations render identically to the editor;
- AOVs (identifier, depth, velocity) flow through unchanged;
- arbitrary frame seeking is exact (stateless, or checkpointed);
- high resolution is a viewport change: the generator window depends on the camera, not on the
  pixels;
- motion blur reads last frame's transform. A generated element's previous position is the same cell
  evaluated at t − dt by the same pure function. **Not implemented**; the velocity AOV of generated
  elements is object motion only (a stated limitation).

**The CPU path tracer cannot see generated populations or simulations.** It must say so in its output,
not silently drop them (implemented: a warning naming the omitted objects). **Bake-to-points** (an
explicit, serialised placement list) is the escape hatch for the path tracer and for hand edits, and
it stays sufficient for those two uses.

**LIVE rendering.**

- Live audio arrives as the newest analysis frame each render frame (ADR-1116 live feed). No GPU sync
  is involved.
- MIDI moves parameters (`ControlHub` → bus → routes). A generator's description and a field's
  strength are parameters, so a MIDI CC changes a world without a rebuild.
- Rapid parameter changes are uniform writes. A *structural* generator change (cell size, window)
  reallocates once; the cost is bounded by the window, not the world.
- Variable frame time: generators and fields are functions of t, and simulations step on a fixed tick
  with a per-frame ceiling (`maxSubSteps`), so a long frame defers steps rather than enlarging them.
- Dropped frames do not change simulated state, only when it is drawn.
- Audio latency is the analysis hop (10.7 ms) plus one render frame. The GPU adds none.
- Device loss (`GPUDevice.lost`) invalidates every buffer, checkpoints included, silently. Recovery is
  the existing context recreation; generators and audio rings rebuild themselves, and simulations
  replay. There is no special path.
- Projection output is the existing `OutputManager` path; nothing here touches it.

**Memory budgets** (measured or computed; per object or per system):

| Item | Bytes | Budget / practical maximum |
|---|---|---|
| Generator window | 96 B × cells in window (records) + cull scratch ~12 B × cells | 1M cells ≈ 108 MB. Default ceiling 4M cells per object; a window above it is refused at load |
| Audio ring | 384 KB fixed | |
| Audio history (CPU) | 64 × 4 B × 93.75 rows/s ≈ 24 KB/s → 5.8 MB for 4 min | |
| Effector live buffer | 96 B × records | as today |
| Grid state | floats × 4 B × 3 (A, B, initial) | 8 MB table cap (ADR-032) |
| Agents | 16 B × agents + 12 B × cells (fixed-point accumulators) | 4M agents = 64 MB |
| Checkpoints | (grid floats × 4 + agents × 16) per checkpoint | **Default budget 512 MB per scene**, spacing 5 s, doubling when over |
| Indirect args | 20 B × 4 levels × objects | negligible |

**Failure and fallback.**

| Situation | Behaviour |
|---|---|
| Allocation fails / window over the ceiling | Refused at load with a message naming the object and its cell count; never truncated silently |
| Population exceeds budget at runtime | The live lever shrinks the window (`drawDistanceScale`), never the world |
| Compute shader fails to compile | Dawn reports it at load; the object is skipped and the error is logged once (existing renderer behaviour) |
| Checkpoint unavailable (budget, or invalidated) | Replay from the nearest older checkpoint or from 0: **slower, never inexact** |
| Simulation state invalid | A structural change or layout-hash change resets to the initial state and replays |
| Device lost | Existing context recreation; everything GPU-side is derived or replayable |
| Offline renderer cannot run a LIVE-only system | Live input has no offline form: an offline render of a project with live input renders its file audio, or silence, and says so |
| CPU path tracer meets a generator or simulation | A warning naming the omitted objects |

## Phase 2: Architecture Decision

The smallest architecture the evidence justifies is **three additive extensions of systems that
already ship, and no new container**.

```text
Scene (Composition -> flatten -> scene::Scene)                              CPU-owned, authored, unchanged
|
+-- Fields (ADR-025)                     the ONE input path to every GPU consumer
|     + spectrum, onset kinds (ADR-1116)  <- spatial::AudioHistory (file: whole track | live: rolling)
|
+-- ProceduralGeometry                   records -> [effectors] -> cull/LOD (prefix scan) -> indirect draw
|     distribution: single | linear | grid | radial | spiral | spline | grammar | scatter     (CPU, at flatten)
|                 | generator  (ADR-1117)  <- a per-frame kernel writes the window's records   (GPU, per frame)
|                 | points     (ADR-1118)  <- explicit, serialised placements: the bake target   (CPU)
|     CPU mirror (scene/generator.hpp): the rule, queried by cell / region / ray / window
|
+-- Simulation (ADR-032)                 fixed 60 Hz steps, gather-only, integer deposits
|     grids (inject / advect / diffuse / dissipate / reaction-diffusion)
|     + agents mode: a population that senses and deposits into its own grid (ADR-1120)
|     + per-step inputs (time, fields, audio at the step's own second)   (ADR-1119)
|     + GPU checkpoints under a budget: exact seek = restore + replay <= K s (ADR-1119)
|
+-- ParticleRenderer                     unchanged; ADR-360 stands (no deposits, no checkpoints)
```

**Two use cases before any shared abstraction**, checked:

- **Fields as the audio path.** Effectors (Echo Field) and grid injection, plus deformers and
  particles. That is more than two consumers, and the abstraction already existed.
- **The generator** is one kernel ("cells"), with two layers of different scale in Endless Meadow and
  a bounded disc in Echo Field. **No generator registry is built.** A second kernel is the trigger for
  one. The kind carries a `name` and `version` so the scene records which rule it was authored
  against.
- **Checkpoints** serve grids and agents, two users inside one owner (`Simulation`). The store is a
  class inside `Simulation`, not a framework.
- **No "GPU Population", "GPU Field" or "GPUWorld" type** is introduced. "GPU population" is just
  `ProceduralGeometry` (records, effectors, cull), as it already was.

**Rejected:**

- **Particle → grid deposits** (the spike's step 3b, literally). Particles carry ADR-360's relaxation:
  their seek is not exact and their dt follows the frame. A grid fed by particles would inherit both,
  and become the first *grid* whose scrub ≠ play. The stateful population lives in `Simulation`
  instead, on its fixed tick, with its checkpoints. Particles can still *read* the grid (they already
  sample grids through fields), which is what Phase 3 C's spores did.
- A generator registry, a GPU scene graph, a GPU VM, GPU entities, GPU flattening, a `GPUWorld`
  object: the spike's do-not-build list, kept.
- Per-instance ids in the identifier target: 16 bits cannot address generated elements, and widening
  the target touches every pass. Picking goes through the mirror.
- Checkpoints in CPU memory: a 46 MB checkpoint costs 5.6 ms to read back and only 0.32 ms to copy on
  the GPU.

### Data flows

```text
Authoring -> GPU
  scene JSON / editor / MIDI / routes -> parameters -> ProceduralGeometry.distribution.generator,
  FieldSpec, GridField -> per-frame uniforms (WriteBuffer). A structural edit reallocates once.

Audio -> GPU
  file:  AnalysisTrack -> buildAudioHistory (64 log bins, per-bin stretch, onset lists)   once per track
  live:  AnalysisRunner -> triple buffer -> Engine -> LiveAudioHistoryFeed.push           per render frame
  both:  Composition::setAudioHistory -> scene.fields.audio
         -> FieldUniforms: ring rows (WriteBuffer, ~400 B/frame) + onset ages (FieldBlock)
         -> fields.wgsl spectrum/onset -> effectors, deformers, particles, grids, materials

MIDI -> GPU
  CoreMIDI -> ControlHub -> control.* bus -> routes -> parameters -> (as Authoring)

Timeline -> GPU
  FrameTime.renderTime -> generator uniforms (t), field block (t), audio clock (t, file)
  Simulation: target step = floor(t * simRate); per-step uniforms at the step's own second

GPU -> Renderer
  generator kernel -> base records -> [effector pass] -> cull/LOD scan -> drawIndexedIndirect
  simulation -> field table (grid region) -> any field consumer

GPU -> Editor/query
  identifier target (object) -> CPU mirror (ray -> cell -> element) -> inspector
  stats: asynchronous ring (cull counts), FieldAudioStats, generator window size

GPU -> Checkpoint
  Simulation, at every K-th step of play or replay: CopyBufferToBuffer(state -> checkpoint slot)

Checkpoint -> Seek
  seek(T): target step S; newest checkpoint <= S with a matching input key -> copy into state,
  stepsTaken = its step; replay S - its step steps with per-step inputs (chunked, no CPU wait)
```

The current implementation satisfied none of these before this work (Phase 0); each is implemented in
Phase 3 below.
