# ADR-1048: Camera breathing: a roll-free camera-frame offset that routes drive

- Status: Accepted (2026-10-01), proto/liminal-space (All You Got art pass 2)
- `scene::CameraBreath` and `applyCameraBreath` in `src/scene/camera.*`; the `camera/breath/*` parameters
  and their application (after the shake, in every camera mode) in `src/scene/composition.cpp`.
- Tests: `tests/unit/test_camera_breath.cpp` (`[breath][adr1048]`).

## Context

Art pass 2 section 10 asks for camera breathing on the quarter note -- a small forward/back, a slight FOV
change, gentle rotation -- that varies by section and is *not* a shake. The journey camera (ADR-1042) has a
distance, yaw, pitch and bob, but routing a beat into the journey's distance moves its chapter swaps in time
(the pass-1 guide forbids it), and its bob follows distance, not the music. The shake (ADR-098) is noise.

## Decision

Seven parameters, `camera/breath/{amount, forward, lift, side, yaw, pitch, fov}`, applied as an offset in the
camera's own frame after whatever placed it (journey, spline, free, an authored rig), exactly where the shake
is applied and for the same reason: it composes with every camera instead of being another way to place one.
Yaw turns about world up and pitch about the horizontal right, so no roll is ever introduced. `amount`
multiplies all of it, so a section's breathing is one keyed number. They have no clock: routes drive them from
the beat grid's smooth `.wave` outputs (ADR-1045), so breathing is as seek-exact as its routes.

## Consequences

- Defaults are zero offsets and amount 1: a scene that never routes them renders exactly as before.
- The breath does not move the journey's distance, so swaps stay where they are, and the journey's collision
  guard does not see it: keep `forward` to a few centimetres to a few decimetres near walls.
- Every scene gains seven idle parameters under `camera/`.

## Amendment (2026-10-01): at rest it touches nothing

The first version rebuilt the aim as `position + normalize(target - position) * distance` whenever `amount` was
non-zero, which is its default. That round trip is not exact in floating point. Every frame with the default
breath therefore moved `camera.target` by an ulp, and the render suite's exact camera check failed
(`test_resource_lifetime_gpu.cpp:242`). Now:
- nothing at all is applied when every offset is 0 or the amount is 0;
- a translation moves the aim by the same delta;
- the aim is rebuilt only when yaw or pitch turn it.

Frames of the art agent's film are byte-identical where breathing is off (1, 60, 200 s). At 5 s, where it is
active, 4,112 pixels of 518,400 changed: all but 10 by under 8 of 255, plus two star-dot pixels. That is the
removed drift. Test: `Camera breath at rest leaves the camera bit-identical`.
