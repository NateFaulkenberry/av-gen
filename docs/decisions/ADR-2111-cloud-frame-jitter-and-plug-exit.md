# ADR-2111: the cloud march's frame jitter is a spread per frame, not per tick; a near fade and a variance clip

- Status: Accepted (2026-11-20), avagent/neon-dreams (Neon Dreams, iteration 9; owner review msg 73: "cloud pop/shadow
  issues in the fly-through ... not reading like" a musical effect)
- Code: `rendering::cloudFrameJitter` (`src/rendering/cloud_renderer.{hpp,cpp}`), `shaders/clouds.wgsl`,
  `shaders/clouds_resolve.wgsl`. Test: `tests/rendering/test_clouds_it9_gpu.cpp` (`[adr2111]`).

## Context

A per-frame luminance analysis of the iteration-8 render (70 s, 1080p30) showed a comb: the whole cloud field stepped
brighter in one frame and sank back over the next eight, every 0.30 s, through 7-14, 18-23 and 44-56 s (up to +5 % of
the frame's mean inside the Veil). No stem has that period.

ADR-2100 jittered the march by `fract(frameNonce x 0.618)`. `frameNonce` counts 1/240 s ticks, so at 30 fps it steps
by 8 a frame and the "golden-ratio" step per frame is `fract(8 x 0.618) = -0.056`: a slow ramp. ADR-2106's light taps
used one value per frame, `fract(2 x jitter + 0.37)`, a ramp of -0.11 a frame that wrapped every 8.97 frames. Every
cloud's self-shadow crept along with it and snapped back on the wrap; the temporal resolve (time constant ~6 frames)
followed the ramp instead of averaging it.

Leaving the Veil's plug (56.6 s), two more artefacts: the camera's last metres of cloud as a stipple (a sample a frame
at a metre's parallax, which no reprojection holds), and the cloud beyond the plug coming in as a stipple for ~0.2 s
because the rays had spent their 72 steps in the plug and ended a jittered step short of it or past it.

## Decision

- `cloudFrameJitter(nonce)` returns two jitters whose step per tick is chosen so the step per FRAME is a good
  low-discrepancy step at 24, 30 and 60 fps: x = (phi^-1 + 2)/8 a tick (0.618 a frame at 30 fps, 0.309 at 60, 0.273
  at 24); y = (0.5698 + 3)/8 (R2's second constant; 0.570, 0.785, 0.462). Still a pure function of the timeline.
- The light taps' offset is per pixel (a second, transposed interleaved-gradient pattern) plus the frame's y jitter.
  The ADR-2106 note that a per-pixel offset left a screen-door predates ADR-2108's tent resolve, which absorbs it.
- The cloud's density fades in over the first two minimum steps from the eye.
- The resolve clips the history to the neighbourhood's mean +- 1.25 sigma inside its min/max (variance clipping).
- Neon Dreams' offline preset marches 128 steps (was 72).

## Consequences

The comb is gone (frame-delta plot in the iteration-9 artefacts). Every v2 cloud scene's noise pattern changes
(hashes, not looks). Scenes with `evenSteps` (ADR-2109) keep their own march, and their light taps are the even ones.
