# ADR-1963: Tile glow gains, one routable parameter per emission term

- Status: Accepted (Musical Geography Phase 7, 2026-11-09), scene/musical-geography.
- Code:
  - `TileDynamics::glow` (`src/scene/tile_dynamics.hpp`; scene JSON
    `environment.tileDynamics.glow {path, contour, crest, pads}`, each 1 by default);
  - parameters `env/tiles/glow/{path,contour,crest,pads}`;
  - the material input `tileGlow` (`MaterialInput::TileGlow`, `MAT_IN_TILE_GLOW`), carried in `FrameUniforms::tileGlow`.

## Context

Phase 7 wires the music to the contours and the path glow, as well as to the ground. Those glows are terms inside the
art's tile material programs. A program constant is already a parameter, but its path names the op's index, and the
scene has four tile programs per palette state, each with a different op layout. Routing one musical value to the
path glow would take four routes on four index-named paths, and every edit to a program would break them.

The material inputs `audio`, `audioBands` and `beatPhase` read the raw analysis. They bypass the routes, the response
bus, the stems and the feeds, and art cannot tune them.

## Decision

Four gains on the moving-ground block: path, contour, crest and pads. Each is a registered parameter, so it can be
routed from any signal, keyed on the timeline, mapped to MIDI or saved in a preset. Each frame they are copied into the
frame uniforms. A program reads them through the `tileGlow` input (x path, y contour, z crest, w pads) and multiplies
each emission term by its lane.

At the default of 1 the frame is unchanged. A program that does not read the input is unaffected. The ground itself
ignores the gains.

## Consequences

- One route drives the path glow in every ring and every state.
- Tests:
  - CPU: JSON round trip; the parameters reach the drawn scene's dynamics; with a silent bus, the routes leave them as
    authored.
  - GPU: the input parity case added to the interpreter parity test; end to end, 3× the gain gives 3× the emission on
    the procedural path and on the mesh path.
- `FrameUniforms` grows by 16 bytes.
