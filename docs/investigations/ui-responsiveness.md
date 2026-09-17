# UI responsiveness: what actually blocks an interaction

**Status:** Complete. Phase 1 traced the architecture and measured the two dominant CPU costs
headlessly; **phase 2 took the end-to-end in-app latencies and found that phase 1 had the wrong
interaction**. Read phase 1 for the invalidation graph and the execution model; read
[phase 2](#phase-2--end-to-end-interactive-measurements) for what actually blocks an interaction.
**Date:** 2026-09-16 (phase 1), 2026-09-16 (phase 2)

> **Before reading phase 1's executive summary:** its finding 2 — "starring a hero costs 282 ms, this
> is the answer" — is correct about the 282 ms and wrong about it being the answer. A timeline click
> costs up to 5.0 s, a hero star costs 1,454 ms of which the flatten is 281, and neither number was
> obtainable without a renderer and a window. Phase 1's numbers are not retracted; its emphasis is.
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

---

# Phase 2 — End-to-End Interactive Measurements

**Status:** complete. Every number below was taken in the running windowed editor, on the GPU, under
`tools/gpu-lock.sh`, with the load average recorded beside it.
**Date:** 2026-09-16
**Scope:** still diagnostic only. Nothing was fixed, refactored or optimised.

> **Phase 1 was looking at the wrong interaction, and this phase says so in its first table.**
> Phase 1's headline was a 282 ms flatten. That number is correct and it is reproduced here in the
> live editor to within 1%. It is also **not the largest cost in the editor and not the cause of the
> complaint about the playhead.** A single click on the timeline ruler costs **166 ms at the very
> start of the piece and 2.5–3.5 seconds anywhere past 90 s**, and a one-second drag along the
> ruler cost **132.4 seconds of wall clock**. Phase 1 could not see this because the cost is inside
> `Engine::seekSeconds`, which its headless flatten bench never called.
>
> **Two of this phase's own hypotheses died here**, and both are recorded rather than quietly
> dropped, because being wrong twice is what produced the anatomy in §6.

---

## 1. Executive findings

### Confirmed (measured, with an arm shown capable of failing)

**F1 — A timeline click blocks the editor for 0.17–5.0 seconds, and the cost is a full re-simulation
of the entity world from the start of the piece.**
`Engine::seekSeconds` calls `EntityWorld::seek(seconds, …, step = 1/60, maxSeconds = 90)`
(`engine.cpp:2265`), which **integrates every entity's behaviours forward at a fixed 1/60 s step from
`target − 90 s` to `target`**. On `glowmere-valley-2` that is 23 entities × up to 5,400 steps =
**124,200 behaviour updates per click**, at a measured 17–30 µs each.

Measured, `click` arm, 21 clicks, min over the run, load average 3.0–6.1:

| clicked second | body-steps integrated | `entity re-sim ms` (min) |
|---:|---:|---:|
| 4.7 | 6,417 | **165.3** |
| 18.0 | 24,840 | 550.3 |
| 31.4 | 43,332 | 1,000.0 |
| 45.2 | 62,445 | 1,776.6 |
| 58.6 | 80,914 | 1,380.0 |
| 72.5 | 100,027 | 2,627.6 |
| 85.9 | 118,496 | 3,072.4 |
| ≥ 90.0 (the cap) | 124,200 | 2,217.9 … 5,003.6 |

The cost is a straight line in *where you clicked*, flattening at the 90 s cap. Nothing else in
`seekSeconds` is measurable: **`engine.seek ms` minus `entity re-sim ms` is 0.025–0.046 ms**, over
every seek in every run. The re-simulation is **99.999%** of a seek.

**F2 — The UI does not acknowledge the click until that work is finished, and the loop does not
process input while it runs.**
`SequencePanel::draw` calls `engine.seekSeconds` synchronously from inside `panel_->draw()`
(`sequence_panel.cpp:1290/1392/1476/1535`), which the frame loop runs inside the `ui.build` phase.
Measured on the same 21 clicks: `ui.build` 171.8 / 2,526.0 / 3,707.9 ms (min / median / max) against
a quiet `ui.build` of 0.27 ms, and `input->ui.build ms` 175.2 / 2,529.4 / 3,711.7 ms. The gap between
the UI acknowledging the click and the frame reaching the compositor is **3.3–5.2 ms**. There is no
stage at which the playhead has moved and the world has not.

**F3 — A drag issues one full seek per frame and nothing coalesces them.**
`strip` arm, one press and a 52-frame drag along the ruler:

* `# seeks/frame` = **exactly 1 on each of the 52 drag frames**, 0 elsewhere.
* Each seek: 1,034 ms min, 2,387 ms median, 3,704 ms max.
* **The 52-frame gesture — about one second of a hand moving — took 132.4 seconds.**
* `# scene flattens` = **0** on every one of them.

Fifty-one of those fifty-two evaluated states were obsolete before they finished.

**F4 — Moving the playhead does not trigger a scene flatten, and costs nothing after the frame it
happens in.** `# scene flattens` is 0 on every seek frame of every run (21 + 52 + 24 seeks across
four runs). The frame *after* a seek is completely ordinary: `engine.update` 3.41 ms median,
`# procedural regen` 4 (the idle baseline), `# scene flattens` 0, `analysis.catchup ms` 0.000,
frame 7.96 ms. **Every cost of a seek is inside the synchronous call.** This kills the natural
hypothesis that a seek schedules downstream world work.

**F5 — Starring a hero costs 1,454 ms in the live editor, and the flatten is only 19% of it.**
`star` arm, 15 star/unstar edits through `ui::setNodesHero` — the same `EditCommand` the star button
builds. Minimum over 15 events:

| stage | min | median | max |
|---|---:|---:|---:|
| `engine.update` (the flatten, `upd.controller ms`) | **275.3** | 282.7 | 284.6 |
| `render.record` | **1,144.4** | 1,147.6 | 1,151.1 |
|  …of which `uploadTextures` (44 textures) | **1,125.5** | 1,129.1 | 1,132.0 |
|  …of which `uploadMeshes` (860 buffers) | 13.2 | 14.0 | 14.2 |
|  …of which everything else | ≈ 5 | | |
| `ui.build` | 0.20 | 0.34 | 0.35 |
| **frame total** | **1,454.1** | 1,463.8 | 1,468.9 |

Phase 1's 282 ms is confirmed to within 1% — and it is **19% of what starring a hero actually
costs**. The other 77% is `SceneRenderer::uploadTextures` destroying and re-creating **every
texture in the scene**, because `Composition::rebuild()` ends with an unconditional
`++scene_.textureVersion` (`composition.cpp:4136`) and the renderer's only invalidation granularity
is that one counter (`scene_renderer.cpp:1898`). Phase 1's bench had no renderer, so it could not
see this.

**F6 — There is no broader UI-thread problem.** Eight interactions that do not touch the world —
hover, panel open/close, a continuous property drag, object selection, tab switching, a camera orbit,
a gizmo drag, a selection-box drag — all cost the same as the idle editor: frame median 22.1–26.8 ms
at a 2.47 Mpx canvas, worst frame 25.0–35.8 ms, `# scene flattens` 0, `# seeks/frame` 0,
`input->ui.build ms` 3.2–4.6 ms. **Only two interactions are pathological, and they are the two that
call into the world.**

**F7 — Reducing the rendering workload does not improve interaction latency, and does improve idle
frame pacing.** Three conditions, same arms, same procedure (§9). A 16× cut in rendered pixels plus
six render passes removed moved the click latency floor by **1.3 ms out of 166 ms (0.8%)** and the
star floor by **24.7 ms out of 1,454 ms (1.7%)** — while moving the idle frame from 22.3 ms to
8.3 ms median (2.7×).

### Hypotheses that died in this phase

**H1 (mine, wrong): the star's 1.15 s `render.record` spike is the mesh re-upload.** It is not.
`uploadMeshes` re-creates all 860 vertex/index buffers and costs **14 ms**. The probe was added
specifically to test this and it refuted it. The cost is the *texture* re-upload, 1,125 ms for 44
textures (25.6 ms each, mipmap generation included).

**H2 (Phase 1's finding 3, the analysis re-walk): it does not execute in the interactive editor at
all.** `analysis.catchup ms` is 0.000 on every frame of every windowed run. The loop lives in
`Engine::update`'s `else if (track_ && !track_->empty())` branch, which is unreachable while
`mode_ == EngineMode::Live` — the branch above it takes the runner's per-frame stream. It is still
the real bug Phase 1 described, and it still costs 0.188 ms, and it still matters for offline
renders. It has never had anything to do with the editor feeling slow.

**H3 (implied by the brief's §7): input backlog explains the beachball.** Not supported, and not
refutable with the instruments available — see §7 and §12's "could not measure".

---

## 2. End-to-end interaction measurements

All at 1600×1000 points (3200×2000 px, Retina scale 2.0), `--preview-mode workspace`, canvas
1914×1288 px = 2.47 Mpx, 150k–393k triangles. Arms interleaved inside one process in blocks of 120
frames with 12 settling frames discarded, 3 blocks each (ADR-150/ADR-170). `pgrep avgen` clean apart
from the main session's CPU-only `avgen_tests` during runs A and B; one-minute load average
2.6–6.8 throughout, recorded per run.

**`input->ui.build ms` is new in this phase.** It is the age of the newest input event at the moment
`imgui_->newFrame()` + `panel_->draw()` returns — the instant the frame's UI state is decided and the
playhead has been drawn wherever this frame's input put it. It is the number the brief asks for in
§1 ("UI-state acknowledgment separately from rendered-frame acknowledgment"); `input oldest->pres` is
the other half.

| interaction | arm | samples | `input->ui.build` min | median | max | frame total (min) |
|---|---|---:|---:|---:|---:|---:|
| **nothing (control)** | `hover` | 324 | **2.99** | 3.37 | 4.02 | 6.98 |
| object selection | `select` | 324 | — (no pointer event) | | | 6.89 |
| panel open / close | `panels` | 216 | — | | | 16.52 |
| property drag (a slider held) | `sliders` | 216 | — | | | 22.37 |
| tab switch | `tabs` | 216 | — | | | 21.56 |
| camera orbit on the canvas | `camera` | 216 | 4.32 (med) | | | 22.10 |
| gizmo drag | `gizmo` | 216 | 4.02 (med) | | | 22.07 |
| selection-box drag | `box` | 216 | 4.18 (med) | | | 21.82 |
| **timeline click** | `click` | 21 | **175.2** | **2,529.4** | **5,009.3** | **194.4** |
| **timeline drag, per frame** | `strip` | 52 | — | **2,390.5** | **3,708.3** | **1,042.6** |
| **star a hero** | `star` | 15 | — (no pointer event) | | | **1,454.1** |

The `select`, `panels`, `sliders`, `tabs` and `star` arms drive the editor through its own API rather
than the pointer, so they produce no SDL event and `input->ui.build ms` has nothing to time. Their
frame totals are the measurement; they are indistinguishable from `hover`'s except for `star`.

**Rendered-frame acknowledgment.** `input oldest->pres` — the oldest event of the frame's batch,
measured after `context_->present()` — runs **3.3–5.2 ms** behind `input->ui.build ms` on every arm
including the click frames (click: 170.3 / 2,623.6 / 5,014.5). Presentation is never the problem.

---

## 3. Event loop analysis

The loop, in the order `application.cpp` actually runs it — note that Phase 1's sketch had it
slightly wrong: **the input poll happens *after* the swapchain wait, not before it**, deliberately
(`application.cpp:3020`, "wait first, then read whatever the device produced during the wait").

```
frame start
  pollEvents #1        window-level events (resize, close, drops); input goes through too
  gpu.acquire WAIT     the vsync park
  ui.script            (scripted runs only) synthetic events, minted here so they are not
                       dated by the wait
  pollEvents #2        the input this frame acts on
  engine_->tick / engine_->update       <- the whole pipeline, every frame
  ai_->pump / job_->step
  ui.build             imgui_->newFrame() + panel_->draw()   <-- SEEKS HAPPEN HERE
  debug.geometry / render.record / imgui.record
  gpu.submit
  viewport.pick
  present
```

Per-phase, `hover` arm, 324 frames, min | median (ms):

| phase | 2.47 Mpx (A) | 0.15 Mpx (B) | 0.15 Mpx + preview tier + 6 passes off (C) |
|---|---|---|---|
| `events.poll` (both passes) | 0.02 \| 0.02 | 0.02 \| 0.02 | 0.02 \| 0.02 |
| `engine.update` | 2.81 \| 3.12 | 2.52 \| 2.73 | 2.56 \| 2.65 |
|  `upd.control` | 0.001 \| 0.001 | | |
|  `upd.signals` | 0.000 \| 0.001 | | |
|  `upd.modulation` (automation, fields, routes, behaviour, sequence, events) | 0.15 \| 0.19 | | |
|  **`upd.controller`** (the scene: `Composition::ensureBuilt`, poses, procedurals) | **2.37 \| 2.60** | | |
|  `upd.other` (camera, post, world effects, shader layers) | 0.001 \| 0.003 | | |
| `ui.build` | 0.14 \| 0.23 | 0.13 \| 0.19 | 0.14 \| 0.19 |
| `gpu.acquire WAIT` | 0.27 \| 15.02 | 0.02 \| 2.52 | 2.51 \| 3.77 |
| `render.record` | 1.59 \| 1.76 | 1.22 \| 1.39 | 0.95 \| 1.00 |
| `gpu.submit` | 0.84 \| 0.99 | 0.79 \| 0.98 | 0.29 \| 0.37 |
| `gpu.present WAIT` | ≈ 0.01 | | |
| **FRAME** | **6.98 \| 22.27** | **6.47 \| 8.29** | **7.05 \| 8.40** |

`engine.update` is **2.5–3.7 ms** and is not the problem. Within it, 83% is `controller_->update()`,
which is the scene derivation, and on a frame with no structural edit that is the per-frame pose and
procedural work, not a flatten. **No `update()` subsystem consumes the interaction frame.** The
interaction frame is consumed downstream of `update`, inside `ui.build`, by a call the panel makes.

---

## 4. CPU / GPU breakdown

GPU numbers are `gpu::FrameTimeline` timestamps (`renderer_->timeline().frameMs()`), surfaced into
the frame profiler as `gpu.frame ms (GPU)`. They are GPU timestamps, two or three frames behind,
never CPU wall-clock around a GPU call. CPU numbers are `steady_clock` on the main thread.

| frame kind | CPU update | CPU UI draw | CPU render prep | **GPU exec** | CPU waiting for GPU | total |
|---|---:|---:|---:|---:|---:|---:|
| idle, 2.47 Mpx | 3.12 | 0.23 | 1.76 + 0.99 | **21.4** | 15.02 (`gpu.acquire WAIT`) | 22.27 |
| idle, 0.15 Mpx | 2.73 | 0.19 | 1.39 + 0.98 | **6.1** | 2.52 | 8.29 |
| idle, 0.15 Mpx + tier/disable | 2.65 | 0.19 | 1.00 + 0.37 | **4.5** | 3.77 | 8.40 |
| **timeline click** (2.47 Mpx) | 3.45 | **2,526.0** | 2.05 + 1.35 | 26.5 | 19.70 | **2,556.7** |
| **timeline drag frame** (2.06 Mpx) | 3.42 | **2,387.3** | 2.36 | 53.5 | — | **2,395.0** |
| **star a hero** (2.47 Mpx) | **282.7** | 0.34 | **1,147.6** | 22.2 | 16.18 | **1,463.8** |

To answer the brief's question in its own form: a timeline click is **CPU 2,530 / GPU 27 / latency
2,557**. A hero star is **CPU 1,430 / GPU 22 / latency 1,464**. It is not 20/20/150; it is one CPU
call holding the thread.

`gpu.acquire WAIT` is the only genuine synchronisation point on the interactive path, it is named as
a wait, and it behaves exactly as vsync should: 15.0 ms median at 2.47 Mpx where the GPU frame is
21.4 ms, 2.5 ms at 0.15 Mpx where it is 6.1 ms. **It never appears on an expensive frame's critical
path** — on click frames it is 19.7 ms out of 2,557. Phase 1's preliminary reading (§"Synchronisation
points": "this does not look like a category-G problem") is confirmed by measurement.

---

## 5. Playhead / scrubbing trace

The complete path a ruler press takes, with the measured cost of each step:

```
SDL_EVENT_MOUSE_BUTTON_DOWN on the Sequence ruler
  |
  pollEvents #2 -> Application::handleInputEvent -> imgui_->processEvent      0.02 ms
  |
  engine_->update(time)     the whole pipeline, at the OLD playhead position  3.45 ms
  |
  ui.build: imgui_->newFrame(); panel_->draw()
     -> SequencePanel::draw
        -> lane == StripLane::Ruler, drag_ = Drag::Playhead
        -> engine.seekSeconds(clamp(snap(mouseTime), 0, duration))     <== 171 .. 3,708 ms
             transport_.seek(seconds)                                        ~0 ms
             player_->seekSeconds(target)        lock-free atomics           ~0 ms
             offlineFrameCursor_ = 0             (dead in Live mode)          0 ms
             modulator_.resetState(), sources_.reset(), music_.reset()       ~0 ms
             director().reset(...)                                    0.005 ms
             clearAimFollowState(), clearCameraEventState()                  ~0 ms
             entityWorld().seek(seconds, ..., 1/60, 90.0, cull)   <== 165 .. 3,708 ms
                 reset() every entity to its authored state
                 for 60 x min(target, 90) steps:
                     for each of 23 entities:
                         every behaviour->update(...)          17-30 us each
             rig.reseedAfterDiscontinuity() for every skinned rig            ~0 ms
             sequenceEvents_.reset(seconds)                                  ~0 ms
             timelineClock_.seconds = seconds
        -> the panel draws the playhead at the new second
  |  <== input->ui.build ms measured here: 175 .. 3,712 ms
  render.record / imgui.record / submit / pick / present                  ~5 ms
  |  <== input oldest->pres measured here: 170 .. 5,014 ms
  |
  next frame: transport discontinuityRevision changed -> resetTemporalHistory()
              engine.update 3.41 ms, 0 flattens, 0 extra procedurals, analysis catch-up 0.000 ms
```

`engine.seek ms − entity re-sim ms = 0.025 … 0.046 ms` across every seek measured. Everything in that
trace except `EntityWorld::seek` is free.

---

## 6. World-evaluation trigger analysis

What changing the timeline position actually causes, measured rather than assumed:

| link in the chain | does it execute on a seek? | cost |
|---|---|---|
| audio seek | yes | lock-free atomics, unmeasurable |
| analysis-cursor reset + catch-up re-walk | **the reset yes, the re-walk never** (Live mode does not reach that branch) | 0.000 ms |
| timeline clock | yes, set inside `seekSeconds` | ~0 |
| modulator / source / music state reset | yes | ~0 |
| director + camera-hold + camera-event reset | yes | 0.005 ms |
| **entity-world re-simulation from `target − 90 s`** | **yes, every time** | **165 … 3,708 ms** |
| skinned-rig discontinuity reseed | yes | ~0 |
| sequence-event rebase | yes | ~0 |
| timeline automation, modulator routes, behaviour | next frame's `update`, as every frame | 0.19 ms |
| parameter updates → scene derivation | next frame's `update`, as every frame | 2.60 ms |
| **composition flatten** | **NO** — 0 flattens over 97 measured seeks | — |
| procedural regeneration | **NO** — `# procedural regen` stays at its idle 4 | — |
| GPU mesh / texture re-upload | **NO** — `# meshes uploaded` and `# textures uploaded` stay 0 | — |
| TAA history reset | yes, the frame after | costs the following frames' quality, not their time |

**Answer to the brief's most important question in §8: no, moving the playhead does not trigger a
composition flatten.** The expensive path is `EntityWorld::seek`, and it is expensive for a reason
the code states plainly (ADR-093/ADR-091): the seeked second must be a pure function of the second,
so the entities are re-integrated from scratch at a fixed step rather than left where the playhead
walked them. That guarantee is correct. Its implementation has no cache, no incremental step and no
bound other than the 90-second window.

By contrast, a **structural edit** (the star) triggers, in one frame:
`setHeroes` → `dirty_ = true` → next `controller_->update()` → `Composition::rebuild()` (281 ms,
80 nodes → 256 entities, 860 meshes, 64 procedurals) → `++scene_.meshVersion; ++scene_.textureVersion`
→ next `renderer_->render()` → `uploadMeshes` re-creating 860 buffers (14 ms) and `uploadTextures`
re-creating 44 textures (**1,125 ms**).

**There are two all-or-nothing invalidations in series, and the second one costs four times the
first.** `Composition::dirty_` is the one the source comments and Phase 1 name. `Scene::meshVersion`
and `Scene::textureVersion` are the other, and nothing in the repository's notes mentions them as a
cost.

---

## 7. Frame backlog / scheduling analysis

**Obsolete work is processed, and here is exactly where.** During a ruler drag the panel calls
`engine.seekSeconds` once per frame from `Drag::Playhead` (`sequence_panel.cpp:1476`). Over the
`strip` arm's 52-frame drag, `# seeks/frame` was **1 on every one of the 52 frames** and the arm
spent **132.4 s**. Requests A→B→C→D are each evaluated in full; there is no "latest requested state
wins" anywhere. `Composition::dirty_` coalesces *within* a frame, and nothing coalesces *across*
frames — the same conclusion Phase 1 reached structurally, now with the count to support it.

**No work is ever cancelled.** Confirmed by inspection in Phase 1 and consistent with every counter
here: the second seek of a drag does not start until the first has finished, because they are the
same thread and the same call stack.

**Whether an expensive frame makes *input* wait behind it: structurally yes, and I could not measure
the magnitude.** `pollEvents` is called exactly twice per frame and both calls precede `ui.build`, so
during the 2.5 s a seek holds the thread nothing drains the SDL queue; events arriving in that window
wait in it. What I cannot do is put a number on it, because **a scripted arm cannot produce a
backlog**: `UiScript::step` mints its events immediately before the poll that consumes them
(deliberately — see the comment at `application.cpp:3048`), so their age at poll time is ~0 by
construction. `# input events` was 1–3 per frame in every run, and `input oldest->pres` ran only
3.3–5.2 ms behind `input->ui.build ms`, which says the synthetic batch was never stale — it does not
say a device's would not be. Stated plainly rather than estimated.

---

## 8. UI responsiveness analysis

Eight interactions that should not require world processing, interleaved in one process, 216 measured
frames each, 2.47 Mpx canvas:

| arm | frame min \| median | worst frame | `input->ui.build` med | flattens | seeks |
|---|---|---:|---:|---:|---:|
| `hover` (control) | 7.52 \| 23.96 | 645.0 † | 4.62 | 0 | 0 |
| `panels` open/close | 16.52 \| 26.80 | 35.8 | — | 0 | 0 |
| `sliders` (a property held and dragged) | 22.37 \| 24.03 | 29.2 | — | 0 | 0 |
| `select` | 21.57 \| 23.75 | 29.3 | — | 0 | 0 |
| `tabs` | 21.56 \| 23.73 | 25.6 | — | 0 | 0 |
| `camera` orbit | 22.10 \| 23.52 | 26.2 | 4.32 | 0 | 0 |
| `gizmo` drag | 22.07 \| 23.43 | 25.0 | 4.02 | 0 | 0 |
| `box` drag | 21.82 \| 23.51 | 25.3 | 4.18 | 0 | 0 |

† the first block's warm-up, before the sky IBL and the first procedural pass have settled; it is in
the settling window of every other arm and is left in the control's numbers rather than trimmed.

**Every one of these responds before any world processing, because there is none to wait for.** The
UI is not blocked by world evaluation in general. It is blocked by world evaluation in exactly the
two cases where a widget's own draw call reaches into the world: the Sequence panel calling
`seekSeconds`, and the Objects list's star calling `setNodesHero`.

The `sliders` arm is worth naming separately because the brief asks about "changing a simple UI-only
setting": a parameter held and written every frame, two parameters per frame, cycling the whole
exposed set, costs the idle frame. Parameter writes are not a responsiveness problem.

---

## 9. Rendering sensitivity

Three conditions, identical arms, identical procedure, one process each (a cross-process comparison
with a ~3 ms noise floor per ADR-150 — which is two orders of magnitude below the effects being
tested, and is called out where it is not).

**The arms are shown to have changed the render before their null result is read (ADR-182):**

| condition | canvas | draw calls | `render.record` med | `gpu.submit` med | **GPU frame med** |
|---|---|---:|---:|---:|---:|
| **A** normal | 1914×1288 = 2.47 Mpx | 90 | 1.76 | 0.99 | **21.4 ms** |
| **B** `--canvas-scale 0.25` | 479×322 = 0.15 Mpx | 94 | 1.39 | 0.98 | **6.1 ms** |
| **C** B + `--tier preview` + `--disable shadows,ao,volume,post,water,particles` | 479×322 | 81 | 1.00 | 0.37 | **4.5 ms** |

A 16× cut in pixels and six passes removed: GPU frame −79%, `gpu.submit` −63%, `render.record` −43%.
The lever moves.

**What it does to interaction latency:**

| | A (2.47 Mpx) | B (0.15 Mpx) | C (0.15 Mpx, minimal) | A → C |
|---|---:|---:|---:|---:|
| idle frame, median | 22.27 | 8.29 | 8.40 | **−62%** |
| idle `input->ui.build`, min | 2.99 | 2.71 | 2.75 | −8% |
| **timeline click `engine.seek`, min** | **166.44** | **165.42** | **165.13** | **−0.8%** |
| **star, frame total, min** | **1,464.7** | **1,443.5** | **1,440.0** | **−1.7%** |

**Reducing the rendering workload buys a 2.7× better idle frame and nothing at all for either
pathological interaction.** That is the falsifiable prediction Phase 1 wrote down under "Output
Preview experiment" — *"a smaller render target reduces GPU work and CPU render preparation but does
not materially improve star/seek responsiveness"* — and it survived.

Two caveats stated rather than buried. First, the 2.47 Mpx idle frame is genuinely GPU-bound
(GPU 21.4 ms against a 16.7 ms vsync budget, `gpu.acquire WAIT` 15.0 ms median), so **there is a real
rendering problem in this project at full canvas** — it is a frame-rate problem, not a latency
problem, and it is a different investigation. Second, `--canvas-scale` **is silently ignored when the
canvas is in `outputFrame`/`outputPreview` mode** (ADR-246 deliberately makes the preview's own
quality rung the lever). The first attempt at this comparison ran with the preview framed and
produced a "reduced" arm rendering 2.06 Mpx; it was caught by the exit log added for exactly this
purpose and the runs were repeated with `--preview-mode workspace`. Without that log this section
would have reported a clean null result about an arm that did nothing.

---

## 10. Root causes

### Confirmed

**RC1 — `EntityWorld::seek` re-simulates the world from up to 90 seconds back, synchronously, inside
the panel's draw, on every seek.** 165–3,708 ms per seek; 99.999% of `seekSeconds`; linear in the
seeked second; once per frame of a drag. This is the cause of "clicking the timeline feels slow",
"scrubbing feels slow", and the beachball.
*Evidence:* `entity re-sim ms`, `# resim ksteps`, `engine.seek ms`, `ui.build`, `input->ui.build ms`
over 97 seeks in four runs; the step-count/cost table in §1.

**RC2 — `Composition::rebuild()` invalidates the renderer's entire texture set, and that costs more
than the flatten it belongs to.** 1,125 ms for 44 textures against 281 ms for the flatten.
*Evidence:* `texture upload ms` / `# textures uploaded` / `rr.uploads ms` on 15 flatten frames;
`composition.cpp:4136` and `scene_renderer.cpp:1898`.

**RC3 — `Composition::dirty_` has no granularity.** Phase 1's finding, confirmed live at 275–288 ms
per star, `# scene flattens` 1 per edit.

**RC4 — Nothing coalesces or cancels across frames.** 52 seeks for one drag gesture; 51 of them
obsolete.

### Probable (consistent with everything measured, not independently isolated)

**RC5 — The serialised loop turns any expensive call into an input freeze.** `pollEvents` is called
twice per frame, both before `ui.build`; a 2.5 s call in `ui.build` is 2.5 s in which no event is
drained. Structurally certain; the *magnitude* for a real device stream is unmeasured (§7).

**RC6 — The 90-second cap is why the cost saturates rather than growing without bound**, and it is a
literal in `engine.cpp:2265`, not a policy anything can tune.

### Ruled out

* **The scene flatten as the cause of playhead lag.** 0 flattens over 97 seeks.
* **The analysis-cursor re-walk (Phase 1 finding 3).** Never executes in the interactive editor.
* **GPU synchronisation.** `gpu.acquire WAIT` is 19.7 ms of a 2,557 ms click frame and behaves
  exactly as vsync should. No readback stalls the CPU.
* **Rendering cost as a driver of interaction latency.** −79% GPU buys −0.8% click latency.
* **A general UI-thread problem.** Eight non-world interactions all cost the idle frame.
* **Audio readiness.** Everything in `seekSeconds` other than the re-simulation totals 0.025–0.046 ms.
* **Allocation churn.** `# kallocs/frame` reads 0 in this build; not measured, and nothing points at
  it. (Requires `-DAVGEN_ALLOC_COUNTERS=ON`.)
* **The mesh re-upload** as the star's dominant cost — 14 ms, refuted by a probe built to test it.

### Unresolved

* **Why a single texture upload costs 25.6 ms.** 44 textures, 1,125 ms. Mipmap generation is the
  obvious candidate and it is not measured. One level deeper than this phase went.
* **Why the per-entity-step cost ranges 17–30 µs.** The spread does not track the load average
  cleanly. Behaviour mix along the timeline is the obvious candidate.
* **How stale a real device's input gets during an expensive frame.** Not measurable with a scripted
  arm (§7).
* **Whether the 2.47 Mpx GPU-bound idle frame matters to the complaint.** It is a real 22 ms frame
  and a separate problem.

---

## 11. Architectural implications

1. **The two pathological interactions have nothing in common except the thread they run on.** A seek
   is an O(playhead position) integration with no caching; a structural edit is an all-or-nothing
   invalidation cascade. A single architectural answer — "make the world async", "add a dependency
   graph" — addresses neither directly and would be adopted on intuition rather than on evidence.

2. **The largest single number in the editor is not in `Composition` at all.** It is
   `SceneRenderer::uploadTextures`, reached through a version counter that `Composition::rebuild()`
   bumps unconditionally. Any plan that ends at the flatten leaves 77% of the star's latency in
   place.

3. **The derived-copy rule is not the obstacle here.** The playhead's *requested* position and its
   *evaluated* consequence are already separable — `Transport` holds the position and `timelineClock_`
   is set at the end of `seekSeconds`. What is not separable today is that the panel calls the
   evaluation itself, from inside its own draw.

4. **The repository has already solved this shape twice, and both precedents are narrow.**
   `TerrainProducts` is a content-hash cache over one sub-product; `interactiveRebuildBudgetMs_` is a
   deferral with an honest "this is a few frames behind" signal. Neither is a framework. Both are the
   right size for the problems above.

5. **An interactive rendering tier is not indicated by any measurement in this document** as a
   latency fix. It is indicated as a *frame-rate* fix for a 2.47 Mpx canvas, which is a different
   complaint.

---

## 12. Candidate solutions

Described, not implemented. Each is tied to a measured number or explicitly labelled speculative.

### S1 — Bound or cache the entity re-simulation

* **Problem:** RC1. **Evidence:** the strongest in this document — 165–3,708 ms per seek, linear in
  the seeked second, 99.999% of `seekSeconds`, 52 per drag.
* **Shape:** the guarantee (ADR-091/093: the seeked second is a pure function of the second) does not
  require integrating from `target − 90 s` *every time*. Keyframing the entity state at intervals and
  integrating forward from the nearest key below the target reproduces the same state for the same
  input with bounded work. A monotonic forward seek could integrate from where it already is.
* **Subsystem:** `EntityWorld`, `Engine::seekSeconds`. **Scope:** moderate and contained — one class.
* **Risks:** the re-simulation's determinism is the whole point; per-entity RNG and behaviour state
  must be captured in a key exactly, or a scrub stops being reproducible. Needs a determinism test
  that scrubs to the same second by two different routes and compares.
* **Success criterion:** `entity re-sim ms` becomes independent of the seeked second; the `click`
  arm's `input->ui.build ms` falls below 50 ms at every position; `strip`'s 52-frame drag completes
  in under 5 s.

### S2 — Coalesce seeks to the latest requested position

* **Problem:** RC4. **Evidence:** `# seeks/frame` = 1 on each of 52 drag frames; 51 obsolete.
* **Shape:** the panel records the requested second; the frame loop performs at most one seek per
  frame, to the newest request. Without S1 this changes nothing during a drag (there is already at
  most one per frame) — **it only pays off once a seek is cheap enough that several can queue, or if
  a seek is moved off the draw call.** Recorded here so it is not mistaken for an independent win.
* **Risks:** interacts with scrub determinism only if requests are dropped *between* frames, which is
  exactly what it is for.
* **Success criterion:** `# seeks/frame` ≤ 1 and total seeks over a gesture < frames in the gesture.

### S3 — Stop invalidating textures that did not change

* **Problem:** RC2. **Evidence:** 1,125 ms of a 1,454 ms star, measured three ways (`rr.uploads ms`,
  `texture upload ms`, `# textures uploaded`).
* **Shape:** `Composition::rebuild()` bumps `scene_.textureVersion` unconditionally; a flatten that
  did not add, remove or change a texture need not. The cheapest form is a content check over the
  texture list before the bump; the next cheapest is per-texture identity in `SceneRenderer`, so an
  unchanged texture is kept rather than re-uploaded.
* **Subsystem:** `Composition::rebuild`, `Scene`, `SceneRenderer::uploadTextures`.
* **Scope:** small for the first form. **Risks:** a missed invalidation shows the previous world's
  textures — the failure is visible, immediate and testable, which is the good kind.
* **Success criterion:** `# textures uploaded` is 0 on a star's flatten frame; the star's frame total
  falls from 1,454 ms to ≈ 330 ms.

### S4 — Granular invalidation for the flatten (Phase 1's C2, unchanged)

* **Problem:** RC3. **Evidence:** 275–288 ms, confirmed live.
* **Scope:** large; touches every `dirty_` setter. **Risk:** high — a missed invalidation is a world
  that silently disagrees with itself, the exact failure `setHeroes` chose a flatten to avoid.
* **Note the new arithmetic:** with S3 in place this is 281 ms of a 330 ms interaction; without S3 it
  is 281 ms of 1,454 ms. **S3 changes the value of S4 by a factor of four and should be settled
  first.**
* **Success criterion:** `# scene flattens` 0 for a star; `upd.controller ms` at its idle 2.6 ms.

### S5 — Hero declaration without a flatten (Phase 1's C3, unchanged)

* A strict subset of S4, narrow, and a good first probe of it. Same caveat as S4: its value depends
  on S3.

### S6 — Move the seek out of `panel_->draw()`

* **Problem:** RC5. **Evidence:** structural (the loop's shape) plus the 3.3–5.2 ms gap between
  `input->ui.build` and present on click frames, which shows the UI and the world are decided in the
  same breath.
* **Shape:** the panel sets a requested position; the frame loop performs the seek in a known place,
  before `engine_->update`. This does not make anything faster; it makes S2 possible, makes the cost
  attributable to a phase rather than to `ui.build`, and lets the playhead be drawn from the
  *request* while the world is still evaluating.
* **Scope:** small. **Risk:** low, but it creates a frame in which the playhead and the world
  disagree — which must then be shown honestly, as `proceduralsAwaitingRebuild()` already does for
  geometry.
* **Success criterion:** `ui.build` on a click frame returns to ~0.3 ms with the cost moved to a
  named phase.

### S7 — Interactive render tier

* **Problem:** the 2.47 Mpx GPU-bound idle frame, not interaction latency.
* **Evidence:** GPU 21.4 ms vs 16.7 ms vsync, `gpu.acquire WAIT` 15.0 ms; and −79% GPU buys −0.8%
  click latency.
* **Explicitly NOT a fix for the reported symptom.** Recorded so that it is not adopted as one.

### S8 — Asynchronous world evaluation

* **Speculative.** No measurement in this document requires it. Both confirmed causes are single
  synchronous calls that are *too expensive*, not work that needs to be elsewhere; S1 and S3 make
  them cheap on the thread they are already on. Listed to record that it was considered and not
  supported by evidence.

### Mapping the brief's §12 concepts to measured problems

| concept | measured problem it addresses | evidence level |
|---|---|---|
| cached / incremental evaluation | RC1 (the re-simulation) | **strong — the largest number here** |
| granular / per-product invalidation | RC2 (textures), RC3 (the flatten) | **strong** |
| latest-state-wins scheduling | RC4 | strong as a count, but worthless until S1 |
| cancellation | RC4 | weak — nothing to cancel while the call is synchronous |
| immediate UI state / decoupled UI+world state | RC5 | moderate — enabling, not curative |
| deferred evaluation | RC1 partially | moderate |
| dirty dependency graph | RC2/RC3 generalised | weak — the two specific counters are the whole problem today |
| asynchronous evaluation | — | **none** |
| progressive rendering | — | **none** |
| interactive-quality rendering | the 22 ms idle frame, not latency | moderate, different problem |
| frame-result caching | — | **none** |

---

## 13. Recommended next engineering experiments

Smallest first, each falsifiable, each with a number that decides it.

**E1 — Delete the unconditional `++scene_.textureVersion` behind a content check, and re-run the
`star` arm.** One condition in `Composition::rebuild()`. Prediction: `# textures uploaded` goes to 0
on the flatten frame and the star's frame total falls from 1,454 ms to ≈ 330 ms. Cost: under an hour.
**This is the highest ratio of latency removed to risk taken anywhere in this document.** If the
prediction fails, something else is bumping the version and this phase's attribution is wrong.

**E2 — Measure what a single texture upload is spending 25.6 ms on.** One timer inside
`gpu::uploadTexture`, splitting creation, `WriteTexture` and mipmap generation. Decides whether E1 is
the whole answer or whether the upload itself needs work. Cost: an hour.

**E3 — Bound the re-simulation with one keyframe and prove determinism.** The narrowest possible form
of S1: cache the entity state at the last seeked second and, on a *forward* seek, integrate from
there instead of from `target − 90 s`. Prediction: a forward drag's per-frame `entity re-sim ms`
collapses to the per-frame delta (≈ 1/60 s of simulation, sub-millisecond) while a backward seek is
unchanged; the `strip` arm's 132.4 s drag drops by an order of magnitude. The determinism test comes
first and must fail before the change and pass after: scrub to 60 s forwards and backwards, compare
every entity's published locomotion state bit for bit. Cost: a day.

**E4 — Move `seekSeconds` out of `SequencePanel::draw`** into a request honoured once per frame
before `engine_->update`. Prediction: no change to any total, `ui.build` back to 0.3 ms on a click
frame, the cost appearing in a phase of its own. It is a prerequisite for S2 and for ever showing the
playhead ahead of the world. Cost: half a day.

**E5 — Only then, decide between S4 and S5** with the 281 ms flatten breakdown Phase 1 asked for —
*after* E1, because E1 changes whether 281 ms is 19% or 85% of the interaction.

**Do not start the granular-invalidation refactor first.** With the numbers in this document it is
the fourth-best thing to do and the riskiest.

---

## Instrumentation added — TEMPORARY, to be removed

Unlike Phase 1, this phase **did** add instrumentation to `src/`. Everything below is marked
`TEMPORARY (ui-responsiveness phase 2)` in the source, adds no logging to the frame loop, and is read
only by `--profile-cpu` / `--profile-csv` phases. Nothing in it is read by anything that decides what
a frame does.

| file | what was added | remove when |
|---|---|---|
| `src/core/phase2_probe.hpp` | **new file.** `probe2::Frame`, a per-frame struct of counters and millisecond sinks, plus a scope guard. Header-only `inline` so `avgen_core`, `avgen_gpu` and `avgen_app` share one instance. | phase 3 closes |
| `src/core/phase_profiler.hpp` | `kMaxPhases` 40 → 56. The existing 39 phases plus this phase's 22 overflow the table, and `phase()` returns −1 silently when it is full — which would have produced a table of zeros. | with the phases below |
| `src/app/engine.cpp` | timers around `seekSeconds`, the director reset and the analysis catch-up loop; stage timers at the five boundaries `Engine::update`'s allocation marks already use | phase 3 closes |
| `src/entity/entity.cpp` | a timer and a step/body count in `EntityWorld::seek` | phase 3 closes |
| `src/rendering/scene_renderer.cpp` | timers and counts in `uploadMeshes`, `uploadTextures` and around `updateEnvironment` | phase 3 closes |
| `src/app/application.cpp` | 22 profiler phases; the input timestamps widened to presses and to the *oldest* event of the batch; `input->ui.build ms`; the renderer's `CpuFrameBreakdown` surfaced; one `phase2:` log line at exit reporting the final canvas size, triangles and draw calls | phase 3 closes |

**Two additions are permanent and are not diagnostic hacks** — they are script arms in the same shape
as the existing eleven, and Phase 1 explicitly anticipated the first:

* **`star`** — toggles a hero on and off every 40 frames through `ui::setNodesHero`, the function the
  star button calls. Reports the hero count before and after and says *"THIS ARM MEASURED NOTHING"*
  if the list did not change.
* **`click`** — a discrete press and release on the ruler every 15 frames, at a new second each time,
  with **no pointer motion between clicks** so `input->ui.build ms` samples only the frames that
  carried a click. It sweeps `u = 0.02 … 0.86` across the ruler, because the cost of a seek is a
  function of where you clicked and an arm that only ever clicks in the second half measures one
  point of a straight line without being able to see that it is one. Reports each click's position
  and resulting second, and says *"THIS ARM MEASURED NOTHING"* if the playhead did not move.

### On probes that cannot fail (ADR-182)

Four things in this phase were caught by a guard rather than believed:

1. **The first run's canvas was 168×94 px — 0.02 Mpx.** At `--size 1280x800` the docked layout
   squeezed the viewport almost to nothing, and the whole rendering-sensitivity comparison would have
   been run on a postage stamp. Caught by the `phase2:` exit line, which was added because the
   existing canvas log prints at frame 60, before a layout has settled. Everything was re-run at
   1600×1000 (0.43 → 2.47 Mpx).
2. **`--canvas-scale` is silently ignored in `outputFrame`/`outputPreview` mode** (ADR-246). The first
   "reduced rendering" arm rendered 2.06 Mpx and would have reported that reducing the render changes
   nothing — true, and for the wrong reason. Caught by the same exit line. Re-run with
   `--preview-mode workspace`.
3. **The mesh-upload hypothesis was refuted by the probe built to confirm it.** 860 buffers, 14 ms.
   Without the count the 1.15 s would have been filed against the wrong function.
4. **`analysis.catchup ms` reads 0.000, and that is a statement about a branch, not a broken
   instrument.** The same `probe2::Add` type produced every non-zero number in this document. The zero
   is `Engine::update`'s `if (mode_ == EngineMode::Live) … else if (track_ && !track_->empty())`: the
   catch-up loop is in the `else if`, and the windowed editor never leaves the `if`.

**One limitation is not caught by any guard and is stated instead:** a scripted arm cannot produce an
input backlog, because it mints its events immediately before the poll that drains them. §7's
magnitude is therefore unmeasured rather than estimated.

### Conditions

macOS, Apple Silicon, Dawn/Metal. Release build. Window 1600×1000 points at backing scale 2.0.
`tools/gpu-lock.sh` held for every run. `pgrep avgen` clean except the main session's CPU-only
`avgen_tests`, noted where it overlapped. One-minute load average 2.6–6.8 across the reported runs
(13.1 at the start of the session, before the reported runs). Arms interleaved inside one process
(ADR-150); minima reported alongside medians (ADR-170); the three render conditions are three
processes and say so.

### Answers to Phase 1's open questions

3. **What is the 285 ms made of?** Still unanswered, and it is now the *fourth* question rather than
   the first — see E5. The 285 ms is real and confirmed; it is 19% of the interaction it belongs to.
4. **Does a drag cost one flatten or one per frame?** Neither: a *ruler* drag costs **zero** flattens
   and one full entity re-simulation per frame.
5. **Does scrubbing issue one seek per frame?** Yes. Exactly one, on each of 52 drag frames.
6. **Does reduced render workload move the latency at all?** −79% GPU moves the click floor by 0.8%
   and the star floor by 1.7%.
7. **Is `select` actually cheap?** Yes — identical to the idle editor.

---

## Appendix — the thirteen questions, answered

1. **Why does clicking the timeline feel slow?** Because `SequencePanel::draw` calls
   `Engine::seekSeconds` synchronously, and that call re-simulates every entity in the world at a
   fixed 1/60 s step from up to 90 seconds before the clicked second. 23 entities × up to 5,400
   steps × 17–30 µs = **166 ms to 5.0 s of blocked main thread, per click.**
2. **How many ms between input and playhead UI acknowledgment?** `input->ui.build ms` on click
   frames: **min 175.2, median 2,529.4, max 5,009.3 ms.** The control (`hover`) is **3.0 ms**.
3. **How many ms before the corresponding world state is evaluated?** The same number — the world is
   evaluated *inside* the call that draws the playhead. There is no interval in which one has moved
   and the other has not.
4. **How many ms rendering?** GPU 21.4 ms median at 2.47 Mpx, 6.1 at 0.15 Mpx, 4.5 minimal. On a
   click frame, 26.5 ms of 2,557.
5. **Is the UI blocked by world evaluation?** For a timeline seek and a structural edit, yes, on the
   same thread and in the same call stack. For everything else measured — hover, panels, sliders,
   select, tabs, camera, gizmo, box — no.
6. **Does playhead movement trigger scene flattening?** **No.** 0 flattens over 97 measured seeks.
7. **Are obsolete timeline states processed unnecessarily?** **Yes.** A 52-frame ruler drag issued
   52 full seeks — one per frame, none coalesced, 51 obsolete — and took **132.4 seconds**.
8. **Does reducing rendering workload materially improve interaction latency?** **No.** −79% GPU
   changes the click floor by 0.8% and the star floor by 1.7%. It does improve the idle frame 2.7×.
9. **Are unrelated UI interactions affected by the same main-thread architecture?** **No.** Eight of
   them cost the idle frame exactly.
10. **Which parts would be solved by asynchronous/deferred evaluation?** None that are measured.
    Both confirmed causes are synchronous calls that are too expensive, not work in the wrong place.
11. **Which parts require incremental flattening/invalidation?** The structural edit, and the
    *renderer's* invalidation before the composition's: `scene_.textureVersion` costs 1,125 ms and
    `Composition::dirty_` costs 281.
12. **Which parts would benefit from an interactive rendering tier?** Only the 2.47 Mpx idle frame,
    which is GPU-bound at 21.4 ms against a 16.7 ms budget. Not the reported symptom.
13. **What is the smallest architectural experiment that validates the strongest hypothesis?**
    Make `Composition::rebuild()` stop bumping `scene_.textureVersion` when no texture changed, and
    re-run the `star` arm. One condition; predicts 1,454 ms → ≈ 330 ms.
