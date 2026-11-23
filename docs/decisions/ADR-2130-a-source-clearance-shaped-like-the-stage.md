# ADR-2130: a source clearance shaped like the stage

- Status: Accepted (2026-11-23), avagent/neon-dreams (Neon Dreams, iteration 20; owner msg 297: "the camera gets less
  distorted and looks less intense when we're nearer to the band -- keep the intensity as you get closer")
- Code: `world/effects/kinds/shockwave_effect.cpp` (rows `sourceEdge`, `sourceShape`; `shockwaveSourceEdge`,
  `shockwaveSourceColumn`; the shell proxy's `rim.w` and `noise.z`), `world/effects/world_pulse.{hpp,cpp}`
  (`PulseFrame::sourceEdge`, `sourceColumn`), `rendering/scene_renderer.cpp` (`pulseInfo.z`, `.w`),
  `shaders/common.wgsl` (`pulseSourceClear(v)`), `shaders/distortion.wgsl` (shape 6)
- Tests: `tests/unit/test_world_pulse.cpp` (`[adr2130]`)

## Context

ADR-2128's clearance is a ball: untouched within half of `sourceClear`, full from all of it. To keep a stage crisp
(deck, players, masts and halo up to ~10 m over the deck) the ball had to be 30 m, so nothing was full strength
within 30 m of the stage -- which is exactly where the crowd and the lens are when the film reaches the band. Neon
Dreams measured the wave's screen shift per front falling from ~6.5 px (lens 60-95 m out) to ~2 px (14-18 m).

## Decision

Two more Shockwave rows (world travel), both off by default:

- `sourceEdge` (m): the clearance's soft edge -- untouched within `clear - edge`, full from `clear` (0: the original
  half-clear edge). Clamped to `clear`.
- `sourceShape`: `sphere` (the original) or `column` -- the distance from the upright line through the centre, so a
  stage is clear to the top of its masts while the crowd round it, at the same distance, is full.

They ride with the clearance: the frame block's `pulseInfo.z` / `.w` (the largest clearance's instance's), and the
shell proxy's `rim.w` / `noise.z` (both unused by shape 6). `pulseSourceClear` takes the point less the centre.

## Consequences

- Neon Dreams: a column of 8.5 m with a 2 m edge keeps the 6 m deck clear and the front row of the crowd full.
- As ADR-2128: one clearance per frame for the pulse consumers.
