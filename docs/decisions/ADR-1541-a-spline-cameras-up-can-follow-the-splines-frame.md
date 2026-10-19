# ADR-1541: A spline camera's up can follow the spline's frame

- Status: Accepted (art/rainbow-world)
- Amends ADR-1075 (`camera/roll`) and ADR-1166 (`camera/splineBank`). This change is additive: the new parameter
  defaults to 0, and at 0 every camera keeps the world's up exactly, as before.
- Code: `src/scene/composition.{hpp,cpp}` add `camera/splineUp` and `Composition::splineCameraUp()`, which the up
  block uses before `camera/roll` and the bank are applied.
- Tests: `tests/unit/test_camera_roll.cpp` (`[adr1541]`). A straight path whose authored roll turns half a turn:
  - at 0 the up is the world's up;
  - at 1 it equals the spline's normal at every sampled point, and stays perpendicular to the path;
  - the camera is upright at the start, on its side half way, and upside down at the end;
  - at 0.5 the angle halves.

  The ADR-1075 and ADR-1166 cases still pass.

## Problem

Rainbow World's camera rides a closed, twisting band of land. Gravity turns with the land, so the camera's up must be
the band's local "up", including upside down (`docs/prototypes/rainbow-world/03-art-direction.md` section 9).

The main camera's spline mode placed the eye and the aim from the spline but always took the world's up. `camera/roll`
turns that up about the line of sight, but a roll keyed against a twisting path is a second, hand-kept copy of the
twist, and it degenerates when the camera looks straight up or down.

The spline already carries the right frame: a rotation-minimising normal plus the points' authored roll. The camera
did not read it. `docs/splines.md` even said it did; it did not.

## Decision

`camera/splineUp` (0..1) turns the world's up toward the spline's normal at the camera's point: a slerp from the
identity to the rotation taking world up onto the normal. `camera/roll` and the ADR-1166 bank are then applied about
the line of sight, exactly as before. The result is a pure function of the parameters, so a seek lands where play
does.

## Consequences

- A closed path's rotation-minimising frame does not close by itself, because of holonomy: the normal at the end
  differs from the start's by the path's total twist. A closed ride must author its points' `roll` so the frame
  closes. Rainbow World's path generator does this.
- Only the main camera's spline mode reads it. Authored rigs (ADR-245) are unchanged.
