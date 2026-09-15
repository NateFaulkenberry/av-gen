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
**Off by default; `-DAVGEN_ALLOC_COUNTERS=ON` to enable**, and every allocation figure in this
document was taken with it on.

Counting at call sites finds the allocations you already suspected. Counting here finds the ones
nobody wrote on purpose — a string a formatter built, a vector an `unordered_map` grew, a
`shared_ptr` control block. An increment is a load-add-store on a line no other thread touches.

It is off by default for a reason worth stating precisely, because the first version of this
sentence was wrong. An early A/B said the interposition cost about 0.4 ms of main-thread work per
frame. That compared two runs taken minutes apart on a machine running builds. Repeated back to
back, three runs each, the **counters-on build came out faster** than the counters-off one
(`ui.build` min 0.086 vs 0.114 ms) — which is causally impossible, and is therefore the result: on
this machine the cost does not rise above run-to-run variance. The honest statement is that it was
not resolvable, not that it is 0.4 ms.

It is off anyway for a reason needing no measurement: it replaces the global operators for the
entire program, Dawn and SDL and ImGui included, and routes aligned allocations through
`posix_memalign` rather than libc++'s own path. That is a real change to a shipping binary in
exchange for a diagnostic, and a diagnostic should be asked for. With it off, `allocCounters()`
still exists and reads zero.

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
--ui-script <arms>        hover,sliders,panels,select,scrub,camera,tabs,edit,strip,gizmo,box
                          -- or idle, or all
--ui-ab <a>:<b>:...       interleave those arms as blocks of ONE process and report them side
                          by side (ADR-233). An arm may carry '+legacy' or '+skynow', each
                          restoring one pre-ADR-233 behaviour for the length of its block
--ui-ab-frames <n>        frames per block (default 120; keep it a multiple of 60)
--ui-ab-blocks <n>        passes over the arm list (default 4)
--ui-ab-settle <n>        frames discarded after each switch (default 12)
--profile-cpu             print the per-phase distribution on exit
--profile-csv <file>      one row per frame, every phase; the group is the first column
--canvas-scale <f>        render the world at this fraction of the canvas's pixels (0.25-1.0)
AVGEN_SLIDER_FILTER=<p>   restrict the sliders arm to parameters under this path prefix --
                          the *cycle*, not only the writes, so one slider means every frame
AVGEN_SLOW_PHASE_MS=<ms>  a phase slower than this logs what the script wrote on that frame
AVGEN_LEGACY_PROCGEN=1    restore the pre-ADR-233 double generation, in any mode, so a
                          headless capture can be taken both ways out of one binary
```

### `--ui-ab`, and why a shell loop over `--ui-script` is not the same thing (ADR-233)

Rule 1 of §3 forbids comparing a frame time from one run against another, and on this machine that
is a practical constraint rather than a pedantic one: the load average during this pass ranged from
6 to 44. `--ui-ab` cycles the arms as blocks inside one session and reports each one's distribution
in one table, so the columns were measured seconds apart under the same contention.

Three details are load-bearing:

- **The settling frames belong to no arm.** The first `--ui-ab-settle` frames of a block are
  labelled `kNoGroup` and excluded. An arm inherits its predecessor's deferral timers and warm
  pipelines; twelve foreign frames in a block of 120 is a 10% error in one direction.
- **A block must be a multiple of sixty.** The pointer arms repeat a gesture on a 60-frame cycle.
  At 90 the gizmo arm went three blocks without completing a press -- and its own probe is what
  said so.
- **An empty group reports no samples rather than zero milliseconds.** A column of zeros meaning
  "this arm never ran" and one meaning "this arm was free" must not look the same.

### The structural counters

`# procedural regen`, `# scene flattens` and `# IBL builds` sit in the same table as the timings,
per frame. §3 rule 2 explains why they matter more than the milliseconds beside them: "none where
there were twenty-two" survives a load average of forty-four, and ADR-170 is the case where exactly
that distinction stopped a phantom regression being filed.

The procedural counter lives **inside `ProceduralGeometry::rebuild`**, not at a call site. That is
why it found anything: there were two call sites per object per frame (§19), and a counter at either
one would have reported half the truth.

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
| FRAME median | 8.99 ms | 8.37 ms |
| FRAME mean | 89.9 ms | 13.7 ms |
| FRAME P95 | 217.2 ms | 51.2 ms |
| `engine.update` mean | 82.3 ms | 6.4 ms |
| `engine.update` P95 | 215.2 ms | 44.6 ms |
| frames > 50 ms | **221/500 (44%)** | **27/500 (5.4%)** |

(Three runs each on the shipping configuration; the spread across the three was 26–29 frames over
50 ms and 44.6–44.9 ms at P95.)

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
2. **`Composition::rebuild()` is all-or-nothing** — but it no longer re-scatters and re-meshes the
   terrain. Ten call sites still set `dirty_` and every one still re-flattens, but a terrain's
   scatter clouds, glow clusters and chunk meshes are now memoised on the node and reused whenever
   the rebuild was caused by something else (ADR-092). Measured on
   `examples/recipes/glowmere.recipe.json` after ADR-090, placing an asset costs **21.7-33.3 ms**
   where it cost **455-478 ms**, and `engine.update`'s p99 across a scripted paint stroke falls from
   **469.6 ms to 26.3 ms**. `AVGEN_NO_TERRAIN_CACHE=1` restores the old behaviour, so the comparison
   stays measurable rather than historical. The cold flatten is unchanged (~620 ms): the first one
   has nothing to reuse. What remains in the ~25 ms is procedural regeneration and entity
   rebuilding, and `dirty_` still has no granularity. P1-2, reduced.

   The gap grew fourfold when ADR-090 landed, which is the point of measuring it this way: real
   rivers and lakes made a world much more expensive to *build* and left it exactly as cheap to
   *edit*. The LOD debug sliders do not rebuild either -- `TerrainSettings::structuralHash` excludes
   `lodDistance` and `viewDistance` because they choose meshes per frame and change nothing that was
   built.
3. ~~**`world::scatter` is single-threaded**~~ — **done, ADR-231.** It was worse than this entry
   suggested: `sample(1)` during start-up put **771 of 1648 main-thread samples inside it**, i.e.
   47% of a 2.14-second project load. It is now threaded in bands of rows, mirroring
   `buildTerrain`'s pool. Interleaved in one session: project load **2147 → 1442 ms**, the
   scene-build stage **1964 → 1259 ms**, and `--headless --capture` byte-identical either way
   (sha256 `3f4dbe24…aefaaa`). `AVGEN_SCATTER_WORKERS=<n>` forces the count so the comparison stays
   checkable. Scaling on this machine, scene-build stage: 1967 / 1532 / 1312 / 1234 / 1202 ms at
   1 / 2 / 4 / 8 / 16 workers — it flattens because the ~800 ms of the scene build that is not
   scatter is still serial.
4. ~~**Viewport pick: up to 5 serial blocking GPU round-trips per click.**~~ -- **done, ADR-231**,
   and this entry was wrong about the number. `pickNormalAt`, which accounted for three of the five,
   has **no caller anywhere in `src/`**; the live click path did **two**, one for the depth and one
   for the identifier. `gpu::readTexelsR32` now batches any number of texels across any number of
   textures into one submission and one map, so a click costs **one** round trip and `pickNormalAt`,
   if it is ever wired up, costs one rather than three. Still on the click, which is the right place
   for it. The editor's *hover* no longer goes near it: the live
   ghost marches a ray against `WorldMap::sample` on the CPU instead (ADR-092), because a preview
   that follows the cursor cannot block on the GPU sixty times a second.
5. ~~**The audio-driven root transform** could regenerate every procedural cloud per frame in a scene
   whose root scale or rotation is audio-routed.~~ -- **wider than this, and fixed, ADR-233.** It
   needed no audio at all: any node not at the world origin was enough, because the node transform
   was folded into `distributionTransform` *after* the parameter pass had already generated against
   the unfolded one and stored that hash. The two hashes ping-ponged, so every procedural object in
   every scene regenerated twice per frame for ever -- measured at **22 per idle frame** on an
   eleven-node scene. `applyProceduralParameterValues` applies without generating;
   `rebuildProcedurals()` generates once, with the real context, under the budget.
   `AVGEN_LEGACY_PROCGEN=1` restores the old path so the comparison stays runnable. P1-1, closed.
6. **482 allocations per frame** in the composition update. P3-1. §18 names where most of them are:
   the per-slot `std::string` concatenation inside `applyProceduralParameters`.
7. **The sky's IBL was rebuilt inside the frame of a lighting drag** -- **fixed, ADR-233.** It took
   `render.record` to a 26.5 ms mean and 77 ms p95 during a sky drag. It now takes ADR-084's
   deferral, through the same pure function, moved to `scene/rebuild_deferral.hpp`. §18 has the
   numbers.
8. **~90 linear parameter lookups per procedural node per frame** in `applyProceduralParameters`
   (§18, row 7). Now the largest identified item in `engine.update`. Not fixed.

---

## 15. Future work

- Give `Composition` incremental invalidation: a material program or a light rig should not rebuild
  terrain. The information is there — `dirty_` just has no granularity. ADR-092 took the terrain out
  of the rebuild by memoising it, which is the same saving arrived at from the other end; the
  granularity itself is still missing.
- Move `world::scatter` onto the job system, or at least thread it the way `buildTerrain` is.
- Make the terrain build asynchronous and show the previous mesh until it lands, so opening a world
  does not freeze the editor at all.
- Coalesce the viewport pick's five readbacks into one submission.
- ~~Give the deferral of §9.2 a visible indicator~~ — **done, ADR-231.** The canvas shows it, with
  job progress and song analysis, after a 180 ms threshold and a 140 ms fade. The threshold is the
  point: the deferral is 90 ms by design, so an indicator without one would blink through every
  frame of every drag.

---

## 16. What opening a project costs (ADR-231)

"Opening a project freezes the application" was not a number, and `Engine::loadProject` now makes it
one. It names nine stages as it enters them and keeps each one's wall clock in `lastLoadTimings()`.
The list is fixed before the first byte is read, so a stage index is a *countable fact* rather than
an estimate of time remaining — ADR-064's rule, obeyed here for the same reason.

`examples/world/glowmere-stylized.json`, five consecutive runs, spread 2%:

```
project load: 2147 ms total -- Reading the project 1 ms; Loading audio 164 ms;
                               Building the scene 1964 ms; Layers and parameters 15 ms
```

**92% of a project load is one stage**, and §14.3 is what was inside it. After the scatter was
threaded the same measurement reads **1442 ms total, 1259 ms of scene build**.

### The load is not on a worker, and that is a decision

The composition is not thread-safe, the environment map's prefilter is GPU work and all GPU work in
this application is the main thread's (§8), and the parameter set is torn down and rebuilt underneath
everything that reads it. ADR-084 §2 rejected threading a far smaller piece of this for the same
reasons.

What the editor does instead is defer the open by one frame and paint "Opening &lt;name&gt;" over the
scene that is still there, so the window holds *that* picture for the length of the load rather than
a stale editor that looks like it ignored the click. **The freeze is the same length.** The third
that came off it came from §14.3, not from the indicator — saying otherwise would be the thing the
brief's section 19 forbids.

The stage reporter fires during a load that nothing repaints, so it cannot animate. It earns its
place for two other things: a load that *fails* now says which stage it reached, and the numbers
above exist at all.

---

## 17. The editor's own frame, re-measured after ADR-231

Whether a palette, cursors, lane headers, a two-tick ruler and context menus made the UI build more
expensive. On this document's own reference point — §5's arm A, empty scene, idle, 1440×900 — they
did not:

| | §5 baseline | after ADR-231 |
|---|---|---|
| `ui.build` min, empty + idle | 0.044 ms | **0.047 ms** (median 0.063, three runs) |
| FRAME median, empty + idle | 8.37 ms | 8.40–8.44 ms |

On a loaded world scene `ui.build` is 0.137–0.146 ms median. That is the World panel's parameter tree
and the sequencer strip, not a regression: the budget in §11 is 1.0 ms, and it is 1.6% of a 120 Hz
frame.

### The caveat this document's own rules require

Every number in §16 and §17 was taken on a machine at load average 8–34, with an interactive `avgen`
session belonging to somebody else open for part of it. Rule 2 of §3 is what was leaned on —
minimum, or best of N by median, because contention is never negative — and the load-stage figures
are trustworthy chiefly because five consecutive runs agreed within 2%.

**No before/after frame-time comparison across two process runs is offered here**, because §3 rule 1
forbids it and building two binaries does not turn two processes into one session. The claims that
carry weight are the structural ones, which contention cannot move: one GPU round trip where there
were two, a byte-identical capture beside a third less wall clock, and 134 of 135 points of the
sequencer strip visible where there were 82 of 195.

### A new arm: `--ui-script strip`

Scenario C of the brief — sequencer drag, scrub and resize — had no instrument, so it has one. It
opens the Sequence panel, **brings it to the front**, presses on the ruler and scrubs, then grabs a
shot and drags it, all through real SDL events.

It failed on its first four runs and every failure was a finding, which is what §3 rule 5 is about:

1. It reported a scrub to `0.00 s`. The bottom dock holds five panels as *tabs*, and opening one does
   not make it active — a background tab's `Begin` returns false and its body never runs, so the
   panel's remembered rectangle was from a single transitional frame during the layout rebuild.
2. With the panel focused it reported `hovered=true` and a shot still at `0.00 s`, and said why:
   **190 points of toolbar above a 272-point panel, 82 of a 195-point strip visible.** The shots lane
   was below the fold at the size the editor opens at.
3. After moving snap/zoom/status below the strip and letting the lanes compress to fit: **138 points
   of toolbar, 134 of 135 visible**, the scrub lands on a beat at 72.50 s, and the shot drag lands at
   35.76 s where it had reported 0.00.

A UI cannot be certified by reading its source, and this is the shape of the alternative.

---

## 18. Interaction, measured (ADR-233)

ADR-231 measured **loading**. The complaint that followed it is about **interaction**. This section
is that measurement and what it found; the decisions are in ADR-233.

Every number below was taken with `--ui-ab`, interleaved inside one process, on
`examples/world/glowmere-stylized.json` at `--size 1440x900`, three blocks of 120 frames per arm. No
figure here compares two process runs.

### The matrix

Nine arms, one session. Medians, in milliseconds.

| | idle | sliders | scrub | box | gizmo | camera | hover | panels | strip |
|---|---|---|---|---|---|---|---|---|---|
| `engine.update` | 0.454 | 0.434 | 0.426 | 0.440 | 0.438 | 0.453 | 0.452 | 0.450 | 0.459 |
| `ui.build` | 0.275 | 0.174 | 0.175 | 0.249 | 0.181 | 0.197 | 0.203 | 0.241 | 0.261 |
| `render.record` | 0.705 | 0.660 | 0.753 | 0.675 | 0.669 | 0.704 | 0.698 | 0.699 | 0.716 |
| `gpu.acquire WAIT` | 5.72 | 6.76 | 5.81 | 5.73 | 6.76 | 5.75 | 5.76 | 5.75 | 5.73 |
| `input->present` | — | — | — | 1.98 | — | 2.08 | 2.08 | — | — |
| `# procedural regen` | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| `# scene flattens` | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| `# IBL builds` | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| FRAME median | 7.98 | 8.42 | 8.27 | 8.12 | 8.48 | 7.95 | 7.97 | 7.96 | 7.97 |
| FRAME max | 25.9 | 9.3 | 9.4 | 25.1 | 9.3 | 9.2 | 9.8 | 9.2 | 26.4 |

**The most valuable rows are the three counters and `ui.build`**, and both are negative results.

- **A slider drag causes no flatten.** Not one, in any arm, in any run. The derived-copy rule holds
  and was not touched. For scale, one cold flatten of this project costs **942 ms**.
- **The UI is not the problem.** `ui.build` never crossed **0.28 ms** across nine arms against a
  budget of 1.0 and a frame of 8.33. That is the whole Dear ImGui pass: every panel, the viewport
  editor, the gizmo overlay, the sequencer strip. Per-panel attribution was not pursued because the
  total forecloses it. **Opening and closing panels** is the one UI interaction that costs anything
  visible, and it is a one-off spike (the `panels` arm's 26 ms maximum: a dock rebuild), not a
  per-frame cost.
- **The frame is the GPU's.** 5.7 ms of an 8.0 ms frame is the swapchain wait. That is
  `docs/renderer-2-architecture.md`'s subject; §7 remains this document's contribution to it.

### Input-to-visible-change, for a control that triggers world work

`input->present` measures ~2.0 ms for the pointer arms and cannot measure the value arms at all —
they write the parameter directly, as a widget's own `changed` branch does, and produce no input
event to be old. The honest figure for "I moved a slider, when do I see it" is therefore two numbers
and not one:

- the frame that carries the write: **one frame**, and its cost is the arm's FRAME median;
- geometry or sky whose regeneration exceeds the 2 ms interactive budget: **90 ms after the drag
  settles**, by design (ADR-084 §9.2), with a mid-drag ceiling of `max(90 ms, 4 x last cost)`. The
  canvas says so while it is waiting.

### Two things were rebuilt every frame that nobody had changed

Both found by the counters rather than by the clock. Full account in ADR-233.

**Twenty-two procedural regenerations per idle frame** — exactly twice the scene's eleven procedural
nodes. `applyProceduralParameters` ended with a `rebuild()` against an empty `GenerationContext` and
stored that hash; `rebuildProcedurals()` then rebuilt against the real one and stored its own; next
frame the first disagreed right back. Interleaved, `idle+legacy` against `idle`:

| | before | after |
|---|---|---|
| `# procedural regen` per frame | 22 | **0** |
| `engine.update` median | 0.740 ms | **0.430 ms** |

The frame median did not move, because on this scene the frame is the GPU's. 0.31 ms of main-thread
work returned is headroom, not frames, and saying otherwise -- implying a fix bought frames it did
not buy -- is the one thing ADR-231 §6 is emphatic about not doing.

**The procedural sky's IBL rebuilt inside the frame, on 46% of the frames of a lighting drag.**
`EnvironmentProcessor::processSky`'s own header says it is load-time work; a drag on any of the ten
sky parameters, or on the key light its sun is resolved from, changed the hash every frame.
`AVGEN_SLIDER_FILTER=env/sky`, `--ui-ab idle:sliders+skynow:sliders`:

| | idle | before | after |
|---|---|---|---|
| `# IBL builds` per frame, mean | 0 | 0.463 | **0.065** |
| `render.record` mean | 0.98 ms | 26.54 ms | **3.76 ms** |
| `render.record` p95 | 1.03 ms | 77.24 ms | **36.92 ms** |
| FRAME median | 33.12 ms | 40.08 ms | **33.08 ms** |
| FRAME p95 | 50.40 ms | 85.11 ms | **60.18 ms** |
| frames over 50 ms, of 324 | 34 | 147 | **56** |

A sky drag now costs the same median frame as an idle editor. With `AVGEN_SLIDER_FILTER=lightrig/`:
IBL builds per frame 0.204 → **0.080**, `render.record` mean 11.87 → **4.83 ms**, frames over 50 ms
92 → **66**.

### The per-frame UI audit, recorded and deliberately not acted on

Found while looking for the cause, and kept because §5's negative result means nobody should repeat
it. None of this was changed: `ui.build` is 0.28 ms and the budget is 1.0.

| | where | what |
|---|---|---|
| 1 | `src/ui/world_panel.cpp:32-128`, `:257-311` | `influencesOf` rescans every modulation route, timeline track, cue (a `presets().find()` by string each), scene state, entity and world macro, **per parameter row**, allocating a vector and several strings each time |
| 2 | `src/ui/control_panel.cpp:1247-1261` | `drawParameters` rebuilds a `vector<string>` order and an `unordered_map<string, vector<IParameter*>>` grouping over every exposed parameter, every frame |
| 3 | `src/ui/world_edit_panel.cpp:523-538` | the map built to avoid "a hundred and sixty thousand string comparisons a frame" calls the linear `findNode` inside its own loop, so it is the O(n²) scan it set out to replace |
| 4 | `src/ui/world_editor.cpp:43`, `:98`, `:156` | `visuals_ = EditorVisuals{}` discards every vector instead of clearing it; `nodeBounds` per selected node reaches `ensureBuilt()`, a UI read that *can* trigger a flatten; two parent-chain walks per node, each via `findNode` |
| 5 | `src/ui/world_editor.cpp:198-388` | `updateNavigation` walks up to 12,000 grid cells and formats `navStatus_` with five `fmt::format` calls every frame, for a string only the World panel's Debug tab ever shows |
| 6 | `src/params/parameter_set.cpp:10-18` | `find(string_view)` does `index_.find(std::string(path))` — a heap allocation on **every** parameter lookup in the program, paths being well past SSO |
| 7 | `src/scene/procedural.cpp:3165`, `:3426` | ~90 `findRel` linear scans over ~77 entries per procedural node per frame inside `applyProceduralParameters`, plus per-slot `std::string` concatenation. On a 45-node scene that is ~310,000 string comparisons a frame. **This is now the largest identified item in `engine.update`**; `ParticleParameters` shows the fix (cached handles at registration) |

### A residual, named rather than explained away

Four of the thirty flattened procedurals regenerate every frame in *some* sessions and none in
others, on the same binary and the same project. It is not audio — `--play` changes nothing. The
likely mechanism is an input that genuinely does change every frame for those four, in which case
the regeneration is correct and the design is what costs. **It is not diagnosed**, and ADR-199's
rule applies: a measurement that the output changed is not a measurement of why.

### What the arms can and cannot establish (ADR-182)

- `gizmo` **is proven**: it names the node it grabbed, the handle it dragged and where the object
  ended up, and it aims with `ui::pickHandle` at the editor's own tolerance so the press is on the
  handle by construction. When nothing selectable is in frame it says so and reports no number.
- `box` **is partial**: the editor confirms a selection box is open mid-drag, but on this scene it
  selects 0 of 16 objects — terrain, procedural clouds and particles are not offered by
  `nodesInScreenRect`. It measures the gesture, not the cost of holding a large selection.
- `strip` is a **one-shot** script keyed on the absolute frame number, so inside `--ui-ab` it acts
  only during the block that happens to cover its window. Its column is an idle editor with the
  Sequence panel in whatever state the script left it.
