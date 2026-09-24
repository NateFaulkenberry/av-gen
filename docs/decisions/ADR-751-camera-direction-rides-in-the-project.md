# ADR-751: Camera direction rides in the project, the sixth of ADR-207's family

**Status:** Accepted
**Date:** 2026-09-24
**Related:** ADR-207, ADR-230, ADR-276, ADR-330 and the lights fix (the first five of this family);
ADR-245 (multiple cameras); ADR-249 (Song Mode's `Directed` shots); ADR-264/271 (the project is
applied over its scene, and the editor does not write scene files); ADR-440 (dirty = no longer
serialises to what it was opened as)
**Implemented by:** `Engine::projectDocument` and `Engine::loadProject` (`src/app/engine.cpp`, key
`cameraDirection`)
**Tests:** `tests/unit/test_director_persistence.cpp` (`[directing][persistence]`)

## Context

The Director will compile camera rigs and camera-track shots (`scene::CameraShot`) into
`scene::CameraDirection`. The Slice 0.3 persistence probe found that this domain did not survive a
project save when the scene is saved **by reference**, which is how every shipped world is saved.

Measured before the fix: a rig and a locked shot added through `Engine::setCameraDirection` gave 2
cameras and 1 shot in the session, and 1 camera and 0 shots after a save and a reload.

The cause is the one ADR-207, ADR-230, ADR-276, ADR-330 and the lights fix each found for their own
family. `CameraDirection` lives on the `Composition`, and the project records a by-reference scene
as a path plus a hash. The project writes only the families that have their own override key, and
camera direction had none. Only "Save Scene As…" wrote it. The Cameras panel's "+ Camera", "Delete"
and "Cut to this camera" have therefore been lost on every Cmd+S of a by-reference project. An inline
composition was unaffected, because it travels whole in the project.

## Decision

The project carries a `cameraDirection` key, written only when the session's collection differs
from the scene file's. The comparison runs both sides through the same `CameraDirection::fromJson`/
`toJson`, so an untouched project writes nothing and stays byte-stable.

- **The whole collection.** This is `lights`' shape, not ADR-330's difference-by-name. A rig's
  placement, aim node and lens are not parameters, so the project owes what each rig is, not only
  which rigs exist. Rig *positions* and *targets* are already parameters (`cameras/<slug>/…`) and
  arrive through `parameters` as before.
- **Authored shots only.** `Directed` shots belong to Song Mode (ADR-249). Song Mode regenerates them
  at load from `autoDirector` and `songPlan`, which the project already saves. Writing them would
  photograph the run and make every directed project look edited. On load, any `Directed` shots the
  scene file carries are kept beside the project's authored ones.
- **Applied before `params::loadProject`**, for the lights' reason: `setCameraDirection` registers
  `cameras/<slug>/…`, and a parameter applied before its path exists is dropped with a warning.
- **A deletion is an edit.** A collection with fewer cameras than the file is written. On reload, the
  deleted camera and every cut that named it stay gone.

## Consequences

- Camera and cut edits in by-reference projects now reach renders, which load the project.
- The benchmark project (`glowmere-valley-2-multicam.json`), loaded and run for two frames, writes
  no `cameraDirection` key. Its cameras are its scene file's, and its Song Mode shots are not
  photographed.
- This is the sixth member of the family. The pattern will recur for any new `Composition`-owned
  list. The Director's round-trip helper (`tests/support/project_round_trip.hpp`) runs through the
  real save after a frame, and exists so the next member is found by a test before a user finds it.
