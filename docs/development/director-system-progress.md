# AV Gen Director System — Development Progress
Last updated: 2026-09-24 13:10
Current branch: `agent/director` (worktree `../av-gen-director`, branched from `main` at 13bc030f)
Current commit: 8b498988
Overall status: Slice 0 in progress (0.1 done, 0.2 next)

## Executive status
Slice 0.1 (repository audit) is complete. Every finding below was checked against main's code rather
than taken from the feasibility report. It found three defects that sit squarely on the Director's
path:
1. **A camera or cut added to a by-reference scene was lost on every project save.** This is now
   fixed (ADR-751), with a reusable round-trip gate.
2. **The AI "Undo this task" button can never appear in the application.**
3. **An AI task's sequence, keyframe, route, node and field edits are not undoable at all in the
   app.** The history records its parameter diff and nothing else.

Slice 0.2 (unified undo) is designed and is next.

## Overall progress
| Slice | Status | Progress | Tests | Notes |
|---|---|---:|---:|---|
| 0 Foundations | In progress | 20% | 6 | 0.1 done; 0.3 started (helper + camera fix); 0.2 designed |
| 1 Plan + Cameras | Not started | 0% | 0 | Gated on Slice 0 |
| 2 Scripted Performances | Not started | 0% | 0 | |
| 3 Airborne + Events | Not started | 0% | 0 | |
| 4 Autonomous Direction | Not started | 0% | 0 | |
| 5 Verification + Scale | Not started | 0% | 0 | |

## Current focus
### Task
Slice 0.2: one undo mechanism that covers every domain the Director writes, for human, AI and
Director edits alike.

### Subtasks
- [ ] `EditCommand` gains `CameraDirectionChange` (whole collection) and `AutomationChange` (author
      timeline tracks, keys and cues, plus modulation routes). The design is below.
- [ ] One shared capture (`app::EditCapture`): snapshot the Director domains before an operation,
      diff them after, and emit one `EditCommand`. It is used by the AI sink, by the Director's
      apply, and by tests.
- [ ] `EditHistoryTransactionSink` records every domain it can diff, not only parameters.
- [ ] "Undo this task" undoes through `EditHistory`, and the panel's snapshot restore is removed.
- [ ] The Cameras panel's add, delete and cut push history.
- [ ] Regression tests (apply → undo → identical; redo → identical), each proven red.

### Acceptance criteria
- [ ] For each Director domain (sequence, camera direction, camera track, author timeline keys,
      routes, events, actors, markers, overlays), apply → undo leaves the domain's JSON identical to
      before, and redo leaves it identical to after.
- [ ] An AI task that adds a shot, a marker, a keyframe and a camera is one Cmd+Z.
- [ ] No code path restores a snapshot as "undo". Snapshots remain only as the abort mechanism of a
      failed or cancelled task.

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

### 0.3 Persistence (started)
- `tests/support/project_round_trip.hpp`: `saveAndReload` (≥ 1 frame, real `saveProject`, fresh
  engine, a frame on the far side), `missingTopLevelKeys`, `differingPaths`, `ScratchDir`.
- `tests/unit/test_director_persistence.cpp` (`[directing][persistence]`), 6 cases, all passing:
  a Director-shaped sequence (behaviour-camera shot, actor with keys, path and cues, cue markers, a
  cue-triggered event, a piece track); inline camera direction (control); by-reference camera
  direction (**was red: 2 cameras / 1 shot → 1 / 0**); untouched project writes no key and leaves
  the scene file alone; a deleted scene-file camera stays deleted; the benchmark project loads with
  0 warnings and writes no `cameraDirection` after 2 frames.
- Commit 8b498988.

## Blocked work
- Effect-domain undo, serialization and compilation: deferred until `agent/entity-effects`
  (ADR-702) merges to main. That branch already adds `EffectChange` to `src/ui/edit_history.*`.

## Architectural decisions
- ADR-750: the module is `src/directing/` (`avgen::directing`). It depends on no AI code.
- ADR-751: camera direction rides in the project (the sixth of ADR-207's family).
- 0.2 design (to become an ADR with the implementation):
  - Whole-domain before/after records, following `TimelineChange`/`LightChange`/`EffectChange`.
    `CameraDirectionChange` captures rigs with their current parameter bases.
    `AutomationChange` covers author timeline tracks, keys and cues, plus routes.
  - One `app::EditCapture` builds a command from a before/after of every domain. The AI sink and the
    Director apply both use it, so there is one definition of "what an operation changed".
  - `applyEdit` order: automation (timeline/routes) → camera direction → sequence → lights →
    composition records → rebind. A camera removal's erased tracks come back with the automation
    record, so the camera is applied after it and `setCameraDirection` finds its tracks present.
  - "Undo this task" undoes the history's command for that task (identified by the history
    `stateId` it produced), and only when it is still the newest command. Otherwise it points to
    Cmd+Z. The snapshot remains only for abort.
  - Known limit: nodes an AI tool *deletes* are destroyed, so a diff cannot restore them.
    `scene.delete_node` will detach and hand the node to the capture instead.

## Known risks
- The effects refactor and this program both touch `src/ui/edit_history.*` (`applyEdit`, the
  `EditCommand` fields). A three-way merge is likely when the effects branch lands. Adding fields in
  a separate block from `effects` reduces that risk.
- `EffectChange` and `AutomationChange` can both carry routes. If one command ever holds both,
  the order of application decides which list wins. This needs a rule when effects merge.
- Glowmere Valley 2 multicam's audio resolves outside the repository (`~/Desktop/Rebuild.mp3`).
  It exists on this machine; the benchmark test is machine-specific.
- Human edits that bypass history (sequence drags and inspectors, timeline key drags, routes, layers)
  are wider than the Director's needs. 0.2 fixes the Director's paths and the Cameras panel. The
  rest is listed above and is not in scope unless it blocks the Director.

## Test status
- Baseline (main, per brief): CPU 3,161 cases with 0 genuine failures (1 expected
  `[!shouldfail]`); GPU 415 cases. Not yet re-run in this worktree.
- `[directing][persistence]`: 6 cases, 48 assertions, all passing (release, 2026-09-24).

## Recent changes
- 2026-09-24: worktree created; 0.1 audit completed; round-trip helper; camera direction
  persistence fix (8b498988); ADR-750, ADR-751.

## Next tasks
1. 0.2: `CameraDirectionChange` + `AutomationChange` in `EditCommand`, with `applyEdit` support
   and apply/undo/redo tests.
2. 0.2: `app::EditCapture`; AI sink on top of it; "Undo this task" through the history.
3. 0.4: scratch-project preview (`Engine::writeProjectCopy`, which must not move the project path
   or the dirty baseline), and `startRenderFromUi` stops saving the user's file.
4. 0.5: capability registry audit and architecture.

## Questions requiring human decision
- **Rendering from the editor currently saves your project first**, silently, and clears the unsaved
  state (application.cpp:2447). 0.4 will make the render read a scratch copy instead. Is there a
  reason the render should keep writing the user's file? I assume not, and will proceed unless told
  otherwise.

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
- [ ] Extend EditCommand
- [ ] Add camera direction change
- [!] Add world effect change (ADR-702 `EffectChange` exists on `agent/entity-effects`; adopt after merge)
- [ ] Add timeline/key change where needed
- [ ] Cover events
- [ ] Cover actors
- [ ] Update AI transaction sink
- [ ] Remove duplicate snapshot undo
- [ ] Add regression tests
### 0.3 Persistence
- [x] Create round-trip helper (steps at least one frame before saving)
- [ ] Audit Director-targeted serialization
- [~] Add round-trip tests (sequence, camera direction)
- [~] Identify existing serialization defects (camera direction found and fixed)
- [ ] Fix defects encountered in touched domains
### 0.4 Preview isolation
- [x] Audit preview/render save path
- [ ] Design scratch-project rendering
- [ ] Implement isolated preview
- [ ] Add regression test
### 0.5 Capability registry
- [ ] Audit existing registries
- [ ] Define generated registry architecture
- [ ] Character capabilities
- [ ] Camera capabilities
- [ ] Effect capabilities
- [ ] Event capabilities
- [ ] Replace stale capability declarations
- [ ] Add tests

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
