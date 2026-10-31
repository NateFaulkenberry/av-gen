# ADR-1880: The crossing: a pure function of the flight, and a flight that continues on the other side

- Status: Proposed (Gate B), scene/strange-attractor.
- Code: `Composition::updatePortal` (`src/scene/composition_portal.cpp`).
- Tests: `[portal][adr1879]`; the crossing is measured on the render (`portals-b/05-measures.txt`).

## Decision

**When.** The crossing time is where the attractor flight's samples change side of the entry plane, interpolated
(32.025 s for the Strange Attractor). It is computed once from the flight, so a seek lands on the same world.

**Before the crossing,** the destination's camera is this world's camera carried through the transform: position,
target, up, lens and field of view.

**After it,** the destination's camera follows its own flight:
- It leaves the arrival point (the flight's pose at the crossing, carried through) with the arrival velocity, so the
  path is C1.
- It then goes through the block's `flight` waypoints (x, height above the ground, z, t) by Hermite segments, with
  Catmull-Rom tangents.
- The view direction blends from the arrival's over 1.6 s to a 1.2 s look-ahead, slightly down. Up blends to the
  world's up.

**The world switch.** The renderer composites the destination over the whole frame (ADR-1879). At the plane the
aperture has already filled the frame for several frames, and the camera there is the same camera on both sides, so
the switch is invisible.

## Consequences

The flicker metric across the crossing shows no step (`portals-b/05-measures.txt`).
