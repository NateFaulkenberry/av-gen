# ADR-2132: a project saves its overrides, not a photograph of the run, and keeps what it does not read

- Status: Accepted (2026-11-24), eng/save-and-osc (engine architecture evaluation, 113-engine-architecture; owner
  approved the data-safety fixes first)
- Code: `params/serialization.{hpp,cpp}` (`ParameterBasis`, `originOf`, `overrideParameters`, `saveProject`'s basis),
  `app/engine_project.cpp` (`projectDocument`, `loadProject`, `kProjectKeys`, `captureParameterOrigins`,
  `resetProjectBasis`, `pinnedOverrides`, `dropPinnedOverrides`), `app/engine.cpp` (the basis reset on `loadScene`,
  `loadOrbScene`, `newComposition`, `loadComposition`), `app/application.cpp` (`--drop-pinned-overrides`)
- Tests: `tests/integration/test_project_overrides.cpp` (`[adr2132]`)

## Context

The evaluation's run 09 (`--ai-script` with `parameter.set`, `modulation.set_depth`, `project.save`) turned a project
with 2 parameters into one with **564**, reproduced here on 8731bf360: 541 bytes became 27 KB. `params::saveProject`
wrote every registered, serialized parameter: the scene's camera, every node transform and every engine default.
A project's `parameters` are applied over its scene (ADR-264), so after that save an edit to the scene file's camera
did nothing. Reproduced: a capture before and after moving the scene's camera from (0, 2.6, 9) to (1, 5, 12) gave
byte-identical PNGs. The comment above `Engine::sampleProjectDirty` had already measured the same thing on eight
examples (hero 1.3 KB -> 67 KB, night-shift 33 KB -> 1.6 MB).

The load was lossy as well:

- A parameter path the build did not register was logged and dropped, so load-then-save deleted it. This is the
  memory note's "a headless save drops 94 `atmos/*` params". It still held on main.
- `projectDocument` builds the document from scratch, so any top-level key it does not produce was deleted by a save.
  In the tracked examples that is `_note` (64 projects), `sonicScene` (41, read by `tools/sonic_vfx`),
  `_parametersNote` (2) and `audioLab` (1, read by `tools/audio_lab/lab.py`). One app save removed them.

## Alternatives considered

1. **Track "authored" writes**: mark a parameter each time the UI, a tool, MIDI learn or OSC sets it. Rejected. There
   are dozens of base-value write sites (59 `setBase` calls in `src/`, plus `setBaseComponent`), and the per-frame
   writeback and staging write through the same calls. A hand-kept list of which writes count drifts, and when it
   drifts it loses work silently (the ADR-440 argument against a dirty flag).
2. **Compare with the registered default**. Rejected. `Engine::installComposition` re-registers every parameter on an
   in-session rebuild (the assistant's rollback, `setCompositionJson`), and `installController` uses the *current*
   values as the new defaults. A node moved before such a rebuild would then equal its default and be dropped.
   `test_project_overrides.cpp` case G covers this.
3. **Compare with the value when the document was opened (the origin)**, plus keep what the file named. Chosen.

## Decision

1. **Origins.** `loadProject` snapshots every serialized parameter (`captureParameterOrigins`) after the scene,
   its `post` block, the project's lights, cameras, staging, effects, layers and palette are installed, and just
   before the project's own `parameters` are applied. At that point each value is the scene's own. A parameter
   registered later (a node added in session, a shader input) has no origin and is compared with its registered
   default. Origins survive in-session rebuilds. They are retaken only when a new document opens: `loadProject`,
   `newProject`, `newComposition`, `loadComposition`, `loadScene`, `loadOrbScene`.
2. **The rule.** A save writes a serialized parameter when its base differs from its origin, **or** when the loaded
   document named it. The second clause keeps an override the author wrote even when it equals the scene. A file is
   never silently turned into a different project.
3. **Unknown parameters are kept.** At the end of the load, every named path that no parameter answers is recorded,
   and every save writes it back verbatim. A path that was registered at load but is gone at save (its node was
   deleted in session) is not written back. Its owner went with it.
4. **Unknown top-level keys are kept.** `kProjectKeys` lists every key this build reads or writes. Any other key in
   the loaded document is written back unchanged unless `projectDocument` produced it. A key in the list is this
   build's to write or leave out, so an emptied timeline does not come back from the loaded copy. The two
   pre-ADR-702 effect keys are listed because the load names and refuses them (ADR-441). The test asserts that
   every key a save writes is either listed or carried.
5. **Already-inflated projects are reported, not rewritten.** `Engine::pinnedOverrides` lists the named paths whose
   value is exactly the origin. From 16 upward the load warns once, with the count and the command that clears
   them. A hand-written project names a few scene values on purpose; a save-everything file names hundreds.
   `--drop-pinned-overrides` (with `--project` and `--save-project`) is the explicit cleanup: it forgets those
   paths, and the save writes them only if they have changed. Nothing differing from the scene and nothing unknown
   is touched.
6. The assistant's rollback point (`ai::SnapshotStore`) still takes the full snapshot: `saveProject` without a basis.
   A rollback must restore every value, not only the overrides.

## Consequences

- Run 09 on this branch: 2 -> **3** parameters (the two it had plus the rotation the script set). The top-level
  key diff is additive (`app`, `pathtrace`, `presets`, `render`, `shaders`, `sources`; no `control`, ADR-2133), and
  the two captures around the scene-camera edit now differ. Evidence:
  `~/Desktop/av-gen-review/113-engine-architecture/02-save-and-osc-fix/`.
- An untouched round trip is byte-stable: load, save, load, save gives the same document.
- Projects written before this keep every value until somebody runs the cleanup. Tracked examples are not
  rewritten here. Which of them to clean, if any, is the owner's call, because a pinned value can be deliberate.
- The ADR-440 dirty check compares two `projectDocument`s, so it follows the new rule with no change.
- Runtime writeback that moves a base (ADR-386's per-frame writes, staging's bodies) differs from its origin and
  is still written, as it was before. The ADR-264 staging filter still removes scenario-owned transforms.

## Revisit triggers

- A subsystem whose registration happens after the origin snapshot but whose values come from the scene file (not
  from code defaults): its scene values would be compared with a code default and pinned. Capture its origin at
  registration instead.
- Unknown keys *inside* known blocks (`control`, `render`, ...) are still parsed and dropped by their owners. If a
  newer build adds fields there, those owners need the `directingPlans`-style "kept but not read" treatment.
