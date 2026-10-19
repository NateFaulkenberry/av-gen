# ADR-1542: A closed spline's roll can wind whole turns

- Status: Accepted (art/rainbow-world)
- Amends ADR-026 (splines). This change is additive: `rollTurns` defaults to 0, is hashed and written only when set,
  and at 0 every spline's frames, hash and JSON are unchanged.
- Code: `src/spatial/spline.{hpp,cpp}`. The field, the seam segment's roll in `evaluateSegment`, the hash, and the
  JSON.
- Tests: `tests/unit/test_spline.cpp` (`[adr1542]`), using a circle whose 16 points' rolls wind one turn:
  - without the key, the seam segment's steps are 15 times the rest's;
  - with it, every step is under 0.5 degrees;
  - the frame closes;
  - half way round, the frame has turned half a turn;
  - half way through the seam segment, the two disagree by half a turn.

  The test also covers the round trip, the absence of the key at 0, the hash, and refusal of a non-integer. All 45
  spline cases still pass.

## Problem

Rainbow World's band is swept along an AV Gen spline's frames, and its camera's up is the spline's normal (ADR-1541).
For gravity to turn as the camera travels, the band must twist about its centreline: a whole number of extra turns
round the closed loop, so it closes. E2's grey-box used one turn.

A spline carries twist as per-point `roll`, interpolated linearly per segment. On a closed spline the seam segment
interpolates from the last point's roll back to the first point's. Rolls that wind one turn (0 up to nearly 2 pi)
therefore spin back the whole turn inside one segment, which is about 100 m of Rainbow World's loop.

The seam correction that closes the rotation-minimising frame does not help. It removes the frame's own mismatch,
modulo a turn, not the authored twist.

## Decision

`rollTurns` (an integer) on a closed spline. The seam segment interpolates toward the first point's roll plus
`2 pi * rollTurns`. Since the roll is applied as a rotation, `+2 pi` is the same frame at the seam's end, so the frame
closes, and the twist is continuous all the way round.

## Consequences

- An authored twisting loop is a list of unwrapped rolls plus `rollTurns`. Rainbow World's `knot_path.py` builds
  exactly that, mirroring the engine's frame code.
- Open splines ignore the key. Their segments already interpolate between neighbours that are never the same point.
