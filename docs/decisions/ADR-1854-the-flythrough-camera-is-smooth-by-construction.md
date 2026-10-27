# ADR-1854: The flythrough camera is smooth by construction; the river flows

- Status: Accepted, scene/strange-attractor (Gate 2c, 2026-10-27). Refines ADR-1853.
- Note: the jitter the owner saw survived this fix. Its real cause was the ray reconstruction (ADR-1855), not the
  path. The path work here stands, but it was not what made the picture shake. The Gate 2c water is superseded by
  ADR-1855's.
- Scope: scene-local. No engine code changed.
- Code:
  - `src/scene/attractor_world.cpp`: `buildFlight`, `smoothSamples`, `attractorCamera`, the flow bake, `bandWarp`
    and the flat gorge floor.
  - `shaders/attractor/world.wgsl`: `shadeWater`, `bandWarp`, `detailAt`.
  - `src/rendering/attractor_renderer.*`: the flow texture.
- Tests: `[adr1853]`. The hidden `[.attractor-camera-trace]` case writes the camera, frame by frame, through the real
  `Composition::update` at the offline step.

## The jitter: measured, then fixed

**Method.**
- The camera was traced through `Composition::update` at the offline clock (n/30 s), so what was traced is what the
  renderer received.
- Per frame: speed, vertical speed, view turn rate, roll rate and acceleration.
- Then the noise itself: each track minus its 9-frame moving average, converted to pixels at 1080p and 72 degrees.
- **The engine is not involved.** The pose goes straight from `attractorCamera` to the scene camera. There is no
  shake or breath on this scene, and no projection jitter in the renderer.

**Before (Gate 2b).**
- View-direction noise: 1.17 px rms, p99 3.7 px, peaks of 19 px.
- Angular acceleration: up to 3,566 deg/s², with a 120 deg/s spike in the last frame.
- Roll rate stepped up and down by 10-15 deg/s at once.

**Causes, all in this scene's flight builder.**
- **The height was re-clamped to the clearance after smoothing.** The clearance is the highest ground within 22 m,
  sampled per frame, so it jumps by metres as the path crosses relief. The aim looked 2.5 s ahead at that noisy
  height, which multiplied the noise into the view direction.
- **Hard limits.** The pitch was clamped with `clamp` and the bank with `clamp`, so both kinked whenever a limit
  engaged.
- **The aim was switched at the path's end**, from a look-ahead to an extrapolation.
- **A fade-in blended raw and smoothed paths at the start.**

**Fix.**
- The clearance only lifts a cone envelope (fall 45 m/s, climb 24 m/s). The envelope is smoothed in two rounds, and
  nothing re-clamps after the last one. A 24 m margin, with 12 directions sampled, keeps the camera clear (tested).
- The aim is precomputed from the smooth eye path: a tanh soft pitch limit, then smoothed itself.
- The bank goes through a tanh soft limit.
- Smoothing extends the ends by odd reflection, so the opening frame's motion is exact without a fade.
- Every flythrough frame has the same three smooth tracks (eye, aim, bank), interpolated linearly.

**After.**
- View-direction noise: 0.52 px rms, p99 1.6 px, max 1.7 px.
- Angular acceleration: max 59 deg/s².
- No steps in roll.

## Other changes

- **The river flows.**
  - The generator bakes a flow field: the downstream direction along the meander distance's contours, signed by the
    trunk.
  - The water's ripples are long streaks advected downstream at 3.2 m/s, with a finer cross-chop.
  - Each octave fades out before it is smaller than a pixel. Sub-pixel chop made glitter and saw-toothed reflection
    edges.
  - Foam is drawn out downstream along the shallow banks, plus drifting flecks.
  - The reflections are kept.
- **The gorge floor is flat at -2 m.** Plain noise had left shoals just under the water, which drew the pale grey
  patches in the river.
- **Bands of uneven thickness.**
  - A monotone wobble (`bandWarp`), shared by the CPU terraces and the shader, removes the evenly spaced diagonal
    stripes on the fin.
  - Near-face relief uses irregular, partly expressed partings.
  - Its gain is capped, because a high gain turned the across-face undersampling into a vertical comb.
- **Pacing.**
  - The dive is about 9 s (it was 13 s), so the river run is about 28 s.
  - The final render is supersampled 2× (`--supersample 2`), which removes the remaining speckle on rims and
    reflection edges.
