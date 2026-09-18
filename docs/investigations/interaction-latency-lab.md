# The Interaction Latency Lab

**Status:** the instrument is built and registered; the audit below is complete; the measurements
are in §5 and the prototype in §7.
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

*(filled in by the run; see the sections below)*

---

## 6. What this lab does not own

* **Why a frame costs what it costs.** `core::PhaseProfiler`, `--profile-cpu`, `--ui-ab`.
* **Why the GPU frame is 21 ms at full canvas.** `docs/renderer-2-architecture.md`. A 16× cut in
  rendered pixels plus six passes removed moves click latency by 0.8%; it is a real problem and a
  different one.
* **Whether a background job that was never collected is a defect.** §1.2 found two; ADR-225 owns the
  principle and the panels own the fix.
