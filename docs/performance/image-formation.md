# Image formation performance (ADR-037, ADR-039)

Probe: `avgen_render_tests "[.perf][post]"` (Release build;
`tests/rendering/test_image_formation_gpu.cpp`). Apple M2 Max, macOS 26.6.2, headless, GPU time
from the frame timer (the whole frame: scene pass, post chain, tone map), mean of frames 15..44 of
45. Scene: one small very bright emissive cube on black — trivial geometry, so effectively all of
the difference between rows is the post chain. Two runs; both are listed because the numbers move
with GPU clock state, and the depth-of-field row moves the most.

`post passes` is `RenderStats::post.passes`: the number of fullscreen passes the chain encoded.

## 1920x1080

| Configuration | post passes | GPU ms/frame (run 1 / run 2) |
|---|---|---|
| chain off (exposure 1, no bloom) | 1 | 0.53 / 0.45 |
| bloom | 12 | 0.80 / 0.77 |
| bloom + automatic exposure | 19 | 1.03 / 1.10 |
| bloom + halation | 24 | 1.24 / 1.14 |
| bloom + halation + anamorphic | 24 | 1.27 / 1.22 |
| bloom + depth of field (physical CoC) | 13 | 1.13 / 2.13 |
| everything (bloom, halation, anamorphic, DOF, auto exposure) | 32 | 2.04 / 3.52 |

## 3840x2160

| Configuration | post passes | GPU ms/frame (run 1 / run 2) |
|---|---|---|
| chain off | 1 | 1.51 / 1.55 |
| bloom | 12 | 1.89 / 1.98 |
| bloom + automatic exposure | 19 | 2.13 / 2.70 |
| bloom + halation | 24 | 2.44 / 2.40 |
| bloom + halation + anamorphic | 24 | 2.07 / 2.55 |
| bloom + depth of field (physical CoC) | 13 | 3.45 / 6.41 |
| everything | 32 | 3.91 / 6.67 |

## Reading the numbers

- **Bloom is cheap**: 0.3 ms at 1080p, 0.4 ms at 4K, for eleven passes. Only the prefilter runs at
  full resolution; the pyramid is 1/2, 1/4, ... so the whole chain costs about a third of one
  full-resolution pass. The energy-conserving upsample (`mix` instead of `+`) costs nothing extra.
- **Halation** adds five more passes for about 0.4 ms at 1080p. Its pyramid starts at quarter
  resolution, so it is cheaper than bloom's despite having the same level count.
- **Anamorphic** is nearly free once halation's `fs_wide` pass exists (both tiers share it): the
  17-tap horizontal gaussian runs at quarter resolution.
- **Depth of field is the expensive stage** and the one that scales worst: 25 taps per pixel at full
  resolution, each of which reconstructs a world position from depth to evaluate its own circle of
  confusion. At 4K it costs 1.9-4.9 ms on its own. It is also the row that varies most between runs
  because it is the one that actually saturates the GPU. Halving the gather resolution is the
  obvious next step if a showcase needs it.
- **Automatic exposure** adds six small reduction passes (0.2-0.6 ms, most of it fixed overhead
  rather than bandwidth) *plus a CPU stall that this table does not show*: the chain maps the 1x1
  metering buffer synchronously at the start of the next frame so the feedback loop is
  deterministic. On this machine the map resolves immediately because the previous frame's work has
  long since completed, but a frame that is already GPU-bound would pay for it on the CPU. Manual
  exposure encodes no metering passes at all.
- A **unit exposure scale skips its own pass entirely** (`|scale - 1| < 1e-3`), which is why the
  "chain off" row is one pass: the composite, which always runs because it also applies the grade.

## Comparison with the pre-ADR-039 chain

The old chain at 1280x720 was 13 passes with everything on. The new one is 32 at its fullest, but
the two default configurations (bloom only, or bloom plus manual exposure) are 12 passes — one more
than before, because the lens distortion and chromatic aberration moved out of the composite into
their own pass and only run when they are non-zero.
