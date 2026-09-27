# ADR-918: Aerial perspective takes the sky's radiance

**Status:** Accepted
**Date:** 2026-09-26
**Follows:** ADR-705 (one air, one law: the surface fog and the march), ADR-347 (fog from the sky's
horizon colour, day/night only), ADR-230 (the aurora and comets as a sky layer), ADR-345 (the
analytic sky in the frame block)
**Found by:** the GV3 revision audit, `docs/glowmere-valley-3/revision/audit/reports/render-post.md`
(engine gap 6; "Draw-distance root cause" 4)
**Implemented by:** `Environment::fogSky`, `Environment::fogSkyDistance`, `scene/fogSky` and
`scene/fogSkyDistance` (`src/scene/scene_types.hpp`, `src/scene/composition.cpp`); the fog's sky map and its pass (`SceneRenderer`,
`shaders/fog_sky.wgsl`); `applyFog`, `fogSkyUv` and `fogSkyRadiance` (`shaders/common.wgsl`); the
background extracted into `shaders/sky_background.wgsl`
**Tests:** `tests/rendering/test_fog_sky_gpu.cpp` (`[fogsky]`)

## Context

The surface fog fades every surface towards one constant colour, `Environment::fogColor`
(`applyFog`, ADR-705). Aerial perspective is not a constant: it is light the air scatters towards
the eye, and what lights the air is the sky around it. So a ridge seen against a bright horizon
faded towards a colour the sky behind it did not have.

- **Glowmere Valley 3 shows it at its worst.** Its fog colour is navy (0.10, 0.18, 0.32) and its
  valley rim, 335-505 m out, stands against an aurora. At 400 m the fog leaves about 4% of the rim,
  so the rim is 96% navy against a bright green-and-violet sky: a dark cut-out, in s06, s12, s14,
  s21 and s39 (render-post.md, stills at 24.5, 62 and 78 s).
- **What already existed does not reach it.**
  - ADR-347 blends the fog colour towards the sky's horizon colour, but only through the day/night
    cycle (GV3's is off), only as one colour for the whole horizon, and never with the aurora,
    which is not part of the sky's parameters at all: it is a separate additive draw over the
    background (ADR-230).
  - `post/look/atmospheric` (Image/Look §68.1) tints by distance towards a constant tint, in the
    composed image; ADR-705's own comment says it is not the way to get aerial perspective.

## Decision

**`scene/fogSky` (0..1, default 0): how much of the colour the surface fog fades towards is the
sky's own radiance in the direction of the ray, and `scene/fogSkyDistance` (metres, default 0 =
automatic): how far away the fog has fully become the sky's colour.** At 1 a far ridge fades into the
sky behind it, while the air near the camera keeps the fog colour it was tuned with.

- **With distance, not everywhere.** `applyFog` blends the constant colour towards the sky's
  radiance by `fogSky * smoothstep(0, D, d)`: nothing at the camera, fully at D and beyond. This is
  the shape production engines use for directional in-scattering (Unreal's height fog fades its
  in-scattering cubemap in over a start and an end distance). Physically, air near the camera is lit
  by the whole sky, not by the sky's radiance in one direction -- and GV3's medium scatters almost
  isotropically (anisotropy 0.12) -- so the view direction's radiance is only right where the fog
  is thick enough to stand in for the backdrop, which is the far rim. Taking it at every distance
  was the first version of this decision, and GV3's first render with it lit the ground mist a few
  metres from the camera with the horizon's brightness (s14's mean level 58 -> 78 at an unchanged
  density; the stills below).
- **D is automatic unless set:** three of the fog's own extinction lengths, `3 / (density x
  absorption)`, where a level ray through the base density is 95% fog. That is where a far ridge
  would otherwise be a cut-out, and it moves with the density: 375 m in GV3's air (0.02 x 0.4), at
  its rim (335-505 m). An explicit D is metres, for a scene that wants the sky's colour nearer or
  further.

- **The sky is read through a small map built each frame.** `fog_sky.wgsl` renders a 128 x 32
  RGBA16F map of the sky's radiance by direction:
  - what the camera would see there: the background through `skyBackgroundAt` plus the atmospheric
    layer through `atmosphereSkyAt` (the aurora and comets);
  - averaged over each texel with 4 x 4 samples, so the fog gets the sky's colour and brightness by
    direction and none of its fine structure;
  - without the stars or the crisp moon disc, which are points of light, not something air scatters
    into a colour.
- **The parameterisation.** u is the azimuth, in the sky equirect's own convention. v is the square
  root of the elevation's sine over the upper hemisphere, which puts twice as many rows near the
  horizon as a linear map. Below the horizon the fog reads the horizon row: looking down into a
  valley, the air is lit by the sky above it, not by the ground colour the sky draws beneath its
  horizon.
- **One sky, not two.** The background's colour logic moved out of `skybox.wgsl` into
  `sky_background.wgsl`, and both the background pass and the map call it. The map therefore holds
  exactly what the background pass draws, including the flat background colour when the skybox is
  off (`frame.fogSky.y`, from `skyBackgroundFor`). `frame.fogSky.z` carries D.
- **Where it is read.** `applyFog` mixes the constant colour towards `fogSkyRadiance(ray)`, so every
  surface the fog reaches takes it: entities, the procedural scatter, skinned
  characters, SDFs, water, shells and ribbons (every caller of `applyFog`).
- **It costs nothing when off.** At 0 the lane is zero, the map's pass is not encoded, `applyFog`
  never samples the map, and the frame is the pre-ADR-918 frame to the bit.
- **Deterministic.** The map is a pure function of the frame block: the sky's parameters, the
  atmospheric effects' state (packed from the transport second) and the camera's position. It
  carries no history, so a seek lands on the frame play did.
- **Where it is seen.** The Environment panel's "Sky and fog" section draws "Fog takes the sky's
  colour" under the fog density, and while it is above zero a line saying what it does ("the far
  distance fades into the sky behind it, aurora included") and "...all sky colour from", the
  distance, in metres with 0 read as "where the fog is thick". The Parameters panel lists both as
  direct rows of `scene`, labelled for what they look like.
- **Bindings.** The map and its sampler are group 0 bindings 16 and 17 of the frame layout. The
  map's own pass draws with the aux frame group, whose binding 16 is a 1 x 1 placeholder, because a
  pass may not sample the texture it renders into. The shadow and mask groups bind the placeholder
  too.

## Consequences

- **No existing scene changes.** The default is 0 and no tracked scene or project sets it.
- **GV3** should set `scene/fogSky` 1.0 and leave `scene/fogSkyDistance` at 0 (automatic: 375 m
  at its base density of 0.02, and 208-500 m as its timeline's density arc runs from 0.036 to
  0.015: thicker air takes the sky's colour nearer). The rim then reads as air
  against the aurora rather than as a navy cut-out, and the near air keeps its navy. Its density
  need not come down for the rim's sake; the audit's 0.009-0.012 remains a look choice, not a fix.
  Measured on a snapshot of GV3 (960x540 x2, offline):

  | Shot | Mean level: fog colour | fog from the sky | the first version (sky's colour everywhere) | near third | far third |
  |---|---|---|---|---|---|
  | s06, 24.5 s | 46.9 | 49.2 | 60.2 | 44.6 -> 49.3 (79.3) | 60.8 -> 63.1 |
  | s14, 78 s | 57.8 | 64.0 | 78.5 | 53.7 -> 58.1 (89.1) | 67.1 -> 81.1 |
  | s21, 144 s | 36.8 | 38.6 | 49.2 | 47.9 -> 50.7 (79.8) | 53.6 -> 56.3 |
  | s12, 62 s | 39.7 | 40.6 | | 46.8 -> 47.1 | 50.4 -> 53.7 |
  | s39, 214.6 s | 58.9 | 63.0 | | 64.6 -> 68.0 | 77.5 -> 86.6 |

  The far third brightens as the valley's ends fade into the sky; the near third barely moves.
- **Cost on GV3:** two seconds of s14 at 3840x2160 x2, 41.0 s without and 42.1 s with (+2.7%).
- **The volumetric march is unchanged.** Where it runs (`volumeMaxDistance` > 0), the near air is
  still lit by the march's own in-scatter; `fogSky` colours the closed-form segment beyond it. GV3's
  march is off (`scene/volumeMaxDistance` 0).
- **Cost.** One 4096-texel pass a frame while `fogSky` is above zero (16 sky and aurora evaluations
  a texel), and one bilinear fetch per fogged fragment.
- **Measured** (`test_fog_sky_gpu.cpp`, 320x200, a ridge 300 m out behind 0.01/m of fog, a warm
  horizon against a navy fog colour):
  - The ridge's band against the sky that stands there without it (relative L1 above the horizon):
    **0.054 fogged towards the sky, 0.865 towards the constant colour.** Mean luminance: the sky
    behind 0.429, the ridge 0.406 from the sky and 0.039 from the constant colour -- the cut-out.
  - The open sky's rows are identical to the bit between the two arms (the sky is not fogged), and
    the map is built only in the arm that asks for it.
  - The aurora behind the ridge moves the fogged ridge by 0.92 (relative L1) when the fog takes the
    sky's colour and by exactly 0 when it does not; the map's horizon rows read 0.948 with the
    aurora against 0.389 without.
  - Two fresh renderers draw the same frame and the same map to the bit (the determinism case).
- **Tests re-baselined:** `test_terrain_fog_gpu.cpp`'s compute harness builds its own group-0
  layout around `applyFog`; it now declares bindings 16 and 17 (a 1x1 placeholder that a zero lane
  never reads). Without them the pipeline was invalid and the test compared zeros. No engine shader
  was affected: every caller of `applyFog` binds the frame layout.
- **The first run of this test measured nothing,** and is recorded because the failure shape is
  this codebase's most common one: the ridge stood 300 m out behind the camera's default 200 m far
  plane, so all three arms were the bare sky and every gap was 0. The fixture now reaches 2000 m.
