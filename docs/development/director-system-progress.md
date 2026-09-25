# AV Gen Director System — Development Progress
Last updated: 2026-09-25
Current branch: `agent/director` (worktree `../av-gen-director`; agent/motion merged at d4854cb4, main 9fa84413 at aa84e42d)
Current commit: see Recent changes
Overall status: Slices 0-2 complete; Slice 3 compile side complete (ADR-761); Director panel (ADR-762); Slice 4 researched and proposed (ADR-763), interface requested from the Motion lead; Slice 5 cost harness incl. seek

## Executive status
**Slice 0 is complete, including the effect items deferred until ADR-702 merged. Slice 1 is complete.**

A Director Plan is a versioned engine document. It is saved in the project as the provenance of
its content, so a follow-up request revises the earlier plan. The resolver turns names into
subjects ("Umbra" is ambiguous; "the Umbra hero mushroom" is `umbra-cap`) and musical time into
seconds ("the second chorus" is 118.6 s on the benchmark). The validator checks capabilities,
clearance, timing, camera precedence and determinism per item. The compiler builds the feasible
part as ordinary native content in a staging copy, with a diff that reads as intent.

Through the AI control plane, a model proposes a plan with `director.propose_plan`, and the task
waits in `AwaitingApproval` having changed nothing. On approval, the plan is re-compiled against
the current project, installed in one transaction (one undo labelled with the request), and
verified against the plan's fingerprints. There is no apply tool: applying is the person's act.

The Rook/Umbra benchmark (spec §34) meets the requirements this build can meet:
- the backflip is refused, naming fall/jump/land;
- the jump needs 5.75 m against a 1.1 m apex;
- the shot and a low-angle chase on Rook's node are built, with a locked cut;
- the effect cues are blocked on the impossible flip;
- it applies on approval as one undo, survives save/reload, and recompiles deterministically.

Since then:
- Performances compile (ADR-759).
- The chase rises over Rook and passes him, as keys on the follow rig's offset (ADR-760).
- On the Motion lead's M1-M5, jumps fly the engine's one arc with the jump clip played once and
  fitted to it; landings, slow motion and the peak marker compile (ADR-761).
- Per the owner, the Umbra leap stays refused: 5.75 m needed against Rook's 1.1 m highest jump,
  with "a character with a larger jump" or "a different path" offered instead.

## Overall progress
| Slice | Status | Progress | Tests | Notes |
|---|---|---:|---:|---|
| 0 Foundations | Complete | 95% | 23 `[directing]` cases | Remaining: human UI edit paths other than Cameras still bypass history (out of Director scope) |
| 1 Plan + Cameras | Complete | 95% | 26 `[directing]` cases + 10 golden plans | Remaining: the Director panel beyond Approve/Reject (spec §35 says do not overbuild); UI not verified |
| 2 Scripted Performances | Complete | 100% | 16 cases + 1 golden | The handoff is M1 (merged from main; this branch's copy dropped). Non-zero `entrySeconds` is NON_DETERMINISTIC in a baked plan |
| 3 Airborne + Events | Compile side complete | 85% | 5 `[directing][airborne]` cases + 2 goldens | ADR-761. Remaining: `fall`; acrobatics need assets; UI not verified |
| 4 Autonomous Direction | Researched | 10% | 0 | ADR-763 proposed: goal -> F7 `CharacterGoal`; live until recorded; interface requested (goal event, semantic events, clip readout, checkpointed considerer) |
| 5 Verification + Scale | Started | 30% | 11 golden plans, `[.perf][directing]` CPU + GPU + seek | Cost harness over the golden plans, and seek after ADR-800 |

## Current focus
### Task
Slice 2: scripted performances. The core is in (f009638d). Next is agreeing the entity-side interface
with the Motion lead (`agent/motion`), who now owns motion mechanics in `src/entity/`.

### Subtasks
- [x] Entity/actor handoff probe (spec §25), measured; decision recorded (ADR-758)
- [x] Performance → `seq::Actor`: run_to, walk_to, run_past, walk_past, run, walk, hold, look_at (ADR-759)
- [x] Entity/actor handoff and restoration (performers on play and replay; handed back at the end pose)
- [x] Character event markers (computed), cues on them
- [x] Keyed chase camera: not needed. A follow rig on the performed node is deterministic (ADR-759)
- [x] Ownership of the entity-side pieces decided: the Motion lead takes them as M1, starting from this
      implementation; this branch drops its copy after M1 lands on main (ADR-758 addendum)
- [ ] When M1 adds `entrySeconds`: validator flags a non-zero entry blend as live-dependent
- [x] Full suites at 6bd98d5f: CPU 3,295 cases, exit 0 (1 expected shouldfail); GPU 443 cases,
      1 failure (`Watching a render does not change it`), a collision on the shared fixed temp
      directory `$TMPDIR/avgen_render_job` with another agent's concurrent GPU run. It passes alone,
      twice; nothing in this branch touches render jobs.

### Acceptance criteria
- [x] Inside its span the body is exactly where the performance says, on the terrain, in the right
      gait; outside, the entity owns it; after, it continues from the end mark
- [x] A seek lands on the performance (checked against the actor, not against a play); a changed
      performance invalidates checkpoints
- [x] Computed event times are the performance's actual moments (closest approach, arrival)
- [x] One undo; survives save/reload (golden plan)

## Completed work

### Slice 0 (complete)
- **0.1 audit** (recorded below).
- **0.2 unified undo:**
  - `AutomationChange`, `CameraDirectionChange` and `PlanListChange` join ADR-702's
    `EffectChange` in `EditCommand`.
  - `app::EditCapture` measures parameters, sequence, cameras, automation, routes, nodes, plans
    and effects. Routes live only in `AutomationChange`.
  - The AI sink uses the capture, and "Undo this task" is Cmd+Z.
  - The transaction opens, commits and rolls back on the main thread.
  - The Cameras panel is undoable.
  - ADR-752 (+ addendum).
- **0.3 persistence:**
  - The round-trip helper steps a frame before saving and compares after a real reload.
  - Found and fixed: camera direction was lost on by-reference saves (ADR-751).
  - Effect instances and effect windows round-trip; Director Plans round-trip, including an
    unreadable newer plan, which is kept verbatim.
- **0.4 preview isolation:** renders read a scratch copy (ADR-753). The staging copy (`Staging`)
  is the dry-run representation.
- **0.5 capabilities:** characters, cameras, events and effects are generated from engine data;
  `capability.list` is derived from the tools (ADR-754 + addendum).

### Slice 1 (complete)
- **1.1 Director Plan** (`src/directing/plan.*`, `issue.*`; ADR-755):
  - versioned, canonical JSON; keyed items; a subject table;
  - shots with semantic camera moves; markers; performances; cues on parameters or ADR-702
    effects; local retimes;
  - provenance; `produced` content refs with fingerprints;
  - project key `directingPlans`; undoable with its content;
  - `planSchema()` is the contract, generated from the vocabularies.
- **1.2 Resolver** (`resolver.*`, `time_ref.*`, `text.hpp`):
  - subjects: one identity per thing, kind hints, exact before partial, never a silent choice;
    effects by owner and type;
  - time: clock, bar/beat, and sections counted as runs, from the authored timeline first;
    constant-tempo fallback with a warning.
- **1.3 Validator** (`validator.*`; ADR-756): reference, capability, spatial clearance, timing and
  overlap, camera support and event-camera conflicts, determinism, author-key protection, hand
  edits, and not-yet-compiled items. Every issue names its item; errors block that item and its
  dependents.
- **1.4 Compiler** (`compiler.*`):
  - Places are framed by `seq::ShotCamera` moves; characters are followed by `CameraRig`s. Every
    shot writes both shot types.
  - Markers.
  - Parameter and effect-field cues compile to baked Add events; effect activation compiles to a
    windowed copy of the owner's instance.
  - Revisions replace item-whole and never overwrite hand edits; rigs are revised in place.
  - The diff reads as intent. `app::installCompilation`, `verifyInstalled`, `applyCompilation`.
- **1.5 Agent integration** (`src/ai/director_tools.*`, orchestrator; ADR-757):
  - seven `director.*` tools;
  - the AwaitingApproval / Committing / Rejected states;
  - approval re-checks the diff, installs in one transaction, and verifies;
  - reject and cancel change nothing;
  - no apply tool;
  - Approve/Reject in the AI panel (not verified in the UI).
- **Golden plans** (spec §39, pulled forward): ten fixtures and one harness, including a recompile
  on the reloaded project that must rebuild byte for byte. That check found and fixed three
  revision defects.
- **Commits:** e1b22fab (1.1/1.2), 6f1338c8 (1.3/1.4), e7104b8d + 61ec54de (effects), ba21a9dc
  (1.5), 8935a24b (golden plans).

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

## Blocked work
(none)

## Architectural decisions
- ADR-750: `src/directing/` (`avgen::directing`), no AI dependency; a boundary test enforces it.
- ADR-751: camera direction rides in the project.
- ADR-752: one undo for every domain the Director writes (+ effects addendum).
- ADR-753: a render or a preview reads a scratch copy.
- ADR-754: capabilities are generated, never listed (+ effect catalogue).
- ADR-755: the Director Plan, its identity (id, revision, `produced` with fingerprints) and its
  resolution.
- ADR-756: validate per item, compile the feasible part, never overwrite a hand edit (+ effect cues,
  + revisions byte for byte).
- ADR-757: a proposed plan waits for the person; approval re-checks, installs in one transaction,
  and verifies.
- Implementation choices from the owner's rulings:
  - Plans are saved as provenance (ADR-755).
  - "The Umbra hero effect" is `umbra-cap-hero-pulse`. Activating it adds a windowed *copy*, so
    Umbra's own hero-focus pulse is unchanged (ADR-756 addendum).
- Bindings for Slice 2:
  - `seq::install` owns every track on a parameter it bakes to.
  - Event times come from the compiler, never the model.
  - Characters' positions are the simulation's, so framing a character needs a performance.

## Known risks
- **Known false positive:** the sequencer bake warns "first shot inherits its camera" for every
  Director shot that cuts to a rig. The bake cannot see the camera track (ADR-245). A fix belongs in
  the bake. The warning appears in project warnings on reload, which a person may find confusing.
- Only one task can wait for approval at a time. "Modify" and "Regenerate" are a new request after a
  reject, not yet a revision of the waiting proposal.
- Approval re-compiles against authored places, not the live simulation (by design, ADR-756).
- The AI sink's parameter diff can include engine-driven base writes during a long task. This is
  pre-existing, and more visible now that a task is one command.
- Glowmere's audio is `~/Desktop/Rebuild.mp3`, outside the repository, so the benchmark tests are
  machine-specific.
- Not verified in the running UI: the Cameras panel's history pushes, the AI panel's Undo /
  Approve / Reject, and the render and queue scratch-copy wiring.

## Test status
- `[directing]`: 49 cases (persistence 6, undo 5, preview 2, capabilities 6, plan 8, resolver 5,
  compile 5, effects 5, agent 6, golden 1 over 10 fixtures) plus a `[performance]` measurement, all
  passing in release.
- **TSan** (Debug+TSan, this worktree): `[ai]` + `[directing][agent]`, 90 cases, 0 warnings, after
  the approval states were added.
- **Full suites after the ADR-702 merge (at 61ec54de):** CPU 3,233 cases, exit 0 (main's 3,192 +
  41), 1 expected shouldfail, 16 hardware skips; GPU 419 cases, exit 0, 1 skip (NDI runtime not
  installed).
- **Full suites on the final Slice 1 state (8935a24b):** CPU 3,241 cases, exit 0 (main's 3,192 +
  49), 1 expected shouldfail, 16 hardware skips; GPU 419 cases, exit 0, 1 skip (NDI). One summary
  each; only this worktree's paths.
- **Full suites after merging main 232a50d7 (Effect Library Wave 1) at 6f0da410:** CPU 3,282
  cases, exit 0, 1 expected shouldfail, 16 hardware skips; GPU 443 cases, exit 0, 1 skip (NDI).
  Note: selecting `[effects]` by tag also runs main's hidden `[.known-defect]` "UFO stack at 150 s
  is the same played and scrubbed". It fails identically, as the known scrub-versus-play defect;
  it is not part of the default suite and not a regression.
- The Director does not depend on "seek to T equals play to T" anywhere. Golden plans compare
  compiled content and fingerprints, and the round-trip steps one frame from zero.
- **Costs (spec §37), `[.perf][directing]` on both binaries, release, minima of 3, 2026-09-24:**
  - Per golden plan (CPU): facts 0.75–0.78 ms; parse ≤0.03 ms; compile ≤0.12 ms; apply 3.9–6.1 ms;
    `seq::install` alone ≤0.06 ms; the next frame 2.1–2.3 ms (steady frame 2.26); undo ≤2.4 ms.
  - No golden plan's apply re-flattens the composition or bumps `textureVersion` (asserted). A forced
    `Composition::rebuild` frame costs 384 ms, which is what an apply would cost if it caused one.
  - GPU, 640x360: a full `SceneRenderer::uploadTextures` re-upload is 53 textures, 942 ms (a 968 ms
    frame against a 17.8 ms steady one). The frame after each golden apply uploads 0 textures.
  - **Seek** (`Engine::seekSeconds`, post-ADR-800, cull lifted, no audio, fresh engine per target;
    load high from concurrent suites):
    - Cold seek (nothing checkpointed) to 30/60/90 s: 609/955/1,592 ms with no plan, and
      459/887/1,371 ms with `rook_hop` applied. Almost all of it is `EntityWorld::seek`.
    - A repeated seek to the same instant: 12–28 ms.
    - The performer does not make a seek dearer.
- **Full CPU suite at the ADR-760 state (before the last test fix):** 3,298 cases, 1 expected
  shouldfail, 16 skips, 1 real failure. The failure was in my new revision test, which read a rig
  pointer the revision had replaced. It is fixed, and `[directing]` now has 65 cases, all green.
  The full suite has not been re-run since that fix. **Full GPU suite at 7ee369e3:** 444 cases,
  exit 0, 1 skip (NDI).

## Recent changes
- 2026-09-25: Director panel (4dcb8f1f, ADR-762): headless capture in
  `~/Desktop/av-gen-review/15-director-panel/`. Slice 4 proposal (ADR-763).
- 2026-09-25: merged agent/motion (d4854cb4), dropping this branch's copy of the handoff; merged
  main 9fa84413 (aa84e42d). Slice 3 on M2-M5 (ADR-761). `entrySeconds` validator rule.
- **Full suites at ce92f3ba (Slice 3):** CPU 3,329 cases, exit 0 (1 expected shouldfail, 16
  hardware skips); GPU 444 cases, exit 0 (1 NDI skip).
- 2026-09-24: ADR-760 `rise_over` / `pass` (45498d03); cost harness `[.perf][directing]` (7ee369e3);
  per-process test temp directory (f61ce286, also on `fix/test-tmpdir-per-process` as 3efe55e3).
- 2026-09-24: Slice 2 core (f009638d): handoff probe → ADR-758; performance compiler → ADR-759.
- 2026-09-24: merged main (ADR-702) at 423fdf2b; effects joined the Director (e7104b8d); Slice 1.5
  (ba21a9dc); golden plans (8935a24b).

## Next tasks
1. Slice 2.0: the entity/actor handoff probe (spec §25). Research, then regression tests. No
   performance compilation before the model is established.
2. Slice 2: compile scripted performances to `seq::Actor` (run_to, walk_to, run_past, hold,
   look_at), with plan-time events as Cue markers so cues `on` them compile.
3. Slice 3: time-varying camera behaviour state (`rise_over`, `pass`), one-shot clip semantics, the
   airborne compiler reusing `Airborne`, and performance-local retime.
4. Consider fixing the bake's first-shot warning to respect the camera track (engine; small).

## Slice 3 plan: the compile side, against the Motion lead's M3-M5

The Motion lead owns the motion mechanics. The Director compiles onto them. These are the interfaces
this side will use, and what it does with each:

- **M3, clip semantics and events.** A per-rig table: activity, loop/once, length, events `takeoff`,
  `peak`, `touchdown`, `plant.<joint>` in clip seconds. `ClipCue` gains `playback` (auto/loop/once)
  and `then`.
  - **Director use:** the capability card reads the table, so its `loops` field becomes truthful,
    and it lists each clip's events.
  - One-shot beats (`jump`, `land`) compile to `ClipCue{playback: once, then: <gait>}`.
  - A beat's `emits` resolves to a clip event mapped to timeline seconds (cue time + event time /
    speed), which is plan-time and baked (spec §30–§31).
- **M4, the jump arc.** A pure function shared with `Airborne`: from, to, apex → trajectory; a
  minimum apex to clear an obstacle; a landing check; a per-character jump capability in data.
  - **Director use:** the validator's clearance check calls M4's minimum-apex function instead of its
    own comparison, and the jump envelope on the card comes from M4's data.
  - A `jump` beat compiles to actor keys sampled from the M4 trajectory, with a Jump/Fall/Land clip
    sequence and `rook.jump_peak` at the arc's apex time.
  - `clearanceMetres` feeds M4.
- **M5, local retime of an actor.** `PlanRetime` compiles to M5's retime on the performance's actor
  (stretched keys, clip speeds) over the window, plus the frame-echo cue as today. Global time warp
  remains out of scope (spec §33).
- **Camera: done (ADR-760).** `rise_over` / `pass` are keys on the chase rig's `followOffset`
  channel. The change is scene-side only; no entity or motion code was touched.
- **Benchmark target after M3/M4:** "Rook runs to Umbra, jumps (not over it: clearance fails), lands,
  runs on", with the peak marker and the Umbra pulse at the computed peak. The backflip stays
  `CAPABILITY_UNAVAILABLE` until an asset exists.

## Known issues found in Slice 2 (reported, not worked around)
- `tests/rendering/test_render_job.cpp` writes to a fixed, shared temp directory
  (`avgen_render_job`), which collides with any other agent's concurrent GPU run (docs/testing.md's
  shared-scratchpad family). It should be namespaced by pid.
- **A seek on an engine that has never stepped a frame** lands differently from the same seek once it
  has: the same engine, same inputs, seeking to 8 s twice gave (3.864, 1.035) then (-2.828, 2.828) on
  an orbiting body. This is in the seek investigation's family (the coordinator's item 3). Tests warm
  engines with one frame and say why.
- `seq::Actor::headingAt` returns degrees; its header says radians.

## Questions requiring human decision
- None blocking. For Slice 2, not blocking yet: when a scripted performance drives Rook's node for
  a shot, should his autonomous simulation be paused for the span (so he resumes where the
  performance leaves him), or should the performance start from wherever the simulation has him?
  The handoff probe will show what the engine supports; I will bring a recommendation.

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
- [x] Add world effect change (ADR-702 `EffectChange`, captured by `EditCapture`)
- [x] Add timeline/key change where needed
- [x] Cover events
- [x] Cover actors
- [x] Update AI transaction sink
- [x] Remove duplicate snapshot undo
- [x] Add regression tests
### 0.3 Persistence
- [x] Create round-trip helper (steps at least one frame before saving)
- [x] Audit Director-targeted serialization
- [x] Add round-trip tests (sequence, cameras, effects, plans, golden plans)
- [x] Identify existing serialization defects (camera direction found and fixed)
- [x] Fix defects encountered in touched domains
### 0.4 Preview isolation
- [x] Audit preview/render save path
- [x] Design scratch-project rendering
- [x] Implement isolated preview (scratch copy for renders; staging copy for the Director dry run)
- [x] Add regression test
### 0.5 Capability registry
- [x] Audit existing registries
- [x] Define generated registry architecture
- [x] Character capabilities
- [x] Camera capabilities
- [x] Effect capabilities
- [x] Event capabilities
- [x] Replace stale capability declarations
- [x] Add tests

## Slice 1 — Director Plan v1
- 1.1 Director Plan: [x] schema · [x] versioning · [x] serialization · [x] references · [x] time
  representation · [x] shots · [x] cameras · [x] markers · [x] cues · [x] effects · [x] provenance ·
  [x] determinism tier
- 1.2 Resolver: [x] subject resolver · [x] alias resolution · [x] ambiguity handling · [x] time
  parser · [x] musical section resolver
- 1.3 Validator: [x] reference · [x] capability · [x] timing · [x] shot overlap · [x] camera ·
  [x] determinism · [x] effect validation · [x] conflict diagnostics
- 1.4 Compiler: [x] shot · [x] camera · [x] marker · [x] cue · [x] effect · [x] sequence compilers ·
  [x] diff generation
- 1.5 Agent integration: [x] plan output contract · [x] AwaitingApproval state · [x] semantic tools ·
  [x] scripted-provider tests · [x] apply transaction · [x] undo

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
[ ] preview thumbnails · [ ] optional vision critique · [x] Director benchmark harness (costs) ·
[x] golden plans (10) · [~] performance benchmarks (Director costs measured) · [ ] seek optimization · [ ] MCP exposure ·
[ ] external-agent integration tests
