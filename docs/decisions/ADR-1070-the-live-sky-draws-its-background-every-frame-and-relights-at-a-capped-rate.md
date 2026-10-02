# ADR-1070: The live sky draws its background every frame and relights at a capped rate

- **Status:** Accepted (2026-10-02), proto/sonic-garden (VFX expansion; the first performance-tier item)
- **Code:**
  - `SceneRenderer::LiveSkyLighting`, `setLiveSkyLighting` and `serviceLiveSky` in `src/rendering/scene_renderer.*`;
  - `EnvironmentProcessor::beginSky` / `advanceSky` and `SkyBuildJob` in `src/rendering/environment.*`;
  - `FrameUniforms::skyLive` and the live branch of `skyBackgroundAt` in `shaders/sky_background.wgsl`;
  - `--live-sky-rate <hz>` in `src/app/application.cpp`.
- **Tests:** `[adr1070]` in `tests/rendering/test_live_sky_gpu.cpp`; the `[adr1022]` cases still pass unchanged.

## Context

On Salt Flat Mirage the chord paints the sky: the routed palette drives `env/sky/zenithColor`, `horizonColor` and
`sunColor`. A sky past ADR-1022's tolerance used to run the whole IBL chain inside the frame:
- the source cube, the irradiance cube and the GGX prefilter;
- blocking on the queue three times.

Live that happened 3.7 times a second, at 9-17 ms each. Every build was a frame over budget.

ADR-233's deferral does not help. It waits for the input to hold still, but a performance never holds still. So its
ceiling rebuilt the sky on schedule, inside the frame, every time.

## Decision

Live only, two separations.

1. **The background is drawn from the frame's own values.**
   - While the procedural sky is the IBL and the background is sharp (`skyboxBlur` 0), the background pass evaluates
     the analytic sky from this frame's resolved parameters. It uses the same maths as the cube's `fs_sky`: the
     sun's intensity, the sky's intensity, `params.w` on top, and the disc floored as the source cube floors it.
   - It does not sample the cube. The sky the audience sees therefore follows the chord on every frame.
   - A blurred background is a prefiltered mip and stays on the cube.
2. **The lighting cube is rebuilt at a capped rate, spread across frames.**
   - A sky past ADR-1022's tolerance of the one the lighting was built from starts a build. A new build starts at
     most `maxRateHz` times a second (default 2).
   - `beginSky` allocates new cubes and records the chain's 96 passes. Each frame, `advanceSky` encodes passes up to
     `passBudget` (4e6 texel-samples, about a fifth of the chain) and submits them without waiting.
   - Once the last pass is submitted, the new IBL is swapped in. WebGPU runs a queue's submissions in order, so the
     frame that binds the cubes reads them finished.
   - The cubes on screen are never written. The lighting lags the sky by at most about 1/rate plus the frames the
     build spans (about 0.6 s at the defaults).

`processSky`, the blocking path, now runs the same recorded pass list with its old submit-and-wait pattern. The live
and offline lighting are therefore one implementation, and the test checks that their lighting agrees to within 1
level.

**Offline is unchanged.**
- `liveSky_` is off by default, and only `Application::runLive` turns it on. `runHeadless` never does.
- An offline render or a seek rebuilds whenever the hash moves and draws the background from the cube, exactly as
  before.
- `skyLive` is zero, so the shader takes its old branch. `[sky]`, `[ibl]` and `[baseline]` pass
  unchanged.
- The schedule reads a wall clock, which is why it is live only.

What is still in place:
- ADR-1022's tolerance still decides whether a sky has moved at all.
- ADR-233's deferral still governs the editor when the schedule is off (`--live-sky-rate 0`) and in the `--ui-ab`
  eager-sky arm.
- A first sky, or a tier change of the cube sizes, is still built at once, blocking.

## Measurements

Salt Flat Mirage, live, the probe's `demo` scenario through BlackHole, with no capture. The project was frozen for
the four runs. The arms are interleaved in one build (`--live-sky-rate 2` against `0`), on an M2 Max with a 60 Hz
display. The statistics cover the performance, from 2.5 s after start-up.

| arm | frames | interval p50 | interval p99 | max | hitches (> 25 ms) | frame work p99 | lighting builds |
|---|---|---|---|---|---|---|---|
| schedule off, run 1 | 1758 | 16.66 ms | 33.80 ms | 34.9 ms | 62 | 14.45 ms | 120 blocking (9.1 ms mean) |
| schedule off, run 2 | 1747 | 16.67 ms | 33.96 ms | 35.2 ms | 71 | 14.24 ms | 120 blocking (9.0 ms mean) |
| **schedule on, run 1** | 1781 | 16.67 ms | **17.13 ms** | 17.4 ms | **0** | **5.73 ms** | 51 spread |
| **schedule on, run 2** | 1820 | 16.67 ms | **17.23 ms** | 17.6 ms | **0** | **5.87 ms** | 51 spread |

"Frame work" is the CPU time from the frame's start to its present. The blocking build was 9 ms of it, and 120 frames
in each run exceeded 8 ms of work. With the schedule on, 1-2 frames did.

The GPU's own p99 is the same in both arms (22.4-22.9 ms over the whole run). The slices did not move it. The
hitches the schedule-on runs still show all fall in the first 2 s, the start-up both arms share.

Interpretation:
- The background drawn from the frame differs from the cube's by the cube's resampling. A 128 px prefiltered face is
  about three screen pixels a texel at 192 px, so the cube softens the sun's rim and the horizon band.
- At 192x192 the difference is a mean of 0.29 levels and a maximum of 48 levels, on the rim.
- 1.8% of channels differ by more than 4 levels, all on the rim and the band. The live sky is the sharper one.
- The lighting a live build lands is the blocking build's to within 1 level.

## Consequences

- A live sky can be routed as hard as a performance wants. Its background is exact every frame, its lighting a
  fraction of a second behind, and no frame pays for the chain.
- Live and offline differ in two ways, both stated:
  - the sun's rim and the horizon band are sharper live;
  - a moving sky's lighting is up to about 0.6 s behind live.
- A `--render` of the same performance is the reference.
- `environmentAwaitingRebuild()` does not report a live build in flight. During a performance it would be on almost
  all the time, and the background, which is what reads as stale, is not stale.
