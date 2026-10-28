# Canyon: the analytic cirrus layer (ADR-1864)

The owner approved replacing the canyon's volumetric streak clouds with a cheap analytic layer. This was done on main
b22d4a91, after ADR-1807 moved the sky rays onto the camera basis; the layer reads its direction through that path and
never unprojects. The lattice, wire, contour and revelation effects are on HOLD and were not touched.

Media is in `~/Desktop/av-gen-review/101-strange-attractor/canyon-cirrus/`:
- `00-cirrus-audio-reactive-46s-1080p30-with-sound.mp4`: the Phase 2 audio project, unchanged apart from the sky.
- `01`–`06`: side by sides at the six checkpoints, the last of them the wide 40 s view. The left of each is the Phase 2
  clip with the volumetric lobes; everything else in the two frames is identical.
- `07-perf-and-flicker.txt`.

## What it is

A thin streak layer in the analytic sky. ADR-1864 has the details; in short:
- Each direction is projected onto a plane 4 km up.
- A four-octave value noise, stretched along the streak axis and warped once across it, sets where the streaks are.
- They are lit from the sun's side and brighten toward the sun.
- One function serves the sky cube, the visible sky and the CPU twin.

The visible sky is always drawn analytically when cirrus is present, because a cube face cannot hold a streak. The cube
carries only `reflection` (0.15 here) of the layer, so the mirror-still river reflects a faint sky rather than blotches.

The canyon's settings:

| Key | Value |
|---|---|
| amount | 0.9 |
| coverage | 0.5 |
| softness | 0.28 |
| altitude | 4000 m |
| length | 12,000 m |
| width | 1600 m |
| litColor | (1, 0.74, 0.78) |
| shadeColor | (0.36, 0.32, 0.62) |
| sunScatter | 1.2 |

ADR-1289's volumetric clouds are unchanged and still available.

## Numbers (1080p ultra, M2 Max, same build, back to back)

| | clouds | GPU total |
|---|---|---|
| volumetric lobes | 20.7 ms | 82.7 ms |
| analytic cirrus | 0 ms (the background pass reads 0.00) | 61.3 ms |

- The offline 46 s render at 2x supersample went from 556 to 435 s.
- Flicker on the new clip (coordinator's metric): median 0.004, p90 0.015, p99 0.045, max 0.16. The Phase 2 clip with
  the lobes measured median 0.007, p90 0.026, max 0.18. The volume march's per-pixel jitter was part of the frame
  noise.

## Verdict

At the checkpoints the streaks read as the 1c sky does: soft, pink-lit and running across the canyon. They are finer
and more numerous, with fibrous detail the lobes never had.

At 40 s the cirrus fills more of the sky than the lobes' three big bands. A lower `amount` or a higher `coverage`
thins it if the owner prefers the sparser sky.

At 22.5 s a faint cloud pattern is visible in the river. That is the cube's 15% share, and `reflection` 0 removes it.
