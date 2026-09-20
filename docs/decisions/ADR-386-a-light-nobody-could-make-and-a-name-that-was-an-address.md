# ADR-386: A light nobody could make, and a name that was an address

**Status:** Accepted
**Date:** 2026-09-19

Extends ADR-033 (lighting), ADR-271 (a UI edit lands in the project), ADR-278 (a scene file authors
a light), ADR-350 (a system the application ran and did not keep), ADR-358 (per-light parameters),
ADR-375 (reachable is not findable). Closes ADR-278's first revisit trigger.

*Numbered 386 because 375-385 were taken on other branches while this was in flight; nine collisions today.*

## Context

Two owner briefs, treated as one because they are one architecture: a Lights panel whose objects
must be selectable in the Canvas, and a Canvas that presents lights and cameras as first-class
objects.

The audit (`docs/lights-and-viewport-authoring-audit.md`) found far more already built than either
brief assumed — the whole light model including real LTC area lights, symmetric scene-file
serialization, a Move/Rotate/Scale gizmo with world/local and snapping, multi-select, GPU picking,
command-based undo, and an overlay painter that cannot reach a render. Three things were missing,
and each blocked everything after it.

## Decisions

### 1. A light's identity is an id, not its name

`AuthoredLight::id` is the parameter path's stem; `light.name` is only the label. They were one
string, and rename is what separated them: a route's target and a timeline track's target are both
`lights/<...>/<field>`, so while `<...>` was the display name, renaming a light silently orphaned
every binding to it — the "Hero Mushroom Pulse" defect one format over, 511 occurrences across 22
files.

The alternative considered and rejected was re-pointing every binding on every rename. With an id
there is nothing to re-point, because nothing moved.

**An id is derived from the sanitised name when a file gives none**, which is what keeps the 81
shipped projects keyed on `lights/celestial-key/intensity` resolving. Measured: the owner's Tree of
Life film loads with **0** unknown-parameter warnings, against **2** for a control project carrying
a deliberately renamed key. `toJson` writes `id` only once it diverges from the name, so no scene in
the repository changed a byte.

**Name uniqueness is kept**, and that is narrower than it looks. Under a stable id a label could be
anything, but `DayNight` binds its sun and moon to lights *by name*, so two lights called "moon" is
an ambiguity with a silent winner. Moving that binding onto ids is the revisit trigger.

### 2. Twenty of twenty-five fields are parameters, and `position` is the one that mattered

ADR-358 registered seven. Every transform in this editor is a parameter write and that is the whole
of what makes a gizmo drag undoable, keyable and modulatable — so until `position` existed, no gizmo
could move a light whatever the viewport drew.

Registered **per kind**: a directional light has no `range`, only a spot has cone angles, only an
area kind has an extent. A control that cannot move the picture is not drawn.

Hard ranges are what the system survives, not what the slider should show — a modulation route
clamps to the hard range and the soft range does not protect it. The spot cones were registered
0..180 degrees and `packLight` clamps both to pi/2, so a route would have had half a travel that
could not move the picture. Now 0..90.

### 3. The project records the light *set*; `parameters` records its numbers

The fifth instance of ADR-207 / ADR-230 / ADR-276 / ADR-330. A project saves its scene by reference,
so a light added in a panel lived in the window and in no document any render reads.

The record is the whole list, not ADR-330's difference-by-name, because most of a light's fields are
not parameters and a by-name difference would record which lights exist and lose what they are. It
is taken from `authoredLightsRest()` — the structure as set — and **not** from the live authored
list, because `applyParameters` writes each light's base values back into that list, so comparing it
against the scene made a project that merely dims a light record its entire rig. That is a second
answer to a question `parameters` already answers (ADR-271).

### 4. Editor helpers are picked on the CPU, in screen space

Not a new `PickSpace`. The tag is two bits with exactly one free value and lights and cameras are
two claimants; a helper written into the identifier target would be in a pass the offline renderer
runs; and an icon has no depth to test against. The cost, stated rather than discovered: a light
behind a mountain stays clickable, which is what every DCC does and is the recoverable direction.

### 5. Navigating the view is not modifying the camera

`releaseDirectedCamera` destroys six timeline targets, the `cameraAimFollow` table and the
`cameraShotSpans` table — **37 and 42 entries on the multicam film** — and the next Save writes the
loss. It was observed for real and recovered from git; re-baking is not a recovery because it
re-photographs the hero anchors (ADR-344).

While that could only be reached by dragging the viewport under a directed camera it was a defect
you had to know the sequence for. Making a camera a draggable object puts it one click from anybody
composing a shot, so the lock lands *with* the dragging rather than after it.

`ui::viewportMayReleaseDirector(directed, locked, deliberate)`: an incidental gesture may not
discard a cut; a deliberate one — the menu's "take the camera back", the panel's unlock — may.
Locked by default, because the destructive direction is the one worth defending.

## What is deliberately not here

* **Solo** (panel brief §19). The natural implementation forces every other light's `enabled` final
  each frame, which made the panel the only writer of a parameter final the Inspector cannot name.
  `test_repo_hygiene.cpp` caught it on the first full run. §19 is conditional on the editor already
  having a solo concept and it has none. Restoring it is an `Influence::Kind::Solo` plus editor
  state visible to `influencesOf`.
* **The Metal hybrid path tracer's acceptance criteria**, struck. There is no Metal backend, no
  `TraceBackend`, no `.metal` file; `docs/offline-backend-audit.md` says so. One path tracer, CPU
  over Embree.

## The measurement

Rendered A/B on `glowmere-stylized`, a spot placed exactly as the panel places it:

| arm | pixels changed | worst channel delta |
|---|---|---|
| spot on | 128923 / 230400 (56.0%) | 192 |
| spot off — *same file*, `enabled: false` | **0** | **0** |

The control is the half that matters: it could have come out the other way, and it is the difference
between "the light renders" and "something in this project renders". A dose-response arm on a second
fixture moved 153 to 799 pixels and delta 4 to 49 for a 20x intensity.

The camera lock is proven by `test_camera_director.cpp`'s two cases against the real
`releaseDirectedCamera` on a real directed engine, with a premise check that the fixture baked
anything at all and a control arm that the *unlocked* gesture does destroy the tables.

**Four false measurements were caught by controls before they became claims**, and they are recorded
because the pattern is the point: a render A/B whose projects never loaded their scene (three
identical frames that read as "the light does nothing"); a fixture whose geometry was off-camera; a
`pgrep -f avgen_tests` wait loop matching its own command line; and `timeout`, which does not exist
on this machine, so two scripted app runs produced empty output that greps read as success.

## Consequences

* `src/scene/composition.{hpp,cpp}`: `AuthoredLight::id`, `authoredLightId`,
  `uniqueAuthoredLightId`, `authoredLightsRest`, `authoredLightsFromJson`, thirteen more registered
  parameters, the `ecology.glow.` reservation.
* `src/app/engine.cpp`: the `lights` key, written and read, the reader before the parameter load.
* `src/ui/lights_panel.{hpp,cpp}`, `lights_panel_logic.{hpp,cpp}`, and a 22nd editor panel.
* `src/ui/world_edit.{hpp,cpp}`: `SelectionRef`; `nodes()` becomes a filter over one storage.
* `src/ui/edit_history.{hpp,cpp}`: `LightChange`.
* `src/ui/world_probe.{hpp,cpp}`: `pickProjectedPoint`.
* `src/ui/ui_logic.hpp`: `editorOwnsArrowKey`, `viewportMayReleaseDirector`.
* `E` is Rotate unconditionally; it used to open an HDR dialog when nothing was selected.
* The arrows nudge only while the pointer is over the viewport.

## Revisit triggers

* **A second consumer keyed on a light's display name.** `DayNight`'s `sunLight`/`moonLight` is the
  one that keeps name uniqueness necessary; move it to ids and the constraint can go.
* **Solo**, via `Influence::Kind::Solo`.
* **Cameras in the Canvas beyond selection.** Frustums, picking and the lock are here; camera
  dragging, "look through camera" and the Sequencer link are not.
* **A ninth shadow view.** A second cascaded directional light is warned about rather than served;
  raising `kMaxShadowViews` is a cost every scene pays and nobody has measured it.
