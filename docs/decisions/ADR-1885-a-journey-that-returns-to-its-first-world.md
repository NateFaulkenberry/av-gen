# ADR-1885: A journey that returns to its first world

- Status: Proposed (Art Pass 2, Milestone 1), scene/strange-attractor.
- Code: `Composition::setPortalToRoot`, `linkPortalRoot`, `ownCameraAt`, `flythroughAt`, `portalCameraAt`
  (`src/scene/composition_portal.cpp`); the root's return in `Composition::update` (`src/scene/composition.cpp`);
  the renderer registry and plain draw (`src/rendering/scene_renderer.*`).
- Tests: `[portal][pass2][adr1885]`.

## Context

Art Pass 1 went canyon, then Moon, then a second canyon at dusk. The dusk canyon was a separate scene file with the
same `attractorWorld` block. Generation was shared by hash, but every node, light and the river were duplicated, and
each later visit (night, dawn) would have added another copy. Art Pass 2 returns to the canyon four times (brief:
"reuse the canyon terrain for all four times of day").

## Decision

A portal may name `"world": "@root"`: its destination is the composition the journey started in, not a new child.

- **Linking.** At attach, each child learns its parent portal. A portal to `@root` walks up to the root, takes it as
  its (non-owned) destination, and tells the root which world it returns from (`returnFrom_`).
- **The root's camera after its own crossing** is the returning world's `portalCameraAt(t)`: the arrival flight
  authored on the portal that leads back. Before the return crossing that path is never asked (the guard needs the
  returning portal's knots, which also ends the recursion root -> child -> root).
- **The root's own portal, corruption and scan are done** once the camera has crossed them. The world may be seen
  again later, through another portal or on a return, without them.
- **Rendering.** Renderers register per scene. A portal view into a scene that already has a renderer (the root, seen
  from the Moon) asks that renderer for a plain draw (no portal, no post) into the portal source, instead of building
  a second renderer with a second copy of the GPU world. A crossed world still delegates its frame down the chain
  (depth limit 3).
- **A view back into the world being drawn is skipped.** Before the first crossing the root draws its own frame and the
  Moon (its portal view) has a portal back to the root. That nested view would be a second draw of the root renderer
  inside its own frame: its per-frame buffers are written once per submit, so the two draws would share them. The
  renderer marks itself busy while drawing its own world; a portal view whose owner is busy draws nothing, and no extra
  renderer is created for it. (The first M1 render, before this, lost the opening's whole foreground: PSNR 18 dB
  against Pass 1 over 0-16 s.)
- **The exit plane clips the root only once the root has handed the frame on** (its own crossing). Before that the
  root is the world the camera is in, and the plane would cut its foreground away.

## Consequences

- The canyon is one world for the whole film; the dusk scene and its builder are gone (no compat shim, ADR-441).
- A time of day on a return is a parameter snapshot (ADR-1886), not a scene.
- Camera continuity across the return is the arrival flight's job (the C1 Hermite knots of ADR-1880/1883).
