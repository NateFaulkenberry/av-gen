# ADR-391: The editor gets its own viewpoint

**Status:** Accepted
**Date:** 2026-09-19

Supersedes the `viewportFreeRoam` override introduced with ADR-245's viewport work. Extends
ADR-091 (a cut is a pure function of the clock), ADR-218 (a UI edit writes a parameter's base),
ADR-245 (several cameras), ADR-246 (the output frame inside the canvas), ADR-350 (a setting the
application does not keep is not a setting), ADR-386 (the camera lock).

*Numbered 389 because 388 was taken twice on other branches while this was in flight — checked
against every live worktree immediately before landing.*

## Context

Navigating the view was the same act as modifying the film's camera. `Application::setViewportPose`
wrote `camera/position` and `camera/target`, and that was the whole of what a viewport drag did, so:

* On a project whose cut is baked, a drag could only be honoured by **destroying the cut**.
  ADR-386 added a lock for exactly that reason — `releaseDirectedCamera` drops the camera-owned
  timeline tracks, the `cameraAimFollow` table (37 entries on the multicam film) and
  `cameraShotSpans` (42), and the next Save writes the loss, which was observed happening for real.
  The lock was correct, and its price was that Option-drag stopped working on the film that needed
  it most.
* **"Look through this camera" could not be built.** An agent asked for it declined and shipped an
  honest "Go to camera" instead, writing: *"Looking through a camera means pinning the viewport to
  that rig, and there is no such concept."* That judgement was right.
* ADR-386 said so itself: *"There is no separate editor camera in this engine — the viewport IS
  `camera/*`."*

There was already an override of the right *shape* and the wrong *destination*. `viewportFreeRoam`
redirected the resolver's answer to `kMainCamera` — but the main camera **is** `camera/*`, so
free-roam only chose which of the film's cameras a drag wrote. Two further things it did, found by
measurement rather than from its comment:

* It overwrote `activeCamera_` with a fictional camera called "Viewport", so for as long as anybody
  was flying, the Cameras panel, the preview label and the physical lens all lost track of what the
  film was on. Its own comment claimed the opposite ("`activeCamera_` still reports it truthfully").
* It was free of *rigs* and not free of *tracks*. A `seq::Shot` bakes to `camera/position` keys on
  the main camera — the very camera free-roam handed the viewport back to — so a baked sequence
  dragged the "free" viewport around by the face. `tests/unit/test_camera_lab_viewport.cpp` had
  asserted that as correct behaviour.

## Decision

**The viewport has a mode, and the mode decides where the frame comes from.**

| mode | the frame is | a drag writes |
| --- | --- | --- |
| `Film` | the director's answer — `camera/*`, the active rig, shake, framing, exactly as before | `camera/*` (with ADR-386's lock) |
| `Editor` | `Composition::editorCamera()`, a pose the film does not own | that pose |
| `Through` | one authored rig, whatever the director is doing | takes you off the rig and onto `Editor`, starting from where you were |

`Editor` is the editor's default. Navigating the view is not an edit to the film; composing a shot
still is, and is one click away.

### The rule that makes a render immune

The override is applied to the **frame**, never inside the resolver — the same rule free-roam obeyed
and the reason it is restated here. `resolveActiveCamera` stays a pure function of (cameras, shots,
events, time), which is what ADR-091 rests on, and `activeCamera()` now reports the film truthfully
in *every* mode. All that changes is the pose written into `Scene::camera` for display.

`Composition::viewportView_` defaults to `Film` and **is not serialised**, so an offline render, a
`RenderJob` and a sequence render — each of which builds its own `Engine` from the file — cannot
inherit an editor's navigation. The invariant is structural, not careful: there is no path that has
to remember to switch it off.

### Where in the frame, and why that exact line

`applyViewportView` runs inside `Composition::applyParameters` **after** the film's camera is final
(resolved, evaluated, shaken, framed) and **before** the terrain LOD, the water, the rig lights that
stand relative to the view, and everything the renderer and the editor derive from `Scene::camera`.
So everything screen-space follows the frame that is actually on screen: picking, the gizmos,
box-select and the light and camera helpers all ray from `engine.scene().camera`, and they keep
doing so without knowing this exists. A picker that rayed from a camera nobody was looking through
would select whatever is under a point in a frame nobody can see — and it would look right until you
clicked something.

Shake and framing are the film's, so they do not follow the editor's viewpoint into it. The lens
does not either: under `Editor` the published focal length is "no opinion", which stops a 24 mm rig
holding the film from re-imposing its focal length on a frame it is not in.

### Two things overrule the choice

`ui::viewportShowsFilm` is the predicate, and both cases are "somebody other than the driver is
looking at this frame":

1. **A preview mode that shows the output frame.** Workspace is the editor's window on the world;
   Output Frame and Preview are the picture that renders (ADR-246). **Workspace navigates, Output
   Frame composes** — a drag there moves the film's camera exactly as it always has.
2. **An open output or share.** `presentAll` and `TextureShare::publish` are handed `finalTexture_`,
   the live viewport's own render target, so whatever the canvas shows is on the projector. While
   anything is watching, the canvas shows the film. *(Known limitation: flying without disturbing a
   live output needs a second render of the film per frame. That is real GPU cost and separate work.
   What must not happen is that it silently re-frames somebody's show.)*

### `viewportFreeRoam` is removed, not kept beside it

Two overlapping concepts for "the viewport is not showing the film" is how the next person gets this
wrong. `Editor` does everything free-roam did and the thing it could not do.

### The editor viewpoint does not persist

A decision, per ADR-350, and the reasoning is in
`tests/unit/test_editor_viewpoint_persistence.cpp` so that adding serialisation quietly is a test
failure rather than a silent change to what a render does. The cost: reopening a project puts the
canvas back on the film's camera. If that is worth keeping, its home is the editor's settings file
beside the panel layout and the preview mode — never the project.

## The general fact, which is bigger than the feature

**A view is not a value.** This engine kept one camera and let it mean two things — where the film
is shot from, and where the person editing is standing — and every symptom above is that conflation
in a different costume: a lock that had to choose between a gesture and a bake, a feature that could
not be described without describing a lie, a "free" viewport dragged around by a baked sequence.

The test for it is one question: *does this state belong to the artefact, or to the person looking
at it?* If it belongs to the person, it must not be reachable from the artefact — not serialised,
not read by a render, not inside any function whose purity something else depends on. Apply the
person's state as an **override on the result**, never as an input to the computation, and the two
cannot be confused again by accident. `PreviewViewMode` had already reached this shape (guides and
letterboxing are painted after the picture and cannot reach a render target). The camera had not.

The counter-pressure is always the same and it is what made the original design reasonable: one
camera is less code, and for as long as the editor is the only viewer it behaves identically. It
stops behaving identically the moment the artefact has a life of its own — a bake, a second output,
a deliverable rendered somewhere else — and by then the conflation is load-bearing.

## Consequences

* ADR-386's lock survives, and only guards what it was written for: moving the **film's** camera by
  hand on a project whose cut is baked. It is no longer in the way of navigating.
  `Application::ensureFreeCamera` returns immediately unless the frame on screen is the film's.
* "Look through camera" exists.
* A drag works in orbit mode. The editor's viewpoint is not `camera/*`, so it does not care about
  `camera/mode` — where a drag used to write parameters the main camera does not read and look
  exactly like a dead input.
* A baked `seq::Shot` no longer moves the editor's viewpoint; the assertion in
  `test_camera_lab_viewport.cpp` that said it did has changed direction.
* `Composition::activeCamera()` reports the film in every mode. Anything that used it to answer
  "what is on screen" must ask `viewportView()` instead — in this repository that was the preview
  camera label and nothing else.

## Revisit when

* Somebody needs to fly while an output is live. That is the second-render question above.
* A second editor view exists (a picture-in-picture, a second window). `ViewportView` is per
  composition today, which is the right grain for one canvas and the wrong one for two.
