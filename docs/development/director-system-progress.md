# AV Gen Director System — Development Progress
Last updated: 2026-09-24 13:45
Current branch: `agent/director` (worktree `../av-gen-director`, branched from `main` at 13bc030f)
Current commit: 8e0e1752
Overall status: Slice 0 complete except the effect domain (blocked on ADR-702 merging); both full suites green

## Executive status
Slice 0 (Foundations) is implemented and tested, with one domain deliberately deferred. The five
parts:

- **0.1 audit:** complete.
- **0.2 undo:** Director, AI and Cameras-panel edits are now one undo on the editor's own history.
  - `EditCommand` covers the sequence, the camera collection and camera track, author timeline
    keys, routes, parameters and added nodes.
  - `app::EditCapture` is the one measurement of what an operation changed.
  - "Undo this task" is Cmd+Z. Snapshots remain only for aborting a failed task.
  - The AI transaction now runs on the main thread instead of the worker.
- **0.3 persistence:** a reusable round-trip gate that runs a frame before saving. It found and
  fixed a real defect: cameras and cuts were lost on every save of a by-reference project.
- **0.4 preview:** rendering from the editor no longer saves the person's project. It reads a
  scratch copy.
- **0.5 capabilities:** a registry generated from engine data. Rook's card says run, walk, jump,
  fall and land, and no backflip.

The effect domain (undo, persistence, catalogue) waits for `agent/entity-effects` (ADR-702) to
merge. `src/directing/` exists, holds the capability registry, and has a boundary test.

## Overall progress
| Slice | Status | Progress | Tests | Notes |
|---|---|---:|---:|---|
| 0 Foundations | Implemented; effects deferred | 80% | 18 `[directing]` cases | 0.1 100%, 0.2 85%, 0.3 60%, 0.4 75%, 0.5 75%; see below |
| 1 Plan + Cameras | Not started | 0% | 0 | Next |
| 2 Scripted Performances | Not started | 0% | 0 | |
| 3 Airborne + Events | Not started | 0% | 0 | |
| 4 Autonomous Direction | Not started | 0% | 0 | |
| 5 Verification + Scale | Not started | 0% | 0 | |

What the Slice 0 percentages leave out, honestly:
- **0.2:** the effect-list record (blocked). Nodes an AI tool *deletes* cannot be restored by undo
  (logged; abort still covers them). Human edit paths other than the Cameras panel still bypass the
  history: sequencer drags and inspectors, timeline key drags, Routes tab, layers.
- **0.3:** round trips for the Director Plan, effect references/windows and semantic metadata do not
  exist yet, because those structures do not exist yet. They land with Slice 1, each with its test.
- **0.4:** the Director preview itself (CompilePreview on a staging session) is Slice 1 work. The
  render button and queue wiring is not verified in the running UI.
- **0.5:** the effect catalogue (blocked on ADR-702).

## Current focus
### Task
Close Slice 0: run both full suites, then report. Then Slice 1.1, the Director Plan schema.

### Subtasks
- [x] Full CPU suite (release) and full GPU suite: both exit 0
- [ ] Measure `EntityWorld::seek` on the benchmark (ADR-700 claims 17.9 ms worst after the first)
- [ ] Slice 1.1: Plan schema ADR, `directing::Plan` with versioned JSON, round-trip tests

### Acceptance criteria (Slice 0)
- [x] Each Director domain (sequence incl. actors/events/markers, camera direction, camera track,
      author timeline, routes, parameter bases) passes apply → undo → identical and redo → identical
- [x] An AI task that adds a shot, a marker and a keyframe is one Cmd+Z (camera covered by the
      capture test)
- [x] No code path restores a snapshot as "undo"
- [x] Director-shaped content survives save → reload after a frame (sequence, cameras)
- [x] A render/preview does not write the person's project
- [x] Capabilities are generated, not listed; `src/directing/` has no AI dependency
- [!] Effect domain: blocked on ADR-702

## Completed work
### 0.1 Repository audit (2026-09-24), complete
Method: I read the code myself (edit_history, ai_edit_sink, transaction, orchestrator, engine
save/load, camera direction, application render path), and a read-only Explore agent swept every UI
and tool mutation path. I spot-checked its load-bearing claims.

**Undo infrastructure**
- There is one `ui::EditHistory`, owned by `app::EditSystem` (edit_system.hpp:203). Cmd+Z, the menu
  and the history list all reach it. `WorldEditor::undo/redo` is dead code.
- `EditCommand` records: `ParamChange`, `ParentChange`, `HeroChange`, `NodeRecord` added/removed
  (with the node *held*, not described), `TimelineChange` (the whole `seq::Sequence` plus audio
  clips), and `LightChange`. On `agent/entity-effects` it also gets `EffectChange` (the whole effect
  list, plus routes when an add attached default routes).
- The whole-domain before/after precedent (`TimelineChange`, `LightChange`, `EffectChange`) is the
  shape to follow.

**What pushes history, and what does not**
| Domain | Pushes history | Bypasses history |
|---|---|---|
| Sequence | Delete (key or menu) and slice (`SequencePanel::beginEdit/commitEdit`) | Add shot/lyric/actor, every drag (move, trim, section boundary, keys), every inspector edit, clip cues, duplicate, structure analysis, lyric import, and the Cameras panel's shot "x" (which also rewrites `seq::Shot`s) |
| Camera direction (rigs, camera track) | Nothing. There is no record kind. | Every writer: Cameras panel add/delete/lens/"Cut to this camera"/shot "x"; "Place here" (param bases); the auto-director's release/resume/discard/install |
| Author timeline (`params::Timeline`) | Nothing | Key drag/delete, track removal, cues, record-key buttons, `sequencer.add_keyframe/remove_track` |
| Modulation routes | Nothing | Routes tab, response sliders, effect panels, hero reactions, macros |
| Staging, entities, fields | n/a (only load writes them) | `field.create` |
| 2D layers | Nothing | Add/duplicate/remove/rename |
| Effects | (on the effects branch) `EffectChange` | main's World Effects panel |

**The AI path**
- `EditHistoryTransactionSink` (app/ai_edit_sink.*) is installed in both GUI and headless
  (application.cpp:935). It diffs parameter **bases** at begin/commit and pushes one
  `ParamChange`-only command. Nodes, sequence, timeline, routes and fields changed by a task never
  reach the undo stack.
- It wraps `SnapshotTransactionSink`, which snapshots the whole document for **abort**. That is a
  legitimate transaction primitive, and it is not undo.
- **Defect: "Undo this task" is unreachable in the app.** The panel shows the button only when
  `outcome.snapshotId` is set. The orchestrator sets that through
  `dynamic_cast<SnapshotTransactionSink*>(sink_)`, but the installed sink is the
  `EditHistoryTransactionSink`, so the cast yields null and the id stays empty. Where the button is
  reachable (a control plane with the default sink), it restores via `SnapshotStore::applyDocument`:
  a full `setCompositionJson` rebuild that bypasses the history and leaves the stack stale.
- Mutating AI tools: parameter bases (7 tools); nodes (create/delete/set_parent); sequence
  (add_marker/add_shot/add_overlay, each followed by a reinstall); `field.create`;
  `sequencer.add_keyframe/remove_track`; `modulation.create/set_depth/remove`; and the destructive
  session tools (`world.generate`, `project.create/open`, `project.restore_snapshot`).
- `scene.delete_node` destroys nodes (`Composition::removeNode`). An after-the-fact diff therefore
  cannot recover them for undo. See the 0.2 design.

**Camera direction**
- Whole-value setter `Engine::setCameraDirection`, no mutable getter (callers copy, edit, set).
- Removing a camera also **erases the timeline tracks aimed at `cameras/<slug>/`** and removes its
  parameters. An undo of a camera removal must therefore restore the tracks too, which is why
  `AutomationChange` travels with `CameraDirectionChange`.
- Re-adding a camera registers its channels from the rig struct. `ParameterSet::add` keeps an
  existing path's base, so a restored rig must carry its current bases in the struct.
- **Persistence defect: found and fixed** (ADR-751). The project wrote no key for it.

**Preview / render (0.4 input)**
- `Application::startRenderFromUi` (application.cpp:2447) calls `Engine::saveProject(projectPath)`
  on the **user's own project file** before rendering. That save also moves the dirty baseline, so
  unsaved edits are silently written and the "unsaved changes" state is cleared. Only a never-saved
  session gets a temp file.
- `projectDocument(path)` makes asset paths relative to `path`, so writing it to a scratch location
  is sound in principle. Whether it has side effects on the engine is still to be verified in 0.4.

**Effects:** deferred. Main's `worldEffects`/`atmosphericEffects` are being deleted by ADR-702. No
undo, serialization or compilation will be built on them.

### 0.2 Unified undo (2026-09-24): 85%
- `ui::AutomationChange` (whole timeline + routes) and `ui::CameraDirectionChange` (collection with
  rig bases captured) in `EditCommand`; `applyEdit` order is sequence, then automation, then
  cameras (see ADR-752 for why).
- `ui::capturedCameraDirection`, `ui::editCameraDirection`.
- `app::EditCapture` (src/app/edit_capture.*) measures parameters, sequence, cameras, automation,
  added nodes and parent changes.
- `EditHistoryTransactionSink` uses the capture. `TransactionSink::committedEditState` →
  `TaskOutcome::editState` → `ui::taskUndoState` → the AI panel's undo calls `EditAction::Undo`.
  The snapshot restore was removed from the panel.
- The orchestrator opens, commits and rolls back on the main thread (it did all three on the worker
  before). `Transaction::abandon` covers a stopped queue.
- The Cameras panel (add, delete, lens drag, eligibility, cut, remove cut, place here) pushes one
  command per gesture. Not verified in the running UI.
- Defects found and fixed along the way:
  - Captured timelines held dangling `IParameter*` after a camera delete (segfault in the first run).
    They are now unbound at capture.
  - A redo duplicated every baked sequence track. Fixed by restoring the sequence before the
    automation.
- Tests: `tests/unit/test_director_undo.cpp`, 5 cases. Proven red three ways: base capture, sequence
  record, automation install. TSan: `[ai]` 83 cases and `[directing][undo]` 5 cases with 0 warnings.
- Files: src/ui/edit_history.*, src/app/edit_capture.*, src/app/ai_edit_sink.*, src/ai/transaction.hpp,
  src/ai/orchestrator.*, src/ui/ai_panel*, src/ui/control_panel.*, src/app/application.cpp.
- Commits f142315a, af4c59f6, 8e0e1752. ADR-752.

### 0.3 Persistence (2026-09-24): 60%
- `tests/support/project_round_trip.hpp`: `saveAndReload` (at least one frame, real `saveProject`,
  fresh engine, a frame on the far side), `missingTopLevelKeys`, `differingPaths`, `ScratchDir`,
  `stepFrames`.
- `tests/unit/test_director_persistence.cpp`, 6 cases:
  - a Director-shaped sequence: behaviour-camera shot, keyed-camera shot with match cut, actor with
    keys, path and clip cues, cue markers, cue-triggered event, piece track;
  - camera direction, inline and by reference;
  - an untouched project writes no key and leaves the scene file alone;
  - a deleted camera stays deleted;
  - the benchmark project loads with 0 warnings and adds no `cameraDirection` key.
- **Defect fixed:** camera direction was lost on every by-reference project save (the sixth of
  ADR-207's family). ADR-751, commit 8b498988.

### 0.4 Preview isolation (2026-09-24): 75%
- `Engine::writeProjectCopy` and `app::renderSourceFor`. The UI render and the render queue load a
  scratch copy, and the output resolves beside the project.
- The queue no longer requires a saved project.
- Tests: `tests/unit/test_director_preview_isolation.cpp`, 2 cases. Proven red by making the copy
  adopt the path.
- ADR-753, commit afb21053. The path tracer still reads the last-saved file (it never wrote, but it
  is inconsistent with the render).

### 0.5 Capability registry (2026-09-24): 75%
- `src/directing/capabilities.*`: `CapabilityRegistry`, `CharacterCard` (entity + loaded rig + jump
  envelope with its source), `CameraCatalog`, `EventCatalog` (with tiers).
- New `scene::allShotTransitions()`.
- `capability.list` availability is now derived from its tools. It had called `entity` and `render`
  unavailable while listing tools in both.
- Tests: `tests/unit/test_directing_capabilities.cpp`, 5 cases, including the ADR-750 boundary scan.
  Proven red for the card, the boundary and `capability.list`.
- ADR-754, commit 3410dcac.

## Blocked work
- [!] Effect domain: `EffectChange` undo, effect persistence, the effect catalogue and effect cues.
  Waiting for `agent/entity-effects` (ADR-702) to merge to main, then `agent/director` rebases.
  Merge hazards are recorded under Known risks.

## Architectural decisions
- ADR-750: the module is `src/directing/` (`avgen::directing`). It depends on no AI code, and a
  test enforces that.
- ADR-751: camera direction rides in the project (the sixth of ADR-207's family).
- ADR-752: one undo for every domain the Director writes. Whole-domain records; `EditCapture`;
  "Undo this task" is Cmd+Z; the transaction runs on the main thread.
- ADR-753: a render or a preview reads a scratch copy, never the person's project.
- ADR-754: capabilities are generated from engine data, never listed.
- Finding that binds the Slice 1 compiler: `seq::install` owns every track on a parameter a
  sequence bakes to, so compiled cues and author keys must never share a parameter.
- Finding that binds Slice 1/2: `seq::Shot` (framing, in the sequence, saved in the project) and
  `scene::CameraShot` (which camera is live, in the camera direction, saved with the scene or,
  since ADR-751, the project) are independent, and nothing validates their correspondence. The
  Cameras panel's "x" deletes both by overlap.

## Known risks
- **Merge with ADR-702.** Both branches edit `src/ui/edit_history.*`: `EditCommand` fields,
  `empty()`, `touched()`, `applyEdit`'s composition-null check, and includes. Expect a small
  three-way merge. After it, `EditCapture` must also capture the effect list, and route capture must
  live in exactly one record: `EffectChange` also carries routes.
- **ADR-702 also rewrites `Engine::projectDocument`'s** `worldEffects`/`atmosphericEffects` block,
  next to ADR-751's `cameraDirection` block. This is a textual conflict, not a semantic one.
- The AI sink's parameter diff can pick up engine-driven base writes made during a long task (the
  per-frame writeback). This was pre-existing, and it is now visible in a bigger command.
- Glowmere Valley 2 multicam's audio resolves outside the repository (`~/Desktop/Rebuild.mp3`).
  The benchmark tests are machine-specific.
- Human edits that bypass history (sequencer drags and inspectors, timeline key drags, routes,
  layers) are outside the Director's needs and remain.

## Test status
- New: 18 `[directing]` cases (persistence 6, undo 5, preview 2, capabilities 5), all passing
  (release). `[ai]` 84, `[edits]` 9, `[undo]` 8 and `[camera]` 124 pass. TSan: `[ai]` +
  `[directing][undo]`, 0 warnings.
- **Full CPU suite (release, 2026-09-24, at 8e0e1752):** exit 0. 3,179 cases (the baseline's
  3,161 plus 18 new): 3,162 passed, 16 skipped (hardware-gated), 1 failed as expected (the
  `[!shouldfail]` at test_character_lab_slopes.cpp:187). One summary; only this worktree's paths.
- **Full GPU suite (release):** exit 0. 415 cases: 414 passed, 1 skipped ("NDI sender publishes…",
  libndi not installed on this machine; environmental, unrelated).

## Recent changes
- 2026-09-24: 8b498988 camera direction persistence · 0a265b79 audit + ADR-750 · f142315a unified
  undo · af4c59f6 Cameras panel undo · afb21053 render reads a scratch copy · 3410dcac capability
  registry · 8e0e1752 redo ordering fix.

## Next tasks
1. Finish the full CPU and GPU suites; report Slice 0.
2. Measure `EntityWorld::seek` and `Composition::rebuild` on the benchmark (spec §37). ADR-700's
   numbers have not been re-measured here.
3. Slice 1.1: the Director Plan schema (ADR), `directing::Plan` JSON round trip, versioning.
4. Slice 1.2: the resolver (subjects: entity / hero / node / camera; time: "1:30", bar, section
   occurrence). "Umbra" is ambiguous between the hero `umbra-cap` and nodes `umbra-*`, and must
   come back as AMBIGUOUS_REFERENCE.
5. After ADR-702 merges: rebase, then the effect-domain undo, persistence and catalogue.

## Questions requiring human decision
- **Rendering from the editor used to save your project first.** It no longer does (ADR-753). If
  there was a reason for that behaviour, say so. I found none in the code or the ADRs.
- No product decisions are blocking Slice 0. Slice 1 questions (not blocking yet):
  - Should Director Plans persist in the project, for refinement and provenance, or only their
    compiled content? This is the feasibility report's open question 6.
  - Does "the Umbra hero effect" mean ADR-702's per-hero Ground Pulse instance owned by
    `umbra-cap`? I will assume so unless told otherwise.

---

# Roadmap and task hierarchy

Legend: [x] done · [~] in progress · [ ] not started · [!] blocked

## Slice 0 — Foundations
### 0.1 Repository audit
- [x] Audit EditHistory
- [x] Audit AI transaction sink
- [x] Audit snapshot undo
- [x] Audit camera mutations
- [x] Audit effect mutations
- [x] Audit timeline/key mutations
- [x] Identify all persistent Director-targeted state
### 0.2 Unified undo
- [x] Extend EditCommand
- [x] Add camera direction change
- [!] Add world effect change (ADR-702 `EffectChange` exists on `agent/entity-effects`; adopt after merge)
- [x] Add timeline/key change where needed
- [x] Cover events
- [x] Cover actors
- [x] Update AI transaction sink
- [x] Remove duplicate snapshot undo
- [x] Add regression tests
### 0.3 Persistence
- [x] Create round-trip helper (steps at least one frame before saving)
- [x] Audit Director-targeted serialization
- [~] Add round-trip tests (sequence, camera direction done; Plan/effects when they exist)
- [x] Identify existing serialization defects (camera direction found and fixed)
- [x] Fix defects encountered in touched domains
### 0.4 Preview isolation
- [x] Audit preview/render save path
- [x] Design scratch-project rendering
- [~] Implement isolated preview (engine primitive + render path done; Director preview is Slice 1)
- [x] Add regression test
### 0.5 Capability registry
- [x] Audit existing registries
- [x] Define generated registry architecture
- [x] Character capabilities
- [x] Camera capabilities
- [!] Effect capabilities (blocked on ADR-702)
- [x] Event capabilities
- [x] Replace stale capability declarations
- [x] Add tests

## Slice 1 — Director Plan v1
- 1.1 Director Plan: [ ] schema · [ ] versioning · [ ] serialization · [ ] references · [ ] time
  representation · [ ] shots · [ ] cameras · [ ] markers · [ ] cues · [ ] effects · [ ] provenance ·
  [ ] determinism tier
- 1.2 Resolver: [ ] subject resolver · [ ] alias resolution · [ ] ambiguity handling · [ ] time
  parser · [ ] musical section resolver
- 1.3 Validator: [ ] reference · [ ] capability · [ ] timing · [ ] shot overlap · [ ] camera ·
  [ ] determinism · [ ] effect validation · [ ] conflict diagnostics
- 1.4 Compiler: [ ] shot · [ ] camera · [ ] marker · [ ] cue · [ ] effect · [ ] sequence compilers ·
  [ ] diff generation
- 1.5 Agent integration: [ ] plan output contract · [ ] AwaitingApproval state · [ ] semantic tools ·
  [ ] scripted-provider tests · [ ] apply transaction · [ ] undo

## Slice 2 — Semantic timeline tools and scripted performances
[ ] performance abstraction · [ ] scripted mode · [ ] seq::Actor compiler · [ ] run-to · [ ] walk-to ·
[ ] run-past · [ ] look-at · [ ] entity/actor handoff · [ ] actor restoration · [ ] character event
markers · [ ] keyed chase camera · [ ] character performance tests

## Slice 3 — Airborne and events
[ ] jump semantic action · [ ] airborne compiler · [ ] clearance validation · [ ] landing validation ·
[ ] pass-through movement · [ ] one-shot animation semantics · [ ] clip event markers ·
[ ] time-varying camera offset · [ ] local performance retiming · [ ] FrameEcho integration ·
[ ] peak events · [ ] jump benchmark

## Slice 4 — Autonomous direction
[ ] goal-mode compiler · [ ] Director authority integration · [ ] Phase F goal considerers ·
[ ] live event producers · [ ] event-driven proposals · [ ] runtime candidate shots ·
[ ] authored-vs-runtime precedence · [ ] replay validation

## Slice 5 — Verification and scale
[ ] preview thumbnails · [ ] optional vision critique · [ ] Director benchmark harness ·
[ ] golden plans · [ ] performance benchmarks · [ ] seek optimization · [ ] MCP exposure ·
[ ] external-agent integration tests
