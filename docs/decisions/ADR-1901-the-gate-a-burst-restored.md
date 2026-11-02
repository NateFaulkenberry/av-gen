# ADR-1901: The Gate A burst restored

- Status: Proposed (Art Pass 2, M3c), scene/strange-attractor.
- Code: `vs_mesh_shard` (`shaders/mesh_wire.wgsl`), the lips in `crDisplace` (`shaders/corruption.wgsl`); the hand-off
  guard in `SceneRenderer::encodePortalView` (`src/rendering/scene_renderer.cpp`).
- Supersedes: the Gate B changes to ADR-1872's shards and ADR-1873's lips; narrows ADR-1899's guard.

## Context

The owner, reviewing M3b: Portals Gate A (21da024a, 65983cf2) was "the best they've looked". Diffing that build against
today's code, three things had changed the opening's look:
- the shards: from Gate B they thinned as the camera closed in (gone over the last 70 m) and streamed along the rim
  instead of into the opening. The Gate A storm of gold round a dark centre was gone;
- the lips: Gate B replaced Gate A's fold (the fan of torn rock) with strata-shaped slabs;
- M3b's rim particles (ADR-1898, now withdrawn).

The scene values (field, opening, wire, glyphs) were unchanged, only scaled with the world (ADR-1893).

Two crossings had also regressed. The Moon's crater floor no longer showed the canyon through it: ADR-1899's guard
skipped any view into a renderer that was handing its frame on, and the canyon is always handing its frame to the Moon
while the camera is there. The river portal had lost its opening: M3b's glide began 25 m out, inside the opening's
26 m reach, so the tear was already open when it came into view.

## Decision

- **The shards are Gate A's:** in toward the focus and back into the tunnel, kept to the crossing, shrinking over the
  second half of their cycle. The destination world shows through behind them (Gate B's composite is kept). A shard
  in the tunnel, behind the plane the destination is composited on, has its depth pulled toward the eye along its own
  ray (it stays where it is on screen) to just in front of that plane, so the storm is drawn over the glimpse.
- **The composite's margin** (how far its plane is pulled toward the eye) is 0.3 of the opening's width, not a fixed
  25 m: at a world scale of 0.4 the fixed margin reached far out of the wall.
- **The lips are Gate A's** fold back toward the camera.
- **The guard** refuses only a hand-off back (a crossed world handing the frame to a world that is itself handing
  off). A plain view into it -- the glimpse through the way home -- is drawn.
- The river portal's field and opening are pulled in (opening from 14 m) so it tears open during the glide. The Mars
  cliff's and the pyramid's reach further, so their code and tears start sooner (owner).

## Consequences

- Every portal has the Gate A look; the Gate B flicker concern (the 30-31.6 s gold storm, irregularity 0.728 at
  Gate B) returns by the owner's choice and is measured on the clips.
