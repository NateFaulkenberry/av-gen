# UI responsiveness: what actually blocks an interaction

**Status:** Phase 1 of 2 — architecture and invalidation traced from source, and the two dominant
CPU costs measured headlessly. End-to-end (in-app) latencies not yet taken.
**Date:** 2026-09-16
**Scope:** diagnostic only. Nothing here is a fix and nothing was refactored.

> **Why this document is split into two phases.** Two agents were holding the GPU under
> `tools/gpu-lock.sh` throughout. ADR-170's rule is *count structure, not milliseconds on a contended
> machine*, and §2 of the brief forbids estimated numbers. So phase 1 delivers everything contention
> cannot corrupt: the execution model, the invalidation graph, the synchronisation audit, and — via
> two **CPU-only, GPU-free** benchmarks reporting **minima over repeats** — the two costs that were
> the leading hypotheses. Every number below is **measured**, **computed from a file on disk**, or
> **quoted from an existing source comment** and attributed as such. Anything not yet measured is
> marked **PENDING**. There are no estimates.
>
> **One hypothesis died here.** The analysis-cursor defect (finding 3) looked like a strong candidate
> from reading and measured at 0.188 ms — cosmetically irrelevant. It is recorded below as a real bug
> with no performance consequence, rather than quietly dropped, because the measurement is the reason
> the investigation did not go and "fix" it.

---

## Executive summary

Three findings, in descending confidence.

**1. The world is re-derived on the main thread every frame, and the UI frame is built after it.**
This is not a hypothesis; it is the shape of the loop in `src/app/application.cpp`. Within one
iteration: `pollEvents` → `engine_->tick` → `engine_->update` → `panel_->draw` → submit → present.
`Engine::update` runs the full pipeline unconditionally — control hub, signals, sources, timeline
automation, spatial fields, modulation routes, behaviour, sequence animation, event scheduling,
`controller_->update()`, camera, post, world effects, shader layers — whether or not anything
changed. So the UI is never *waiting for* world evaluation in the sense of a handshake; it is
**serialised behind it on the same thread, every frame**. An interaction feels slow when it makes
that frame's evaluation expensive, not when it schedules something.

**2. Starring a hero forces a full scene flatten, and it costs 282 ms. This is the answer.** Measured
headlessly on `glowmere-valley-2.scene.json`, minimum of 5 repeats, with the terrain cache already
hot, by `tools/bench_flatten.cpp`:

| arm | cost | rebuild happened? |
|---|---|---|
| project load (cold, cache empty) | **1,017.7 ms** | yes |
| warm flatten, nothing changed | **285.8 ms** (min of 5) | yes, guarded |
| **star a hero** (`setHeroes` + flatten) | **282.7 ms** (min of 5) | yes, guarded |

Every flatten re-derives the same thing: *80 nodes → 256 entities, 860 meshes, 64 procedurals*.

Two things follow. **`setHeroes` itself is free** — the star arm and the no-change arm are the same
number to within noise, so 100% of the cost is the flatten it triggers. And **the existing
`TerrainProducts` cache is already working and is not enough**: 285 ms is what remains *after* the
terrain cache hits.

282 ms of main-thread work, before the UI frame that would show the star is built, is squarely in
beachball territory and matches the reported symptom exactly.

The star toggle
routes `ui::makeHero` → `applyEdit` → `Composition::setHeroes`, which sets `dirty_`. `dirty_` gates
`rebuild()`, and the codebase already says what that costs, in `composition.hpp`:

> *"`dirty_` has no granularity: adding one flower re-flattens the world"*

and in `composition.cpp` at `rebuild()`:

> *"A flatten is the single most expensive thing the editor does on the main thread, it is triggered
> by structural edits an artist makes constantly"*

`setHeroes` is explicit that it accepts this cost (ADR-193, a hero is a navigation obstacle):
*"Heroes are declared rarely and a flatten is expensive, but a flatten that does not happen is a
world that disagrees with itself."* That reasoning is sound for correctness and is the direct cause
of the reported symptom. This is the single highest-confidence explanation for the starring lag.

**3. Every seek re-walks the analysis track from zero — a real bug that costs nothing.**
`Engine::seekSeconds` contains:

```cpp
if (track_) {
    // The offline analysis cursor walks forward through the frames, so a backwards seek has to
    // rewind it or every frame between here and where it had got to is skipped. Rewound rather
    // than reset to zero: a forward seek keeps its place.
    offlineFrameCursor_ = 0;
}
```

The comment describes a rewind that preserves a forward seek's position. The code resets to zero
unconditionally. The next `Engine::update` then walks `while (cursor < frames.size() &&
frames[cursor].timeSeconds <= time.renderTime)`, calling `music_.consume()` on **every analysis
frame between zero and the target**, synchronously, in one frame.

Computed from the files on disk (48 kHz, `Analyzer::hopSize = 512`):

| track | duration | analysis frames | frames re-consumed by a seek to the end |
|---|---|---|---|
| `glowmere-valley.wav` | 90.0 s | 8,437 | **8,437** |
| `night-shift.wav` | 105.0 s | 9,843 | **9,843** |

**Measured** by `tools/bench_music_consume.cpp` (min of 9): consuming the **entire** 8,434-frame
Glowmere track costs **0.188 ms**, or 0.022 µs per analysis frame. A seek to the very end of the
piece therefore wastes about **two tenths of a millisecond**.

So this is a genuine defect — forward seeks lose their place, contrary to the comment, and the
classifier's order-dependent state is rebuilt from zero every time — but it is **not** a cause of the
sluggishness, and fixing it would not be felt. It is recorded so nobody spends a day on it.

**What this is probably not.** The GPU-synchronisation hypothesis (§8) looks weak on inspection: the
readback paths are documented as explicitly non-blocking, the cull path consumes *"the last completed
cull readback"*, and `SceneRenderer::timeline()` is *"non-blocking: the timeline reports whichever
frame's readback has completed, two or three back."* The one genuine wait is already named as one in
the profiler — `gpu.acquire WAIT` — and is reported separately from work. This should still be
confirmed by measurement rather than closed by reading.

---

## The instrument that already exists

A significant part of the brief is already built, and phase 2 should use it rather than add a second
instrument beside it.

`core::PhaseProfiler` (`src/core/phase_profiler.hpp`) is a per-frame CPU phase profiler with a
2,048-frame ring, reporting order statistics. Two of its design notes matter here:

* It deliberately reports **min over a long run**, for exactly the reason this phase is constrained:
  *"this machine is usually running several builds. Contention is never negative, so on a contended
  machine the minimum over a long run is the honest estimate of the work itself."*
* It refuses to label CPU wall-clock around a GPU call as GPU time, and names its one wait as a wait.

Phases already registered in `application.cpp`:

| phase | what it covers |
|---|---|
| `engine.update` | the whole world derivation (finding 1) |
| `ui.build` | `imgui_->newFrame()` + `panel_->draw()` |
| `gpu.acquire WAIT` | swapchain wait, named as a wait |
| `gpu.submit`, `gpu.processEvents`, `present` | submission side |
| `render.job` | background offline render steps |
| **`input->present ms`** | **the symptom metric, already instrumented** |
| `# procedural regen`, `# scene flattens`, `# IBL builds` | **structural counters** |
| `alloc.*` | allocation counts per phase |

The counters are the contention-proof evidence this investigation needs, and `flattenCount()` exists
for precisely this question — from `composition.hpp`:

> *"Counted so an interaction can be judged by what it caused rather than by how long the machine
> happened to take: 'one flatten per click' and 'one per frame of the drag' are different defects and
> a millisecond figure on a contended machine tells them apart badly."*

There is also `--ui-ab`, which interleaves scripted UI arms **inside one process** in blocks with
settling frames — the only frame-time comparison shape this project accepts. Eleven arms exist
(`src/app/ui_script.cpp`): `hover`, `sliders`, `panels`, `select`, `scrub`, `camera`, `tabs`, `edit`,
`strip`, `gizmo`, `box`. They drive real SDL events through the real routing rather than calling into
the editor directly.

Mapping the brief's five required interactions onto what exists:

| brief's interaction | existing arm | gap |
|---|---|---|
| playhead relocation | `scrub` (per-frame transport), `strip` (press + drag on the ruler) | none |
| hero starring / unstarring | — | **needs a new arm** |
| object selection | `select`, `box`, `gizmo` | none |
| cheap UI-only interaction | `hover`, `panels`, `tabs` | none |
| expensive world modification | `edit` (scatter brush stroke, undo, redo) | none |

So phase 2 needs **one new script arm**, not an instrumentation framework.

---

## Current architecture

```
SDL event queue
     |
  pollEvents ---> handleInputEvent ---> UI/editor state mutation
     |                                        |
     |                                  Composition::dirty_ = true   (structural edits)
     |
  engine_->tick(clock)          transport position for this frame
     |
  engine_->update(time)         <=== ALL OF THIS, EVERY FRAME, MAIN THREAD
     |   controlHub_.update
     |   updateTimeSignals / music_.publish / updateTimelineClock / applyCues
     |   states_.update
     |   sources_.update / params_.resetFinals
     |   timeline_.apply                    (automation)
     |   controller_->updateFields          (spatial reactivity)
     |   modulator_.applyRoutes
     |   controller_->updateBehaviour
     |   seq::applyAnimation
     |   sequenceEvents_.advanceTo / drain
     |   controller_->update(time) --------> Composition::ensureBuilt()
     |                                          if (dirty_) rebuild()   <== THE FLATTEN
     |   applyCameraParameters / focus / post / worldEffects / shaderLayers
     |
  ai_->pump()                   (budgeted queue; one mutex when idle)
  job_->step(4, 0.010)          (background render, budgeted to 10 ms)
     |
  imgui_->newFrame(); panel_->draw()        <=== UI BUILT AFTER THE WORLD
     |
  gpu.acquire WAIT -> submit -> processEvents -> present
     |
  prof.count(# procedural regen / # scene flattens / # IBL builds)
```

The important structural point: **there is no queue between the UI mutation and the world
evaluation.** A structural edit sets a flag, and the very next `controller_->update()` in the same
frame pays for it in full, before the UI that would show the result is built.

---

## Interaction traces

### 1. Playhead relocation

`Sequence panel ruler press` → transport seek → `Engine::seekSeconds(target)`:

1. `transport_.seek(seconds)` — clamps, cheap.
2. `player_->seekSeconds(target)` — **lock-free**. `audio_player.cpp` hands seeks to the audio
   callback through `seekTarget_`/`seekPending_` atomics. Not a blocker.
3. `offlineFrameCursor_ = 0` — **finding 3**. Sets up an O(target position) re-walk next frame.
4. `modulator_.resetState()`, `sources_.reset()`, `music_.reset()`, cue state cleared.
5. Entity world + director re-simulated to the seeked second (ADR-093) — deliberate, so that
   scrubbing to the same second twice gives the same frame. Cost **PENDING**.
6. Next `Engine::update`: the analysis catch-up loop runs (step 3's consequence).
7. `engine_->transport().discontinuityRevision()` changes → `renderer_->resetTemporalHistory()` →
   TAA history dropped, so the following frames are also more expensive and visibly noisier.

Note that steps 3–5 all scale with *where* you seek to or with scene complexity, and none of them is
deferred or coalesced.

### 2. Hero starring / unstarring

`world_edit_panel` star → `ui::makeHero` (`world_edit.cpp:614`) → builds an `EditCommand` →
`applyEdit` → `Composition::setHeroes` (`composition.cpp:1243`):

1. Validate the whole hero list (names unique) — cheap.
2. `heroes_ = std::move(heroes)`; re-anchor every hero to its node's current world transform.
3. `++heroRevision_`.
4. `dirty_ = true` (ADR-193: a hero is a navigation obstacle and only `rebuild()` calls
   `obstaclesFromHeroes`).
5. Next `controller_->update()` → `ensureBuilt()` → **`rebuild()`** — full flatten: obstacle set
   rebuilt from scratch, every node re-flattened, navigation grid re-baked.

Mitigation that already exists: `TerrainProducts`, a hash-keyed cache of the terrain's chunk meshes
and ecology scatter, *"~390 ms of a rebuild, most of it `world::scatter` walking a quarter of a
million grid cells"* on a Glowmere world (quoted from `composition.hpp`; **not my measurement**). So
the terrain half of a flatten is already skipped when the terrain has not changed. **What remains of
a flatten after that cache hits is the number this investigation most needs and does not have.**

### 3. Object selection

`select` arm moves the World panel's selection. Selection does not call any `dirty_`-setting API on
the paths read. Expected to be cheap; if it is not, the cost is in panel rebuild (category A), not
invalidation. **PENDING.**

### 4. Cheap UI-only interaction

`hover` / `panels` / `tabs`. No world mutation. This is the control arm: it establishes the floor
that `engine.update` + `ui.build` + present costs when nothing has changed, which every other number
must be read against. **PENDING.**

### 5. Expensive world modification

`edit` arm — arm a scatter brush, drag a stroke across the canvas, undo, redo. This is the case the
`# scene flattens` counter was written for: the question is whether a stroke costs one flatten or one
per frame of the drag. **PENDING**, and it is a counter, not a timing, so contention cannot corrupt
it.

---

## Measurements

### Phase 1 — taken (CPU-only, no GPU, minima over repeats)

```
$ ./build/release/tools/avgen_bench_music_consume assets/audio/glowmere-valley.wav 9
  90.00 s, 8434 analysis frames (hop 512)
  consume over the whole track (min of 9): 0.188 ms
  per analysis frame: 0.0223 us

$ ./build/release/tools/avgen_bench_flatten <abs>/examples/world/glowmere-valley-2.scene.json 5
  nodes 80, heroes 11
  cold arm: 1.9 ms  [flattens 1 -> 1, so it rebuilt NO -- this arm measured nothing]
  warm flatten (terrain cache hot, no change), min of 5: 285.8 ms
  STAR a hero (setHeroes + flatten),  min of 5: 282.7 ms
  (per-rebuild log: flattened 80 node(s) -> 256 entities, 860 meshes, 64 procedurals)
  (load-time cold flatten, cache empty: 1017.7 ms)
```

**On the probe's honesty (ADR-182).** The first version of the flatten benchmark reported the cold
arm at 1.8 ms, which read as a fast flatten. It was not — the project load had already flattened, and
the arm rebuilt nothing. The arms now compare `flattenCount()` before and after and refuse to report
a time when no rebuild happened; the cold arm now prints *"rebuilt NO — this arm measured nothing"*
instead of a plausible small number. The warm and star arms abort outright if their rebuild does not
happen. Without that guard this document would have claimed a 1.8 ms flatten.

### Phase 2 — PENDING (needs an uncontended GPU)

In-app, end-to-end, from the existing profiler's **min** column:

| phase | hover (control) | scrub | strip | select | star | edit |
|---|---|---|---|---|---|---|
| `engine.update` | | | | | | |
| `ui.build` | | | | | | |
| `gpu.acquire WAIT` | | | | | | |
| `gpu.submit` | | | | | | |
| `input->present ms` | | | | | | |
| `# scene flattens` | | | | | | |
| `# procedural regen` | | | | | | |
| `# IBL builds` | | | | | | |

---

## Blocking operations

| # | operation | where | category | evidence |
|---|---|---|---|---|
| 1 | Full scene flatten on any structural edit | `Composition::rebuild`, gated by `dirty_` | **E** + **B** | **measured 282 ms** (star), 286 ms (no-change), 1,018 ms (cold) |
| 2 | ~~Analysis-track re-walk from zero on every seek~~ **not a bottleneck** | `Engine::seekSeconds` | **H** (a correctness bug, not a cost) | measured 0.188 ms for the whole track |
| 3 | Whole-pipeline `Engine::update` every frame regardless of change | `Engine::update` | **D** | unconditional by construction |
| 4 | Entity/director re-simulation on seek | `Engine::seekSeconds` | **H** (probably legitimate) | required for scrub determinism, ADR-093/091 |
| 5 | TAA history reset on transport discontinuity | `application.cpp` | **H** | correctness; costs the *following* frames |

Categories per the brief: A UI, B main-thread CPU, C scheduling, D state architecture,
E dependency/invalidation, F GPU/rendering, G synchronisation, H legitimate.

---

## Synchronisation points

Audited by inspection; **to be confirmed by measurement.**

* **`gpu.acquire WAIT`** — the swapchain acquire. Already named as a wait and reported separately
  from work. Under vsync this is where a fast frame parks, and time here is *not* evidence of a
  problem.
* **GPU readbacks — non-blocking by design.** `SceneRenderer::timeline()` reports *"whichever frame's
  readback has completed, two or three back"*; the indirect-draw path uses *"the last completed cull
  readback"*. Neither stalls the CPU.
* **`ai_->pump()`** — budgeted, and with no task running it is *"one mutex and an empty-deque
  check."*
* **`job_->step(4, 0.010)`** — the background offline render is explicitly budgeted to 10 ms per UI
  frame, i.e. already cooperative.
* **Audio seek** — atomics, no lock.
* No `future::get`, `thread::join`, or condition-variable wait was found on the interactive path.

**Preliminary conclusion: this does not look like a category-G problem.** That is a claim from
reading and it is the one most deserving of a measurement that could refute it.

---

## Invalidation analysis

`dirty_` is a single bool for the entire composition. Any of the 14 sites that set it causes the same
all-or-nothing rebuild. Confirmed setters include `setComposition`, `setNavCellSize`,
`setNavBodyRadius`, `setNavWadeDepth`, and `setHeroes`.

So the invalidation breadth for "star one hero" is: **the whole world**, minus whatever the
`TerrainProducts` hash-cache saves. There is no dependency graph and no partial invalidation. Two
mitigations already exist and are worth noting as precedent, because they show the codebase has
already solved this shape of problem twice:

1. **`TerrainProducts`** — content-hash cache over the most expensive sub-product. *"Keyed on the
   hash of exactly those inputs, so a terrain that has changed rebuilds and one that has not does
   not. ... nothing has to remember to invalidate it."*
2. **`interactiveRebuildBudgetMs_` / `proceduralsAwaitingRebuild()`** — procedural regeneration is
   already deferred under a time budget, with the editor able to show which objects are deliberately
   a few frames behind. *"geometry that is deliberately a few frames behind the slider has to say
   so."*

Both are exactly the patterns the brief's §11 research points at, already present and working. That
materially lowers the risk of extending the same approach, and argues against introducing a new
generalised async framework.

---

## State-coupling analysis

The brief asks where AV Gen sits between "UI waits for committed world state" and "UI reflects the
request immediately while the world catches up".

**It sits at neither end, and the distinction the brief draws does not quite fit the code.** There is
no request/commit handshake to wait on. The UI mutates state, and the world is re-derived from
scratch the same frame, on the same thread, before the UI is drawn. The effect resembles blocking,
but the mechanism is serialisation, not waiting.

Consequences for the brief's §4 (`requestedState` vs `evaluatedState`):

* For the **playhead**, the two are already separable in principle — `Transport` holds the position
  and the derived state follows it. The expense is that following it is O(position) (finding 3) and
  re-simulates entities.
* For **structural edits** like starring, there is genuinely no representation of "starred but not
  yet flattened". The star's *visual* state could be shown immediately; what cannot be shown
  immediately is the navigation/obstacle consequence. Those are separable in principle and are not
  separated today.
* The project already has a precedent for showing exactly this split honestly:
  `proceduralsAwaitingRebuild()` exists so the editor can say "this is a few frames behind."

**The derived-copy rule constrains any redesign here.** The parameter is authoritative and `Scene` is
a per-frame derivation. Any "immediate interaction state" must not become a second authority, or it
will be silently overwritten by the next `applyParameters`.

---

## Scheduling analysis

* **Within a frame**, `dirty_` coalesces naturally: many edits, one rebuild.
* **Across frames**, nothing coalesces a flatten. A drag that dirties every frame flattens every
  frame. This is the defect `flattenCount()` was added to detect, and it is untested for the star and
  seek paths.
* **No cancellation exists anywhere.** No work started for an obsolete state is abandoned.
* **Seeks are not coalesced.** Each seek resets the cursor and pays its own catch-up; a rapid scrub
  across 10.0 → 12.0 s pays for each intermediate position rather than only the newest.
* **Procedural regeneration is the exception** and is already budgeted and deferred.

So the brief's latest-state-wins hypothesis (§5) applies most directly to **seeks** and to
**structural edits during a drag**, and is already implemented for procedurals.

---

## Rendering analysis

**PENDING**, and deliberately held as a separate hypothesis. The §9 experiment (normal vs reduced
render workload vs volumetrics/shadows off vs reduced resolution) has not been run because it is
precisely the measurement GPU contention destroys.

What can be said now: the editor already has a render-scale lever (`panel_->canvasRenderScale`,
clamped 0.25–1.0), and `application.cpp` notes that the canvas is the display's backing scale times
its size, *"so on a Retina screen it is several times the pixel count the renderer's benchmarks
quote."* That makes §9 cheap to run — the arm already exists — and makes the render-scale sweep the
natural control.

---

## Professional software research

The established pattern across mature DCC and compositing tools is a **dependency graph with dirty
propagation and pull (lazy) evaluation**: marking a plug dirty propagates downstream, and nothing is
recomputed until a consumer pulls it. Maya's documentation calls out the two cases that trigger
propagation — a time change (from each animation curve outward) and a value change (from the edited
plug outward) — which maps precisely onto AV Gen's two problem interactions, seek and structural
edit. The relevant contrast is that Maya propagates dirtiness *per plug*, where AV Gen has one bool
for the whole composition.

The second established pattern is **progressive refinement / adaptive degradation**: run a fast
preview during interaction, then refine when the user stops. Modo's preview viewport performs *"only
as many samples per frame as can be afforded to maintain interactivity"*; the interactive-editing
literature describes a fast first phase followed by a refinement phase once interaction ceases. The
patent literature on "user selectable adaptive degradation" covers the same idea from the 1990s.

The third is **keeping non-UI work off the UI event thread** so that editing stays responsive.

References:

* [Maya — The Dependency Graph](https://help.autodesk.com/cloudhelp/2016/ENU/Maya-SDK/files/GUID-AABB9BB4-816F-4C7C-A526-69B0568E1587.htm)
* [Autodesk — Using Parallel Maya (2026)](https://damassets.autodesk.net/content/dam/autodesk/www/html/using-parallel-maya/2026/UsingParallelMaya.html)
* [Maya API Programming — dirty propagation](https://www.chadvernon.com/maya-api-programming/)
* [Foundry Modo — Preview viewport (progressive refinement)](https://learn.foundry.com/modo/content/help/pages/modo_interface/viewports/utility/preview.html)
* [HiPR: Hierarchical Progressive Rendering for Immediate Feedback](https://arxiv.org/pdf/2606.26612)
* [US 6,072,498 — User selectable adaptive degradation for interactive computer rendering](https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/6072498)
* [US 11,494,966 — Interactive editing of virtual three-dimensional scenes](https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/11494966)

**What this says about AV Gen.** The two mechanisms the industry relies on are *granular
invalidation* and *deferred/progressive work*. AV Gen already has the second in one place
(procedurals) and lacks the first almost entirely. That is a useful asymmetry: it suggests the
highest-value direction is granularity of invalidation, not a progressive renderer.

---

## The "After Effects" hypothesis

Answering the brief's seven questions with what is now known:

1. **What problem does it solve?** Perceived latency when the *render* of a frame is the expensive
   part. It hides render cost, not evaluation cost.
2. **Which parts apply to AV Gen?** Immediately honouring the requested playhead position does.
   Rendering at reduced quality during interaction is already partly available via
   `canvasRenderScale`.
3. **Which parts are unnecessary?** A full progressive/tiled refinement architecture, on the evidence
   so far — AV Gen renders a real-time frame, not a converging ray trace.
4. **Could AV Gen get the same perceived responsiveness without low-resolution rendering?** On
   current evidence, probably yes, *if* findings 2 and 3 dominate — both are CPU evaluation costs
   that a cheaper render would not touch.
5. **Is async evaluation more important than progressive rendering?** The structural evidence says
   yes, but this is exactly what §9's measurement exists to test, and it has not been run.
6. **Could cached/previous-frame presentation hide the latency?** Possibly for a flatten, but it
   conflicts with the TAA history reset already performed on discontinuity, and with the scrub
   determinism guarantee (ADR-091).
7. **Could an interactive render tier complement async evaluation?** Yes — and the render-scale lever
   already is one. It should be measured before anything is built.

**Conclusion: do not adopt the After Effects model on current evidence.** It targets a cost that has
not been shown to dominate.

---

## Output Preview experiment

Output Preview is being implemented on `agent/output-preview` and is not a dependency of this work.

Once it lands, repeat the phase-2 table with the preview at a reduced target size. The prediction
that follows from findings 1–3 is that a smaller render target **reduces GPU work and CPU render
preparation but does not materially improve star/seek responsiveness**, because those costs are in
`engine.update`, upstream of anything the render target size can affect.

That prediction is falsifiable and is worth stating now precisely so it can be wrong: if shrinking
the target *does* improve star/seek latency proportionally, findings 1–3 are not the dominant cost
and this document's emphasis is misplaced.

---

## Candidate solutions

Classified by confidence, scope, risk and evidence, **not ranked by preference**, and none
implemented.

### C1 — Fix the analysis cursor rewind

* **Problem addressed:** finding 3.
* **Change:** rewind the cursor to the seek target instead of 0 — i.e. make the code do what its
  comment already says.
* **Confidence:** high that it is a defect; **measured as immaterial** (0.188 ms for a whole track).
  Worth doing for correctness — forward seeks lose their place and the classifier restarts — but it
  will not be felt, and it should not be sold as a performance fix.
* **Scope:** a few lines in `Engine::seekSeconds`. **Risk:** low, but the catch-up loop feeds
  `music_.consume`, whose state is order-dependent, so a naive jump changes classifier state. Needs
  care and a determinism test.
* **Touches:** `Engine`, music classifier. **Prototypable independently:** yes.
* **Success measured by:** seek latency becomes independent of target position.

### C2 — Granular invalidation for the flatten

* **Problem addressed:** findings 1–2 (category E).
* **Change:** replace the single `dirty_` bool with per-product invalidation, extending the
  `TerrainProducts` content-hash pattern to the other rebuild products.
* **Confidence:** high that it addresses the cause, now that the cause is measured at 282 ms with the
  terrain cache already hot. **Scope:** large — touches every `dirty_` setter and `rebuild()`.
  **Risk:** high; a missed invalidation is a world that silently disagrees with itself, which is the
  exact failure `setHeroes` chose a full flatten to avoid.
* **Evidence:** strongest of any candidate here. It is the only one backed by a measured number that
  is large enough to explain the symptom on its own.
* **Touches:** `Composition`, navigation baking, obstacles, editor undo/redo.
* **Prototypable independently:** partially — one product at a time.
* **Success measured by:** `# scene flattens` and `engine.update` min for the star arm.

### C3 — Hero declaration without a flatten

* **Problem addressed:** finding 2, narrowly.
* **Change:** incrementally update the obstacle set for one hero rather than re-deriving it, so
  `setHeroes` need not dirty the whole composition.
* **Confidence:** medium. **Scope:** narrow. **Risk:** medium — this is the correctness trade
  ADR-193 deliberately made; it must not be undone silently.
* **Prototypable independently:** yes. A strict subset of C2 and a good first probe of it.

### C4 — Coalesce seeks and structural edits to latest-state-wins

* **Problem addressed:** category C.
* **Change:** collapse rapid repeated requests to the newest before evaluation.
* **Confidence:** medium — depends on whether scrubbing actually issues per-frame seeks, which is
  **unmeasured**. **Scope:** moderate. **Risk:** medium; interacts with scrub determinism (ADR-091).
* **Prototypable independently:** yes.

### C5 — Interactive render tier

* **Problem addressed:** category F, *if* measurement shows it dominates.
* **Change:** formalise `canvasRenderScale` as an automatic interaction-time degradation.
* **Confidence:** low that it addresses the reported symptom. **Scope:** small. **Risk:** low.
* **Prototypable independently:** yes — the lever already exists, so §9 tests this for free.

### C6 — Move world evaluation off the main thread

* **Problem addressed:** finding 1 generally.
* **Confidence:** low. **Scope:** very large. **Risk:** very high — collides with the derived-copy
  rule and with offline determinism.
* **Explicitly recorded as the option not to reach for first.** The brief warns against a generalised
  async framework adopted on intuition, and nothing measured so far justifies one.

---

## Recommended next experiment

The phase-1 experiments have already run and they have narrowed this to one question. The next
experiment is therefore **not** an end-to-end latency sweep — it is:

**Find out what the 285 ms is made of.**

A flatten produces 256 entities, 860 meshes and 64 procedurals, and the terrain cache is already hot,
so the remaining cost is in node flattening, mesh building, procedural generation, obstacle
collection or the navigation bake — and the existing source notes say the navigator alone was once
170 ms of a 1,676 ms flatten. Add phase timing *inside* `Composition::rebuild()` (temporary, in the
same shape as the existing per-rebuild log line) and re-run `avgen_bench_flatten`. It needs no GPU,
no UI and no render, and it costs one afternoon.

That single breakdown decides between C2 and C3 and tells us whether a hero star could skip most of a
flatten cheaply, or whether granular invalidation is genuinely required. **Do not start C2 — the
large, risky refactor — before that breakdown exists.** Its risk is only justified if the breakdown
says the cost is spread across products rather than concentrated in one.

**Then, when the GPU is free**, confirm the in-app picture in one `--ui-ab` process:

```sh
tools/gpu-lock.sh ./build/release/avgen --project examples/world/glowmere-valley-2.json \
  --ui-ab hover:scrub:strip:select:edit --ui-ab-frames 120 --ui-ab-blocks 4 --ui-ab-settle 12
```

reading the **min** column and the three structural counters, with `hover` as the control arm. Then
add the one missing arm (`star`) and repeat. Then the §9 render-scale sweep.

---

## Instrumentation added — TO BE REMOVED

Two temporary CPU-only benchmark targets. Neither is wired into the application, the tests, or any
build the user runs; both exist only to answer this document's questions.

| file | purpose | remove when |
|---|---|---|
| ~~`tools/bench_music_consume.cpp`~~ | timed the analysis catch-up loop | **already removed** — its question is answered (0.188 ms), and the exact invocation and output are recorded above, so it is a few minutes to re-create if ever needed |
| `tools/bench_flatten.cpp` | timed the flatten and the star | after the phase-2 breakdown is taken |

Each also has two lines in `tools/CMakeLists.txt`, marked `# TEMPORARY`, to delete with the file.

**No instrumentation was added to `src/`.** The application itself is unmodified. The existing
`PhaseProfiler` phases and structural counters cover everything phase 2 requires except a `star`
script arm, which would be a permanent addition to `ui_script.cpp` in the same shape as the existing
ten, not a diagnostic hack.

## Open questions for phase 2

1. ~~Cost of `music_.consume()`~~ — **answered: 0.188 ms for a whole track. Not a bottleneck.**
2. ~~Cost of a flatten after the `TerrainProducts` cache hits~~ — **answered: 285.8 ms.**
3. **What is the 285 ms made of?** The one question that now matters. See *Recommended next
   experiment*.
4. Does a drag cost one flatten or one per frame?
5. Does scrubbing issue one seek per frame?
6. Does reduced render workload move `input->present ms` at all? (the §9 control)
7. Is `select` actually cheap, as assumed here?
