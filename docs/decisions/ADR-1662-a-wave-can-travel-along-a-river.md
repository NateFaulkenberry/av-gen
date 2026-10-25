# ADR-1662: A wave can travel along a river

- Status: Accepted (scene/aurora-borealis)
- Extends ADR-207/702 (surface waves: Ground Pulse and Travel Beam) with a third distance metric, and ADR-981 (each
  trigger its own front). Reads the terrain feature ADR-046 defines and the water's ADR-1643 flow follows.
- Asked for by Aurora Borealis pass 3 (`docs/prototypes/aurora-borealis/20b-brief-river-light.md`,
  `22-research-pass3-scene.md` §6, E2): a kick or snare sends light down the river's winding course. The owner's
  notes since: slower, with each beam fading on its own envelope; it should light the mountains more; the water
  ripples are required; no cellular caustic.
- Code:
  - `src/world/wave_effect.{hpp,cpp}`: `PropagationKind::PathWave` ("path"); `PathShape` on `Propagation` (the
    node and feature by name, ease, wake growth, core width, twin offset, halo, land reach and height, land share,
    embed, ripples, near fade); `PathGeometry` (project, walk, wrap a polyline); `resolvePathFront`; `WaveGpu`
    144 → 208 bytes (`path[4]`, an eight-point chain); the lanes a path wave re-reads are tabled on `WaveGpu`.
  - `src/world/effects/effect_timing.hpp`: `EffectSceneQuery::featurePath` (optional). `src/app/engine.cpp`: the
    composition answers it from a terrain node's `worldMap.features[].samplePath()` and its world transform.
  - `src/world/effects/kinds/wave_rows.hpp`: the Path rows (routable parameters) and the "path" choice; the node
    and feature are written beside the rows. `src/world/effects/effect_registry.{hpp,cpp}`: `EffectField::written`,
    so the Path rows are written only by a path wave and no other wave's file changes;
    `src/world/effects/effect_conformance.cpp` does not expect an unwritten row back.
  - `shaders/common.wgsl` (`Wave.path`), `src/rendering/scene_renderer.hpp` (the frame block's sizes and offsets,
    +1 KB).
  - `shaders/wave_effects.wgsl`: `wavePathHit` (projection onto the chain), `wavePathLand` (inside `wavesAtSurface`;
    `wavesAt` is it with `onWater` false), `wavePathWater`, `wavePathDisturb`.
  - `shaders/water.wgsl`: three marked blocks: the ripples' slope added to the normal before the reflection; the
    light added under the reflection, `(1 - fresnel) × under + surface`; its share of the emission. The water calls
    `wavesAtSurface(..., true)`, which skips path waves.
- Tests: `tests/unit/test_wave_path.cpp` (`[adr1662]`: geometry on an L and a closed square; the front's arc, its
  direction away from the camera, the ease, the wake's growth; every chain point on the polyline past a bend;
  seek equals play, byte for byte; a path the scene lacks is inactive; the packing; the JSON, and a non-path
  wave's file without the path rows); `tests/rendering/test_wave_path_gpu.cpp` (`[adr1662]`: round an L, the
  path past the corner is lit and the first leg's straight continuation is not; a dark or distant path wave
  beside a ring leaves the ground's hash; the water with no path wave lit, or a dark one, hashes the same as a
  shader with the three blocks removed, plain and cinematic, and its light and its ripples each change it).

## Context

The scene's pass-2 river pulse was a directional ground pulse: a slab perpendicular to the camera's heading, so it
lay across the river as a bar and ignored the bends. The river has a centreline the engine already holds, the
`river` feature's 256-point polyline, which the water's flow and streaks are built from. Research B measured that
a straight beam leaves the river by over 100 m within a kilometre at its bends, and that the water carries no
along-river coordinate (its `uv.y` is across the channel).

## Decision

A third metric, distance along a named terrain feature. Each frame, for each live front, the CPU projects the
source (the camera) onto the polyline, picks the direction that faces away from the camera, and places the front
`startOffset + range × eased(age / travel)` along it. It hands the GPU eight points: just ahead of the front, at
it, then back along the wake at 5, 13, 26, 45, 70 and 100% of its length. The spacing is closest at the head,
where the core is brightest: at the scene's tightest bend (radius about 300 m) the chord there sags under 2 m. The
wake grows with distance travelled, so it keeps a length on screen.

The shader projects onto that chain, which gives metres behind the front (`u`) and metres from the path. From
those it draws three things:

- **On water**, a core (or twin filaments), a halo and a white-hot leading edge. The edge is widened to the
  distance the front moves in a frame, so a fast front does not strobe. The light goes under the reflection,
  with a share on top so it reads at grazing distance.
- **On the water's normal**, the wake's ripples. Crests spread from the path behind the front, wander and break
  with position, and die in about half a second.
- **On land** (any surface but the water: banks, mountains, a riverbed), the beam's colour across `reach` and up
  `reachHeight`, weighted by how squarely each surface faces the path. On faceted land that lights the facets
  themselves, plane by plane, instead of laying a pattern over them.

A near fade by distance from the camera keeps the first frames of a front out of the bottom of the frame without
delaying its onset.

A front lives as an ADR-981 front, so a trigger with a lifetime gives each hit its own beam fading on its own
envelope, at most four per instance. Past its range the front holds while its fade takes it away.

## Consequences

- Every other wave packs and draws as before. The path chain is zero for them, and the GPU tests hold the ground
  and both water paths to their hashes.
- The frame block is 1 KB larger, and every offset after the waves moved, on both sides.
- A path wave costs only where its chain and reach are, behind a distance early-out per front.
- `EffectField::written` lets a kind write a row only in the mode that uses it. The row stays a parameter.

## Rejected

- **A narrow directional beam** (the zero-engine route): a straight line, off the river at every bend.
- **The polar angle as the arc** (research B's analytic probe): it is this scene's geometry, not the engine's, and
  it varies ±15% from true metres.
- **A single-plane approximation** (origin and tangent at the front): 17 m off the centreline over a 200 m wake at
  a 300 m radius.
- **Uploading the whole polyline** as a buffer: a new binding for what eight points round the front answer.
- **A Worley caustic net**: probed by research B, rejected by the owner ("this pattern does not look good"). The
  land term lights real facets instead (the scene adds a faceted riverbed for it to light).
- **A ripple simulation** in a render target: persistent state, a pass and a target, and not seek-exact. The
  analytic wake is a function of the front and the second.
