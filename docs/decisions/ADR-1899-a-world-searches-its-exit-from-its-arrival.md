# ADR-1899: A world searches its way out from the camera's arrival

- Status: Proposed (Art Pass 2, M3b), scene/strange-attractor.
- Code: `Composition::arrivalCrossing` (`src/scene/composition_portal.cpp`); the hand-off guard
  (`SceneRenderer::handingOff_`, `src/rendering/scene_renderer.*`).
- Tests: `[portal][pass2][adr1899]`.

## Context

M3b sends the ocean's launch-bay portal home to the canyon (`"world": "@root"`), as Mars's does. The first render
crashed with the stack exhausted: the canyon handed its frame to the ocean (the camera had crossed), and the ocean
handed it straight back (it reported its own crossing home as passed too). A world reached through a portal searched
for its own crossing from time 0, while before the camera arrives its camera path is the source's camera carried
through the portal: that path can pass the exit plane anywhere. The ocean's did, at a time long before the camera
was ever under water.

## Decision

- A world reached through a portal searches for its crossing from the camera's arrival in it: its parent's crossing
  for the leg that leads to it. Until that crossing is known, the search waits.
- The renderer refuses a hand-off back to a renderer that is itself handing the frame on: a contradiction in the
  crossings now draws one world, not an endless loop.

## Consequences

- A journey's crossings are in order by construction: no world's way out can come before the way in.
- Mars's crossing home is unchanged (it was found after the arrival anyway).
