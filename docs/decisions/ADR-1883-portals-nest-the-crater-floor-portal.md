# ADR-1883: Portals nest: the crater-floor portal, the camera as a pure path, the join, and the crossed frame

- Status: Proposed (Gate C), scene/strange-attractor.
- Code: `Composition::ownCameraAt`, `flythroughAt`, `portalCameraAt`, `buildPortalArrival`, `updatePortal`
  (`src/scene/composition_portal.cpp`); `SceneRenderer::render` and `encodePortalView` (`src/rendering/scene_renderer.cpp`);
  the Moon's `corruption`, `meshWire` and `portal` blocks (`examples/strange-attractor/build_moon.py`).
- Tests: `[portal][gate-c]` (nesting, both crossings, the third world's camera at the exit); `[gpu][portal][adr1879]`
  (a crossed chain draws the innermost world).

## Context

Gate C's second portal is in the Moon itself: the floor of the hero crater cracks and opens under the diving camera
onto the canyon at dusk. The Moon is already a portal's destination, so the portal machinery has to nest.

## Decision

**A world seen through a portal may hold a portal of its own.** Nothing about the Moon is special: it is a scene file
with a `corruption` block (here facing down, `axis` [0, -1, 0]) and a `portal` block, exactly like the canyon's. Over
a generated ground the corruption's focus y is a height above that ground, as a portal's exit already was.

**The camera is a pure path.** The crossing search needs the camera at any second, not only at this frame.
- A source hands its destination `setCameraPath`: a pure function of time, its own `portalCameraAt`.
- `ownCameraAt` is that path, or else the composition's own flythrough (attractorWorld, flight mode).
- The crossing is the first time the path passes the entry plane within twice the opening's size of its centre,
  scanned once at 60 Hz (the lateral limit keeps a horizontal floor plane from catching the camera anywhere else
  on the Moon). The canyon still crosses at 32.025 s, the Moon at 51.830 s (bar 27).
- The frame itself still draws from the source's exact camera of the frame (`setCameraOverride`), so effects the
  source composes on its camera stay consistent through the opening.

**The join.** `portal.join` [from, at, rate]: at `at` seconds the destination camera *is* the destination's own
flythrough at its second `from + (t - at) x rate`. The arrival Hermite ends on that point with that velocity (C1),
and the view eases into the flythrough's aim and bank over the last 1.5 s. The dusk canyon joins its river run at
its second 17, at half speed, from 55.5 s: the camera continues along the river on the line that was already flown
and proven clear of the walls.

**The crossed frame.** Once the camera has crossed, the destination's renderer draws the frame into this renderer's
target, with its own post chain (the worlds share their post settings), and this world is not drawn at all. Nested,
that is a chain: the canyon hands the frame to the Moon's renderer, which hands it to the dusk renderer. Before, the
crossed world was drawn underneath every frame for nothing (about 25 ms offline at Gate B); with three worlds that
waste would have tripled. Renderers nest up to three deep.

**The crater-floor portal.** The Moon's corruption is the canyon's, turned down and cooled: lunar scan lines in a
cooler gold (`meshWire` on the regolith), glyphs on the floor, the floor torn open from a seam to a ragged hole
46 x 40 m as the camera dives at it. There is no strata slide on the Moon: its uv.x is the regolith's albedo.

## Consequences

- The dive through the floor is continuous: the third world's camera, just past the floor, is at the exit within
  the test's 3 m, heading down; on the render the crossing shows no step (`portals-c/05-measures.txt`).
- A path the search never sees cross (no flythrough and no path) never crosses: the composition marks it and does not
  search again.
- Parameter edits that change a flight after its crossing was found do not move the crossing until the portal is
  set again.
