# ADR-158: A directed shot's aim follows its hero, and nothing else follows

**Status:** Accepted
**Date:** 2026-09-14

## The gap

Directing is a bake (ADR-075): the cuts, the camera's path and its lens become timeline keys, which
is what makes a directed camera scrubbable, renderable offline and identical every time. A bake
cannot express a subject that *moves after the bake*, and Glowmere's wanderer walks — so a hero
walked out of its own close-up and the only remedy was to cut the film again.

The sequence system already solved this for its own shots: `seq::ShotCamera::lookAtActor` samples the
actor at each camera sample and blends it into the aim, calling it "the one thing `app::Shot` cannot
express". The director had no equivalent.

## The decision

Bake the hero's identity alongside the keys, and let the camera's **aim** follow that hero's current
position inside the shot the director already chose.

* `scene::AimFollow` — one entry per shot: time range, hero name, and where that hero stood when the
  keys were written. `app::installSequence` fills it; `releaseDirectedCamera` clears it.
* `Composition::applyDirectedAim` runs once a frame, after `syncHeroesToNodes` so it reads this
  frame's hero position rather than last frame's, and adds `heroNow - heroAtCut` to the camera's
  target.

Four boundaries, each deliberate:

1. **The aim, not the position.** The path is the bake — the stand-off, the framing, and the
   clearance lift that kept it out of the canopy are all in those keys. Moving the camera would be
   re-cutting the shot one frame at a time.
2. **An offset, not the hero's position outright.** A look mode may aim ahead of the subject rather
   than at it, and the aim keys are what the bake decided. Adding the hero's *movement* to whatever
   the keys say preserves both, and is exactly zero for a hero that has not moved: a shot of
   something standing still is bit-identical to what it was before this existed.
3. **`LookMode::Subject` only.** The other modes are about somewhere the camera is going rather than
   something it is watching — `Ahead` aims down the move, `Parallel` freezes a direction so the
   parallax *is* the shot, `Fixed` holds a place, and `Handoff` leaves one subject for another
   halfway through, where following the first would drag the very thing the shot is leaving.
4. **Re-cutting stays parked-only.** Unchanged from ADR-075. Tracking is not re-cutting.

## Why it round-trips with the project

`doc["cameraAimFollow"]`, a sibling of `timeline` rather than part of the scene: the shots are camera
automation, and a scene shared between two projects must not carry one project's cut. It has to
persist at all because an offline render reloads the project before drawing it — a table that only
lived in memory would make the rendered file differ from the window that asked for it, which is the
one thing a deterministic engine may not do. Absent from a file means cleared, not inherited.

Determinism is unaffected: the hero's position at time *t* is a function of the scene's own
simulation at *t*, so the same second gives the same aim in a 120 Hz window and a 30 fps render.

## A stale table is inert

An entry naming a hero that is no longer in `heroes()` does nothing, and so does one whose window
the playhead is not in. That matters because the table outlives the heroes it names whenever
somebody unstars one, and the alternative to "inert" is a camera chasing something that is not there.

## What does not follow

The lens. `camera/lens/focusDistance` is baked from the subject's distance at cut time, so a hero
that walks a long way toward or away from the camera stays aimed at and drifts out of focus. It is
invisible in Glowmere, whose depth of field is authored through `post/dof` with `physical` off, and
it is a second behaviour with its own failure modes — recorded here rather than fixed quietly.

## Evidence

`tests/unit/test_camera_director.cpp`: directing Glowmere's score against three heroes produces 5
subject-holding shots of 10; moving the subject 19.2 m moves the aim by the same vector to within a
centimetre while the `camera/target` keys are byte-identical; the aim outside every shot's window is
the bake's; handing the camera back empties the table. Negative control: with `applyDirectedAim`
commented out the aim assertion fails by the full 19.2 m. A second test round-trips the table through
a saved project and checks an undirected project does not inherit one.
