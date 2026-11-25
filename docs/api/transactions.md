# Transactions and rollback: what each operation's rollback restores

ADR-2153 (Engine API stage S1). This page is authored, not generated: it states the guarantee behind each
tool's `undoable` annotation, which `schemas/tool-catalogue.json` publishes. When a tool is added or its
annotations change, update this page in the same commit.

## The two kinds of transaction

- **Implicit.** A call to a `mutatesProject` tool that names no transaction runs in one of its own. If it
  fails, the session puts the project back and deletes what the call wrote (see "Files" below). The call's
  `transaction` field in the envelope names it.
- **Explicit.** `transaction.begin {label}` returns an id; calls pass it (`CallOptions::transaction`, or
  `transaction` on the wire). `transaction.commit {id}` keeps the changes as one change (in Studio, one
  Cmd+Z); `transaction.rollback {id}` puts the project back as it was at `begin`. One per session. While
  one is open, a mutating call that does not name it is refused (`FAILED_PRECONDITION`).
  A call that fails inside an explicit transaction does **not** roll the transaction back: its own files are
  removed, and the client decides what to do next.
- **Irreversible calls.** Inside an explicit transaction, a mutating call whose tool is `undoable: false` is
  refused (`FAILED_PRECONDITION`, `details.field = "irreversible"`) unless it passes `irreversible: accept`.
  Accepting marks the transaction `partial`; `commit` reports the accepted tools under `notRestorable`, and
  `rollback` reports them under `notRestored`. Studio's assistant accepts on every call, because an
  assistant task has always been able to mix an edit with a save or an import.

## What the snapshot restores

A rollback restores the document captured at `begin` (`ai::SnapshotStore::captureDocument`):

| Restored | Not restored |
|---|---|
| parameter base values (every serialised parameter) | the source rack, the audio track, stems and feeds |
| modulation routes and chains | the environment-map path, shader layers |
| presets | the transport position (the playhead) |
| the timeline (tracks and keys) | the project path, and which project is open |
| the composition (`Composition::toJson`: nodes, lights, environment, post, kept keys; ADR-2151) | files on disk (see "Files") |
| the sequence (shots, markers, overlays) | the live-quality ceilings and settings |
| | the Director's kept evaluations (`Engine::directingEvaluations`) |

A rollback is a restore, not a reverse diff: whatever else changed the same domains while the transaction
was open (a person editing in Studio's panels) is put back too.

## Files

Each tool reports the files it writes; the session keeps a compensation log (`src/api/compensation.hpp`).
On a rollback, or when the call itself fails:

1. A file the call **created** is deleted, unless:
   - the project or scene as restored, the engine's audio or environment path, or the project or scene file on
     disk names it (a save made inside the transaction cannot be un-written, so what it points at stays);
   - another transaction depends on it (another session imported the same file and was given this copy), or a
     use outside any transaction or by a transaction that committed pinned it;
   - it is a render output (artefacts are never deleted by a rollback).
2. A file the call **replaced** whose previous bytes it kept (the project's `assets/manifest.json`) gets those
   bytes back, under the same "another transaction" rule.
3. A file the call **overwrote** without keeping a copy (an import over an existing file of the same name) is
   left as it is now and reported under `notRestored.files`.
4. A file that existed before the transaction is never deleted: a tool reports `created` only for a path that
   did not exist when it wrote it.

The rollback result lists `files.deleted`, `files.restored` and `files.kept` (each kept file with its reason).

## Per tool

Read-only tools (execution class `read`) change nothing and have nothing to roll back. The rest:

| Tool | Class | `undoable` | A rollback restores | A rollback does not restore |
|---|---|---|---|---|
| `parameter.set`, `parameter.reset` | instant | yes | the base value | — |
| `scene.set_node_transform`, `scene.set_node_visible` | instant | yes | the node's parameters | — |
| `camera.set`, `camera.frame_node` | instant | yes | the camera parameters (and mode) | — |
| `environment.set` | instant | yes | the atmosphere parameters | — |
| `modulation.set_depth` | instant | yes | the route's amount | — |
| `modulation.create`, `modulation.remove` | rebind | yes | the route list, re-bound | — |
| `sequencer.add_keyframe`, `sequencer.remove_track` | rebind | yes | the timeline | — |
| `sequence.add_marker`, `sequence.add_shot`, `sequence.add_overlay` | rebind | yes | the sequence, re-installed | — |
| `scene.create_node`, `scene.delete_node`, `scene.set_parent` | structural | yes | the composition and its parameters | the model file a created node uses (it is the import's, not the node's; the node's use of it keeps it through another transaction's rollback, and a project saved naming it by id or path keeps it through this one's) |
| `field.create` | structural | yes | the composition | — |
| `document.patch` | structural | yes | the scene document as it was before the patch (the composition and its parameters, through the snapshot), with the session's overrides and routes as they were. A refused patch (an operation that does not apply, a new schema error or unknown key, a document the parser refuses) changes nothing, inside or outside a transaction; a failure inside the install puts the pre-patch document back itself (ADR-2157). `dryRun` changes nothing | entity simulation state: the rebuild restarts it, and so does the rollback's |
| `document.get`, `document.validate`, `project.validate` | read (validators: job) | — | nothing to restore: they build any private engine they need and write only scratch files, which they remove | — |
| `world.generate` | structural | yes | the composition (the terrain's ecology) | — |
| `project.restore_snapshot` | structural | yes | the document before the restore | — |
| `asset.import` | session | no | the copied model, texture or environment and its glTF dependencies are deleted, and `assets/manifest.json` gets its previous bytes back (or is deleted when the import created it), under the file rules above. A duplicate import copies nothing; it records a use of the existing file, so that file outlives a rollback of the import that made it while this one depends on it. Each copy is written under a dot-name and renamed into place (ADR-2156), so a failed or interrupted copy never sits at the name its id resolves to | a file it overwrote (same name, other bytes); a manifest rewrite that upgraded a version 1 file is undone with the rest (the old bytes come back). Not staged until commit: the returned id must resolve inside the same transaction (ADR-2156, Staging) |
| `asset.import_audio` | session | no | the copied track is deleted, unless the engine still plays it or a saved project names it (both are true right after a successful import, so in practice it is kept and reported) | the engine's loaded audio (not in the snapshot) |
| `project.save`, `project.save_as` | session | no | — | the written project and scene files |
| `project.create`, `project.open` | session | no | — | the replaced session, and the files `project.create` wrote (the project file and, with `scene: "empty"`, `<stem>.scene.json`; they are never deleted by a rollback). A rollback after one restores the **old** project's document into the newly opened engine: do not open or create a project inside a transaction you may roll back |
| `sequencer.set_playhead` | session | no | — | the transport position |
| `performance.set_live_quality` | session | no | — | the live-quality settings |
| `performance.optimize_scene`, `performance.apply_low_risk_optimizations` | job | no | — | the live ceilings they add to the project when the child process answers |
| `performance.profile_scene`, `performance.benchmark_before_after` | job | no | — (read-only: a scratch copy in a child process) | — |
| `director.record_plan` | job | no | — (it proposes; nothing changes until a person approves, and the approval is its own transaction) | — |
| `director.evaluate` | job | no | — | the evaluation record it keeps with the plan |
| `transaction.begin`, `transaction.commit`, `transaction.rollback` | session | no | they are the mechanism; never wrapped in a transaction | — |
