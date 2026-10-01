# ADR-1042: The journey camera: a periodic path through a screw-repeated world, wrapped invisibly

- Status: Accepted (2026-09-30), proto/liminal-space
- Implemented in `src/scene/journey.*` (pure maths) and `src/scene/composition.*` (camera mode 3, the
  `camera/journey/*` parameters, the collision guard, nodes on the journey).
- Tests: `tests/integration/test_liminal_journey.cpp` (`[liminal][journey][adr1042]`).

## Context

§3: the viewer must feel they are going somewhere, walking, climbing, passing through doorways, with the
audio muted too; the addendum: a wanderer that pauses, hesitates, turns slowly and looks into rooms. The
composition camera had orbit, free and spline modes; a spline cannot climb forever and keyed free-camera
positions are 3D keys authored blind against geometry.

## Decision

- **Camera mode 3.** The scene's `camera.journey` block: `path` (control points in cell 0), `screw` (the
  world's screw, ADR-1040), `collide` (an SDF node), `radius`, optional `wrapCells`. The path is a
  centripetal Catmull-Rom through the points continued by the screw (the point after the last is the first
  carried into cell 1), parameterised by arc length.
- **Parameters** (only when the scene has a journey): `camera/journey/distance` (metres along the path),
  `lookAhead`, `height`, `yaw` (+ turns left), `pitch`, `bob` and `stride` (a walk bob that is a function of
  distance, so it stops when the walker stops), `sway` and `swayRate` (a slow noise drift of the aim, a
  function of time), `radius`.
- **The wrap.** After `wrapCells` cells (n for a helix, 1 for a translation, so S^wrapCells is a pure
  translation) the camera is carried back by S^wrapCells. The world is invariant under S, so the frame is
  the same frame; the camera never leaves the first cells, so lights and props placed there are never
  outrun, and precision never degrades. Directional light and sky are translation-invariant, so they never
  jump.
- **The collision guard** evaluates the collide object's live tree (without the screw's seam cap) at the
  eye and pushes it out along the normal to `radius` (two passes); a pure function of the frame's
  parameters, so seek-exact. The guarantee is by construction and by test: the example's path is sampled
  with the deformations at their maxima against the true (neighbour-cell) distance.
- **Nodes on the journey.** A composition node with `"journey": {"distance": d}` rides the path at
  `nodes/<name>/journey/distance`, wrapped with the camera, facing along the path's heading; its position
  becomes an offset in the path frame (x right, y up, z forward).

- **Chapters** (added the same night): `"journey": {"chapters": [...]}`. Chapter c owns the global
  distances `[start_c, start_{c+1})`; inside it the camera walks the chapter's own path at
  `from + (d - start)`, wrapped by its own screw, carried into the world by the chapter frame (`offset`,
  `yaw`, the composition-node convention, so its SDF nodes take the same position and rotation). Nodes and
  lights a chapter lists are hidden or switched off while the camera is in another chapter, so a parked
  world costs no march. The swap is a cut in world position at a distance; hide it in a white-out or a
  threshold whose geometry is identical in both worlds.
- **A look-at blend**: `camera/journey/lookAt` (a world point, in the journey's unwrapped frame) and
  `camera/journey/lookAtWeight` (0..1) ease the aim towards a point (the beacon); it holds still across a
  wrap. `camera/fov` is keyable in every mode (the dolly zoom).

## Consequences

- Holds are two equal distance keys; audio adds pace through an `integrate` route (ADR-1041).
- Everything visible must be periodic under S^wrapCells: deformations inside the screw, lights and props
  replicated over the covered cells (`tools/liminal_sdf.py:screw_apply`). Non-periodic things pop at the
  wrap.
- A swap is at a journey DISTANCE: a route that adds pace moves it in time, out of a white-out keyed to hide
  it. Keep audio pace off the distance ahead of a swap.
- No camera roll (the pose has no up vector), so no screw about the travel axis.
- The walk bob follows the unwrapped distance, so the wrap itself is exact only with no bob; with a bob it
  stays continuous (no pop), which is what the eye needs.
