# Application performance

**This document is about everything except the 3D renderer.** The GPU's own frame — what the
shadow, volumetric, ecology and post passes cost — belongs to `docs/renderer-2-architecture.md`,
`docs/renderer-2-benchmark.md` and `docs/performance.md`, and is measured with `gpu::FrameTimeline`
(ADR-051, ADR-077). This document is the main thread: the UI, the event loop, the engine update,
the scene evaluation, the allocation behaviour, the threading, and the interfaces between those and
the renderer. Where the two meet — the swapchain wait, the canvas's size, the CPU cost of recording
commands — it is the *interface* that is discussed here, never the renderer's internals.

Keeping those separate is the point. A future agent asking "why is the editor slow" should be able
to tell in one line whether the answer is here or there.

---

## 1. The complaint, and what it turned out to be

The complaint was "the whole UI feels sluggish". That is not a number, and the first job was to make
it one.

It is worth stating the conclusion up front, because it is not the obvious one:

> **The editor's frame rate was never the problem. The editor's frame *latency* was.**

On the world scene the main thread did 1.6 ms of work per frame and then stood still for 15.6 ms
waiting for a swapchain image. That wait was in the wrong place: input was polled, the UI was built,
and only *then* did the CPU block. So the picture that reached the screen had been built from input
sampled a whole wait earlier, and every millisecond the GPU fell behind was also a millisecond of
staleness under the pointer. A frame-rate graph cannot see this at all — the frame rate tells you
how often the picture changes, not how old the picture is.

Measured input-to-present latency, world scene: **17.2 ms median. After reordering the loop: 1.4 ms.**

The three other findings, in order of how much they mattered:

| | finding | measured |
|---|---|---|
| 1 | The swapchain wait sat between input and the frame built from it | 17.2 → 1.4 ms latency |
| 2 | Dragging a structural procedural parameter regenerated geometry inside every frame | frames of 145–216 ms, 44% of frames over 50 ms |
| 3 | Opening a scene with an environment map flattened the whole world twice | 775 ms frozen in the first frame |
| 4 | The canvas is 2.6–3.3× the pixel count every benchmark quotes, with no control over it | 48 fps → 111 fps at scale 0.75 |

And the single most useful negative result: **an empty scene with an idle UI runs at 8.37 ms median
on a 120 Hz display, with the UI build costing 0.044 ms and allocating nothing.** The sluggishness
was never systemic. The UI itself is cheap. That ruled out a whole class of investigation on the
first measurement.

---

## 2. The instruments

Three were added, because the existing ones could not answer the question.

### `core::PhaseProfiler` (`src/core/phase_profiler.{hpp,cpp}`)

Per-frame totals for each named phase of the main thread's loop, in a ring of the last 2048 frames,
with order statistics computed on demand.

- **Order statistics, not averages.** `16,16,16,120,16,16,80,16` has a fine average and is a
  terrible experience. Median, P95, P99, max and a spike count are what pacing questions turn on.
- **On demand, never per frame.** Sorting twenty phases' samples every frame would make the
  instrument the thing it measures.
- **`min` matters too.** This machine usually has a build or two running. Contention is never
  negative, so the minimum over a long run is the honest estimate of the work and the median walks
  with whatever else is running. `docs/performance.md` reaches the same conclusion for the GPU.
- **Waits are named as waits.** `gpu.acquire WAIT` and `gpu.present WAIT` are labelled so, because a
  number that grows when the GPU falls behind must never be read as the CPU getting slower. **No
  number in this profiler is GPU time, and none is ever presented as such.** CPU wall-clock around
  an asynchronous GPU call is the cost of *issuing* it plus whatever it waited for.
- The report prints an `unaccounted` row: the median frame minus the median of every phase. A
  statistic with a hole in it that admits to the hole is usable; one that quietly fills it is not.

### Allocation counters (`src/core/alloc_counters.cpp`)

The global `operator new`/`delete` are interposed with **thread-local, non-atomic** counters.

Counting at call sites finds the allocations you already suspected. Counting here finds the ones
nobody wrote on purpose — a string a formatter built, a vector an `unordered_map` grew, a
`shared_ptr` control block. An increment is a load-add-store on a line no other thread touches,
which is cheap enough to leave compiled in on the audio callback, where the reading that matters is
binary: did this callback allocate at all.

### `app::UiScript` (`src/app/ui_script.{hpp,cpp}`) — `--ui-script <arms>`

The sluggishness only exists while somebody is touching the editor, and a headless run touches
nothing. So the interaction is scripted: each arm is one kind of interaction and nothing else, the
arms compose, and two runs of the same arm do the same thing.

| arm | what it does | how it enters |
|---|---|---|
| `hover` | sweeps the pointer over the whole window | real SDL events |
| `camera` | press, drag, release on the canvas | real SDL events |
| `sliders` | writes parameters continuously | the parameter, directly |
| `panels` | opens and closes each panel | the layout flag the View menu writes |
| `select` | moves the World panel's selection | panel state |
| `scrub` | moves the transport | the engine |
| `tabs` | switches the authoring layer | panel state |

Pointer arms push real SDL events, so they travel the whole path — SDL queue, the application's
handler, the ImGui backend, the viewport routing — and cost what the real path costs. Value arms
write the parameter directly, which is exactly what a widget's own `changed` branch does; driving
the widget through the pointer would additionally measure ImGui's hit-testing, which `hover` already
measures on its own, and would depend on where the panel happened to be docked. Each is honest about
which it is; neither claims to be the other.

The pointer arms produce their events **immediately before the poll that consumes them**, which is
what a device at 125–1000 Hz does. Pushing them at the top of the frame would date every synthetic
event by a whole swapchain wait and make the measurement blind to the thing being measured.

### Flags

```
--ui-script <arms>        hover,sliders,panels,select,scrub,camera,tabs -- or idle, or all
--profile-cpu             print the per-phase distribution on exit
--profile-csv <file>      one row per frame, every phase
--canvas-scale <f>        render the world at this fraction of the canvas's pixels (0.25-1.0)
AVGEN_SLIDER_FILTER=<p>   restrict the sliders arm to parameters under this path prefix
AVGEN_SLOW_PHASE_MS=<ms>  a phase slower than this logs what the script wrote on that frame
```

That last one is how "changing a property costs 200 ms" became the name of the property. Bisecting
by prefix was not enough — the cost turned out not to be attached to any single prefix but to a
*state* one of them had entered — and only naming the writes on the slow frame found it.

---

## 3. Method

Every number in this document was measured on an M2 Max, macOS, release build, with the method
stated beside it. The rules, learned the hard way:

1. **Repeat, and say so.** One 600-frame run reported `render.record` at 35.5 ms median and 506
   spikes. Five consecutive repeats of the identical command reported 0.18 ms median and 2–5 spikes.
   The first run was contention, and chasing it would have wasted the whole task. Nothing here rests
   on a single run.
2. **Best of N by median, or the minimum**, on a contended machine. Contention is never negative.
3. **Prove the edit applied.** The first version of the environment-map guard compared raw paths,
   which never matched, and the measurement was unchanged. That was visible only because the number
   did not move.
4. **Removal-basis A/B** where a thing can be switched off (`--disable`, `--canvas-scale`).
5. **Break the code and confirm the test fails**, before trusting a test that guards a defect class.
   Done for the environment-map guard: reverting it fails 2 of 6 assertions.

---

## 4. The frame, as it is now

Order matters here more than anything else in this document.

```
  poll the SDL queue (pass 1)      window events: resize, close, dropped files -> ImGui too
  handle resize / canvas sizing / ensure the final texture
  engine.tick
  step the background render job
  ---- acquire the swapchain image ------------------  THE WAIT (vsync pace, or the GPU's backlog)
  ui script (stands in for the device)
  poll the SDL queue (pass 2)      input, as fresh as it can be
  engine.update                    signals -> parameters -> modulation -> scene
  imgui newFrame + panel draw
  create encoder
  build debug geometry
  renderer.render(encoder, ...)    CPU command recording, not GPU time
  clear the window, record ImGui
  encoder.Finish + queue.Submit
  collectFrameTimings              non-blocking readback of last frame's GPU timestamps
  viewport pick                    only on a click; blocking, see §8
  present
  outputs / Syphon / NDI
  instance.ProcessEvents
```

**Why the wait comes first.** Under Fifo this wait is however long the CPU must stand still before a
swapchain image is free — the vsync pace when the frame is cheap, the GPU's backlog when it is not.
Standing in it *after* sampling input means the frame is built from stale input. Standing in it
*before* means the frame is built from whatever the device produced during it.

The queue is pumped twice because the two passes want different things: pass 1 takes the
window-level events that must be acted on before a surface image is asked for at all, pass 2 takes
input. Nothing is dropped — both go through the same handler and the same ImGui backend. A resize
arriving in pass 2 is carried to the next frame in `pendingResize_` rather than acted on, because
this frame's surface is already configured and its image already acquired; the `FrameEvents` that
used to hold that flag is a per-frame local, so without the member it would simply vanish.

---

## 5. Baseline measurements

All at `--size 1440x900` (a 2880×1800-pixel window at backing scale 2.0) unless stated. The canvas
size is logged by every run; it is **not** the window size and it is not what the benchmarks quote.

### The matrix

| arm | scene | interaction | FRAME median | acquire WAIT | CPU work | spikes >16.7 ms |
|---|---|---|---|---|---|---|
| A | empty | idle | 8.37 ms | 6.39 | ~2.0 ms | 1/400 |
| B | empty | all | 8.88 ms | 5.12 | — | 50/400 |
| C | world | idle | 17.34 ms | 15.65 | ~1.7 ms | 251/300 |
| D | world | sliders | 8.99 ms median / 89.9 mean | — | see §6 | 221/500 over 50 ms |
| E | world | audio playing, idle | 8.15 ms | 5.05 | — | 61/400 |
| F/G | covered by D (`sliders`, `scrub`) | | | | | |

**Arm A is the most valuable row in the table.** An empty scene with an idle UI runs at the
display's rate, the UI build costs 0.044 ms and allocates nothing. The problem was never systemic.

Arm C's frame is 90% swapchain wait: the GPU is the bottleneck on a real world, and that is the
renderer effort's subject, not this one. What *this* document is responsible for is that the UI was
made hostage to it (§4) and that the canvas is much larger than anyone was benchmarking (§7).

### Per-phase, world scene, idle

```
phase (ms)                  min   median     mean      p95      p99      max
engine.update             0.311    0.377    0.402    0.568    0.896    0.988
ui.build                  0.022    0.044    0.054    0.059    0.081    1.738
gpu.acquire WAIT          0.391   15.649   15.898   18.042   22.889  143.983
render.record             0.497    0.643    0.746    0.889    1.274   24.593
imgui.record              0.013    0.018    0.019    0.027    0.055    0.072
gpu.submit                0.689    1.938    1.740    2.408    2.529    2.795
gpu.present WAIT          0.006    0.021    0.019    0.031    0.036    0.065
FRAME                     5.869   17.343   17.779   19.713   25.316  145.837
```

### Allocations per frame, world scene, idle

| where | allocations/frame |
|---|---|
| `ui.build` | **0** (median) |
| `engine.update` | 482 |
| `render.record` | 938 |
| `imgui.record` | 26 |
| whole frame | 1685 |

The UI is clean. Of the engine's 482, **every one is inside `controller_->update()`** — the
composition — and control, signals, modulation and parameter propagation allocate **zero**. The
parameter and modulation core, which is the product's differentiator, is not a source of per-frame
garbage. At roughly 40 ns each, 482 allocations is about 0.02 ms, which is 5% of an 0.38 ms engine
update and 0.1% of the frame: **real, attributable, and not worth optimising yet.** It is recorded
here so nobody has to find it again. See P3-1.

---

## 6. What a single property change actually does

The chain is: widget → `IParameter::setBaseComponent` → (next frame) `params_.resetFinals()` →
timeline → modulation routes → `controller_->update()` → `Composition::applyParameters()` →
`rebuildProcedurals()` / `rebuildSdfs()` → renderer reads the flattened scene.

Most of that is cheap and correctly guarded:

- **A slider does not rebuild the scene.** `Composition::dirty_` — which triggers a full re-flatten —
  is set only by structural operations (add/remove node, reparent, graph edit, light rig,
  environment map). No parameter write sets it.
- **Procedural regeneration is hash-guarded per object.** `ProceduralGeometry::rebuild` short-circuits
  on an unchanged `contextualHash`, and the hash is per object, so changing one object's parameter
  does not regenerate its siblings.
- Dragging a *material* parameter on a procedural object on the world scene costs **0.374 ms** in
  `engine.update`. That is fine.

What was not fine: **dragging a parameter that changes the geometry's structure regenerates it,
inside the frame, on every frame of the drag.** Driving
`procedural/elder-stem/hierarchy/depth` to 4 put every subsequent frame at 145–216 ms — 5–7 fps —
for as long as the drag continued. This is the class the brief names: particle count, subdivision
level, instance count, hierarchy depth.

The fix is in §9. The principle: the work is legitimate, the *timing* was not.

### The audio-driven variant, and why it did not fire

`Composition::addDefaultRoutes` wires `audio.bass → root/scale` and `audio.mid → root/rotationSpeed`.
When those are non-zero the composition's root transform changes every frame, which folds into every
procedural's `distributionTransform`, which changes every `contextualHash`, which would regenerate
every procedural cloud every frame. On the world scene it does not happen — `engine.update` moves
from 0.373 ms to 0.640 ms with audio playing, nowhere near a full regeneration — but the mechanism
is real and a scene that did trip it would be catastrophic. **Unresolved; see P1-1.**

---

## 7. The canvas is not the window, and it is not 1440×900

The editor renders the world into the dock tree's centre region, at the display's backing scale.
Nothing in the interface said how big that was.

| window (points) | canvas (pixels) | Mpx | vs. a "1440×900" benchmark |
|---|---|---|---|
| 1440×900 | 2880×1166 | 3.36 | **2.6×** |
| 1920×1200 | 2466×1766 | 4.36 | **3.4×** |

So a renderer benchmark reporting 25.7 ms at 1440×900 corresponds to something like 65–85 ms in the
editor as actually used, and the editor defaults to opening maximised. **This is the most important
thing in this document for the renderer effort to know**, and it is an interface fact, not a renderer
one: it is the editor that chooses how many pixels to ask for.

Every run now logs it once:

```
canvas: world rendered at 2466x1766 px (4.36 Mpx) inside a 3840x2400 px window (scale 2.00)
```

---

## 8. Threading model

Nine thread classes; **all GPU work is on the main thread**. Synchronisation is `std::mutex`,
`std::condition_variable` and atomics; there is no `std::shared_mutex`, no `std::future`, and no
`std::async` anywhere in `src/`.

| thread | purpose | produces | consumed by | synchronisation | frequency |
|---|---|---|---|---|---|
| main | events, UI, engine update, **all** WebGPU submission and readback, present | the frame | — | — | vsync |
| audio playback callback | mixes, downmixes to mono | `AnalysisStream` | analysis | lock-free SPSC ring + atomics | device buffer, 2.7–10 ms |
| audio capture callback | gained, peak-tracked mono | `AnalysisStream` | analysis | same | same |
| analysis (`std::jthread`) | FFT, bands, onset, tempo | `AnalysisFrame` | main, via `TripleBuffer` | one atomic | hop rate, 10–23 ms |
| job workers ×2 | world composition | `GeneratedWorld` | main | mutex + condvar | on demand |
| render encoders ×N | PNG/EXR/video writing | files | — | mutex + two condvars | per rendered frame |
| CoreMIDI receive | parsed MIDI | `MidiInbox` | main | mutex | per packet |
| CoreMIDI run loop | hot-plug notifications | listener callbacks | — | mutex | rare |
| OSC receive | decoded messages | inbox | main | mutex + `poll()` | per datagram |
| terrain pool (transient) | chunk height fields | chunk meshes | its caller | none — partitioned, then joined | per terrain build |

**Both real-time audio callbacks honour their contract**: no allocation, no locks, no logging, no
exceptions. The producer/consumer boundary to analysis is a lock-free SPSC ring; the analysis→main
boundary is a `TripleBuffer` with a single atomic. Nothing in the UI can make the audio thread
block, and **no UI or editor operation performs audio analysis synchronously** — the one place that
could, `AnalysisRunner::stop()`'s join, is on file open and device switch, not on any per-frame path.

### Where the main thread blocks on another thread

| what | where | bounded? | when |
|---|---|---|---|
| swapchain acquire | `Application::runLive` | vsync | every frame — now before input, §4 |
| terrain pool join | `world::buildTerrain` | by the work | inside a scene rebuild |
| viewport pick | `Application::serviceViewportPick` | 10 s timeout | **per click: up to 5 serial GPU round-trips** |
| render-job encoder backpressure | `RenderJob::handleFrame` | **no timeout** | in-app render with a stalled encoder |
| readback ring full | `ReadbackRing::acquireSlot` | 10 s | in-app render |
| CoreMIDI client construction | `ClientHost::ClientHost` | **no timeout** | first MIDI open |
| Syphon announcement | `SyphonShare::ensureSurface` | 2 s | every canvas resize while sharing |
| analysis thread join | `AnalysisRunner::stop` | one hop + 1 ms | opening audio, switching device |

The two unbounded waits (`RenderJob::spaceCv_`, CoreMIDI's `readyCv_`) are on the UI thread and are
P2 items — neither fires in normal editing, and neither was reachable by any measurement in this
pass, which is why they are recorded rather than changed.

In live mode `RenderJob` runs its **GPU work on the main thread** (`step(4, 0.010)` per UI frame);
only the file encoders are threaded. So every readback-ring stall during an in-app render is a UI
stall. Recorded, not measured — nobody rendered during this pass.

---

## 9. What was changed

### 9.1 The swapchain wait moved ahead of input sampling

See §4. Latency 17.2 → 1.4 ms median (world), 8.3 → 0.38 ms (empty). Frame time unchanged.

### 9.2 Expensive procedural regeneration waits for the drag to stop

`Composition::setInteractiveRebuildBudget(ms)`; the policy is a pure function,
`advanceRebuildDeferral`, so it can be reasoned about and tested as arithmetic.

An object whose last regeneration cost more than the budget (2 ms, set only by the live editor)
keeps **two** timers:

- **settled**: since its inputs last moved. A drag restarts it every frame, which is what keeps the
  drag out of the regeneration. Fires at 90 ms — about five frames at 60 Hz, short enough that
  letting go of a slider feels immediate.
- **held**: since it first wanted to regenerate and was not let. Only resets on an actual
  regeneration, so a drag that goes on for seconds still gets a refresh. Fires at
  `max(90 ms, 4 × last cost)` — nothing spends more than about a fifth of its time regenerating, and
  a cheap object is not starved to protect an expensive one.

One timer cannot express this: the first thing a moving target does is reset it, which is how the
first version of this never refreshed during a drag at all.

**The budget is zero by default**, meaning regenerate whenever the inputs change. Only
`Engine::installController` sets it, and only in `EngineMode::Live`. The deferral reads a wall clock,
and a wall clock has no business deciding what a deterministic render contains — see §10.

### 9.3 Opening a scene no longer flattens the world twice

`Composition::attach()` flattens once (it fits the camera to the resulting bounds).
`Engine::loadComposition` then called `setEnvironmentMap()` with the path the scene had just been
loaded with, and that set `dirty_` unconditionally. `dirty_` is not a request to re-read the image —
the registry caches images by path — it is a request to rebuild **everything**: 256 terrain chunks,
~260k scatter cells, every procedural cloud, a full vertex scan for bounds.

The guard compares **resolved** paths, because the two sides genuinely differ in form: the scene file
stores a relative path and the engine hands back the absolute one it resolved in order to load the
image.

### 9.4 A canvas render scale

See §7. Default 1.0, so nothing changes unless asked.

---

## 10. Determinism

Nothing in this pass may change what an offline render contains, and nothing does.

- The deferral in §9.2 is the only change that reads a wall clock. It is gated on a budget that is
  **zero unless the live editor sets it**, and `Engine::installController` sets it only in
  `EngineMode::Live`. Offline takes the same unconditional loop it always did.
- The loop reorder in §9.1 is live-mode only; `runHeadless` is untouched.
- The environment guard in §9.3 removes a *redundant* rebuild; the rebuild it skips would have
  produced the identical scene from the identical cached image.
- The canvas scale in §9.4 affects only the editor's canvas target. Offline rendering sizes its own
  targets from `RenderSettings`.

**Verified by capture, not by argument.** `--headless --capture` of frame 4/6 is byte-identical
before and after, for both `examples/world/terrain.scene.json` (3,888,016 bytes) and
`examples/world/glowmere-stylized.scene.json`.

The snapshot boundary is unchanged and remains: mutable editor state → `params_.resetFinals()` +
timeline + modulation → `controller_->update()` → flattened `scene::Scene` → renderer. The renderer
reads; it does not write back.

---

## 11. Budgets

The display on this machine is 120 Hz, so the frame budget is **8.33 ms**, not 16.67. The UI is not
entitled to all of it; the application needs headroom for the world.

| | budget | measured now (world scene, idle) |
|---|---|---|
| UI construction (`ui.build`) | **≤ 1.0 ms** | 0.044 ms |
| Engine update (signals, modulation, scene) | **≤ 2.0 ms** | 0.377 ms |
| CPU command recording | **≤ 1.5 ms** | 0.643 ms |
| ImGui recording + submit | **≤ 2.0 ms** | 1.96 ms |
| **total main-thread work** | **≤ 5.0 ms** | ~3.0 ms |
| audio callback | must not allocate, lock or log | holds |
| input → present | **≤ 4 ms** | 1.40 ms |
| frame pacing | P95 ≤ 1.5 × median; no frame > 50 ms during interaction | see §12 |

Main-thread work has ~3 ms of the 8.33 and the budget gives it 5, leaving 3.3 ms of headroom. The
rest of the frame is the GPU's, and whether it fits is the renderer effort's question — but §7 says
what size it is actually being asked to fill.

---

## 12. Frame pacing

Pacing, not FPS. From `--profile-csv`, world scene, 500 frames, `--ui-script sliders`:

| | before | after |
|---|---|---|
| FRAME median | 8.99 ms | 8.38 ms |
| FRAME mean | 89.9 ms | 13.7 ms |
| FRAME P95 | 217.2 ms | 48.2 ms |
| frames > 16.7 ms | 222/500 | 48/500 |
| frames > 50 ms | **221/500 (44%)** | **24/500 (4.8%)** |

The mean being ten times the median is the signature of exactly the problem the brief describes: an
acceptable typical frame with a catastrophic tail. The remaining tail is the deliberate refresh
ceiling of §9.2, and the scripted drag is a worst case — it cycles every parameter, so many objects
defer at once and all reach their ceilings together. A person dragging one slider defers one object.

---

## 13. Logging and diagnostics

No change was needed, and the audit is recorded so it is not repeated.

- The per-frame log is `log::debug` behind `framesRendered % 120`, i.e. off at the default level and
  once every two seconds when on.
- The input self-test is behind `AVGEN_UI_SELFTEST` and throttled to every 30th frame.
- spdlog's sink is `_mt` (takes a lock) and is **never called from the audio callbacks**, which is
  stated in their headers and holds.
- The Analysis panel's ImPlot graphs are the most expensive diagnostic drawn, and the UI build still
  allocates zero at idle and costs 0.044 ms — they are not a bottleneck and have not been touched.
  **No diagnostic was removed or disabled to improve a number.**

One piece of dead diagnostic cost was found and is recorded as P3-2: `AnalysisRunner` maintains a
512-frame history ring under `historyMutex_` on the analysis thread, and `history()` has **no caller
anywhere in `src/`**.

---

## 14. Remaining bottlenecks

1. **The GPU, on a real world.** 15.6 ms of a 17.3 ms frame at 3.36 Mpx. Renderer 2.0's subject;
   §7 is this document's contribution to it.
2. **`Composition::rebuild()` is all-or-nothing.** Ten call sites set `dirty_`, and every one
   re-flattens the entire world: adding one material program, adding one hero node, or swapping the
   HDR re-scatters ~260k ecology cells and rebuilds 256 terrain chunks. The redundant *second* one
   is gone (§9.3); the ~390 ms first one is real work done synchronously on the main thread, and is
   the largest remaining editor freeze. P1-2.
3. **`world::scatter` is single-threaded** — ~260k grid cells, ~1.3M `WorldMap::height` evaluations,
   one layer at a time, while `buildTerrain` beside it is already threaded. P1-3.
4. **Viewport pick: up to 5 serial blocking GPU round-trips per click.** P2-1.
5. **The audio-driven root transform** could regenerate every procedural cloud per frame in a scene
   whose root scale or rotation is audio-routed. P1-1.
6. **482 allocations per frame** in the composition update. P3-1.

---

## 15. Future work

- Give `Composition` incremental invalidation: a material program or a light rig should not rebuild
  terrain. The information is there — `dirty_` just has no granularity.
- Move `world::scatter` onto the job system, or at least thread it the way `buildTerrain` is.
- Make the terrain build asynchronous and show the previous mesh until it lands, so opening a world
  does not freeze the editor at all.
- Coalesce the viewport pick's five readbacks into one submission.
- Give the deferral of §9.2 a visible indicator; `proceduralsAwaitingRebuild()` exists for it.
  Geometry that is deliberately a few frames behind the slider should say so.
