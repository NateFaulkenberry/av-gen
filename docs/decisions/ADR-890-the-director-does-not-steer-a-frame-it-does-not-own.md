# ADR-890: The director does not steer a frame it does not own

**Status:** Accepted
**Date:** 2026-09-25

Extends ADR-391 (the editor gets its own viewpoint) and ADR-582 (the aim-follow table survives a
composition being replaced).

## Context

Reported on `examples/world/glowmere-valley-2-multicam.json`: *"if I have control of the camera in
the world editor sometimes it is still moved by the director."* The owner was on the editor
viewpoint, straight after opening the project.

The editor's pose was never re-seeded and nobody else wrote it. The **frame** was written after it.
`Composition::update` runs `applyParameters` (which ends in `applyViewportView`, putting the
editor's pose on screen) and then `applyDirectedAim`, which adds the director's aim-follow offset
(the hero's movement since the cut) to `Scene::camera.target`. It did not ask whose pose that was.
The multicam film carries 39 aim-follow entries. So whenever the film was on the main camera inside
one of those shots, the editor's aim was dragged after the hero. When an authored rig had the film,
it was not. That is the "sometimes".

The `viewpoint` UI-script arm showed it on the real project. It chooses the editor viewpoint,
Option-drags, plays and seeks to 113 s. At 114.82 s ('Hero Free Roam', main camera) the shown aim
was 93.39 m from the pose the drag left. 154 frames moved, and the editor pose itself was unchanged.

There is a second route to the same symptom. `Engine::setCompositionJson` (used by an assistant
task that is rolled back or aborted) builds a new `Composition`, and the editor's viewpoint lived on
the old one. The next frame re-seeded it from the film's camera, which is wherever the director had
the film.

## Decision

* `applyViewportView` records whether the frame on screen is one the film does not own
  (`viewportOwnsFrame_`: the editor viewpoint, or an authored rig looked through).
  `applyDirectedAim` still runs its smoother, but it adds the offset only to a frame that is the
  film's. Film mode, and so every render, is unchanged.
* `setCompositionJson` carries the editor pose and the viewport mode across the replacement. A
  project load (`loadComposition`) does not, because a viewpoint in one world means nothing in
  another.

## The general rule

Anything that adjusts the film's camera **after** `applyViewportView` has to ask whose frame it is
adjusting. Today `applyDirectedAim` is the only such step.
