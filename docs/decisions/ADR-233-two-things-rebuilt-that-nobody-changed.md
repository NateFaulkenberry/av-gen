# ADR-233: Two things were rebuilt every frame that nobody had changed

**Status:** accepted (2026-09-15)
**Context:** "continue UI work — it's still pretty sluggish", after ADR-231
**Extends** ADR-084 (the editor's frame, the interactive rebuild budget), ADR-231 (the first UI pass),
ADR-170 (the GPU lock does not establish exclusivity), ADR-182 (a diagnostic arm that cannot fail)

## Context

ADR-231 measured and fixed **loading**. The complaint that followed is about **interaction**, and it
says so in its own words — *still* sluggish, after a load that got a third faster. ADR-231 was
explicit about the gap it left: it offered no interactive frame-time measurement at all, because the
machine sat at load average 8–34 throughout and this repository forbids comparing a frame time from
one process against another.

So the instrument had to come first, and it had to be an instrument that a contended machine cannot
lie to.

## Decisions

### 1. Interactions are compared as blocks of one process, never as two runs

`--ui-ab <arm>[:<arm>...]` cycles the `--ui-script` arms in blocks inside a single session and
reports each arm's distribution side by side. This is `--ab`'s rule (ADR-113, ADR-170) applied to the
main thread's frame: the columns were measured against each other, seconds apart, under the same
contention and the same thermal state, so the difference between two of them is a difference.

`core::PhaseProfiler` grew a group per frame to carry it. Three details are the decision:

- **`kNoGroup` is not a group.** The first twelve frames after a switch are labelled and excluded.
  An arm inherits its predecessor's deferral timers, its warm pipelines and whatever its pointer was
  doing; a 120-frame block carrying twelve of somebody else's frames is a 10% error in one
  direction, which is larger than most of the differences being looked for.
- **A block length must be a multiple of sixty.** The pointer arms repeat a gesture on a 60-frame
  cycle, and a 90-frame block hands one of them a cycle chopped in half. Measured: at 90 the gizmo
  arm went three whole blocks without completing a single press. The default is 120 and the flag
  says why.
- **An empty group reports `samples == 0`, not `0.000 ms`**, because a column of zeros that means
  "this arm never ran" and a column of zeros that means "this arm was free" look identical.

An arm may carry `+legacy` or `+skynow`, each restoring one of the fixes below for the length of its
block. That is what gives each fix a *before* arm in the same process rather than a claim about two
binaries that no longer both exist — the same reason `AVGEN_SCATTER_WORKERS` and
`AVGEN_NO_TERRAIN_CACHE` exist.

### 2. Three structural counters, because a millisecond on this machine is a statement about the machine

`# procedural regen`, `# scene flattens`, `# IBL builds`, per frame, in the same table as the
timings. ADR-170 is the precedent: a phantom 3.8× regression was nearly filed off timings, and what
indicted the machine was that the structural counters moved 3.6% while the timings moved 280%. "None
where there were twenty-two" survives a load average of forty-four. A millisecond delta does not.

The procedural counter is incremented **inside `ProceduralGeometry::rebuild`**, not at a call site.
That is the whole reason it found anything: there were two call sites per object per frame, and a
counter at either one would have reported half the truth.

### 3. A procedural object's parameters are applied in one place and generated in another

**The finding: an editor that nobody was touching regenerated twenty-two procedural clouds per
frame, for ever.** Eleven procedural nodes; exactly twice each.

`Composition::applyParameters` did two things in order. It copied the parameter finals into the
flattened object — and `applyProceduralParameters` ended by calling `rebuild()` against an **empty**
`GenerationContext` and storing *that* hash. Then it folded the node's world transform into the
distribution transform. Afterwards `rebuildProcedurals()` asked for a rebuild against the **real**
context, whose hash differed for two independent reasons, so it regenerated and stored its own hash
— and next frame the first step disagreed right back.

Neither call site looks wrong. The comment beside the second one has said for a long time that
generation happens "once every object and spline has its finals", and it was simply not true.

So `applyProceduralParameterValues` applies and generates nothing, and `applyProceduralParameters`
— the three-argument function the tests and every scene-free caller use — is now that plus the
rebuild. The composition calls the first. Generation happens once, with the real context, under
ADR-084's budget.

**Two things this also fixes rather than merely speeds up.** The unbudgeted rebuild ran *whatever
the budget said*, so the ADR-084 deferral was partly a fiction: an expensive object it declined to
regenerate had already been regenerated, against a placeholder context. And a spline-distributed or
`PrimitiveKind::Procedural`-sourced object was generated against an empty context every frame before
being generated correctly.

**Why it survived two performance passes:** the fold is a no-op at identity, so an object at the
world origin never showed it. Everything a person places is somewhere else. The regression test puts
its node at (3, 1, −2) for that reason and says so.

### 4. The procedural sky's IBL waits for the drag, like everything else expensive

**The finding: dragging a sky or moonlight slider rebuilt the whole image-based lighting chain
inside the frame, on 46% of the frames of the drag, at 40–70 ms each.**

`EnvironmentProcessor::processSky`'s own header says it "blocks like `process` does; call it when
the sky's hash changes, not per frame". A drag on any of the ten sky parameters changes the hash
every frame — and so does a drag on the key light, because `resolveSky` takes the sun from it. Moon
azimuth is not an exotic control; it is one of the first things anybody touches.

This is precisely the class ADR-084 §9.2 named and solved for procedural geometry, in a subsystem
its fix never reached. So it gets the same policy and the *same function*: `advanceRebuildDeferral`
moved to `scene/rebuild_deferral.hpp`, a header light enough for the renderer to include, with
`Composition::ProceduralRebuildState` kept as an alias so nothing that already named it had to
change. Nothing in that arithmetic was ever about procedural geometry; it is about any expensive
derived thing whose inputs a person is dragging.

`SceneRenderer::setInteractiveEnvironmentBudget(ms)`, **zero by default**, set only in
`Application::runLive`. `runHeadless` never calls it, so an offline render rebuilds whenever the
hash moves, exactly as it always did. The first build of a scene is never deferred: there is nothing
to be behind.

**And the canvas says so.** `environmentAwaitingRebuild()` feeds the activity indicator ADR-231
built, which now reads "Updating the sky" alongside "Updating geometry". ADR-231's rule holds: this
is not a spinner in place of a fix. The frame really is faster; the indicator exists because a
picture that is deliberately three frames stale must not be silently stale.

### 5. What was measured and deliberately not changed

**The UI is not the problem, again.** Across nine arms — idle, sliders, scrub, box, gizmo, camera,
hover, panels, strip — `ui.build` never exceeded **0.28 ms**, against a frame budget of 8.33. The
whole Dear ImGui pass, every panel, the viewport editor, the gizmo overlay and the sequencer strip
together are under 3.5% of a frame. An audit of the panels found real and avoidable per-frame work
— `influencesOf` rescanning every route, track, cue, state and entity per parameter row;
`drawParameters` rebuilding its group map every frame; `WorldEditPanel::drawObjects` calling the
linear `findNode` inside the loop it was written to make O(n) — and **none of it was touched**,
because the total forecloses the question. That audit is recorded in
`docs/application-performance.md` §18 so the next person does not have to do it again, and so that
it can be acted on if a scene ever makes it matter.

**A slider drag causes zero flattens.** `# scene flattens` reads 0 in every arm of every run. The
derived-copy rule is intact and was not touched: a parameter is authoritative and `Scene` is a
per-frame derivation. One cold flatten of `glowmere-stylized` costs **942 ms**, which is why that
zero matters.

**The frame is GPU-paced on a real world.** `gpu.acquire WAIT` is 5.7–14.8 ms of a 8.0–16.7 ms
frame depending on the display mode in force. That is the renderer's subject, not this one's; what
this pass is responsible for is that the main thread's share is now ~1.6 ms and that nothing in it
is work nobody asked for.

## What was measured

All interleaved inside one process, `glowmere-stylized.json` at 1440×900, three blocks of 120 frames
per arm, on a machine at load average 6–44 belonging to three agents.

### §3, the procedural double generation — `--ui-ab idle+legacy:idle:sliders+legacy:sliders`

| | idle+legacy | idle | sliders+legacy | sliders |
|---|---|---|---|---|
| `# procedural regen` per frame | 22 | **0** | 22 | **0** |
| `engine.update` median | 0.740 ms | **0.430 ms** | 0.730 ms | **0.423 ms** |
| FRAME median | 16.78 ms | 16.83 ms | 16.76 ms | 16.75 ms |

The frame median does not move, and saying so is the point: on this scene the frame is the GPU's,
and 0.31 ms of main-thread work returned is headroom rather than frames. On a scene whose main
thread is the bottleneck it is 42% of the engine update.

### §4, the sky IBL — `--ui-ab idle:sliders+skynow:sliders`, `AVGEN_SLIDER_FILTER=env/sky`

| | idle | sliders+skynow (before) | sliders (after) |
|---|---|---|---|
| `# IBL builds` per frame, mean | 0 | 0.463 | **0.065** |
| `render.record` mean | 0.98 ms | 26.54 ms | **3.76 ms** |
| `render.record` p95 | 1.03 ms | 77.24 ms | **36.92 ms** |
| FRAME median | 33.12 ms | 40.08 ms | **33.08 ms** |
| FRAME p95 | 50.40 ms | 85.11 ms | **60.18 ms** |
| frames over 50 ms, of 324 | 34 | 147 | **56** |

**A sky drag now costs the same median frame as an idle editor** — 33.08 against 33.12 — where it
cost 40.08 median and 85 at the 95th percentile. The residual tail is the deliberate refresh ceiling
of ADR-084 §9.2, which is a drag that still updates rather than one that freezes.

The same run with `AVGEN_SLIDER_FILTER=lightrig/`: IBL builds per frame 0.204 → **0.080**,
`render.record` mean 11.87 → **4.83 ms**, frames over 50 ms 92 → **66**.

### The filter had to be fixed before any of that was true

`AVGEN_SLIDER_FILTER` advanced its cursor over every parameter and wrote only the matching ones — so
a filter naming the sky wrote a sky parameter on roughly one frame in a thousand, and the arm
measured an editor that was almost always idle while calling itself a sky drag. It now restricts the
*cycle*. Dragging one slider means writing that slider every frame, because that is what a hand on a
slider does.

## Determinism

Neither change may alter what an offline render contains, and neither does.

- §3 is a pure removal of a redundant generation: the same object was generated twice per frame,
  first against a placeholder context and then against the real one, and the second result is what
  every consumer saw both before and after.
- §4 is gated on a budget that is **zero unless `Application::runLive` sets it**. `runHeadless` does
  not call the setter.

**Verified by capture, out of one binary rather than two.** `AVGEN_LEGACY_PROCGEN=1` restores the old
generation path in *any* mode for exactly this purpose, so `--headless --frames 6 --capture` can be
taken both ways from the same build and compared byte for byte:

| scene | legacy | fixed |
|---|---|---|
| `examples/world/terrain.scene.json` | `1d96925b…6bec5` | `1d96925b…6bec5` |
| `examples/world/glowmere-stylized.scene.json` | `9a8562d7…9035f` | `9a8562d7…9035f` |

3,888,016 bytes each. The same two hashes again after §4 landed.

## Consequences

- **A probe caught the arm that could not fail, twice.** `--ui-script gizmo` pressed at
  (−20092, 26181) for three blocks running and reported a perfectly healthy frame time, because the
  arm took the first glTF node and that node stood off past the edge of the frame. It now chooses
  the candidate nearest the middle of the frame and aims with `ui::pickHandle` at the editor's own
  `kHandlePickRadius`, so the press is on the handle by construction; and it prints which handle it
  actually dragged and where the object ended up. When nothing selectable is in frame it says
  **"this arm measured nothing"** rather than reporting a number.
- **The box arm is honest about being partial.** It opens a selection box across the canvas and the
  editor confirms one is open mid-drag, but on `glowmere-stylized` it selects 0 of 16 objects — the
  scene is terrain, procedural clouds and particles, and `nodesInScreenRect` does not offer those.
  It therefore measures the gesture and not the cost of holding a large selection. Recorded, not
  papered over.
- **Nine arms were asked for and eight ran**, silently, because `kMaxGroups` was 8. It is 12 now and
  the truncation is a warning. A run that reports one fewer arm than it was asked for is a
  measurement of something other than the question.
- **A residual, named and not diagnosed:** four of the thirty flattened procedurals regenerate every
  frame in *some* sessions and none in others, on the same binary and the same project. It is not
  audio: `--play` makes no difference. The likely mechanism is an input that genuinely changes every
  frame for those four — in which case the regeneration is correct and the design is what costs —
  and it is written down here rather than explained away, because a measurement that the output
  changed is not a measurement of why (ADR-199).
- **Not done, and why:** the per-frame panel work in §5; incremental invalidation for `dirty_`
  (ADR-084's P1-2, still open and still without granularity); the ~90 linear `findRel` scans per
  procedural node per frame inside `applyProceduralParameters`, which are now the largest identified
  item in `engine.update` and want cached parameter handles the way `ParticleParameters` already
  has; and `ParameterSet::find` allocating a `std::string` on every lookup. All four are recorded in
  `docs/application-performance.md` §18 with file and line.

## Revisit triggers

- If a third subsystem starts rebuilding something expensive inside a drag, it takes
  `scene/rebuild_deferral.hpp` rather than growing a third copy of the arithmetic.
- If the equirectangular environment path (`EnvironmentProcessor::process`) ever becomes reachable
  from a per-frame parameter, it needs §4's treatment too; today it is keyed on texture identity and
  cannot fire during a drag.
- If `ui.build` on any arm crosses 1.0 ms — the §11 budget — §5's audit is the list to work from and
  it is already written.
