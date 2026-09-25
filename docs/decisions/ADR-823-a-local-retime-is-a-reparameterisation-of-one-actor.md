# ADR-823: A local retime is a reparameterisation of one actor

**Status:** Accepted
**Date:** 2026-09-24
**Related:** ADR-758/820 (performances), ADR-821 (cue playback), ADR-822 (jump spans); the
Director's `PlanRetime` (spec §33: a global time warp is out of scope)
**Implemented by:**
- `seq::retimeMap` and `seq::retimeActor` (`seq/retime.hpp`);
- `ClipCue::offsetSeconds` and `AnimationCue::originSeconds()`;
- `Actor::timeWarps` and `timeScaleAt`;
- `PerformerPose::timeScale` and `DirectorMotion::timeScale`;
- the gait reading the un-warped speed and playing at the warped rate.

**Tests:** `tests/unit/test_actor_retime.cpp` (`[motion][retime]`)

## Context

"Slow the flip down" is a local retime: the one performer plays at a fraction of the timeline's rate
over a window, and the music, the world and every other body do not. The Director compiles it as
stretched keys and scaled clip speeds.

Three things make that harder than it sounds:
1. **Keys can straddle an edge.** A key interval that crosses a window edge, if only its keys are
   moved, slows the whole interval rather than the window.
2. **A cue can run across an edge.** Splitting it restarted the clip, because a cue always began at
   frame zero.
3. **The gait would read a slow-motion run as a walk.** A performer's gait reads the path's speed
   (ADR-758), and a slowed run moves at a walking pace on the timeline.

## Decision

- **The map.** Before the window, time is unchanged. Inside it, time is stretched by `1 / rate`.
  After it, time is pushed later by the time the window gained. The actor's performance stays in one
  piece.
- **`retimeActor` is exact for `Linear` keys.**
  - It inserts a key at every edge a key interval straddles, then moves every key by the map.
  - For `Smooth` keys the Catmull-Rom tangents are re-derived from the new neighbours, which is
    close but not exact. The Director compiles `Linear` keys.
- **A cut cue continues from where it was.**
  - `ClipCue::offsetSeconds` is the clip time already elapsed at the cue. The phase origin handed to
    the player is `time - offset / speed`.
  - A cue is split at each edge. The continuation carries the clip time reached and a speed of
    `speed × rate` inside the window, with no cross-fade, because it is the same clip continuing.
  - A one-shot's `then` fires at its retimed end: `origin + length / speed`.
- **Airborne spans and earlier warps** are moved by the map.
- **Refusals.** It refuses, changing nothing, in any of these cases:
  - a rate that is not positive;
  - an empty window;
  - a window that overlaps an earlier warp;
  - a window that cuts through a path. A path is a single arc-length parameterisation, so it must be
    baked to keys first.
- **The actor keeps a record of the window** (`timeWarps`: start, stretched end, rate). A
  performance hands that rate to the entity, and the gait works from it:
  - It picks the clip from `speed / timeScale`, the speed the performance was authored at.
  - It plays the clip at `timeScale` of its usual rate.
  - Measured on Rook, a 6 m/s run slowed to 0.25 is still `Run`, playing at under half rate.
  - Without the record, the same keys read as 1.5 m/s and the body walks.

## Consequences

- Tested as a reparameterisation, not against written-down numbers. At every retimed second, the
  body's position and the clip's frame equal the original's at the second that maps there, to 1e-4.
- Cue markers and the effects the Director times on a window, such as the frame-echo, stay the
  Director's responsibility. Scheduled clips (`PlayClip` events) are not retimed by an actor retime.
