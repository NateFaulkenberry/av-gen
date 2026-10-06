# ADR-1166: A spline camera banks into its turns

- Status: Accepted (proto/digital-mosh)
- Amends ADR-026 (splines drive the camera) and ADR-1075 (`camera/roll`).
- Found by: DIGITAL MOSH pass 5. The owner asked for a soaring camera that banks into its turns, and the spline
  camera (mode 2) always kept the world's up.

## Problem

Mode 2 places the camera at `splineT` along a spline and aims it `lookAhead` metres further on. That is the right
primitive for a flight, because the curve gives the path and the look-ahead gives a smooth aim. But its up vector
was the world's. A camera sweeping through a 90° turn at speed stayed level, which reads as a dolly on a rail and not
as flight. The spline's own per-point `roll` turns the frame's normal, which deformers and emitters read, but the
camera never used it. Authoring a roll at every point is also exactly the work a turn already implies.

## Decision

`camera/splineBank` is a float parameter, in degrees of roll per radian of turn, default 0. In mode 2:

- The **turn** is how far the spline's heading (its tangent, seen from above) turns between the camera's point and the
  point it looks at.
- The camera **leans into the turn:** a left turn tilts the up vector left. The lean is `bank × turn`, clamped to
  ±45° so a hairpin cannot roll the horizon over.
- The lean is **added** to `camera/roll`, which keeps its meaning (ADR-1075).

The heading is read over the look-ahead the aim already uses. A camera that looks further ahead therefore starts its
lean earlier and holds it more smoothly, as a pilot would. At bank 0, and in every other mode, nothing changes.

It is an ordinary parameter, so it is reachable from scene and project JSON, presets, routes and the Parameters window.

## Consequences

- DIGITAL MOSH flies a closed path of three petals (`examples/digital-mosh/flight.py`). Each stage's variants set
  its bank: about 20° per radian in the Dream, up to 46° in the Nightmare.
- Test: `a spline camera banks into its turns` (`[adr1166]`) checks three things:
  - bank 0 keeps the world's up exactly;
  - entering a left turn, the up vector leans left of the line of sight, and stays more than 45° from horizontal;
  - on the straight, it is level.

## Rejected alternatives

- **The spline frame's normal as the camera's up.** The normal is rotation-minimising, so on a climbing or curving
  path it tilts and drifts in ways unrelated to turning, and an authored roll at every point would have been needed
  to correct it.
- **A bank that scales with speed** (`tan φ = v²κ/g`). The camera's speed is a route integral the composition does not
  see. The look-ahead already ties the lean to how fast the turn arrives.
