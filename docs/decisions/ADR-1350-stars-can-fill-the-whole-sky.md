# ADR-1350: Stars can fill the whole sky

- Status: Accepted (2026-10-14), the Sonic VFX art pass (Event Horizon)
- Code: `src/world/effects/star_field.{hpp,cpp}` (`StarField::wholeSky`), `src/world/effects/kinds/stars_effect.cpp`
  (the `wholeSky` row), `src/rendering/scene_renderer.cpp` (`starsA.x` = 2), `shaders/skybox.wgsl`
  (`effectStars`), `shaders/common.wgsl` (the lane's comment).
- Tests: `tests/unit/test_star_field.cpp` ("Stars: whole sky (space) is off by default and resolves when set").

## Context

The Stars effect draws a planet's night sky: stars above the horizon, faded in over `horizonFade` (as the sine of
the elevation), and nothing below it, where the ground would be. Its projection (`dir.xz / (1 + dir.y)`) is the
stereographic one from the south pole, which is only well behaved above the horizon.

Event Horizon is set in open space, with the camera a few degrees above a black hole's disk plane, so half the frame
looks below the horizon. Its first pass hid the problem behind a matte-painted nebula plane. That plane was a
full-screen interpreted fBm program, and it cost 8 ms of a 18 ms frame at 1080p. Without the plane, the stars stopped
at a visible line across the middle of the frame, with black below it.

## Decision

1. Stars gets one boolean row, `wholeSky` ("Whole sky (space)"), off by default.
2. When it is on:
   - the horizon fade is skipped;
   - the lower hemisphere is projected stereographically from the north pole (`dir.xz / (1 - dir.y)`), offset so it
     does not mirror the upper one. Both projections have the same scale at the horizon, so the field's density is
     continuous across it.
3. The renderer passes the mode in the lane that already said "on": `starsA.x` is 1 for on and 2 for on over the
   whole sphere. No uniform grows.

## Consequences

- With the row off, which is every existing scene, the shader takes the same values as before: the frame is
  unchanged.
- Event Horizon drops its nebula plane and uses the sky's stars with `wholeSky`. Its frame went from 19.6 ms to about
  10 ms GPU p50 at 1080p (realtime tier).
- The two projections meet in a seam at the horizon. There the coordinate jumps, and the derivative-based footprint
  suppresses the stars on that one pixel row. Nothing else is visible there, because the stars are random on both
  sides.
