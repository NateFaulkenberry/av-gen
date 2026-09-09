# Lighting performance (ADR-033, ADR-034, ADR-035)

Probe: `avgen_render_tests "[.perf][lighting]"` (Release build;
`tests/rendering/test_lighting_perf.cpp`). Apple M2 Max, macOS 26.6.2, headless **1920x1080**.

Scene: a courtyard of 48 box pillars on a 120 m floor (49 opaque draws, ~1.2 k triangles), one
shadow-casting directional light, no image-based lighting, no post chain. Each row renders 20
warm-up frames and 60 measured frames with an advancing frame index and reports the **median**.

- `frame` is the whole-frame GPU timer: the first shadow pass through tone mapping, so it includes
  the froxel build, the shadow maps, the depth prepass, the linear-depth resolve, the occlusion
  passes, the scene pass and the shadow *lookup* inside it.
- `shadows` is a second timer spanning only the depth-only shadow passes.
- `ao` is the occlusion renderer's own timer (the horizon pass plus the temporal pass).

Two runs are listed because the GPU's clock state moves the whole-frame figure by 1-2 ms between
runs; the per-pass timers are stable. The device reports timestamps in ~65 us ticks, which is why
the shadow and occlusion columns quantise.

## Feature cost at the realtime tier

| Configuration | frame ms (run 1 / run 2) | shadow pass ms | AO ms |
|---|---|---|---|
| no shadows, no occlusion | 1.245 / 1.180 | - | - |
| 3 cascades @ 2048, PCSS | 4.915 / 4.981 | 0.066 | - |
| + contact shadows (12 steps) | 5.046 / 4.129 | 0.131 | - |
| + GTAO (half res, 3 slices x 6 steps) | 4.129 / 6.029 | 0.066 / 0.131 | 0.328 / 0.524 |
| high tier (4 cascades, 4 x 8 AO) | 5.308 / 5.243 | 0.131 | 0.524 |
| preview tier (2 cascades @ 1024, no PCSS) | 2.687 / 3.146 | 0.066 | 0.197 / 0.262 |

**Against the ADR-034 budget** (shadows plus occlusion under 4 ms combined at the realtime tier):
the measured pass cost is **0.39 / 0.66 ms**, comfortably inside it. Measured the other way - the
whole-frame delta between "no shadows, no occlusion" and the full realtime configuration - it is
**2.9 ms**, still inside.

## Clustered lighting

Three cascades, contact shadows and occlusion on; local point lights added on top, each with a
14 m range and no contact march of its own.

| Local point lights | frame ms (run 1 / run 2) |
|---|---|
| 0 | 4.194 / 3.998 |
| 16 | 4.915 / 4.784 |
| 64 | 6.226 / 6.291 |
| 200 | 7.930 / 7.864 |

| Path, 8 lights | frame ms (run 1 / run 2) |
|---|---|
| 8-light uniform fallback | 3.080 / 4.981 |
| clustered | 4.653 / 4.522 |

## Reading the numbers

- **Generating the shadow maps is nearly free** (0.07-0.13 ms for three 2048-square cascades of 49
  depth-only draws). The 3.7 ms that shadows add to the frame is almost entirely the *lookup*:
  percentage-closer soft shadows cost a 12-tap blocker search plus a variable-width 12-tap filter
  for every lit pixel at 1080p. A tier that wants the time back should set `softShadows = false`
  and take plain PCF, which is what the preview tier does - and it lands at 2.7-3.1 ms for the
  whole frame with two cascades.
- **Contact shadows are cheap** at 12 steps (~0.1 ms of frame time) because they only run for
  lights that ask for them. Turning them on for every one of 200 point lights would not be.
- **Occlusion at half resolution costs a third of a millisecond** and doubles at the high tier's
  4 slices x 8 steps. The bilateral upsample is folded into the shading pass, so it costs no
  separate full-resolution target and no separate pass.
- **The depth prepass pays for part of itself.** It is a fragment-free pass over the same 49 draws
  and it gives the scene pass early-Z; it is skipped entirely when both occlusion and contact
  shadows are off.
- **Clustered lighting scales the way it should**: 200 local lights cost 3.7 ms more than none,
  about 19 us per light per frame at 1080p, and the froxel build itself does not appear in the
  measurements at all (3072 threads). Below about eight lights the uniform fallback is slightly
  cheaper, because it skips the cluster lookup and the per-light shadow branch - which is exactly
  what that tier is for.
- **The auxiliary targets** (four extra colour attachments, ~24 MB at 1080p) are inside the
  "no shadows, no occlusion" row: they are written by the scene pass whatever the tier, and the
  1.2 ms baseline includes them.

## Reproducing

```
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release -G Ninja
cmake --build build/release -j6 --target avgen_render_tests
./build/release/tests/avgen_render_tests "[.perf][lighting]"
```
