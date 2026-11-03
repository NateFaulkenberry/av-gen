# ADR-1903: No one-frame pop at a crossing

- Status: Proposed (Art Pass 2, M3d), scene/strange-attractor.
- Code: `shaders/portal.wgsl` (the composite).

## Context

The owner saw a single frame of particles as the camera passed through the pyramid portal. Frame by frame, every
crossing had one frame showing the wrong thing: the wall behind the opening, or the launch bay's interior. The crossing
time is found on a 1/60 s grid and interpolated, so a 30 fps frame can land with the camera already past the opening's
plane but before the crossing instant. The composite then discarded every pixel (the plane is behind the camera), and
that frame showed the source world from the wrong side.

## Decision

- **The composite checks the camera itself.** If the camera is through the opening's plane (or within half a metre of
  it) and inside the aperture, the other world fills the frame, as after the crossing.
- **A hand-back is drawn, not refused.** A hand-back to a renderer that is itself handing off draws that world plainly
  (no portals, so nothing can loop) straight into the frame. Before, it drew nothing, and the world being left showed
  through.
- **The frame landing exactly on the crossing instant still showed the wrong world.** Measured frame by frame, the
  camera stood on the plane and the state was degenerate. The authored crossing knots are moved half a frame off the
  30 fps grid (+1/60 s), so no frame lands on an instant.

## Consequences

- Measured at 30 fps on every crossing (`01-portals/pops.txt` in the review folder): no one-frame pops.
- **Limitation:** at 60 fps the shifted instants fall on the grid again. The degenerate instant itself is not fixed in
  the engine yet.
