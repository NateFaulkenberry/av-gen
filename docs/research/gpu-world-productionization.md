# GPU Procedural Systems: Productionization

Branch `gpu/productionization`, from main `e94ac0b8`, started 2026-10-05. The brief is
`docs/research/gpu-productionization-brief.md` (the owner's, verbatim). This document belongs to the
engine agent (brief Phases 0-3). Phase 4, the flagship LIVE scene and its art, belongs to a separate art
agent, and the handoff to it is at the end of this file. Machine: Apple M2 Max (38-core GPU, 64 GB),
macOS 26, Dawn/WebGPU on Metal. Every GPU run goes through `tools/gpu-lock.sh`.

> **Status: PHONOTAXIS (Phase 4, the flagship LIVE scene) was dropped by the owner on 2026-10-05.** The scene
> (`examples/phonotaxis/`: its build script, projects, performer, replay and recorded performance) and its
> `examples/index.json` entries were deleted before the merge to main. Every PHONOTAXIS section below is kept as
> history only; the files it names no longer exist. What the scene produced and stays on main: ADR-1122 (a
> grid's behaviour is parameters), ADR-1123 (orbit camera pivot), ADR-1124 (scene states read the bus after the
> sources), `tools/frame_coverage.py` (the dead-space check), and the engine findings recorded below (the
> compound-field typing defect and the "Found and recorded" list). Phases 0-3 are unaffected.

Read with: `docs/research/gpu-world-architecture-spike.md` (the spike, final decision GREEN, narrowly
scoped), `docs/development/gpu-sim-seek-investigation.md`, ADR-1114 and ADR-1115.

## PROGRESS (resume from here)

| Phase | State | Where |
|---|---|---|
| 0. Reality check | done | §Phase 0 |
| 1. CPU/GPU bridge research | done (primary sources plus measurements on this machine) | §Phase 1 |
| 2. Architecture decision | done: three additive extensions, no container | §Phase 2 |
| 3. Production hardening | done: ADR-1116 to ADR-1121, three regression scenes, benchmarks | §Phase 3, §Regression benchmarks |
| 4. Flagship LIVE scene | **DROPPED by the owner 2026-10-05** (scene deleted; history below). Earlier: **PAUSED by the owner 2026-10-05** ("I'm not sure I want to use that one"). PHONOTAXIS was redesigned as a flight and passes the dead-space check; no final measurements or deliverables for the flight. ADR-1122 to ADR-1124 and the Critic's LIVE mode are done | §PHONOTAXIS: where it stood (below), §Phase 4 |

### PHONOTAXIS: where it stood (paused, then dropped by the owner on 2026-10-05)

*History: the scene was dropped by the owner on 2026-10-05 and `examples/phonotaxis/` was deleted.*

**How the design got here.** The owner redirected the design three times:

1. No dead space in any frame, and no vortex hero.
2. No centrepiece at all: "a huge reactive world the camera is flying through".
3. The track is **Feline Footwear** (`assets/audio/feline-footwear.wav`: gitignored, symlinked, never
   committed). Trench and All You Got are out of this scene.

The Phase 4 text further down still describes the earlier design, the orbit around the Cochlea tower.
It is history: where it differs from this section, this section is right.

**The flight design (`examples/phonotaxis/build.py`; never edit the generated JSON).**

- **The camera.** It flies forward continuously, with no orbit and no hero.
  - Speed comes from the `speed` macro (x 50 m/s), integrated into distance. Every layer that travels
    with the camera follows it: the floor, the roof, the fields around the traveller, the spores and the
    lantern.
  - Two incommensurate LFOs weave the camera sideways. The heading leads the weave, the camera banks
    with it, and a slow crane LFO moves it up and down.
  - Each state is a stage of the journey with its own speed, altitude, look target and field of view,
    so every state change is a climb, a dive or a change of pace.
  - Three knobs fly it: ALTITUDE (CC 25, +-30 m), SPEED (CC 26) and STRIKE (pad 36, a lunge forward).
- **The layers.**
  - **Floor:** the 420k-agent organism's trail on a 2 km plane that follows the camera. It is
    sampled twice, the second read rotated and scaled, so the wrapping tile never shows.
  - **Roof:** the same trail again, 85 m up. Looking up is never looking into nothing.
  - **Fossil roads:** a dim voronoi web in the floor program, so ground the organism has not reached is
    dark but never a void.
  - **Reeds:** they grow where the organism walked. Each one hears its own band, at a delay set by its
    distance from the traveller.
  - **Sea whips:** tube groves. A corridor field keeps the flight line clear.
  - **Colossal horns:** 80-230 m tall, with the music climbing them. They have a faint fresnel rim.
  - **Drifters and spores:** drifters float 20-55 m up; spores rise around the traveller.
  - **Kick fronts and the strike:** they race outward from the traveller.
  - **Colour:** colour is frequency everywhere. Ember is the lows, teal the mids, violet the highs.
    Rebirth uses ember, gold and white.
- **The states on Feline Footwear.** These come from production's analysis and a `--sonic-trace` of
  `visual.drive`, with the state machine simulated offline; they are not yet confirmed in a render.

  | Time | State |
  |---|---|
  | 0 s | Dormant |
  | 7 s | Germination |
  | 32 s | Chorus |
  | 80 s | Surge |
  | 159 s | Eruption |
  | 178 s | Surge |
  | 193 s | Eruption (climax) |
  | 203-206 s | a fast fall through Surge and Chorus to Collapse, as the song stops |

  Rebirth is reached only by pad or live input. On Night Shift, at sensitivity 0.8, the sequence is
  Germination, Chorus, Collapse at 77 s, Rebirth at 84 s and Collapse at 102 s.

**The dead-space check: `tools/frame_coverage.py`.**

- **The method.** It box-blurs the frame and splits it into a 32x18 tile grid. A tile is empty when its
  p99 luma is under 6/255 and its p99-p1 spread is under 3/255. That separates empty from dark: grain
  does not count as content. A frame fails above 3% empty, or with an empty region larger than 1.5%.
  Exit code 1 means a failure.
- **It catches the failures it should.** It fails the owner's screenshot at 53.6% and every still
  from the Cochlea era.
- **Results on the final flight design.**

  | Sample | Frames | Failed | Worst empty |
  |---|---|---|---|
  | Whole song on Feline Footwear, every 0.5 s | 422 | 1 (19.4 s) | 1.7%, as one 1.7% region (limit 1.5%) |
  | Whole song on Night Shift, every 0.5 s | 210 | 0 | 0.9% |
  | Each of the 7 states held for 40 s, every 1 s | 280 | 7 | see below |
  | Single stills at t = 40 | 7 | 0 | 0.4% |
  | Song start (t = 0) | 1 | 0 | 0.9% |

  The 7 state failures are all `frame_000001`, the first frame after the render seeks into the range.
  All seven are identical (9.4% empty), so they are an artefact of landing after a seek, not a view.
- **Before the fixes.** Dormant was 24.1% empty, Germination 11.6% and Collapse 19.6%. The end of the
  song reached 77%.
- **The fixes:** the roof; the fossil roads; a base appetite, so silence thins the roads but never
  erases them; a stalk is never a black silhouette; a brighter fog colour; dark floor albedo that the
  lantern can light.
- **Not done:** live (editor-path) frames have not been checked. The Critic gained the same rule,
  `still_dead_space`, on branch `live-critic` (commit `2040e62`).

**Weakest points when paused.**

- The look runs lavender. The fossil web plus the brighter fog lift the mean luma to 0.18-0.27, and
  Eruption to 0.56. "Darkness is the canvas" has been partly given up to pass the check.
- The roof and floor make two parallel planes, which risks reading as a tunnel or slit-scan look.
- The horns read mostly as dark silhouettes.
- Eruption's temporal echo smears.

**What was not done for the flight design.**

- Flight performance on the live path. The numbers further down are the Cochlea's. The roof doubles
  the per-fragment trail sampling, and the fossil voronoi adds more.
- A live capture, a new performance or replay, and a Critic LIVE run.
- The review deliverables in `~/Desktop/av-gen-review/36-flagship-live/`, which are still the
  Cochlea's.
- The final suites.
- The stale `phonotaxis-replay*.json` files (the Cochlea design on All You Got) were deleted.
  `performance-2026-10-05.csv` is kept as a record of the Cochlea-era performance.

**An engine defect worked around, not fixed: a compound field is always typed scalar on the GPU.**

- **Where it lives:**
  - `spatial::packField` in `src/spatial/field.cpp` sets `g.type` from `fieldTypeOf(kind)`, which
    returns Scalar for `compound`. Only grid fields override the type, from their bound grid.
  - `materialFieldValue` in `shaders/material.wgsl` dispatches on that type.
- **What goes wrong:** a material program's `field` op on a compound of vector children (for example
  two agents-grid reads combined with `max`) receives `vec4(length)` instead of the vector. On the CPU,
  `effector.cpp` uses the spec's `type()` in the same way, so an effector on a compound is scalar too.
- **The effect:** PHONOTAXIS's floor rendered grey, because the species, and so the colour, were lost.
- **Who else is exposed:** any scene that reads a compound of vector, agents-grid or colour children
  through a material program. Compounds of scalar fields are unaffected.
- **The workaround here:** two `field` ops added together in the program.
- **The proper fix:** type a compound from its children, for example Vector if any child is a vector
  and Color if all are colour, on both the CPU and the GPU. It needs an ADR, because existing programs
  that read such compounds would change.

Resume points, if anything has to be picked up cold (the PHONOTAXIS points are history: the scene was dropped
by the owner on 2026-10-05 and its files deleted):

- PHONOTAXIS is generated: edit `examples/phonotaxis/build.py`, then run `python3 examples/phonotaxis/build.py`.
  Never edit the JSON it writes. `perform.py` is the scripted performer (audio into BlackHole, MIDI from a
  virtual source). `replay.py` turns a recorded performance into an offline project. The review stills
  and the clips are in `~/Desktop/av-gen-review/36-flagship-live/`.
- The Critic's LIVE mode is on branch `live-critic` of `~/Documents/GitHub/creative-critic` (not pushed).

- Every GPU run goes through `tools/gpu-lock.sh`.
- Tests: `avgen_tests "[audio-fields],[generator]"`; `avgen_render_tests "[audio-fields],[generator],[effectors],[simulation],[gpu-regression]"`.
- The benchmark is `avgen_render_tests "[.perf][gpu-regression]"` and the bridge probe is
  `"[gpu-bridge]"`. Run both alone, at a load under 5.
- The regression projects are `examples/gpu-regression/*.json`.

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
twice: once while other agents were compiling (load 11-25), and once at load 5.9. The ranges below
span both, and they agree.

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
| Upload, `queue.WriteBuffer` | 1 MB: 0.03 ms. 64 MB: 5.9-6.7 ms to return, 7.6-8.1 ms to complete (~8 GB/s) | Per-frame uploads must stay at KB scale. The audio ring writes ~400 B a frame; a full refill (384 KB) is about 0.05 ms |
| GPU→GPU copy (`CopyBufferToBuffer`) | 8 MB: 0.08-0.11 ms. 46 MB: 0.32-0.38 ms. 128 MB: 0.80-0.87 ms (~120-150 GB/s) | A checkpoint save or restore is cheap enough to do inside a frame |
| Blocking readback (copy + map + wait) | 16 B: 0.16-0.27 ms. 1 MB: 0.26-0.28 ms. 46 MB: 5.6 ms | **A readback is never in the frame loop.** Even a 16-byte one costs a quarter-millisecond stall plus a GPU drain |
| Empty submit + wait | 0.024-0.027 ms | The floor of any synchronisation |
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

## Phase 3: Production Hardening (what was built)

Order as proposed: audio inputs, then the generator, then stateful systems. Each item below is reachable
from the scene file, the editor, the CLI, live mode and an offline render. Each is exercised end to end
through `app::Engine` + `SceneRenderer`, not only unit-tested. None reads the GPU back in the frame
loop, and each reports its bytes to `--live-profile` (`resources.gpuSystems`) and the Performance
panel.

### Step 1: per-element audio inputs (ADR-1116)

- **Two field kinds:**
  - `spectrum`: each element hears its own band at its own delay-by-distance, as a range, an
    element-random band or angle fans.
  - `onset`: the newest 8 onsets of low / mid / high / beat, as travelling fronts.

  Fields are the one input path, so effectors, Field deformers, emissive fields, particle field forces,
  grid injection and agent deposits all hear audio with no new code.
- **Element.** `InstanceRecord::random.w` is the record's own random, set in `cs_effectors`, the
  vertex stage and the particle pass (`var<private> fieldElement`).
- **Data.**
  - The 64-bin spectrogram is a 1,536-row ring (384 KB) appended to the field table at binding 15,
    so there is no new binding.
  - Onset ages and strengths ride in the `FieldBlock`, which grows from 5,904 to 6,192 B.
  - The audible delay is bounded at 12 s (ADR-1119).
- **Sources.**
  - The whole analysed track: offline, editor, and live playback of a file. It is folded once to log
    bins and stretched per bin over the track's 20th to 98th percentile. Its clock is the transport,
    so play and seek are exact.
  - Live input: a rolling history from the newest analysis frame, with running percentiles; its
    clock is the input's. Visually equivalent, not equal.
- **Reach:** scene JSON (`audioBand`, `bandLow/High/Repeat`, `audioDelay`, `audioSpeed`,
  `onsetSource/Decay/Width`); the World inspector (these register as parameters for audio kinds
  only, so routes and MIDI reach them); live (the live feed); offline (`RenderJob` through the engine).
- **Tests:**
  - CPU/GPU parity ≤ 2e-4 through forward, back, jump and new-track ring updates.
  - The effector pass gives records diverging scales (spread > 0.5).
  - The history, the builder, the live feed, JSON and parameters are covered in `avgen_tests`.

**Found and fixed on the way (ADR-1121, the owner's ruling).** The GPU effector pass disagreed with the
CPU reference ADR-025 names in **17 of 36 (op, blend) pairs**, Scale + Add (the default) among them.
The GPU now transcribes the reference, and a parity test over all 36 pairs fails before and passes
after. Four shipped scenes change visually (ADR-1121's table).

### Step 2: the generator distribution, its mirror, picking and bake (ADR-1117, ADR-1118)

- **`distribution.kind: "generator"`.**
  - A per-frame kernel (`shaders/generator.wgsl`) writes a camera-centred window of hashed cells into
    the object's record buffer.
  - From there the **existing** effectors and prefix-scan cull and LOD draw it. Empty cells are
    zero-scale records the cull rejects.
  - One kernel ("cells"), no registry.
  - Layers share a ground through `groundSeed`.
  - Only cell size, view distance and region are structural; presence, size, tilt, ground and
    clusters are per-frame parameters, so a route or a MIDI CC moves a world with no rebuild.
- **The CPU mirror** (`scene/generator.hpp`) is the rule. It answers queries for a cell, a region, a
  ray, the nearest element and the present count of a window. It holds no records.
- **Picking.** The depth position goes into generator space, and the nearest element is resolved
  there. The World inspector shows the element's cell, position, size and random lanes, plus every
  effector field the GPU applies to it, computed by the CPU reference with the element's own random.
  Nothing is read back.
- **Bake to points.** The new serialised `points` distribution (at most 65,536 placements) is the
  bake target. The inspector button and `Composition::bakeGeneratorToPoints` write it.
- **CPU consumers.** The path tracer notes "generator distribution: Unsupported". Navigation, ecology
  lights and the vegetation simulation skip generators.
- **Reach:** scene JSON; the World inspector (description parameters, the element, the bake); the
  viewport pick; live levers (the window shrinks with `drawDistanceScale`); offline; CLI.
- **Tests:**
  - Mirror parity is identity-exact: 0 mismatches over 8,844 present cells, bounded and unbounded,
    at two cameras. Positions agree within 16 ulps.
  - Visible ≤ present.
  - The same (t, camera) draws byte-identical frames after different histories.
  - 100 km costs what 0 m costs.
  - Effectors work on generated records.
  - Unit tests cover the bake, round-trips and refusals.

### Step 3: stateful GPU systems (ADR-1119, ADR-1120)

- **Per-step inputs.** Every simulation sub-step reads a field block packed at its own second, bound by
  dynamic offset. Long replays reposition the audio ring per chunk. **Moving and audio-driven inputs
  now replay exactly**, which was ADR-1114 §4's open defect.
- **GPU checkpoints.** GPU-to-GPU copies every `checkpointInterval` (default 5 s) of steps, under a
  budget (`--sim-checkpoint-mb`, default 512). Overflow doubles a grid's spacing. A checkpoint is
  keyed on the grid, the layout and `FieldSet::inputKey` (every parameter base plus the audio
  revision). A seek restores the best checkpoint at or before the target and replays at most the
  spacing. Live-input audio never restores.
- **Agents grid** (`mode: "agents"`). Up to 4M agents on a plane of up to 1024². They sense, turn,
  steer by a field, move and deposit with u32 fixed-point atomics, scaled by a deposit field (audio).
  The trail blurs and fades. The grid is sampled through ordinary `grid` fields.
- **Particles keep ADR-360 and never deposit:** the rejected step 3b (ADR-1120).
- **Catch-up chunks are synced.** A replay waits for each catch-up chunk before queueing the next.
  Measured: an unsynced 6 s backlog produced a silently empty frame in `avgen --render` (see Risks).
  The wait happens on a seek or a fresh render only, never in playback.
- **Reach:** scene JSON (`grids[].mode/agentCount/species/sensor*/turnAngle/stepSize/depositAmount/
  repel/depositField/checkpointInterval`); the CLI (`--sim-checkpoint-mb`); the Performance panel
  (agents, state, checkpoints); `--live-profile`; offline; live.
- **Tests:**
  - An agents grid driven by onset, element-band spectrum and moving curl noise: played, fresh seek
    and restored seek give byte-identical cells and agents. The controls hold: one frame later
    differs in more than 10% of cells and more than half the agents.
  - A wave-fed grid replays exactly.
  - Two runs are bit-identical.
  - The budget holds.
  - Through the engine (Mycelium scene, 1M agents), the seek lands on the played grid byte for byte,
    and on the played frame within the project's same-frame rule (below).

### Regression scenes (permanent)

`examples/gpu-regression/`, each a scene plus a project that zeroes the composition's four default
audio routes. Left in, they spin the whole world, which would make every field a function of frame
history. Each scene says its purpose in its `_note`.

- **echo-field**: a generator disc (220 m, 0.22 m cells, about 1M cells) of reeds whose height and
  glow follow an angle-band `spectrum` field at 9 m/s, plus `onset` kick fronts.
- **endless-meadow**: four unbounded generator layers (ground tiles, grass, reeds, spires) on one
  ground. The reeds hear their own bands; the spires flare on the kicks.
- **mycelium**: an agents grid (1M agents, 512², three species) depositing by band and kick and
  steered by moving curl noise, shown by a 110k-cell generator carpet through grid fields.

Tests: `tests/rendering/test_gpu_regression_scenes_gpu.cpp`, `[gpu][gpu-regression]` in the default run,
and `[.perf][gpu-regression]` for the benchmark.


## Regression benchmarks

Two instruments, both run under the lock at a 1-minute load under 5 (M2 Max, 1920×1080):

1. `avgen --live-profile --project examples/gpu-regression/<scene>.json --quality ultra --start 20`:
   the product's own profiler, 300 measured frames, headless.
2. `avgen_render_tests "[.perf][gpu-regression]"`: the Realtime tier through the engine, 120 frames.
   It also times seeks, and it CHECKs the expected ranges below, so a step change fails it.

**Live profile, Ultra (2026-10-05, load 4.4-5.0):**

| Scene | Population | GPU median / P95 | Scene pass | Shadows | Generator pass | Effectors | Cull | Simulation | CPU work median | GPU-system bytes held |
|---|---|---|---|---|---|---|---|---|---|---|
| Echo Field | 696,000 generator cells (bounded disc, camera window) | 45.2 / 49.8 ms | 31.1 ms | 10.6 ms | 0.26 ms | 0.98 ms | 0.26 ms | — | 0.74 ms | 96.0 MB records, 0.4 MB audio ring |
| Endless Meadow | 280,820 cells over 4 layers (unbounded) | 16.9 / 21.4 ms | 12.3 ms | 2.9 ms | 0.13 ms | 0.13 ms | 0.20 ms | — | 0.70 ms | 27.0 MB records, 0.4 MB ring |
| Mycelium | 1,000,000 agents (512², 3 species) + 110,000-cell carpet | 42.1 / 48.4 ms | 31.7 ms | 5.8 ms | <1 tick | 0.46 ms | 0.07 ms | **3.01 ms** (1 step/frame) | 0.81 ms | 24.0 MB carpet, 32.8 MB state, 5 checkpoints 101 MB |

What the numbers say:

- **The CPU is flat.** 0.70-0.81 ms of CPU work per frame in every scene, from 110k to 1M elements. No
  frame reads the GPU back.
- **Generating is nearly free.** The generator kernel costs 0.26 ms for 696k cells, which matches the
  spike's prototype (0.20 ms per 1M).
- **Drawing is the cost**, and so are the shadows of what is drawn. None of these scenes is
  60-fps-ready at Ultra as authored, and that is by design: they are regression fixtures at the scale
  the spike built them. The handoff's budget rules come from this table.
- **1M agents cost 3.0 ms a step.** That is 4.5× the spike prototype's 0.66 ms per 2M. The production
  kernel samples a compound audio field and curl noise per agent per step; the prototype had neither.

**Expected ranges** (`[.perf][gpu-regression]`, Realtime tier; each widened about 1.5-2× from measured):

| Scene | CPU work | GPU frame | GPU-system bytes held at 60 s | Population | Seek 20→60 s | Seek 60→58 s |
|---|---|---|---|---|---|---|
| Echo Field | ≤ 8 ms | 15-90 ms | ≤ 140 MB | ≥ 600k cells | ≤ 1 s (stateless) | ≤ 1 s |
| Endless Meadow | ≤ 8 ms | 4-40 ms | ≤ 60 MB | ≥ 200k cells | ≤ 1 s | ≤ 1 s |
| Mycelium | ≤ 10 ms | 10-80 ms | ≤ 600 MB | ≥ 1M agents | ≤ 15 s (replays from its newest checkpoint) | ≤ 2 s (restores the 55 s checkpoint) |

**Benchmark run, Realtime tier** (`[.perf][gpu-regression]`, 2026-10-05, exit 0: every range held). The
first run was at load ~5. This rerun gave up waiting for a quiet machine and ran at load ~10, and agrees
with the first within a few percent:

| Scene | CPU work | GPU frame | Generator / effectors / cull / sim GPU | Bytes held at 22 s | Seek 20→60 s | Seek 60→58 s |
|---|---|---|---|---|---|---|
| Echo Field | 1.08 ms | 43.4 ms | 0.20 / 0.98 / 0.26 / — ms | 91.9 MB | 50 ms | 48 ms |
| Endless Meadow | 0.70 ms | 15.9 ms | 0.13 / 0.13 / 0.20 / — ms | 26.1 MB | 27 ms | 20 ms |
| Mycelium | 0.73 ms | 34.4 ms | 0.07 / 0.46 / 0.07 / 2.88 ms | 131.6 MB (231 MB of checkpoints by 60 s) | **6.5 s** (2,370 steps replayed past the played checkpoints) | **550 ms** (restored 55 s, replayed 180 steps) |

Synchronisation in every scene: **no blocking readback in the frame loop.** A seek on Mycelium waits
for the GPU once per replay chunk, on purpose (Risks, item 1). A *forward* seek past the newest
checkpoint replays at 2.9 ms a step for 1M agents. A backward one restores and replays at most the
spacing.

## Phase 4 handoff (for the art agent)

You own the flagship LIVE scene: brief Phase 4, its research, art direction, MIDI vocabulary, camera,
post, evaluation and the art pass. This section is what the engine now does for you, how to author
it, what it costs, and where it bites. Everything below is on `gpu/productionization`. Read ADR-1116
to ADR-1121 if a detail matters.

### What the engine can do now that it could not before

1. **Audio as a spatial field.** `spectrum` and `onset` field kinds. Any element of anything a field
   reaches can hear:
   - its own frequency band, through the element random or angle fans;
   - the past, through a delay that grows with distance (up to 12 s);
   - the last eight kicks, snares, hats or beats, as fronts that travel outward and decay.

   Fields reach effectors (procedural records: position, scale, rotation, colour, emission, density),
   Field deformers (per vertex), emissive fields, particle field forces, grid injection and agent
   deposits.
2. **Worlds that are functions.** `distribution.kind: "generator"`: unbounded or bounded lattices of
   hashed cells, generated every frame around the camera. A layer costs its window, not its extent; a
   camera can fly forever. Presence, size, tilt, ground and clustering are live parameters, so MIDI and
   routes reshape a world with **no rebuild**. Layers share a ground through `groundSeed`.
3. **A living population that remembers.** `grids[].mode: "agents"`: up to 4M agents in up to three
   species. They sense, turn, steer by a vector field and deposit trails, with the deposit scaled by
   any field (so audio). The trail is a grid field anything can sample. Seeks are **exact**, from
   GPU checkpoints.
4. **Exact time for stateful systems.** Every simulation sub-step reads its own second's inputs, so an
   audio-driven grid scrubs and renders offline exactly as it played.
5. **Escape hatches.** Bake a generator region to `points` to hand-edit it. Pick a generated element
   in the viewport to inspect it, including the field values the GPU applies to it.

### Scene JSON, by example (copy from `examples/gpu-regression/`)

An audio field is a `field` node:

```json
{"name": "echo", "kind": "field", "position": [0, 0, 0],
 "field": {"kind": "spectrum", "audioBand": "angle", "bandLow": 0.0, "bandHigh": 1.0, "bandRepeat": 2,
           "audioSpeed": 9.0, "audioDelay": 0.0, "waveGeometry": "radial", "axis": [0, 1, 0],
           "strength": 1.0, "falloff": {"kind": "none"}}}
{"name": "kick", "kind": "field",
 "field": {"kind": "onset", "onsetSource": "low", "onsetDecay": 3.0, "onsetWidth": 2.5, "audioSpeed": 9.0,
           "strength": 1.0, "falloff": {"kind": "none"}}}
```

`audioBand` is one of:

- `range`: the mean over `bandLow..bandHigh` (0 = 32 Hz, 1 = 16 kHz, on a log axis);
- `element`: each element its own band, from its random;
- `angle`: fans of frequency about `axis`.

`onsetSource` is `low` (kick), `mid` (snare), `high` (hat) or `beat`. With `onsetWidth` 0 an onset is a
flash everywhere at once.

A generator layer is a procedural node:

```json
{"name": "reeds", "kind": "procedural", "procedural": {
  "source": {"kind": "cylinder", "radius": 0.04, "height": 1.6, "radialSegments": 5, "caps": false},
  "sourceTransform": {"position": [0, 0.8, 0], "rotation": [0, 0, 0], "scale": [1, 1, 1]},
  "variation": {"seed": 31},
  "distribution": {"kind": "generator", "generator": {
    "cellSize": 0.9, "viewDistance": 140.0, "presence": 0.55, "clusterSize": 30.0, "clusterContrast": 0.95,
    "jitter": 1.0, "sizeMin": 0.7, "sizeMax": 1.6, "tilt": 0.15,
    "bounded": false, "regionMin": [-50, -50], "regionMax": [50, 50], "regionRadius": 0.0,
    "groundHeight": 0.0, "groundAmplitude": 6.0, "groundFrequency": 0.012, "groundSeed": 7}},
  "effectors": [{"field": "reedBands", "op": "emission", "blend": "add", "strength": 6.0}],
  "lod": {"cull": true, "maxDistance": 140.0, "count": 1},
  "material": {"baseColor": [0.05, 0.05, 0.06], "emissiveColor": [0.3, 0.8, 1.0], "emissiveIntensity": 0.4},
  "materialVariation": {"emissiveRandom": 0.5, "emissiveSparsity": 0.3}}}
```

An agents grid goes in the top-level `grids` array. Show it through a `grid` field node
(`{"kind": "grid", "reference": "<grid name>"}`) on effectors, emissive fields or particle forces:

```json
{"name": "mycelium", "mode": "agents", "wrap": "wrap", "resolution": [512, 1, 512],
 "boundsMin": [-40, -1, -40], "boundsMax": [40, 1, 40], "agentCount": 1000000, "species": 3,
 "sensorAngle": 0.5, "sensorDistance": 8.0, "turnAngle": 0.4, "stepSize": 1.0, "depositAmount": 0.02,
 "repel": 0.7, "depositField": "feed", "velocityField": "flow", "advect": 1.5,
 "diffusion": 0.35, "dissipation": 2.5, "simRate": 60.0, "maxSubSteps": 4, "seed": 2026, "checkpointInterval": 5.0}
```

### Live and MIDI hooks

These parameter paths are confirmed by `[.list-params]` on the regression scenes:

- `field/<name>/strength`, `bandLow`, `bandHigh`, `bandRepeat`, `audioDelay`, `audioSpeed`,
  `onsetDecay`, `onsetWidth`: audio fields.
- `procedural/<name>/distribution/generator/presence`, `sizeMin`, `sizeMax`, `tilt`, `clusterSize`,
  `clusterContrast`, `jitter`, `groundHeight`, `groundAmplitude`, `groundFrequency`, `regionRadius`:
  uniform writes, no rebuild. `cellSize` and `viewDistance` are registered too, but they **reallocate
  the record buffer** (once, grow-only). Do not put them on a fast MIDI knob.
- `procedural/<name>/effector/<n>/strength`: how hard a field acts.
- (Superseded by ADR-1122: a grid's behaviour is now `grid/<name>/<leaf>` parameters, and only its layout
  re-seeds it.) Grid settings are scene-file only (no parameters). Drive a grid through its fields instead: its
  deposit field's strength, its velocity field's strength or speed.

A MIDI CC to a parameter, in the project:

```json
"control": {"midi": {"enabled": true, "filter": "*", "bindings": [
  {"source": "*", "channel": 0, "kind": "cc", "number": 1,
   "parameter": "procedural/reeds/distribution/generator/presence", "component": 0, "min": 0.1, "max": 1.0}]}}
```

Routes work as everywhere (`audio.bass`, `audio.onsetLow`, `control.*`, ...). Use routes for
continuous signals. Use audio *fields* when each element must hear the music differently: its own band,
its own moment.

### Budgets (M2 Max, 1920×1080, Realtime tier; see "Regression benchmarks")

See the measured table below. The rules of thumb from it:

- **Drawing dominates; generating is nearly free.** A generator window of 700k cells is about 0.5 ms of
  compute, but 700k *drawn* reeds are tens of milliseconds of raster at production shading. For
  60 fps, budget **≤ ~250k visible** thin elements. Use `lod.maxDistance`, a coarser `cellSize` for far
  layers, and several layers of different scale rather than one dense one.
- **Agents cost per step.** 1M agents is about 3 ms a step, with one step per frame at 60 fps and a
  60 Hz `simRate`. 250k is about 4× cheaper. A seek replays up to `checkpointInterval` of steps (5 s =
  300 steps ≈ 1 s at 1M), and a *fresh* render or a forward seek past the checkpoints replays from the
  last one it has. Lower `simRate` (30) halves the cost if the motion allows it.
- **Checkpoints:** 1M agents on 512² is 19.3 MB each, 231 MB per minute at 5 s spacing. The budget
  defaults to 512 MB (`--sim-checkpoint-mb`).
- **The audio ring** is 0.38 MB fixed and about 400 B a frame.

### Pitfalls (each one met while building this)

1. **The composition's default audio routes spin and scale the whole world** (`root/rotationSpeed ←
   audio.mid`, integrated over frames; `root/scale ← bass`; `scene/brightness ← rms`;
   `root/impulse ← onsets`). A scene that does not route those four itself gets them. The
   regression projects zero them; your project must decide on purpose. The integrated spin is not a
   function of t, so it breaks exact seeking for every field and grid hanging off the root.
2. **Agent trails scale with density.** Physarum packs agents into lanes. A lane holds hundreds of
   agents per cell, and the trail reaches the thousands unless `depositAmount` is small and
   `dissipation` is high. A trail that blows the frame white means emission ≈ trail × strength is
   huge; scale the effector strength to the trail's range (the regression scene uses 0.05).
3. **A spectrum field hears at most 12 s into the past.** Anything older reads 0.
4. **Live input is not seekable.** Agents fed by live-input audio never restore a checkpoint. The live
   spectrogram is stretched by running percentiles, so it is visually equivalent to a file render,
   not equal.
5. **Generated elements have no CPU records:**
   - no navigation obstacles, no ecology lights, no CPU path trace (the tracer says so);
   - the LOD debug overlay skips them;
   - `MaterialVariation.hueShift` is not applied; use `chromaDrift`, a colour field or a Color
     effector;
   - a baked `points` copy keeps placement, not the random lanes.
6. **Scale effectors changed meaning (ADR-1121).** `scale` with `add` now multiplies the authored scale by
   (1 + s·k), as ADR-025 always said. Before, the GPU nearly doubled it. Tune against the new reading.
7. **f32 positions quantise far out:** 7.8 mm at 100 km. An endless flight is fine for the hashing, but
   a camera kilometres from the origin should keep its nearby content near the origin.
8. **Picking is through the CPU mirror.** It resolves an element from the clicked depth position, so it
   ignores effector offsets (a picked element is where the rule put it, before the effectors moved it).
9. **A seek on a big agents grid stalls the CPU on purpose** while it replays (it waits per chunk; see
   Risks). In a performance, prefer cues that do not jump the transport.
10. **`examples/gpu-regression/` scenes are regression fixtures. Do not art-direct them.** Copy them.

## Risks found while building, and what is left

**Fixed here:**

- **GPU effectors disagreed with the CPU reference** in 17 of 36 (op, blend) pairs, Scale + Add
  among them. ADR-1121 fixes it, on the owner's ruling. Four shipped scenes change.
- **Grid replay was exact only for still inputs** (ADR-1114 §4). Per-step inputs (ADR-1119) fix it.

**Open, and known:**

1. **A long unsynced GPU backlog produced a silently empty frame.**
   - What happened: a fresh `avgen --render` of Mycelium at 32 s queued about 6 s of catch-up work
     in eight chunks. The PNG came back all zeros, with "GPU errors: 0" and the device not lost. At
     28 s (about 5.2 s of work) it rendered. The test harness, whose readbacks happen to wait,
     rendered 40 s correctly.
   - Mitigation: `Simulation` waits for each catch-up chunk.
   - **The root cause in Dawn or Metal was not found.** Any other path that queues seconds of GPU
     work without a sync could meet it. `RenderJob` reporting success over an empty frame is a
     defect worth its own investigation.
2. **One-level frame residue between continuous play and a fresh seek.** In Mycelium, 36 of
   230,400 channels differ by one level, while the simulation state underneath is byte-identical, and
   a restored scrub matches a fresh seek byte for byte. This is renderer frame history, unexplained,
   and inside the project's same-frame rule (ADR-1114's tolerance).
3. **The composition's default routes spin the world**, and the spin integrates over frames (pitfall
   1 in the handoff). This is not new, but it now matters more: it makes every field, generator and
   grid under the root a function of frame history, not of t. A scene that wants exact seeks must route
   those four itself. Zero-amount routes, the regression projects' answer, log "dead route" warnings
   at load.
4. **Opt-in blocking readback.** The LOD debug overlay (`readProceduralLodLevels`) reads back
   synchronously when enabled. It is a debug view, not a product path, but it is in the frame loop
   when on.
5. **Still frame-sampled inside simulations:** a node transform the engine animates, a triggered
   field's age (ADR-906), and a Grid field read by another grid.
6. **Live input** is visually equivalent, not equal. The live spectrogram holds a row between render
   frames instead of the lost hops, because the analysis triple buffer delivers only the newest frame.
7. **No camera-relative rendering.** Generated positions are f32 object space, 7.8 mm quantum at
   100 km.
8. **The CPU path tracer** cannot see generators or simulated grids. It says so; the bake is the
   escape hatch.
9. **Unverified on screen:** the World inspector's Generator section (element, derived fields, bake
   button) and the Performance panel lines. ImGui is not visible to this agent. The logic under them
   is tested; the widgets are not.
10. **Not built:** a velocity AOV for generated elements' own motion, `MaterialVariation.hueShift` for
    generators, a generator registry (no second kernel yet), a lossless live spectrogram (a FrameTap
    feeding an SPSC queue), and per-element picking that includes effector offsets.

## Suites (end of the engine run)

Run at `ba8a0092`, after a reconfigure and a full build, one after the other under `tools/gpu-lock.sh`,
with each binary's exit code captured in the same shell:

- `avgen_tests`: **exit 0**. 4,127 cases: 4,107 passed, 19 skipped, **1 failed as expected** (the one
  `[!shouldfail]`, `test_character_lab_slopes.cpp:187`). 10,321,496 assertions.
- `avgen_render_tests`: **exit 0**. 601 cases: 600 passed, 1 skipped. 633,786 assertions.

Not merged, not pushed.

---

## Phase 4: PHONOTAXIS (the flagship LIVE scene) -- dropped by the owner on 2026-10-05

*History only: the scene was dropped by the owner on 2026-10-05 and `examples/phonotaxis/` was deleted. ADR-1122,
ADR-1123, ADR-1124 and `tools/frame_coverage.py` stay.*

The art agent's run, 2026-10-05: brief Phase 4, from the handoff above.

- **Scene:** `examples/phonotaxis/` (generated by `build.py`).
- **Engine work it needed:** ADR-1122, ADR-1123 and ADR-1124.
- **Critic:** the new LIVE mode, branch `live-critic` of the Creative Critic.
- **Review material:** `~/Desktop/av-gen-review/36-flagship-live/`.

### Art research

- **What I studied:**
  - the spike's own Phase 3 stills and clips. These are the strongest evidence, because they were made
    in this engine's data model;
  - Notch concert practice, TouchDesigner GPU and feedback practice, and Unreal Niagara simulation
    stages;
  - Ryoji Ikeda and Robert Henke (restraint; darkness as material);
  - Max Cooper (systems that grow; slow structural reveals);
  - Tarik Barri's Versum (sounds as places; moving the camera is part of performing);
  - Robert Hodgin's Magnetosphere (each particle listens to its own frequency);
  - Sage Jenson's physarum work, through Bleuje's write-up (density-dependent behaviour; contrast curves
    on density);
  - the NIME/ICMC mapping literature (avoid one-to-one mappings);
  - Ableton's "A/V Interchange" interviews (Rick Feds: "play much less", or the picture turns to chaos).
- **The rules I took from it and kept throughout:**
  1. **Darkness is the canvas.** One region per frame is bright. The climax is earned by the dark
     before it.
  2. **Three scales, always.** A hero structure (tens of metres), the organism's roads (metres), and the
     forest and spores (decimetres). Never three things competing at one scale.
  3. **Memory beats amplitude.** The primary audio relationships are remembered ones (a band at a delay
     that grows with distance, onset fronts, trails), not the current RMS.
  4. **Sound is a place.** Frequency has a location and a colour, everywhere the same.
  5. **Few mappings, used compositionally.** One meaning per band, each with its own envelope; most
     parameters are never modulated.
  6. **A state change is a system growing or dying,** not a crossfade between two pictures.
  7. **The camera is a performer.** Its vantage belongs to the musical state.
  8. **No visualizer tells:** no bars, tunnels, rainbow cycling, constant pulsing or white flash on
     every kick.

### The concept

**PHONOTAXIS** is the movement of an organism toward sound. The scene is a night basin. A physarum-like
organism of 380,000 agents in three species crawls over it, and each species feeds on one band: lows,
mids or highs. It draws veins of light that spiral inward, by an inflow field, toward the **Cochlea**.

The Cochlea is a funnel 36 m tall made of threads of light. Each band is two counter-wound threads, so
the tower is a woven lattice. Every point on a thread hears its band at a delay set by its height
(3.2 m/s), so the last eleven seconds of the music climb the tower. A loud moment is a bright ring that
rises through the weave and fades.

A generated forest of 80 m radius grows only where the organism has walked. Each reed hears its own band
at its own distance-delay (5.5 m/s outward), and takes that band's colour. Kick fronts race out from the
Cochlea's foot across the veins and the forest, and on to a generated horizon of dark spires, which flare
in sequence. Warm spores rise from the veins and are drawn up the throat.

One colour rule holds everywhere: **colour is frequency**. Ember is the lows, teal the mids, violet the
highs; the reborn world transposes this into ember, gold and white. The image this aims at is a living
instrument: the floor writes the music down, the tower remembers it, and the forest repeats it.

### What the GPU systems do in it

| GPU system (ADR) | In PHONOTAXIS | Scale |
|---|---|---|
| Agents grid (1120), checkpointed (1119) | the organism: three species, each depositing by its own band, swirled by curl noise plus an inflow spiral; its trail colours the floor, sizes the forest and emits the spores | 380k agents at 30 Hz on 1024² over 180 m; 73 MB of state |
| Generator distribution (1117) | the forest (80 m disc, 0.52 m cells, a camera window of 62 m) and the horizon (unbounded, 9 m cells, a 420 m window) | 23k-62k generated cells per frame, no records stored |
| Spectrum field, element and range bands, delay by distance (1116) | the forest's echo (each reed its own band, delayed by its distance); the tower's three strands (each its band, delayed by height); the organism's diet (each species its band) | a 64-bin ring, 0.4 MB |
| Onset field (1116) | kick fronts across the floor, forest, horizon and tower, and into the organism's deposits | the last 8 kicks |
| Fields everywhere (025) | material programs read the trail grid per fragment (floor colour by species, white-hot cores); effectors read the trail to grow the forest; the spores are emitted where the trail is | 16 GPU fields, the engine's limit |
| Grid behaviour as parameters (1122, new) | MIDI knobs and routes change how the organism behaves (gaze, turn, hunger, fade) without replacing it; the scene states set its character | 6 behaviour leaves live |

None of these is a "faster particle". Four things are new compared with the regression scenes:

- the organism's trail is the map the forest grows on;
- the forest and the tower hear the music's past, not its present;
- a performer changes the organism's behaviour while it runs;
- every layer answers one shared colour rule.

### The performance vocabulary

**Audio (automatic, on any input):**

| Signal | Role | Shaping |
|---|---|---|
| LOW: the kick onsets (`onset` field, `low` source) | mass and fronts. Rings race outward over the floor (up to 5.5x lift), forest (they heave 0.6 m), horizon and tower, and into the organism's deposit; a heavy kick nudges the camera only when the energy is high | front width 6.5 m, decay 0.8 s, 22 m/s; shake by peakhold, depth = energy |
| MID: `audio.mid` | movement. The organism grows restless (turn angle +0.22) and wanders | attack 0.6-0.8 s, decay 2.5-3 s |
| HIGH: `audio.treble`, `audio.onsetHigh` | emission. Spores rise; the forest's tips glint | attack 80 ms, decay 0.7 s; peakhold glints |
| Per band, through fields | each species eats its band; each reed and each tower strand shows its band at its moment | the spectrum ring's own per-bin stretch |
| ENERGY: an interpreter of `audio.rms`, `spectralFlux` and `treble`, slow-followed into the `energy` macro | the arc. It moves the seven states and lifts the tower, forest, spores and bloom a little within a state | attack 2.5 s, decay 4 s; depth = SENSITIVITY |

**States (scene states, ADR-031).** Each is a preset of the world's configuration and its camera vantage:

| State | What it is | Vantage |
|---|---|---|
| Dormant | the tower a dark silhouette with embers; faint veins | low and close, looking up |
| Germination | the organism feeds; the first reeds | 42 m, off-tower pivot |
| Chorus | the full world | 64 m, a wide hero shot circling an off-tower pivot |
| Surge | bolder veins; the horizon flares on kicks | high and wide (98 m, 48 m up) |
| Eruption | spores stream up the throat; trails; the tower and forest at full | inside the forest, under the crown |
| Collapse | the organism starves and fades; the tower dims | near top-down: the eye |
| Rebirth | a new organism (long gaze, straight highways), the gilded palette | a long lens, grazing |

The music moves the states through energy thresholds with hysteresis (entering a state needs more than
leaving it). A performer moves them with pads. Transitions are preset morphs of 1.5-10 s, and since the
vantage is in the preset, every transition is also a camera move.

**MIDI (`phonotaxis-live.json`).** Every knob is neutral at its default, so an unplugged controller
changes nothing.

| Control | Name | Moves |
|---|---|---|
| CC 1 | ENERGY | pushes the arc (the `energy` macro's base) |
| CC 21 | HUNGER | the organism's deposit (more or fewer roads) |
| CC 22 | RESTLESS | its turn and gaze angles (calm lanes to frantic mesh) |
| CC 23 | CURRENT | the inflow (drained into the throat, or wandering) |
| CC 24 | MEMORY | temporal echo strength and decay (afterimages) |
| CC 25 | REACH | camera distance and height |
| CC 26 | ORBIT | camera orbit speed and direction |
| CC 27 | GLOW | bloom and halation |
| CC 28 | SENSITIVITY | how hard the music pushes the energy (depth 0-4; 0.25 = 1) |
| note 36 | STRIKE | one shock front (30 m/s, 9 m wide) across floor, forest and horizon, a burst of spores, a camera jolt |
| note 37 | SCATTER | a burst of spores |
| notes 40, 41, 43, 45, 47, 48, 50 | states | Dormant, Germination, Chorus, Surge, Eruption, Collapse, Rebirth |

**Camera.** The camera is an integrated orbit around a pivot (ADR-1123), and it moves in four ways:

- each state owns a distance, height, field of view and pivot;
- two slow, incommensurate LFO drifts dolly and crane it;
- the energy lifts it;
- a heavy kick at high energy nudges it, and the performer's REACH and ORBIT knobs move it too.

**Post.**

- Exposure, bloom and the temporal echo strength are per state.
- Bloom 0.4-0.7, halation 0.18, chroma retention 0.65, vignette 0.45, grain 0.015.
- A thin noise-free medium (density 0.008) scatters the heart's ember light only near the tower.
- The strategy is `effects_first`, so the thin threads keep their resolution when LIVE AUTO trims.

### Engine work the scene needed (the CRITICAL DEVELOPMENT RULE)

Three reusable changes. Each has an ADR and tests, and each is reachable from scene JSON, project
parameters, routes, MIDI, OSC and the editor. Everything specific to the scene stays in
`examples/phonotaxis/`.

- **ADR-1122: a grid's behaviour is parameters, and only its layout re-seeds it.**
  - The problem: any change to an agents grid's settings changed the one hash the simulation compares,
    so it replaced a running organism with noise; and the settings had no parameters at all.
  - The fix: `GridField::layoutHash()` now decides a re-seed, and behaviour registers as
    `grid/<name>/<leaf>`.
  - Tests: unit and GPU. A behaviour change mid-play moves no agent more than two steps allow
    (0 of 20,000), and 10,278 turn differently; a seed change re-seeds the grid.
- **ADR-1123: an orbit camera circles its pivot.**
  - The problem: the orbit always looked at the scene's bounds centre, so a state could not aim the live
    camera.
  - The fix: `camera/orbitPivot` and `camera/orbitPivotWeight`. Weight 0 is the old orbit, byte for
    byte, and saves stay unchanged.
- **ADR-1124: scene states read the bus after the sources.**
  - The problem: found on the live path, a MIDI pad bound as `noteEvent` could never change a state,
    although the docs said it could. The state machine ran before the sources published the pulse, and
    the bus cleared it at the end of the frame. Eleven pad hits reached the engine and moved nothing.
  - The test fails on the old order and passes on the new one.

Also built in the scene's own folder:

- `perform.py`: a scripted performer (audio into BlackHole, MIDI from a CoreMIDI virtual source, events
  logged on the host clock).
- `replay.py`: turns a recorded performance into a deterministic offline project (pads become timeline
  events, knobs become tracks).

### Found and recorded (not fixed here)

- **A bar- or beat-quantized state transition never fires on live input.** No reliable bar clock is
  running, and while the transition is pending every other trigger, pads included, is refused. The
  scene does not quantize. The Critic now flags it (`quantized_transition_live`).
- **A node name shared by a field node and a procedural node silently drops the procedural's
  parameters.** The tower was named like its fields, so the Rebirth palette never reached it. There
  is no warning.
- **A preset applied as the initial state lands before material-program parameters exist.** A pinned
  "Rebirth" opened with the night palette. Transitions are fine.
- **The live capture (`--live-capture`) re-renders the editor's viewpoint,** which is the film only when
  an output is open or the preview shows the output frame. Captures must pass
  `--preview-mode outputFrame`.
- **A vectorised grid gather was measured slower.** Reading three channels per corner and skipping the
  y lerp for a plane was 1.5 ms slower in three interleaved pairs (it is bit-identical), so it was
  reverted.
- **The effector warning "no field of that name reached the GPU field table"** fires once at start for
  a triggered field that has not fired yet. It is harmless noise.

### Art passes

| Pass | What changed | Weakest remaining after it |
|---|---|---|
| First build | organism, Cochlea, forest, horizon, the colour rule | the hero read as a Christmas tree, then a neon spring; marbled-oil floor |
| 1 | the tower became a woven lattice of threads lit only above each band's loud threshold; veins with white-hot cores; air that glows only near the heart; a vantage per state through ADR-1123 | live cost (Emergency); the floor dominated |
| 1b (budget) | one object per band (points), a 30 Hz organism, band-coloured reeds, effects first | LIVE AUTO at Low |
| 2 (from the first LIVE Critic report) | kicks you can see (the onset response had been 0.76% of luma); a 90 m basin whose edge recedes; Collapse keeps embers; the Rebirth palette fixed | the strike was invisible; spores read as snow |
| 3 | the strike is the biggest front; spores are warm sparks drawn up the throat; no state flapping after a pad | blocky thread ends up close; the camera inside the tower |
| 4 | round threads; Eruption's vantage outside the tower; the performance replay | one framing held for 112 s of Chorus |
| 5 | an arc for the whole song (Surge comes and goes; Collapse at the breakdowns), off-tower Chorus, slow camera drift; Night Shift trimmed and finding its own arc | see the weaknesses below |

<!-- PHONOTAXIS-RESULTS -->
