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

## Revision at Gate C 2: the exit in a canyon wall

The owner asked for the second portal's canyon side to be part of a canyon wall, as portal 1 is: we come out of the
rock, not out of the sky.

**The rotation.** A portal may now turn the camera, not only yaw it. `portal.exitAxis` names the direction the camera
leaves the exit. With it, the rotation takes the entry's frame (the corruption's axis and `entryUp`) onto the exit's
(`exitAxis` and `exitUp`). Without it, the rotation is `exitYaw` as before.

**The Moon side stays in the crater floor.** Two options were weighed:
- **The crater's inner wall** (both sides vertical, a pure yaw). The hero's inner wall slopes at about 30 degrees, so a
  vertical opening there stands in the slope like a window rather than a hole torn in rock. It would also give up the
  floor opening, the highlight of Gate C.
- **The floor, turned** (chosen). The floor's axis (down) maps onto the wall's (level, out of the rock), and the
  floor's `entryUp`, the dive's heading (-z), maps onto the world's up. The camera tips over above the hole and drops
  into it almost vertically, its up pointing ahead. It comes out of the wall nearly level, with its up the world's.
  The horizon does not jank: through the hole, the canyon is seen upright from the moment the hole opens, and the
  crossing changes nothing on screen.

The camera hovers over the hole before it drops (from 50.2 s), so the hole opens while the camera is above it. A
camera behind the hole, still moving forward, maps below the exit, where the canyon wall's foot would be seen from
underneath.

**The clip.** The camera carried through the portal sits behind the canyon wall until the crossing. The destination's
`Scene::portalClip` is the exit's plane. The camera's passes (lit, prepass, refraction, SSR) discard everything
behind it, so the camera looks out through the rock. The shadow views zero the plane, so the rock behind the wall
still casts its shadows. It is the frame block's last vec4.

**The front reach.** A sloped wall's foot stands in front of a vertical opening. The corruption's new `front` sets how
far in front of its plane the tear reaches (default 30 m, as before).

**No join.** The dusk camera leaves the wall over open water and flies its own knots, so the 55.5 s join and its view
turn are gone. The join's blend is also now 3 s, eased at both ends, for any scene that uses one. A pose's up is made
perpendicular to its view before it is carried through a portal (`orthoUp`).

**The look does not follow a climb.** The flight's look-ahead takes only a quarter of an upward motion. Rising over
the hero's rim to the hover, the full look-ahead tipped the view into the black sky for almost a second.

**Back faces too (the merge fix).** The dark rounded blob at 53.0–53.4 s was not the Moon's torn floor. It was a spur
of the canyon wall's foot, seen from inside the rock. The camera's lowest rays cross the exit plane below the rock's
face there, so they travel inside the spur and meet its surface from behind. While a scene has a portal clip, its
camera passes also discard back faces, so those rays pass through to what lies beyond and the blob is gone.
