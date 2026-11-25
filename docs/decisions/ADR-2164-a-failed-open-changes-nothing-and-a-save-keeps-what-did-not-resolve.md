# ADR-2164: a failed project open changes nothing (or leaves an empty untitled project), and a save keeps every reference that did not resolve

- Status: Accepted (2026-11-25), eng/project-load-safety (found by the UI-readiness audit,
  113-engine-architecture/05-ui-readiness/02-project-lifecycle.md, sections 2.3, 2.4 and G1/G2)
- Code: `app/engine_project.cpp` (`preflightProject` and the readers it shares with the install, `loadProject` split
  into the checks and `installProjectDocument`, the fallback, `KeptReferences`, `unresolvedReferences`,
  `projectDocument`'s kept references), `app/engine.{hpp,cpp}` (`UnresolvedReference`, `unresolvedReferences()`,
  `audioGeneration_`), `params/serialization.{hpp,cpp}` (`validateProjectDocument`), `api/document_tools.cpp`
  (`project.validate` reports the engine's list), `ai/engine_tools.cpp` (`project.open`: `unresolved`,
  `details.previousLost`, recovery text), `api/warning_codes.hpp` (`asset.missing`'s meaning),
  `app/application.cpp` (File > Open's failure path refreshes the title)
- Docs: `docs/api/errors.md` (generated)
- Tests: `tests/integration/test_project_load_safety.cpp`, `tests/integration/test_project_unresolved_refs.cpp`
  (`[adr2164]`)

## Context

Two defects, each able to destroy a person's project without saying so, both reproduced on main:

1. **A failed open was not atomic.** `Engine::loadProject` had four early refusals (cannot open, not JSON, not a
   project, migration), which changed nothing, and about eighteen later ones plus a `json::exception` catch, which
   ran after it had reset the parameters, cleared the timeline, replaced the audio, stems, scene and environment and
   moved `projectGeneration`. A late refusal left that half-reset engine under the OLD project's path, marked dirty,
   and the next save wrote it over the old file. Measured: a 5,929-byte project with an override became 1,059 bytes
   without it after opening a document with `control.tempoSource: "bogus"` and saving. Studio's File > Open is the
   same call.
2. **A save dropped references that did not resolve.** A missing scene opened "clean" with a text warning and the
   save wrote `kind: orb`; a missing audio or environment was removed from the file; `project.validate` with no
   path called the project valid because the in-memory copy had already lost them. Worse, found while fixing it: a
   scene that failed to load left the PREVIOUS project's composition installed, so the save wrote the previous
   project's scene path into this one; and a missing environment left the previous project's environment in force.

## Decision

1. **Check, then install.** After parsing and migrating, `preflightProject` runs every reader of the load that reads
   only the document, before anything changes: the 2D composition, the palette, `params::validateProjectDocument`
   (sources, presets, the shape of `parameters`, routes), the control map and the tempo source, the shader entries'
   shape and stage, the timeline, the Auto-director, the song plan, the shot spans, the parked cut, the states, the
   world macros, the World Director, render and path-trace settings, the transport block, the sequence, the scene
   reference's `kind`, the audio-clip and stem fields and the aim-follow table. The inline `.value()` reads that
   could throw on a value of the wrong type (clips, stems, shot spans, transport) were moved into shared readers
   (`clipFromJson`, `stemFromJson`, `shotSpansFromJson`, `readTransport`), so the check and the install are the
   same code. A refusal there returns `'<file>' is not a valid project: <why>` and the open project is untouched:
   same path, same values, same revisions, same `projectGeneration` (so Studio keeps its workspace).
2. **What the checks cannot see falls back to an empty untitled project.** Everything after the checks is
   `installProjectDocument`. If it still fails, `loadProject` calls `newProject()` and returns `<why> -- the project
   could not be opened after the last one was closed, so the session now has an empty untitled project (nothing
   was saved)`. A save then has no path and cannot overwrite either file. The failures that still take this path:
   - a parameter value of the wrong type for a parameter that is registered (`"camera/exposure/compensation":
     "bright"`): whether a path names a registered parameter depends on the scene the install loads;
   - a `json::exception` from a reader that needs the engine (inside a scene, effects, heroes, lights install), and
     the "validated above; cannot fail" re-reads.
   A missing or unloadable scene, audio, environment, clip, stem or shader is NOT a failure: the project opens
   degraded, as before, with the editor able to fix it.
3. **A reference that did not resolve is kept as written.** At load, a scene (any kind whose load failed: missing
   file, parse refusal, unknown kind, bad inline composition), `assets.audio`, `assets.audioClips` (the whole
   arrangement when it could not be built, otherwise each missing clip's `file`), `assets.environment`, each
   `assets.stems[i].file` and each `shaders[i]` entry that did not install is recorded verbatim with the project
   folder it is relative to. `projectDocument` writes the recorded JSON back unchanged while the engine still holds
   what the load left in its place, judged by generations: the scene while `sceneGeneration` has not moved and the
   orb stands in; the audio while `audioGeneration` (bumped by every `setAudioClips` and a live input) has not moved
   and nothing is loaded; the environment while `environmentPath` and the scene are unchanged; a clip or stem while
   that file is still listed and still missing; a shader entry, at its original index, until the session adds that
   file itself. A scene that did not load also keeps the top-level keys that only mean something against it
   (`sceneNodes`, `heroes`, `effects`, `lights`, `cameraDirection`, `staging`, `cameraAimFollow`,
   `cameraContinuousTake`). A save into another folder rebases a relative path so it still names the same file; a
   save into the same folder is byte-for-byte what was read.
4. **The orb stands in for a scene that did not load, never the previous project's world**; and an environment that
   did not load, with no scene environment underneath, leaves no environment rather than the previous project's.
5. **The project knows.** `Engine::unresolvedReferences()` returns `{kind, pointer, path, reason}` for each kept
   reference still in force (kinds `scene`, `audio`, `audioClip`, `environment`, `stem`, `shader`; pointers
   `/assets/scene/path` (or `/assets/scene` for an inline or unknown kind), `/assets/audio`,
   `/assets/audioClips/<i>/file`, `/assets/environment`, `/assets/stems/<i>/file`, `/shaders/<i>/path`).
   `project.open` returns it as `unresolved`, and on failure says in `details.previousLost` and the recovery text
   whether the open project survived. `project.validate`, with a path and with none (the scratch copy now carries
   the kept references), reports each as an `asset.missing` error with its pointer, before its own file-existence
   pass (which keeps covering anything the engine did not try to load).
6. **Studio** refreshes the window title on a failed open: an atomic failure leaves the title as it was; a fallback
   shows "Untitled", clean, and `serviceProjectSwitch` resets the workspace because the generation moved.

## Rationale

- The checks are the readers themselves, run twice; the second run costs microseconds against a load that analyses
  audio. The rejected alternative, pre-flighting the whole load on a private engine (what `project.validate` does),
  doubles every open and still does not make the real one atomic.
- A fallback to a defined state is the guarantee that stops the overwrite even for failures nobody has listed yet.
- Keeping by generation rather than by "the engine holds nothing" means an authored replacement (an import, a scene
  opened, an audio file dropped) ends the keeping without each of those paths having to know about it.

## Consequences

- Measured with the base binary (aaeec50da) against this one, `document.get {document: project}` after
  `project.open` on 31 shipped examples: 29 identical; the 2 that differ (`rainbow-world.json`, `rift-live.json`)
  are projects whose generated scene file is gitignored and missing, and differ only in `assets.scene`, which now
  matches the file instead of `{"kind": "orb"}`.
- D5 is unchanged and green: the projects whose gitignored files are missing already had `asset.missing` or
  `scene.missing` and were already on the list; no other example gained an error.
- A degraded project's `projectDocument` equals what was read for those references, so it does not open dirty.
- The error message of a late refusal now starts `'<file>' is not a valid project:`.

## Limitations

- Fully atomic only for what the checks cover; a wrong-typed value for a registered parameter still falls back.
- Studio's File > Open still clears the undo history and the world editor before the load (`openAny`), so an atomic
  failure keeps the project and its values but not its undo stack. Unchanged here (no redesign).
- An `assets.audio` that names the same path the previous project was playing, now missing, keeps playing the
  previous buffer and is saved by its current path (size and hash re-read) rather than verbatim.
- `session.info` does not report `degraded`/`unresolved`; `project.open` and `project.validate` do.

## Revisit triggers

- A parameter-type check that can run before the scene is installed (a schema of the scene's parameters): the last
  common fallback becomes atomic.
- G3 (`ifDirty` on `project.open`), G4 (`project.close`) and G5 (`engine.requires`) from the same audit.
