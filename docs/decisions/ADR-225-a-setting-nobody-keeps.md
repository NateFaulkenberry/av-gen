# ADR-225: A setting the application does not keep is not a setting

**Status:** Accepted
**Date:** 2026-09-15

## The defect

`AutoDirectorSettings` — every control in the Auto-director panel — lived on `DirectorState`, which
is a member of the running `Application` and of nothing that is written anywhere. The panel edits it
in place, `directEngine` reads it, and at quit it goes with the process. Opening the project again
did not restore the direction it was cut with.

For this struct the defaults are the settings the controls exist to move away from:

| control | default | what the default means |
|---|---|---|
| `maxViewRate` | 0 | **off** — no cap on how fast the view swings |
| `maxCameraSpeed` | 0 | **off** — no cap on how fast the camera travels |
| `dwellShots` | 1 | a new subject every shot, which is the thing ADR-203 was written to fix |

ADR-200 added the two caps because "moving too fast" was the complaint; ADR-203 added the dwell
because importance could not express it. All three came back off, every launch, without saying so.

## The decision: the project file

`Engine` gains an `AutoDirectorSettings autoDirector_`, `saveProject` writes it and `loadProject`
reads it. It is a sibling of `cameraAimFollow` and `cameraShotSpans` and belongs with them for the
same reason those two do: they are *the cut*, and this is what the cut was made from. A project
reopened a month later is directed the way its author directed it, and — the half that is not about
memory at all — **an offline render loads a project document**, so a setting that lived only in the
panel was a setting no exported frame ever saw. That is the lesson ADR-207 recorded for the shot
spans, arriving again.

Three details.

**The key is `autoDirector`, not `director`.** `director` is already the World Director's knob
mappings (ADR-088), written a few lines further down the same function. Two blocks under one key is
one block and the second write wins. This is not hypothetical: the first version used `director`,
and the round-trip test caught it as a *load failure* — the loader was handed the other subsystem's
document and refused it. Worth recording, because the collision would have been silent in the other
order.

**The keys inside it are `--director`'s keys.** `mode`, `minShot`, `maxSpeed`, `maxSwing`, `dwell`
and the rest are already the spelling the command-line flag uses for the same fields. One spelling
for a setting whether it arrives from a command line or a file.

**A value outside `validate()`'s range is refused, not clamped.** A project is written by this
application, so the only route to one is a hand edit or a file from a build that meant something
else by the key — and a silently different film is worse than a message. `dwell` accepts 1..12
(ADR-203) and nothing else.

**Absent is the ordinary state, not a fault.** A project written before this block existed loads
with the defaults and no warning, and the block is *written* only when the author has moved
something — so a project nobody directed keeps the file it had, and a save/load/save on one is
byte-stable in this block.

### Rejected: the application's global settings

The first implementation put it in `AppSettings`, beside the canvas render scale and the AI
provider, on the argument that a cap on camera speed is a statement about this viewer rather than
about the piece. That is wrong for the case that matters. The Auto-director's settings *are* part of
the work: a piece cut with six shots per subject and a 8 deg/s swing is a different film from the
same piece at the defaults, and a global preference would silently re-cut somebody else's project
to this machine's taste when it was opened. It also leaves an offline render — which sees only the
project document — directing from defaults the author never chose.

## How the two copies stay in step

The panel edits `DirectorState::settings`, which is what `refreshDirection` re-cuts from; the
engine's copy is what the file is written from. Neither can be told when the other moves, so
`Application::syncDirectorSettings` reconciles them once a frame:

* **The engine's copy moved and the panel's did not** — a project was loaded. The panel's copy
  follows. Checked first, deliberately: a load landing on the same frame as a slider drag is the
  load winning, because the file is what the author saved and the drag is on a shot that no longer
  exists.
* **The panel's copy moved** — the engine's follows, in memory. There is nothing to debounce here,
  which is the nicest consequence of this home over the other one: a project is written when
  somebody asks for it, so a slider drag costs an assignment rather than sixty file renames a
  second.

Reconciling rather than hooking means no load site has to remember: a project opened from the
command line, from the menu, or by a file drop all arrive the same way. `storeOutputsToProject`
calls it once more immediately before a save, so a headless `--direct --director=... --save-project`
run, which never reaches the frame loop, still records the direction it was given.

## Evidence

`tests/integration/test_project_system.cpp`, four cases.

The expected values are literals written down in the test and compared against what comes back out
of the file — read as a JSON document first, so what is asserted is what is in the file rather than
what a loader chose to make of it, and then through a second `Engine` that is checked to have
started on the defaults before it loads.

* all ten fields, each moved off its default, survive save and load
* a project with no block loads silently onto the defaults — asserted on an engine whose director
  was non-default first, so "the defaults came back" is a statement about the load
* save → load → save leaves the block identical
* `dwell` of 40 and of 0, and a `mode` of `improvised`, are each refused; the same document with
  both put right loads, so the three refusals are about the fields they name

The negative control is the defect: with the one line that writes the block removed and nothing else
changed, the document has no `autoDirector` key and the round trip stops being stable.

## What this does not cover

`Application::syncDirectorSettings` is in `application.cpp`, which is not compiled into the test
binary (it has no window layer). The serialisation and the project round trip are tested; the
reconciliation is reviewed and compiled. Putting it under test means a headless `Application`, which
is a larger change than this defect is worth.

`AutoDirectorSettings` also gained a defaulted `operator==`. The panel compared it with `std::memcmp`
over a struct with padding in it; the reconciliation needed a comparison that is about the fields.
