# ADR-1895: Journey legs: out from the root and home, more than once

- Status: Proposed (Art Pass 2, M2), scene/strange-attractor.
- Code:
  - `Composition::addPortalLeg`, `activateLeg`, `legForTime`, `legCameraAt`, `legRotation` and the `LegState` slots
    (`src/scene/composition*.{hpp,cpp}`);
  - the scene key `portals`, and a portal's `entry` and `entryAxis`.
- Tests: `[portal][pass2][adr1895]`.

## Context

ADR-1885 let a world's portal lead back to the root, so the canyon could be visited at dusk after the Moon. Art Pass 2
goes out and home again: canyon, Moon, canyon (dusk), Mars, canyon (night), and later the ocean and the UFO. The
root therefore needs more than one portal, each opening at its own site in the canyon, in order.

## Decision

- **Legs.** A scene's `portal` is its first leg. `portals` lists the later legs, in order. Each later leg names its
  world (loaded and owned like the first) and its own site in this world: `entry` (the corruption's focus) and
  `entryAxis` (its axis). It also carries the usual exit, rotation, flight and aims.
- **One leg is active.** It is the first leg whose world has not yet sent the camera home, a pure function of time.
  The active leg's state lives in the composition's portal members, so all the single-portal code (the crossing search,
  the arrival, the clip, the renderer's portal view) reads the active leg unchanged. A leg not active keeps its state in
  a slot, and switching swaps them. A frame that finds a crossing which changes the leg runs once more on the new leg,
  so a seek lands on the same leg as play.
- **Each leg's camera is its own.** A world reached by a leg asks that leg's path for its camera (`legCameraAt`), so a
  world from an earlier leg never follows a later one. The root's own camera, after any leg's crossing, is the latest
  crossed leg's return path.
- **On a later leg the root's scene camera is stale until the end of the frame**, where it is placed on the return
  path. Two things therefore read the journey's camera instead: the corruption's field distance, and the camera
  carried into the leg's world before the crossing. The first M2 renders had no tear on the cliff, and the view through
  it popped at the crossing (a frame difference of 16.8). The leg test now runs on a canyon with its own flythrough and
  checks the carried camera is continuous.
- **The search for a later leg** starts at the previous leg's return home. The corruption opens at the active leg's
  site and measures its field from the journey's camera. Every site's chunks are prepared for the tear (double-sided,
  echoes).
- **Looks count the crossings across legs** (out, home, out, home ...). A later leg's corruption sizes come from a look
  from the previous return (the canyon's `dusk-portal`).

## Consequences

- The canyon's second portal (an outer cliff behind the dusk camp, to Mars) and the return from Mars (the night
  canyon) run on the same canyon, geometry shared, as one journey.
- Further legs (the ocean) are one more entry in `portals`.
