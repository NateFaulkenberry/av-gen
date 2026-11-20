# ADR-2112: bobbing cloud formations -- lobes, lights and scene nodes that float as one

- Status: Accepted (2026-11-20), avagent/neon-dreams (Neon Dreams, iteration 9; owner msg 73: "more floating cloud
  structures ... bob up and down (floating in the ocean)"; msg 78: floating rock islands, their buildings and their
  wrapping cloud move together)
- Code: `scene::CloudBob`, `cloudBobOffset`, `cloudBobReach` (`src/scene/clouds.{hpp,cpp}`); `binCloudLobes(lobes,
  bobReach)` and the per-frame offsets (`src/rendering/cloud_renderer.cpp`); `bobOffset` / `cellBob` in
  `shaders/clouds.wgsl`; `CompositionNode::bob`, `Composition::cloudBobFor` (`src/scene/composition.{hpp,cpp}`).
- Tests: `tests/rendering/test_clouds_it9_gpu.cpp` (`[adr2112]`).

## Decision

- The clouds block gains `"bobs": [{name, amplitude [x, y, z] m, period s, phase 0..1, liftMax m}]` (up to 32). The
  offset at second t is `(ax sin(2pi(t/(1.618P) + 0.7ph + 0.31)), ay sin(2pi(t/P + ph)) + lift, az sin(2pi(t/(1.83P) +
  1.3ph + 0.67)))`: y bobs at the period, x and z drift on incommensurate periods (no closed orbit). A pure function of
  the timeline second and the lift, so a seek lands where play would.
- `lift` is a parameter, `clouds/bob/<i>/lift` (metres, clamped to +-liftMax): the route target (a bass swell lifts an
  island a little).
- A lobe (inline or in a lobes file), a glow, a beam and a wisp zone (ADR-2113) take `"bob": "<name>"`. A scene node
  takes `"bob": "<name>"` and its local position moves by exactly the same offset, so its children (buildings on an
  island) follow; a procedural node's instances move with its transform.
- The lobe bins stay static: a riding lobe is binned over its footprint grown by its formation's reach (|amplitude| +
  liftMax), and so are the column height ranges and the lobes' box. The march adds the formation's offset (per lobe,
  index + 1 in the lobe row's spare lane) where it tests the lobe. The erosion and edge detail at a sample are taken
  at the sample less the offset of the formation its cell's first lobe rides, so a bobbing body's billows move with it
  instead of the world-space noise sliding through it.
- Glow positions, beam origins and wisp zones are offset on the CPU each frame.
- Limits raised for Neon Dreams' iteration-9 islands: 32 formations, 8,192 lobes (`kMaxCloudLobes`, was 4,096), 2,048
  procedural objects (`kMaxProceduralObjects`, was 1,024: every material part of every per-formation group is one).
- Authored lights ride a formation through an empty `group` node that rides it (ADR-278's light `node`).

## Consequences

- Motion of a few metres over seconds is well inside what the temporal resolve reprojects (it reprojects by the cloud's
  own distance and clips to the neighbourhood); nothing smears in the tests or the Neon Dreams render.
- A cell where two formations' lobes meet takes the erosion offset of its first lobe; the other's billows slide by the
  difference there (metres at most).
