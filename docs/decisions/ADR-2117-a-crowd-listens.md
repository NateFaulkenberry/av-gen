# ADR-2117: a crowd listens (routable look, sway and clock for a group of skinned nodes)

- Status: Accepted (2026-11-20), avagent/neon-dreams (Neon Dreams, iteration 10; owner msg 112: "investigate more
  audio-reactivity possibilities"; the art agent's notes/audio-ideas-it10.md, proposals 2, 3 and 6).
- Code: `NodeAnimation::crowd`, `crowdWeight`; `Composition::CrowdParams`, `registerCrowdParameters`, `driveCrowds`
  (`src/scene/composition.{hpp,cpp}`); `SkinnedRig::clockLag`, `layersActive` (`src/scene/animation.{hpp,cpp}`).
- Tests: `tests/unit/test_crowd_pose.cpp` (`[adr2117]`).

## Context

Everything that answered the music in Neon Dreams was a light. The art's ranked proposals asked for bodies: heads
turning to the Great Lantern as the bass swells, a town that holds its breath through the tone-block rest and lets
go on the crash, groups swaying on the drum bus. The pose layers (ADR-300) could already turn a head (Aim) and add
breathing (Secondary), but on a plain glTF node their weight and target were fixed in the file: nothing a route or a
timeline track could reach. A slower clip needed `setSpeed`, which integrates over frames and so cannot be sought.

## Decision

1. A node joins a crowd with `animation.crowd` (and `crowdWeight`, 0..1, how much of the crowd's look it takes).
2. Each crowd name gets four ordinary parameters, so routes, timeline tracks and the UI reach them like any other:
   - `crowds/<name>/look` (0..1) weights every member's layer named **`crowd-look`** (an Aim layer the node authors,
     with its own joints, pivot and limits); `crowds/<name>/lookTarget` (world point) is where it looks, converted
     into each member's own frame every frame.
   - `crowds/<name>/sway` (0..1) weights every member's layers named **`crowd-sway`** (a Secondary layer: its period
     and phase are authored, so it can sit on the beat; the route sets only its depth).
   - `crowds/<name>/lag` (seconds) is how far the members' clips run behind the timeline. A timeline track of it is a
     seek-safe change of speed: its slope is 1 - speed, so a breath held at 0.15 speed for 1.5 s is a lag rising by
     1.3 s. Position tracks that should slow with the clips are warped by the same function where they are written
     (Neon Dreams' build.py does this for its walkers).
3. `driveCrowds` runs every frame before the rigs are posed and is a pure function of that frame's parameters.
4. A layer at weight 0 changes nothing, so a rig whose layers are all idle still shares its pose (ADR-2116):
   `poseKey` now refuses only when a layer is active.

## Consequences

- A radial wave (heads turning outward from the Lantern) is several crowds -- rings by distance -- with the same route
  delayed per ring (the route chain's `delayMs`, ADR-900, up to 4 s), not per-node state.
- The look and sway are as seek-safe as the routes that drive them; the lag is exactly seek-safe when it comes from a
  timeline track.
- A crowd member with an active look poses on its own (no sharing) for as long as the look lasts.
