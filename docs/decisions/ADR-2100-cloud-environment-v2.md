# ADR-2100: Cloud Environment v2 -- an unbounded sea, glow emitters, routable knobs and a cheaper march

- Status: Accepted (2026-11-19), avagent/neon-dreams (Neon Dreams, iteration 2)
- Extends: ADR-1289 (sculptural clouds from authored lobes). Same block, same renderer, same Environment seam
  (ADR-1200); no second cloud renderer.
- Code: `src/scene/clouds.{hpp,cpp}` (the block, `registerCloudParameters` / `applyCloudParameters`),
  `src/rendering/cloud_renderer.{hpp,cpp}` (three passes, the lobe bins, the noise volume), `shaders/clouds.wgsl`
  (the march), `shaders/clouds_resolve.wgsl` (the temporal resolve and the upsample), `Composition` (registers the
  knobs, applies their finals each frame), `EnvironmentFrame::historyValid`.
- Tests: `tests/unit/test_clouds.cpp` and `tests/rendering/test_clouds_v2_gpu.cpp` (`[adr2100]`), and ADR-1289's
  `tests/rendering/test_clouds_gpu.cpp`.

## Context

The Neon Dreams blockout built its cloud ocean from ADR-1289 lobes: about 380 lobes, of which about 250 formed a
jittered "sea" sheet. At 1080p on this Mac, the cloud pass cost 33-68 ms per frame (pass medians: far 34, mid 39,
near 68, toward the secondary 33). The look had three problems:
- The sea ended where the lobes ended.
- A lobe's spheres showed as rings.
- The settlements' light never reached the cloud, because the pass knew only the key, an ambient and a "warm"
  horizon term.

The causes were all in the march (`clouds.wgsl` v1):
1. Every pixel tested every lobe.
2. Every sample evaluated every lobe its ray had touched (up to 160), with three hash-noise fbms.
3. The light march repeated that work.
4. The pass ran at full resolution.
5. Each ray's lobe span was split into a fixed number of steps, so the samples sat on shells concentric with the
   lobes. Those shells are the rings.

The block was also static: it had no parameters, so routes and palette moods could not reach it.

## Decision

Extend the `clouds` block and its renderer.

**Sea** (`"sea": {base, top, coverage, coverageScale, density}`). An analytic height band, unbounded in x and z:
- A coverage map (two octaves of the noise volume's slow channel, in xz only, so a break goes all the way down)
  decides where there is cloud. `coverage` sets how much.
- A height profile gives a flat floor and a top that rises where the coverage is dense.
- Within it, a Perlin-Worley shape keeps only the strongest noise near the top, so the tops billow.

With a sea, `lobes` become optional.

**Lobes**: these are unchanged as authored. They are now **binned** on an xz grid: cells about half a typical lobe's
radius, at most 64 along a side, at most 64 lobes per cell (the biggest are kept, with a warning). A sample reads
only its own cell's lobes. They unite with the sea (max), erode with the same noise, and then lose their thin edges
to a finer detail noise.

**Noise**: one tiling 64^3 RGBA8 volume, built on the CPU once per process (deterministic):
- R: Perlin-Worley.
- G, B: Worley fbm at two frequencies.
- A: Perlin fbm.

The volume replaces per-sample hash fbms with one filtered tap per use. Every tap is offset by `drift` (m/s) ×
timeline seconds, which is a pure function of the clock, so a seek lands where play would.

**Glows** (`"glows": [{position, color, radius, intensity, name}]`, up to 16). These are coloured lights inside or
below the cloud: a settlement's neon.
- Each sample adds `colour × intensity × falloff`. The falloff is inverse-square-like in `radius`, cut to zero by
  4 radii.
- The glow is dimmed by `exp(-extinction × glowOcclusion × density × distance / 2)`, so it lights the cloud's near
  surfaces and fades inside it. This is a local estimate, not a shadow march: cheap and art-directable.
- `glowGain` scales them all.

**Light**: the key still has its shadow march (`lightSteps` ≥ 1). `lightSteps: 0` selects an **analytic** estimate:
the cloud above the sample (the sea's profile, the lobes' vertical chords), slanted toward the key. It costs nothing
and reads well at night, when the key is faint.

**March**:
- It runs at `resolution` of the frame (0.25-1, default 0.5).
- The span is the union of the sea band and the lobes' box, cut at the scene depth and `maxDistance`.
- Steps grow with distance (`minStep`, `stepGrowth`) and double through empty air.
- Sample positions depend only on the distance from the eye (no shells, so no rings). They are offset by
  interleaved-gradient noise rotated each frame by a golden-ratio sequence over `FrameTime::frameNonce` (timeline
  based, so a re-rendered range matches).
- The scene depth each march pixel is cut against is the nearest or the farthest of its full-resolution footprint,
  in a checkerboard.

**Temporal resolve** (`temporal` = history weight, 0-0.97, default 0.85):
- Last frame's result is reprojected through the view-projection *it* was drawn with, at this pixel's
  transmittance-weighted cloud distance (the sky's far distance where there is none).
- It is clamped to this frame's 3x3 neighbourhood and blended.
- History is dropped on any discontinuity: `EnvironmentFrame::historyValid` (false on the first frame, a seek, a
  cut or a scene swap, exactly when the renderer drops its own), a resize, time going backward, or a gap over 0.5 s.

**Upsample**: the four march texels are weighted bilinearly and by how well the scene depth each was cut against
matches the pixel's own (within 8 %). The composite is premultiplied over HDR, as before.

**Knobs**: `clouds/<leaf>`, registered with the block's own values as defaults, so registering changes no picture:
- floats: `density`, `swell`, `lightIntensity`, `glowGain`, `glowOcclusion`, `noiseAmount`, `detailAmount`,
  `hazeDistance`, `powder`;
- colours: `lightColor`, `ambientTop`, `ambientBottom`, `warmColor`, `hazeColor`;
- vectors: `drift`;
- sea: `sea/coverage`, `sea/density`;
- per glow: `glow/<i>/intensity` and `glow/<i>/color`.

`swell` (0-1) raises the sea's top by up to half its depth, adds coverage and fattens the lobes. It is the bass
target. Palette moods reach the cloud through the colour knobs.

**Quality**: `resolution`, `steps`, `lightSteps`, `stepGrowth` and `temporal` are the quality knob. For example:
- Live: 0.5 / 64 / 0 / 0.035 / 0.85.
- Offline: 1.0 / 128 / 2 / 0.02.

## Measurements (this Mac, release, 1080p, headless, gpu-lock)

Cost is measured as the GPU frame median with the block minus without it (60 frames), because the per-pass split
attributes overlapping passes unreliably here: one run charged 0.00 ms to `clouds` while the frame said otherwise.

| View | v1 cloud pass (blockout) | v2, frame delta | v2 frame total |
|---|---|---|---|
| far | 34 ms | 0.7 ms | 17.1 ms |
| mid | 39 ms | 2.0 ms | 20.3 ms |
| near (inside the formation) | 68 ms | 5.1 ms | 30.5 ms |
| toward the secondary | 33 ms | 1.1 ms | 17.4 ms |

At the offline settings (full resolution, 128 steps, 2 light steps), the near view costs about 53 ms and the mid
view about 15 ms. The old renderer on the old blockout scene (lobes only, no sea) drops from 29.9 ms to 2.6 ms
(pass medians), from the bins and the reduced resolution alone.

## Consequences

- **Scenes using `clouds` before this ADR change look** (biovalley, digital-mosh, rainbow-world experiments). They
  now march at half resolution with temporal accumulation, use the texture noise (a different billow) and use
  distance-growing steps. No compatibility mode, per ADR-441. Their owners should check the look before this
  branch merges.
- A single-frame render (a still with no history, the first frame after a seek) shows the raw jittered march: a
  little grain at cloud edges at `resolution` 0.5. A still meant for print should use `temporal: 0` with
  `resolution: 1` and more steps, or be captured after a few frames.
- The glow is not shadowed by distant cloud, only by cloud near the sample. A glow behind a far bank still lights
  the bank's far side. That is cheap and reads as "the city lights the cloud around it", but it is not a light
  transport.
- The analytic light ignores lobes that do not overlap the sample's xz column.
- Glows do not light anything but the cloud: the scene's own point lights still light the buildings and the fog.
  A settlement wants both.
- The temporal history belongs to one view. A renderer that draws two different cameras in alternation would see
  ghosting (it has no consumer today).

## Also found: the blockout's "blocky rectangles"

The rectangles in the blockout's "toward the secondary" view were not the clouds. With the clouds off, the volume
off and the post chain off, they remained. They were z-fighting between the top and bottom faces of the 2 m thick
"cosmic floor" box, 60 km wide and 1.6 km below the camera. At that distance the 24-bit depth resolves about 0.3-3 m,
and Apple's tile-based HSR resolved the fight per 32x32 tile, so it drew as black rectangles with stair-stepped edges.
The fix is in the scene: the floor is now 400 m thick. No engine change was needed.
