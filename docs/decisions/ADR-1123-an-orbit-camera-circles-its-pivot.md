# ADR-1123: An orbit camera circles its pivot

- Status: Accepted (gpu/productionization, the flagship LIVE scene)
- Found by: PHONOTAXIS, whose camera language is a live orbit whose vantage and look point belong to the musical
  state.
- PHONOTAXIS was dropped by the owner on 2026-10-05 and its scene deleted; this capability stays.

## Problem

The main camera's orbit (mode 0) is the one placement a live performance can drive continuously. It integrates
`orbitSpeed`, so a knob can change its speed without a jump, and `camera/distance` and `camera/height` are
parameters. But it always circled and looked at `center_`, the centre of the flattened scene bounds.

- Nothing could aim it. `camera/target` is read only by the free camera.
- The bounds centre is an accident of what the scene contains. A 4 km ground plane puts it at the ground, so an
  orbit could never look up at a 36 m structure's crown.
- A state, a route or a MIDI knob could change how far the camera stood, but never what it looked at.

The alternatives each lose something a live camera needs:

- **The free camera** has no motion of its own. Routing its position from two LFOs makes a speed change jump,
  because an LFO's phase is rate x time.
- **The spline camera** looks along its path, not at a subject.
- **The camera director's rigs** are cut by time-based shots, not by a live signal.

## Decision

The orbit circles and looks at a pivot:

    pivot = mix(boundsCentre, camera/orbitPivot, camera/orbitPivotWeight)

Both are parameters: `camera/orbitPivot` (vec3) and `camera/orbitPivotWeight` (0..1). In a scene file they are
the `camera` block's `orbitPivot` and `orbitPivotWeight`, written only when the weight is above 0.

- **Weight 0 is the default and the old orbit.** Every existing scene behaves, and saves, exactly as before.
- **Weight 1 is "circle this point".** It is the same integrated angle, distance and height, around the authored
  pivot.
- **In between** is a continuous blend, so a state transition, a preset morph or a knob can move the look point
  without a cut.

## Consequences

- A scene state can now own a vantage: "low, close, looking up at the crown", or "high, far, looking down on the
  basin". Its transition is a slow camera move, because a preset morph interpolates the pivot, distance and
  height together.
- Seek: the orbit's angle is still integrated (`cameraAngle_`), as before. Only the pivot is new, and it is a
  pure function of its parameter.
- Tests: `[orbit-pivot]` in `tests/unit/test_composition.cpp`. At weight 0 the pose is unchanged, at weight 1
  the camera circles the pivot at the authored distance and height, at weight 0.5 it uses the midpoint, and a
  save round-trips while files without a pivot are unchanged.

## Rejected alternatives

- **Make the orbit read `camera/target`.** That changes every existing orbit scene, whose authored target was
  never used, so their pictures would move.
- **A new camera mode.** Mode 0 already has the integrated angle a live orbit needs. A new mode would duplicate
  it and leave the old one unaimable.

## Revisit triggers

- The orbit needs an inclination (a tilted circle), not just a height. Add `camera/orbitTilt`.
