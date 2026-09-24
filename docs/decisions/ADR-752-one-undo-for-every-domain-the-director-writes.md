# ADR-752: One undo for every domain the Director writes

**Status:** Accepted
**Date:** 2026-09-24
**Related:** ADR-092 (edit history), ADR-101 (edit system; AI edits land in the same history),
ADR-094 (AI control plane, transactions), ADR-245 (cameras), ADR-702 (effects; its `EffectChange`
is the same shape and lands when that branch merges), ADR-750 (the Director module)
**Implemented by:** `ui::AutomationChange`, `ui::CameraDirectionChange`, `ui::capturedCameraDirection`,
`ui::editCameraDirection`, `applyEdit` (`src/ui/edit_history.*`); `app::EditCapture`
(`src/app/edit_capture.*`); `app::EditHistoryTransactionSink` (`src/app/ai_edit_sink.*`);
`ai::TransactionSink::committedEditState`, `ai::TaskOutcome::editState`, `Transaction::abandon`, and
the orchestrator's main-thread open/commit/rollback (`src/ai/`); `ui::taskUndoState`
(`src/ui/ai_panel_logic.hpp`); the Cameras panel (`ControlPanel::drawCameras`)
**Tests:** `tests/unit/test_director_undo.cpp` (`[directing][undo]`), and the existing `[ai]`,
`[edits]`, `[undo]` and `[camera]` suites

## Context

The Director will apply a compiled plan as one operation touching several domains at once: the
sequence, the camera collection and camera track, author timeline keys, routes and parameter bases.
The AI control plane does the same over many tool calls. The Slice 0.1 audit found:

1. `EditCommand` had no record for the camera collection or the author timeline.
2. `EditHistoryTransactionSink` recorded **parameter bases only**. An AI task's shots, markers,
   keyframes, routes, nodes and fields never reached the undo stack.
3. **"Undo this task" could never appear in the application.** The panel showed it only when the
   outcome carried a snapshot id. The orchestrator obtained that id with
   `dynamic_cast<SnapshotTransactionSink*>(sink_)`, but the application installs the history sink,
   so the cast always failed. Where the button *was* reachable, it restored a whole-document
   snapshot (`setCompositionJson`) outside the history and left the stack stale. That made it a
   second undo mechanism.
4. The orchestrator opened, committed and rolled back the transaction **on the worker thread**,
   while the sink read and wrote the engine that the frame loop was using.

## Decision

**Whole-domain records, on the existing precedent.** `TimelineChange` (the sequence), `LightChange`
and ADR-702's `EffectChange` already record a small domain whole, before and after. Two more records
follow that pattern:

- `AutomationChange`: the whole `params::Timeline` (tracks, keys, cues), plus the route list when
  routes moved. Copies are unbound the moment they are captured, because an operation that deletes
  a camera frees the parameters a bound copy would still point at. This was found as a segfault
  while writing the tests.
- `CameraDirectionChange`: the collection, with each rig's current `cameras/<slug>/*` **bases**
  written into its struct (`capturedCameraDirection`). A restored camera registers its channels
  from the struct, and `ParameterSet::add` keeps an existing base but seeds a new one from the
  struct, so without this a deleted camera that had been moved came back where it was created.
- `applyEdit` order: **the sequence, then automation, then cameras**, then the rest.
  - The sequence goes first because the automation record holds the whole timeline, including the
    tracks the sequence baked. Installing the side's sequence first re-bakes exactly those tracks
    (a bake is pure) and sets the engine's owned targets to match, and then the timeline overwrite
    restores every track in its captured order. In the other order, a redo duplicated every baked
    track: the install erases only the targets the *outgoing* install owned. The redo arm of the
    capture test found this once actors and events were added to it.
  - Cameras go last because a camera being restored must find its tracks already present, and
    `setCameraDirection` erases the tracks of a camera that leaves.
- `seq::install` owns **every** track on a target the sequence bakes to, and erases an author track
  that shares one on the next install. That is the engine's rule, not the history's. The Director's
  compiler must therefore never put author keys on a parameter a baked cue drives.

**One measurement of "what an operation changed": `app::EditCapture`.** `begin` snapshots every
domain; `finish` diffs them and returns one `EditCommand`, already applied. It covers parameter
bases, the sequence (compared by document), the camera collection, author automation, added nodes
and parent changes. The AI sink, the Director's apply and the Cameras panel all use it. A camera
that only *moved* is a parameter edit, not a new collection, because the decision is made on the
authored collection. Sequence-owned tracks are masked out of the timeline comparison, because they
come back with the sequence record.

**"Undo this task" is Cmd+Z.** `TransactionSink::committedEditState()` returns the history state the
commit produced, and `TaskOutcome::editState` carries it. The panel offers the undo only while that
state is still current (`taskUndoState`); otherwise it points to the history. The snapshot restore
was removed from the panel. Snapshots remain only as the transaction's **abort**: a failed or
cancelled task is rolled back to the document it started from, which is a different promise from
undo.

**The transaction runs on the pumping thread.** The open, commit and rollback are marshalled through
`MainThreadQueue`. Rollback and commit use a fresh cancel token, because a cancelled task is exactly
the one whose rollback must still run. If the queue has shut down, the transaction is `abandon`ed
rather than touched from the worker.

**The Cameras panel** (add, delete, lens, Auto-director eligibility, cut, remove cut, place here)
now pushes one command per gesture. A lens drag is one command, from press to release.

## Consequences

- An AI task that adds a shot, a marker and a keyframe is one Cmd+Z. Measured in the tests; before
  this change the history was empty after such a task.
- There is no code path that restores a snapshot as "undo".
- **Known limit:** a node a tool *destroys* cannot be brought back by a diff taken afterwards.
  `EditCapture::unrecoverable()` names such nodes and the sink logs them. The task's abort still
  covers them. The Director does not delete nodes. `scene.delete_node` should hand its nodes to the
  capture instead of destroying them. That is recorded as a follow-up, not done here.
- **Merge note for ADR-702:** `EffectChange` also carries routes. If one command ever holds both an
  `EffectChange` and an `AutomationChange` with routes, the one applied last wins. When the effects
  branch merges, `EditCapture` must take over effect-list capture, and route capture must live in
  exactly one of the two records.
- Human edits that still bypass the history are listed in the progress record: sequencer drags and
  inspectors, timeline key drags, the Routes tab and layers. They are out of scope unless they block
  the Director.
- Not verified in the running UI: the Cameras panel's pushes, and the AI panel's button. Both call
  code that is unit-tested (`editCameraDirection`, `EditCapture`, `taskUndoState`), but the draw
  code is not.
