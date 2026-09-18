# The Interaction Latency Lab

**Status:** complete. The instrument is built, registered and shown detecting a slowdown it was not
told about (§5.4); the audit is §1-§2; the measurements are §5; the prototype and its before/after
are §5.3; the recommendations are §7 and what could not be measured is §8.
**Subject:** how long after a person acts does the application answer.
**Not the subject:** how long the GPU took to draw a frame once it was asked for, and why a frame
costs what it costs. Those are `docs/renderer-2-architecture.md` and `core::PhaseProfiler`.

---

## 0. What this lab is for, and the one sentence that justifies it

> A frame budget of 14 ms GPU / 8 ms CPU / 3 ms UI can coexist with a 183 ms click-to-response, and
> the frame measurement misses it entirely.

That is why `core::PhaseProfiler` — which is a good instrument, and which found four real defects in
`docs/application-performance.md` — could not answer the complaint that followed it. Its unit is the
frame. A 2,500 ms click is one frame in fifty, and a distribution over frames drowns it. It showed up
there only because somebody added a temporary probe (`src/core/phase2_probe.hpp`, marked
`TEMPORARY ... delete this header when the investigation closes`) and read one phase's maximum.

So the unit here is the **interaction**: one record per thing a person did, seven stamps on it.
`src/core/interaction_latency.hpp` holds the reasoning; the short version is in §4.

**Thresholds.** Nielsen: ≤100 ms feels instantaneous, >1 s breaks flow. The bands this lab reports
against are <50 ms preferred, <100 ms interactive, <250 ms noticed, <1000 ms a serious defect,
≥1000 ms flow-break.

---

## 1. Phase 0 — the audit

### 1.1 The UI surface, inventoried

`src/ui/` is 23,649 lines across 59 files. `control_panel.cpp` (3,960) and `sequence_panel.cpp`
(3,686) are 32% of it between them, and `control_panel.cpp` alone hosts 14 of the 19 panel bodies.

There are **exactly three `ImGui::Begin` call sites** in the whole tree:

| site | window |
|---|---|
| `src/ui/editor_shell.cpp:233` | `"Viewport"` — the docked central canvas |
| `src/ui/control_panel.cpp:592` | generic; the title is the registry `id` of whichever panel is being submitted |
| `src/ui/control_panel.cpp:659` | `"Help"` — a hand-rolled duplicate, because Help must not get the reserved label column |

Nineteen panels are registered in a `constexpr std::array<EditorPanel, 19>` at
`src/ui/editor_layout.cpp:22`, and every one of them is submitted through the wrapper lambda at
`control_panel.cpp:580`.

### 1.2 §29 — what does work while hidden

**This is the audit's first correction to the brief, and it is a correction in the repository's
favour.** §29 asks which UI subsystems do work while hidden, collapsed, offscreen or inactive. For
the nineteen registered panels the answer is **none**. The wrapper is:

```cpp
bool* open = layout_.slot(id);
if (open == nullptr || !*open) {
    return;                      // nothing at all runs for a closed panel
}
...
if (ImGui::Begin(id.data(), open)) { ...; body(); ... }
ImGui::End();
```

Both gates are present: a closed panel costs one pointer lookup, and a panel that is open but
collapsed or in a background dock tab runs nothing either, because the body is inside the `Begin`
result. Tab bodies are gated the same way by `BeginTabItem`. **No `ImGui::Begin` return value is
ignored anywhere in the tree**, and **nothing does work after `ImGui::End()`**.

Three things do run unconditionally, and two of them matter:

| where | what | gate |
|---|---|---|
| `src/ui/control_panel.cpp:163` → `src/ui/editor_shell.cpp:127` `enforceCanvasCentre` | walks the central dock node's window list **and the whole of `ImGuiContext::SettingsWindows`**, doing a linear `findEditorPanel` scan (19 entries) plus a `FindWindowByName` per saved window setting | **ungated, every frame.** Cost scales with what the `.ini` has ever seen, not with what is open |
| `src/ui/control_panel.cpp:717` `editor.update(...)` → `src/ui/world_editor.cpp` | `selection.retainOnly`, `nodeBounds` per selected node, `descendantsOf` per selected group (allocating a container purely to take `.size()`), `updateGhost`/`updateGizmo`/`updateBox` ray-marching terrain | inside the canvas window's `Begin` body — but the canvas has no close button, so **it runs for the life of the session**. The only scene-size-dependent per-frame UI work that no flag can turn off |
| `src/ui/control_panel.cpp:131` | builds a `std::string` of the selection every frame to compare against the last one | ungated; one string copy, negligible |

**And two pieces of non-UI work are accidentally gated on panel visibility, which is §29 with the
sign flipped and is the more interesting finding:**

1. `src/ui/control_panel.cpp:693` — `worldBuilder.applyFinished(engine, *builder)` installs a
   completed background world-generation job into the scene. It is inside the World Builder panel's
   body. **A finished job is never installed while that panel is closed.**
2. `src/ui/sequence_panel.cpp:137,141` — `pollStructureAnalysis` and the auto-start of song-structure
   analysis are inside the Sequence panel's body. **Structure analysis is neither started nor
   harvested while Sequence is closed.**

Neither is a latency defect. Both are correctness defects of the ADR-225 family — a background job
the application does not collect is a job the application did not run — and they are recorded here
because the audit is what found them, not because this lab owns them.

### 1.3 §7 — virtualization

**`ImGuiListClipper` appears zero times in this repository.** So do `ImGui::IsItemVisible`,
`ImGui::IsRectVisible` and `ImGui::IsWindowAppearing`. **No list in this codebase is vertically
virtualized.** The only range-limiting anywhere is *horizontal* time-axis culling in the timeline
strip.

| list | site | reachable count | virtualized |
|---|---|---|---|
| asset browser table | `control_panel.cpp:1339` | every file `scanAssets` found (recursive, depth ≤ 4) | no |
| asset palette (World Edit) | `world_edit_panel.cpp:243` + `:251` | whole manifest; **two full passes per frame**, the first only to find `tallest` | no |
| scene hierarchy | `world_edit_panel.cpp:771` → recursive `row()` at `:642` | the code's own comment says "a scene with four hundred nodes" | no |
| timeline lanes | `sequence_panel.cpp:1062` | one per actor | **no vertical culling at all** |
| timeline shots / clips / overlays / markers / sections / beats | `sequence_panel.cpp:1007, 1083, 1121, 822, 860, 812` | unbounded | **partial** — an x-range `continue` skips *drawing* off-screen items, but the loop is still O(n) |
| keyframes — camera / actor / track | `sequence_panel.cpp:3040, 3135`; `control_panel.cpp:2646, 2665` | unbounded | no |
| **parameter inspector** | `control_panel.cpp:1838` (grouping) then `:1871` (widgets) | the code's own comment: *"Glowmere Valley 2 registers over three thousand"* | **no.** The grouping pass builds a `vector<string>` and an `unordered_map<string, vector<IParameter*>>` over all 3,000+ **every frame, even with every group collapsed** |
| World "Influences" inspector | `world_panel.cpp:368` | all params × `influencesOf()` each | no — see below |
| shot list (camera track) | `control_panel.cpp:3494` with a nested `:3509` | `direction.shots.size() × pieceShots.size()` — **quadratic per frame** | no |
| undo/redo history | `world_edit_panel.cpp:1086, 1101` | capped at 256 ✔ | no; and `history.labels()` + `redoLabels()` at `:1078` allocate two vectors of up to 256 `std::string` **every frame** |
| help search results | `help_panel.cpp:539` | unbounded | no — **but the search itself is cached**, re-run only when the `InputText` returns true (`:311, :458`). The one correctly event-driven list in the tree |

The worst of these by a wide margin is `world_panel.cpp:368`: for *every* parameter under the
selected prefix, `influencesOf()` walks all modulation routes, all timeline tracks, all cues (each
doing a `presets().find()` by string), all scene states, all entities and their behaviours,
`director().writersOf(path)` and all world macros — allocating a `vector<Influence>` with `string`
members per call — and that whole thing is repeated for every ancestor node × 3 transform fields × 3
root paths. It is called **before** the `TreeNodeEx` at `:377` that decides whether to display any of
it.

`docs/application-performance.md` §18 recorded this one and deliberately did not act on it, on the
grounds that `ui.build` is 0.28 ms against a 1.0 ms budget. That decision still looks right on the
measurement it was made on, and §5 below re-tests it on a project with 3,000 parameters rather than
eleven procedural nodes.

### 1.4 The classification table

`UI_SAFE_FAST` — bounded, allocation-free, no shared state. `UI_SAFE_BOUNDED` — bounded by something
the UI controls. `EXPENSIVE` — proportional to the scene or the piece. `BLOCKING` — waits on another
thread or a device. `ASYNC` — returns before the work is done.

The entries that matter are the ones whose *name* does not say which they are.

| API | class | why | site |
|---|---|---|---|
| `Composition::nodeBounds(name)` | **EXPENSIVE** | **calls `ensureBuilt()`, which calls `rebuild()` — a full scene flatten — when `dirty_` is set.** It is also recursive for Group nodes and each level does a `std::find_if` over all nodes, so it is O(N²) on a group tree | `src/scene/composition.cpp:2359`, flatten at `:2411` |
| `Composition::nodeCorners(name)` | **EXPENSIVE** | same, `ensureBuilt()` at `composition.cpp:2263` | `composition.cpp:2256` |
| `Engine::seekSeconds(s)` | **EXPENSIVE**, O(the second you seek to) | re-simulates every entity forward from `target − 90 s` at a fixed 1/60 s step | `src/app/engine.cpp:2311`; the loop is `src/entity/entity.cpp:714` |
| `Engine::composition()` | UI_SAFE_FAST, *but* | an RTTI `dynamic_cast` on **every** call, and there are 52 call sites in `src/ui/`, most per-frame | `src/app/engine.hpp:113` |
| `Composition::findNode(name)` | UI_SAFE_BOUNDED | linear `find_if` with `std::string` compares; **46 UI call sites, many inside per-node draw loops** | `composition.cpp:2589` |
| `Composition::nodeWorldTransform(node)` | UI_SAFE_BOUNDED | walks the parent chain, each hop a linear `findNode` → O(depth × N); 18 UI call sites | `composition.cpp:2188` |
| `Composition::terrainQuery()` | UI_SAFE_BOUNDED | `const`, and builds a fresh query struct per call by walking the node list; 4 UI call sites including two per-frame | `composition.cpp:1867` |
| `Engine::addNode` / `removeNode` | **EXPENSIVE** | sets `dirty_` (⇒ next flatten) **and** calls `rebind()` over the whole parameter set | `engine.hpp:115,116` |
| `Engine::rebind()` | **EXPENSIVE** | `modulator_.bind()` + `timeline_.bind()` over every parameter; 14 UI call sites | `engine.cpp:416` |
| `Engine::setWorldEffects` / `setAtmosphericEffects` | **EXPENSIVE** | tears down and re-registers the whole `worldfx/` / `atmos/` parameter group, then `rebind()`. 7 UI call sites | `engine.cpp:445, 470` |
| `Engine::morphPresets` | **EXPENSIVE** | whole-parameter-set snapshot and apply — **and it is driven by a slider** (`control_panel.cpp:1709`), so it runs every frame of a drag | `engine.hpp:316` |
| `Engine::loadProject` | **BLOCKING** | the header's own comment: *"one synchronous call that can hold the main thread for seconds"* | `engine.hpp:132` |
| `JobSystem::statuses()` | **BLOCKING** (briefly) | takes `mutex_`, then `job->mutex` **once per job**, copying its strings. Called every frame from two panels | `src/app/job_system.cpp:288` |
| `audio::listCaptureDevices()` | **BLOCKING** | an OS device enumeration, on the UI thread, from inside `drawTransport` — rate-limited to once per 5 s by a function-local `static` | `control_panel.cpp:1720` |
| `app::filterAssets(...)` | **EXPENSIVE** | rebuilds the filtered vector **every frame**, and with a non-empty search box builds a fresh lowercased `name + category + description` string **per asset per frame**. No keying on `(kind, search)` | `src/app/asset_browser.cpp:181`, called `control_panel.cpp:1330` |
| `HelpPanel::ensureLoaded()` | **BLOCKING**, once | `SDL_GetBasePath` + a search-dir walk + markdown parse, on the UI thread, on the first open. Correctly latched | `help_panel.cpp:400` |
| `WorldBuilderPanel` library load | **BLOCKING**, once | `std::filesystem::exists` ×3 + an `AssetLibrary::loadFile`, on first open. Correctly latched, and the code says why | `world_builder_panel.cpp:35` |
| `app::pickAt` (viewport pick) | **BLOCKING** | `MapAsync` + `WaitAny` — a real GPU round trip — but **only on a left click**, and batched to one submission since ADR-231 | `src/app/viewport_pick.cpp:56`, the wait at `src/gpu/readback.cpp:250` |
| `Composition::rebuild()` | **EXPENSIVE** | see §2 | `composition.cpp:3177` |

**No file in `src/ui/` performs a GPU readback, a queue wait, `WaitAny`, `MapAsync` or a device
tick.** The only blocking GPU call reachable from a gesture is the viewport pick, which lives one
layer up in `Application::serviceViewportPick` and runs after submit, on a click only.

The single most important row is the first. **`nodeBounds` reads like a getter, is `[[nodiscard]]`,
returns a `WorldBounds` by value, and can flatten the entire world.** It is called from
`WorldEditor::update` per selected node every frame, and from `src/ui/brush.cpp:81` and `:157`
**inside a loop over every node in the composition**, every frame, whenever the brush is in
Eraser/Replace mode or `avoidCollisions` is on. Whether it actually flattens depends on whether
`dirty_` is set at that moment — and `engine.update` runs before `ui.build` and clears it, so
normally it is not. The hazard is a UI edit that sets `dirty_` *during* `ui.build`: every subsequent
`nodeBounds` in that same pass then flattens inside the UI draw. §5.4 measures whether that fires.

---

## 2. Phase 2 — the synchronous coupling graph

**The question: can every common UI interaction be performed without a synchronous full-scene
evaluation?**

### 2.1 What sets `dirty_`

There is no `markDirty()`; `Composition::dirty_` (`src/scene/composition.hpp:944`) is assigned
directly, in fourteen places:

`setComposition`, `setNavCellSize`, `setNavBodyRadius`, `setNavWadeDepth` (all
`composition.hpp:463–523`); `clearGraph`, `evaluateGraph`, `addGrid`, **`setHeroes`**,
`addMaterialProgram`, **`setParent`**, **`addNode`**, **`detachNode`**, `attach` (rename path),
`update` (a child scene's counts changed), `setLightRig`, `installLightRig`, `setEnvironmentMap`
(`composition.cpp:1156 … 5738`).

The bolded four are reachable from an ordinary gesture: starring an object, re-parenting one in the
hierarchy, placing one with the brush, deleting one.

`setWorldEffects` and `setAtmosphericEffects` explicitly do **not** set it, and say so in a comment.

### 2.2 What consuming it costs

`Composition::rebuild()` (`composition.cpp:3177`, ~1,180 lines) is the flatten. In order it:

1. clears and reallocates `scene_.meshes/textures/entities/rigs/particles/procedurals/fields/
   splines/sdfs` and `obstacles_` **wholesale**;
2. passes over all nodes for terrain ground materials, then over `materialPrograms_`;
3. per node, calls `registry_.loadScene(assetPath)` and `registry_.loadImage(...)` — **asset load and
   decode inside the flatten**;
4. for city nodes, calls `assets::AssetLibrary::loadFile(manifest)` — **filesystem and JSON parse
   inside the flatten**;
5. runs `world::scatter` per ecology layer — the header's own words, *"most of it … walking a quarter
   of a million grid cells"*, now memoised on the node by ADR-092 and threaded by ADR-231;
6. meshes terrain chunks and water bodies; merges nested child scenes; `rebuildProcedurals()`;
   `rebuildSdfs()`; `entityWorld_.setNavigator(buildNavigator())`;
7. and ends with an unconditional `++scene_.textureVersion` (`composition.cpp:4136`).

The header says the thing worth quoting: *"`dirty_` has no granularity: adding one flower re-flattens
the world"*.

### 2.3 The graph

| interaction | reaches a full evaluation? | via | measured |
|---|---|---|---|
| hover, panel open/close, tab switch | **no** | — | §5.2 |
| object selection | **no** | — | §5.2 |
| camera orbit, gizmo drag, selection-box drag | **no** | — | §5.2 |
| property drag (a slider held) | **no** — `dirty_` is set by no parameter write; procedural regeneration is hash-guarded per object and budgeted (ADR-084) | — | §5.2 |
| **timeline click / scrub** | **no flatten — and it is the most expensive interaction in the editor anyway** | `Engine::seekSeconds` → `EntityWorld::seek`, an O(second-you-clicked) integration | §5.1 |
| **hero star** | **yes** | `setNodesHero` → `applyEdit` → `Composition::setHeroes` → `dirty_` → `rebuild()` → `++textureVersion` → the renderer re-uploads **every texture in the scene** | §5.3 |
| place / delete an object (brush) | **yes** | `Engine::addNode`/`removeNode` → `dirty_` **and** `rebind()` | §5.4 |
| re-parent in the hierarchy | **yes** | `setParent` → `dirty_` | not measured; same path as the star |
| change the environment map | **yes** | `setEnvironmentMap` → `dirty_` | guarded since ADR-084 §9.3 |

**The answer to the brief's §45, stated plainly: yes, for all but two families.** Seven of the nine
common interactions require no whole-scene evaluation today, and they measure as the idle frame. The
two that do not are a *structural edit* (which genuinely changes the world and legitimately needs the
world rebuilt — the argument is about granularity, not about whether) and a *seek* (which needs no
flatten at all and is expensive for a completely unrelated reason).

**This is the audit's second correction to the brief.** The two pathological interactions have
nothing in common except the thread they run on. A seek is an uncached O(position) integration; a
structural edit is an all-or-nothing invalidation cascade. A single architectural answer — "make the
world async", "add a dependency graph", "introduce a `UISceneSnapshot`" — addresses neither directly,
and would be adopted on intuition rather than on evidence.

---

## 3. What was already known, and is not re-derived here

`docs/application-performance.md` and `docs/investigations/ui-responsiveness.md` are the prior work
and both are load-bearing. In particular: the swapchain wait was moved ahead of input sampling
(17.2 → 1.4 ms); a structural procedural drag was deferred (44% → 5.4% of frames over 50 ms); the
double procedural regeneration was found and removed (22 per idle frame → 0); the sky's IBL stopped
rebuilding inside the frame of a lighting drag. None of that is re-measured here.

What *is* re-examined is the claim those documents could not make, because their unit was the frame.

---

## 4. Phase 1 — the instrument

`src/core/interaction_latency.{hpp,cpp}`, `tests/unit/test_interaction_latency.cpp`, and the flags
`--latency`, `--latency-csv <file>`, `--latency-inject <kind>:<ms>`.

Seven stamps: `T0 input → T1 receipt → T2 command → T3 model → T4 presentation → T5 submit →
T6 visible`. `T6` is taken twice — `firstVisible`, the first frame that presented *anything*, and
`visible`, the frame that presented the evaluated result. They are the same number for every
interaction in this editor today, because every interaction is synchronous. Separating them is what
makes a deferral visible instead of arguable.

Four properties, each of which cost something to decide:

1. **A stage that did not happen is `unavailable`, never zero.** A hero star produces no SDL event,
   so it has no T0; the CSV writes an empty field rather than `0.0000`. An interaction that was begun
   and never finished is counted as *abandoned* and excluded — filing it would report a crash as a
   fast interaction.
2. **Distributions, not minima — and this deliberately disagrees with ADR-170.** ADR-170 is right for
   throughput, where the question is how much work there is and contention is never negative. It is
   wrong here, because the question is what a person experiences and a person experiences the tail.
   Percentiles are nearest-rank; an interpolated p95 is a latency nobody had. The load average is
   printed in the report header rather than left to a caller's log line.
3. **The counters travel with the record.** `flattens`, `seeks`, `proceduralRegens`,
   `texturesUploaded`, `entitySimBodies` are attributed to the interaction that caused them. "One
   flatten per click" and "one per frame of the drag" are different defects, and a millisecond figure
   on a machine at load average 27 tells them apart badly.
4. **`--latency-inject` is the arm that can fail (ADR-182).** It slows a named interaction by a known
   amount *inside the engine work the interaction performs* — at the top of `Engine::seekSeconds`,
   inside `setNodesHero` — and **not** inside the instrument, because an instrument that slowed
   itself would only prove that it can add. A mistyped kind is refused rather than defaulted.
   Injected records carry `injectedMs`, are counted under `injectedSamples`, and are excluded from
   every distribution, so the control cannot leak into a result.

`T2` is taken at the UI call site rather than inside `seekSeconds`, because the engine cannot tell a
person's click from the transport's own end-of-piece wrap, and an instrument that cannot tell them
apart reports the second as the first.

**What the instrument costs in honesty (ADR-250).** The Render Quality Lab does not link the
renderer, and that separation is what makes a finding about the renderer a finding about the
renderer. This lab cannot have that: its subject is a latency that only exists while the process is
running, so the instrument must be inside the process it measures. The mitigation is that every
stamp is a `steady_clock` read and an `optional` assignment on the main thread, and no control flow
anywhere reads any of them — the one exception being `applyInjectedDelay`, which exists to change
behaviour and is zero unless a flag asked for it.

---

## 5. Measurements

### 5.0 Conditions

`examples/world/glowmere-valley-2-multicam.json` — 80 nodes, 256 entities, 860 meshes, 44 textures,
a 226.3 s piece. Window 1600x1000 points, canvas **1914x1256 px = 2.40 Mpx** at backing scale 2.0,
209,761 triangles, 134 draw calls, `--preview-mode workspace`. Release build, M2 Max, macOS.
Every run took `tools/gpu-lock.sh`. **Load averages are stated per run and they are not small**:
three other agents were building and holding the GPU throughout.

Two rules, in tension, resolved as the instrument's header says:

* **Interaction latency is reported as a distribution**, because the tail is the experience. A scrub
  whose minimum is 165 ms and whose p99 is 5,009 ms is not a 165 ms scrub.
* **Throughput-style numbers are reported as minima**, and the *counters* are preferred to both,
  because contention is never negative and "nine seeks where there were four hundred and
  seventy-four" survives a load average of fifty-one.

### 5.1 The matrix

`--ui-ab click:star:hover:sliders:select:tabs:panels:box:gizmo:camera`, 2 blocks of 120 frames,
12 settling frames discarded per block, interleaved inside one process. **Load average 6.7 at the
start of the run and 51.0 at the end** — so the medians below are contended, and the minima and the
counters are the load-bearing figures.

| interaction | records | `input->ack` min..max | `input->final visual` min / med / p95 | CPU med | flat | seeks | tex |
|---|---:|---:|---:|---:|---:|---:|---:|
| **timeline-click** | 16 | **3.1 .. 4.2 ms** | **197.8 / 2437.5 / 3737.1 ms** | 2437.6 | 0 | 16 | 0 |
| **hero-star** | 12 | — (no input event) | — | **2119.5** | **12** | 0 | **528** |
| selection | 35 | — | — | **8.7** | 0 | 0 | 0 |
| property-drag | 240 | — | — | **7.7** | 0 | 0 | 0 |
| panel-toggle | 12 | — | — | **8.8** | 0 | 0 | 0 |
| tab-switch | 22 | — | — | **8.8** | 0 | 0 | 0 |

**The most important number in this document is the first row's second column.** A timeline click is
acknowledged as a command in **3.1 ms** and answered on screen in **2,437 ms**. The application knows
what you asked for almost immediately and then takes two and a half seconds to show it — a ratio of
about 780. Nothing about that is a frame-rate problem and no frame-rate instrument can see it.

`hero-star`'s **528 texture uploads over 12 edits is 44 per edit — every texture in the scene** — and
it is a count rather than a duration, so the attribution holds at any load average.
`Composition::rebuild()` ends with an unconditional `++scene_.textureVersion` and the renderer's only
invalidation granularity is that one counter.

The bottom four rows are the control, and they settle the brief's §45: **selection, a held property
drag, opening and closing panels and switching tabs cost 7.7–8.8 ms of CPU and cause no flatten, no
seek and no texture upload.** There is no general UI-thread responsiveness problem. There are two
expensive paths.

**What this table cannot say, and why.** The four cheap interactions have no `input->ack` or
`input->final visual` at all, because they are driven through the editor's own API rather than the
pointer and produce no SDL event to age. Their CPU column is the measurement; their latency columns
are honestly blank rather than filled with a plausible number. Comparing `2,437.5 ms` against
`8.8 ms` is therefore comparing a latency against a CPU cost, and the only thing that makes it a fair
comparison is that the click's own CPU column reads 2,437.6 — the click's latency *is* its CPU cost,
because there is nothing else in it.

### 5.2 Where a click's 2,437 ms goes

Every stage but one is free. `Engine::seekSeconds` does the transport, the audio player, the
modulator, the music classifier, the director reset, the camera state, **`EntityWorld::seek`**, the
rig reseed and the event rebase; the prior investigation measured everything *except*
`EntityWorld::seek` at 0.025–0.046 ms in total. The re-simulation integrates every entity forward
from `target − 90 s` at a fixed 1/60 s step (`src/entity/entity.cpp:714`; both the step and the
90-second cap are literals passed at `src/app/engine.cpp`), so the cost is a straight line in *where
you clicked*, flattening at the cap.

**Independent corroboration, from another agent and from the opposite direction.** While this lab was
being built, the character-intelligence work measured a single `EntityWorld::seek(90 s)` with an
autonomous cast: **161.51 s at 100 `explore` characters and 594.66 s at 250** (commit `38c01ab`,
now on main). Ten minutes of stalled editor for one timeline click. That is the same call this lab
reached from the other end, and it changes the conclusion in §8: the deferral prototyped below is
necessary and nowhere near sufficient.

### 5.3 The prototype, before and after

`--ui-ab "drag+seeknow:drag"`, 3 blocks of 240 frames, one process, **load average 2.56** — the
cleanest conditions of any run here. `+seeknow` restores the undeferred seek for the length of its
block, so these are two halves of one session and not two runs.

| | `drag+seeknow` (before) | `drag` (after) | |
|---|---:|---:|---|
| timeline-drag records | 237 | 9 | |
| **seeks performed** | **474** | **9** | **53x fewer** |
| requests coalesced into a batch | 237 | 351 | |
| `input->ack` median | 3.8 ms | 3.8 ms | unchanged, as it must be |
| **`input->first visual` median** | **2,712.5 ms** | **8.9 ms** | **305x** |
| `input->first visual` p99 | 3,802.9 ms | **9.1 ms** | |
| `input->final visual` median | 5,376.6 ms | 4,616.0 ms | −14% |
| `input->final visual` p95 | 7,521.8 ms | 4,821.4 ms | −36% |
| CPU median | 5,376.8 ms | 3,886.4 ms | |
| flattens | 0 | 0 | |

And the press that begins each gesture, which is a *click* and is deliberately not deferred:

| timeline-click | before | after |
|---|---:|---:|
| `input->first visual` median | 2,356.9 ms | **8.5 ms** |
| `input->final visual` median | 2,366.9 ms | 2,347.8 ms |

**Read those two tables together, because either one alone is misleading.**

The playhead now answers in **8.5–8.9 ms — one frame, inside the preferred band** — where it
previously answered in 2.4 to 2.7 seconds, which is the flow-break band. The gesture became usable:
a scrub follows the pointer. That is a 305x improvement in the number a person actually perceives,
and it is what the brief's cheap-immediate-response pattern buys.

**The work did not go away, and the final visual barely moved.** 4,616 ms median against 5,377 — 14%,
and all of it from evaluating nine times instead of 474. The world is still up to five seconds behind
the playhead when a gesture ends. **A deferral turns an unusable interaction into a responsive one
with a lagging result; it does not make the result arrive sooner.** Anybody who quotes the 305x
without the 14% beside it will conclude the seek problem is solved. It is not.

The `blocked` column moves from 0.1 ms to 725.1 ms between the arms. That is an artefact of the
measurement and not a cost: a deferred record stays open across more frames, so it accumulates more
of the swapchain wait. It is named as a wait for exactly this reason.

### 5.4 The control (ADR-182)

A latency harness that reports plausible numbers for an interaction it never performed is worse than
no harness, because it will be trusted. So the arm is shown detecting a slowdown it was not told
about: `--latency-inject timeline-click:250` spends 250 ms inside `Engine::seekSeconds` — inside the
work the interaction performs, not inside the instrument — and the same `click` arm is run twice,
back to back, under the same lock. **Load average 2.18 and 2.02.**

`input->final visual`, the same rank in each distribution, 13 clicks each:

| rank | no injection | +250 ms injected | difference |
|---|---:|---:|---:|
| minimum | 187.4 ms | 443.5 ms | **+256.1** |
| 2nd | 649.1 ms | 898.2 ms | **+249.0** |
| 3rd | 1141.8 ms | 1388.3 ms | **+246.5** |
| maximum | 2797.6 ms | 3039.7 ms | **+242.1** |

Every quantile moved by 242–256 ms against an injection of exactly 250. The minima are the
load-bearing comparison here rather than the medians — this is a "did the work get bigger" question,
which is the kind ADR-170's rule is right about, and contention can only ever make the difference
larger.

**Two things the control caught that nothing else would have.** The first run of it reported an
entirely empty table: injected records are excluded from every distribution, correctly, and with
*every* record injected there was nothing left to print. A working control that reports a table of
dashes is a control nobody can read, so the calibration samples now get their own line with their
own distribution, labelled as calibration and pooled with nothing. The second is that a mistyped
interaction name is refused at the command line rather than defaulting — without that,
`--latency-inject timeline-clik:250` would have calibrated the harness against an interaction nobody
asked about and the null result would have looked like a clean one.

### 5.5 The regression baseline

The full CPU suite was run at this branch's merge-base (`74f9c0e`) and on this branch, on the same
machine, with the same binary target:

| | cases | failed | failed as expected | failing assertions |
|---|---:|---:|---:|---:|
| merge-base `74f9c0e` | 2,144 | 6 | 3 | 14 |
| this branch | 2,165 | 6 | 3 | 14 |

The failing *files and line numbers* are identical in both: `test_beam_lab.cpp:831/875/887`,
`test_character_lab_slopes.cpp:187`, `test_shadow_lab.cpp:404/432`,
`test_song_beginner_path.cpp:119/207/276/389/392/401`. Twenty-one cases were added and all pass.

This was worth a build cycle rather than an assertion. The four `test_song_beginner_path` failures
looked like a regression — they pass on current `main` and fail here — and the explanation is that
**main has advanced 28 commits since this branch point**, two of which (`5e6dfd6` "A fixed 7.5
seconds was a bet on a fixture, and the owner then edited the fixture" and `6896054` "Two Song Mode
tests were measuring a JSON field, and the owner changed it") are the fixes for exactly these
assertions. Reporting them as pre-existing without measuring would have been a guess that happened to
be right.


---

## 6. What this lab does not own

* **Why a frame costs what it costs.** `core::PhaseProfiler`, `--profile-cpu`, `--ui-ab`.
* **Why the GPU frame is 21 ms at full canvas.** `docs/renderer-2-architecture.md`. A 16× cut in
  rendered pixels plus six passes removed moves click latency by 0.8%; it is a real problem and a
  different one.
* **Whether a background job that was never collected is a defect.** §1.2 found two; ADR-225 owns the
  principle and the panels own the fix.

---

## 7. What is recommended, and what is explicitly rejected

### Recommended, in the order the numbers put them

**R1 — Delete the unconditional `++scene_.textureVersion` behind a content check.** One condition in
`Composition::rebuild()` (`composition.cpp:4136`). Measured here: **44 texture uploads per hero
star**, 528 over twelve edits, on a flatten that changed no texture at all. The prior investigation
attributed 1,125 ms of a 1,454 ms star to this and its prediction is falsifiable — `# textures
uploaded` should read 0 on a star's flatten frame. This is the highest ratio of latency removed to
risk taken anywhere in this work, and a missed invalidation fails visibly and immediately, which is
the good kind of failure. **Under an hour.**

**R2 — Bound the entity re-simulation.** This is the one that matters and this work did not do it.
`EntityWorld::seek` integrates from `target − 90 s` at 1/60 s every single time; keyframing the
entity state at intervals and integrating forward from the nearest key below the target reproduces
the same state for the same input with bounded work, and a *forward* seek could integrate from where
it already is. The determinism test comes first and must fail before the change and pass after:
scrub to the same second by two different routes and compare every entity's published state bit for
bit (ADR-091 is exactly this property and it is the whole value of the re-simulation). The
character-intelligence measurement of 161–594 s per seek with an autonomous cast is what turns this
from an optimisation into the thing that decides whether an autonomous cast can be authored at all.
**A day, and it is the day worth spending.**

**R3 — Keep the deferral prototyped here.** It is small, it reuses ADR-084's tested arithmetic, it
adds no thread and no second copy of any state, and it converts a scrub from unusable to responsive
today, without waiting for R2. After R2 it costs nothing and protects against whatever the next
expensive thing on that path turns out to be. It should not be *mistaken* for R2, which is why §5.3
prints the 14% beside the 305x.

**R4 — Cache `filterAssets`.** `control_panel.cpp:1330` rebuilds the filtered asset vector every
frame and, with a search box in use, builds a fresh lowercased `name + category + description` string
per asset per frame. `help_panel.cpp` already shows the fix — re-run when the `InputText` returns
true. Unmeasured here (`ui.build` never crossed 0.28 ms on these projects) and recorded because it is
one line of discipline away from being right.

**R5 — Fix the two background jobs gated on panel visibility** (§1.2). Not latency; correctness.

### Explicitly rejected

**Replacing Dear ImGui.** Nothing measured here is attributable to it. `ui.build` is 0.28 ms against
a 1.0 ms budget on a project with 3,000 parameters, and the two pathological interactions cost the
same with the renderer reduced by 79%.

**Putting world evaluation on a worker thread.** No measurement here requires it, and the ones that
exist argue against it: both expensive interactions are single synchronous calls that are *too
expensive*, not work that needs to be somewhere else. The composition is not thread-safe, all GPU
work in this application is the main thread's, and ADR-084 §2 already rejected threading a far
smaller piece of this. Deferring work is not the same thing as moving it, and only one of the two
needs a lock. **The prototype in §5.3 is the deferral, not the thread, and the 305x came from the
deferral.**

**A `UISceneSnapshot`.** The brief proposes one; the measurement does not support it. The thing a
snapshot would buy — the UI reading a stable copy while the world is evaluated — already exists for
the one interaction that needs it: `Transport` holds the requested playhead position and the
re-simulation was always downstream of it. The prototype needed *no* new state, and a second copy of
the scene would have to be kept in step with the first by something, which is a new class of bug in
exchange for a problem that has not been demonstrated.

**A general invalidation taxonomy / dirty dependency graph.** `Composition::dirty_` has no
granularity and that is a real defect, but it is reached by **one** of the nine common interactions
measured here, and the flatten it triggers is the *smaller* half of that interaction's cost. R1
changes whether the flatten is 19% or 85% of a star, so R1 must be settled before anybody can price
this. Starting the granular-invalidation refactor first would be the fourth-best available action and
the riskiest.

**Virtualizing the lists.** `ImGuiListClipper` is absent from the whole repository and several lists
are genuinely unbounded (§1.3). Not one of them appeared in any measurement. Recorded as a latent
scaling risk, not proposed as work: the parameter inspector's 3,000-entry grouping pass is the
strongest candidate and it lives inside an 0.28 ms `ui.build`.

---

## 8. What could not be measured, and why

* **Latency for the value-driven interactions.** Selection, property drag, panel toggle and tab
  switch are driven through the editor's own API and produce no SDL event, so they have no T0 and
  their `input->ack` and `input->final visual` columns read `--`. Their CPU cost is measured;
  their latency is not. Driving them through the pointer would measure ImGui's hit-testing as well,
  which is a different question, and the arms' own headers say which of the two they are.
* **How stale a real device's input gets during a 2.4-second frame.** A scripted arm mints its
  events immediately before the poll that consumes them, so it cannot produce a backlog. The loop's
  shape makes it structurally certain that no event is drained for the length of the call; the
  magnitude is unmeasured, and the previous investigation could not measure it either.
* **Whether `nodeBounds`'s flatten ever actually fires from the UI.** The hazard is proven by
  reading — it calls `ensureBuilt()`, it is called per selected node every frame from
  `WorldEditor::update` and per *every* node from `brush.cpp` — but `engine.update` runs before
  `ui.build` and normally clears `dirty_`, so it should only fire when a UI edit sets `dirty_`
  earlier in the same `ui.build`. The `edit` arm was not run under the latency instrument and this
  is the single most valuable unrun measurement left. **If `# scene flattens` ever exceeds 1 on a
  brush-stroke frame, that is a flatten inside the UI draw and a defect of its own.**
* **Why a single texture upload costs ~25 ms.** Mipmap generation is the obvious candidate and it is
  one level deeper than this work went. R1 makes it moot for the star; it would still matter for a
  flatten that genuinely changed a texture.
* **Anything about how the interface looks.** This agent cannot see the UI. Every claim here comes
  from a counter, a timestamp or a scripted arm's own self-report, and none from reading ImGui code
  and reasoning about what it would draw.
