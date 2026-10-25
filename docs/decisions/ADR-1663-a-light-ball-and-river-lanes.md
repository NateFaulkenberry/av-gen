# ADR-1663: A ball of light thrown across the view, and lanes and splashes for river light

- Status: Accepted (scene/aurora-borealis)
- Extends ADR-1662 (path waves) and ADR-207/702 (surface waves).
- Asked for by the Aurora Borealis owner's pass-4 notes (`docs/prototypes/aurora-borealis/30-owner-notes-pass4.md`):
  - "remove the shooting stars instead use a dynamic ball of light - shoot it across the canyon from the camera
    perspective ... it should be able to throw lots of light onto the geometry below";
  - "increase the amount of wake ... closer to the camera, as if someone dropped a heavy ball into the water";
  - "give them space across the width of the river".
- Code:
  - `src/world/wave_effect.{hpp,cpp}`:
    - `PropagationKind::LightBall` ("ball") and `BallShape`: launch and target in the camera's frame (right, up,
      forward), the flight's seconds, its arc, the light's half radius and range, the core and glow sizes;
      `resolveBallFront`.
    - `PathShape::{lane, laneJitter, splash, nearWake}`.
    - `WaveGpu` 208 → 224 bytes (`extra`).
    - `ResolvedWave::launch`.
  - `src/world/effects/kinds/wave_rows.hpp`: the rows. The ball rows are written only by a ball, the new path rows only
    by a path wave (`EffectField::written`, ADR-1662).
  - `shaders/common.wgsl`: `Wave.extra`, `waveBallGlow`, `wavesBallSky`. `shaders/wave_effects.wgsl`:
    `waveBallLight`, the ball in `wavesAtSurface`, the splash and near-wake in `wavePathDisturb`.
    `shaders/atmosphere_fx.wgsl`: the ball against the sky. `src/rendering/scene_renderer.hpp`: the frame block's
    sizes, +256 bytes.
- Tests:
  - `tests/unit/test_wave_path.cpp` (`[adr1663]`): the lane is on the right side; every chain point is 40 m off the
    centreline; a jittered hit keeps its lane; the splash and near-wake pack; the ball's chord, bow and hold; its
    packing and JSON.
  - `tests/rendering/test_wave_path_gpu.cpp` (`[adr1663]`): the ball lights the ground beneath it, and its core shows
    where it stands in front of the ground. With no ball the frame is the ground's, hash for hash.

## Context

The scene's land is unlit: its programs compute every facet's colour. So an engine point light (LIGHTMOD's pool)
reaches nothing there. A comet draws only on unoccluded sky pixels, so a ball flying low over the canyon, in front of
the mountains, would vanish. The comet's ground term is a plane-distance pool, not a light. The surface-wave
contribution (`wavesAt`), however, reaches every surface: lit and unlit, the procedural scatter, the water, and a
riverbed seen through the water.

## Decision

A fourth wave kind, a light ball. Each frame the CPU places the ball on the chord between two camera-relative points,
eased and bowed up by `arc`. The GPU then does two things:

- **Lights every surface** within range, falling to half at the light radius, weighted by how squarely each surface
  faces the ball. Facets turned toward it flare and the rest take a little, so the light reads as passing over the
  land.
- **Draws the ball** wherever a view ray passes close to it before reaching what it hits: in `wavesAtSurface`,
  bounded by the surface's distance, so the ball hides behind what it flies behind; and in the sky pass, unbounded. Its
  core never shrinks below about a pixel and a half.

A trigger with a lifetime gives each event its own ball (ADR-981). A scene pools them, as Aurora Borealis does two.

Path waves gain:

- **A lane:** metres off the centreline, plus a per-hit jitter hashed from the hit's launch second. The CPU moves the
  chain, so the shader is unchanged by it.
- **A splash:** rings spreading from the launch point for about two seconds.
- **A near-wake gain:** the ripples are up to `nearWake` times stronger at the camera, falling to 1x by 700 m.

The path wave's light below the water now reaches a fixed 40 m: its lane carries the seconds since launch, which the
splash needs.

## Consequences

- Every other wave packs as before (`extra` zero, path chain zero), and a frame without a ball is unchanged.
- The ball costs per surface pixel only within its range, plus one ray test per pixel.

## Rejected

- **An engine point light:** unlit surfaces ignore it.
- **The comet** as the ball: it draws only on sky pixels.
- **An animated node:** a node moves on timeline tracks or XFORM effects, and neither throws on a live crash.
