# Post-processing artifact forensics: the Glowmere water lattice

The symptom: with `post/anamorphic/enabled` on over Glowmere's sparkling river, the frame grows a
lattice of dots and banded streaks that follow the water but do not look like water.

Three fixes were shipped and reverted before this investigation — a luminance-weighted (Karis) bloom
prefilter, raising the streak's tap count from 8 to 48 a side, and selecting a coarser pyramid level
for the wide tier plus a 3×3 tent on the ghosts. All three were plausible. None was localised, which
is why one of them appeared to do nothing, one changed the look, and none of them settled the
question. This document is the localisation those changes should have had.

The rule that made the difference: **a post chain judged on its final frame cannot be debugged.**
Bloom, the wide tier and the grade all land on the same pixels, so a change anywhere upstream moves
the final image and every change therefore "does something". The only evidence that separates the
stages is the stages themselves.

## 1. The pipeline map

Measured, not inferred: every resolution below is read back from the target the chain actually
rendered (`PostProcessor::armCapture`), not computed from the code. Frame width *W*, height *H*.

| # | Stage | Entry | Input | Output | Resolution | Format | Sampling | Spatial support |
|---|---|---|---|---|---|---|---|---|
| 1 | exposure | `fs_exposure` | scene HDR | exposed | W×H | RGBA16F | point (1 tap) | the pixel |
| 2 | defocus | `fs_dof` | exposed + depth | defocused | W×H | RGBA16F | bilinear gather | the circle of confusion |
| 3 | motion blur | `fs_motion_blur` | + velocity | blurred | W×H | RGBA16F | bilinear along velocity | the tile's max velocity |
| 4 | lens | `fs_lens` | previous | distorted | W×H | RGBA16F | bilinear, 3 taps | a few texels |
| 5a | bloom prefilter | `fs_prefilter` | previous (+ emission) | `down[0]` | W/2×H/2 | RGBA16F | 4-tap box, `texelSize` = **1/frame** | 1 frame texel |
| 5b | bloom downsample ×5 | `fs_downsample` | `down[n-1]` | `down[n]` | W/2ⁿ⁺¹ | RGBA16F | 13-tap Jimenez, `texelSize` = **1/source** | ±2 source texels |
| 5c | bloom upsample ×5 | `fs_upsample` | `down[n+1]`, `down[n]` | `up[n]` | = `down[n]` | RGBA16F | 9-tap tent × `radius`, `texelSize` = **1/coarse** | ±1.15 coarse texels |
| 5d | halation pyramid | as 5a–c | previous | `halation` | starts W/4 | RGBA16F | as above | as above |
| 6 | **wide tier** | `fs_wide` | `bloom` (= `up[0]`, **W/2**) + `halation` | `wide` | **W/4×H/4** | RGBA16F | 17-tap Gaussian + 2 point taps, `texelSize` = **1/output** | see below |
| 7 | composite | `fs_composite` | previous + `bloom` + `wide` | graded | W×H | RGBA16F | point at W×H, **bilinear ×2 from `bloom`, ×4 from `wide`** | magnification |
| 8 | fxaa | `fs_fxaa` | previous | — | W×H | RGBA16F | edge search | ±12 texels |
| 9 | tone map | `tonemap.wgsl` | previous | display | W×H | swapchain | point | the pixel |

Sampler throughout: one `linearSampler`, `ClampToEdge`, `Linear` min and mag, no mips (every target
is single-level). Colour space: scene-linear HDR from stage 1 to stage 9; the tone map is the only
transfer. There is **no temporal history anywhere in the post chain** — no jitter, no motion vectors
feeding back, no previous-frame texture — except auto-exposure, which reads back a 1×1 luminance
from the previous frame. `resetExposure()` clears it, and the forensics harness does so per arm.
So a single frame is a sufficient reproduction, and this was checked rather than assumed.

### The two facts the map contradicts

- The shader comment on `fs_wide` says its source is "a coarse bloom level". It is not: `textures.source = bloom`,
  the **result** of `buildPyramid`, which is a fully upsampled pyramid at the *finest* level's
  resolution — W/2, the highest resolution in the pyramid.
- `post.texelSize` inside `fs_wide` is **1/(W/4)**, the wide target's own texel, while the texture it
  samples is W/2. One number expressed in one resolution and applied to another.

## 2. Where the artifact first appears

An impulse — one texel at 40.0 in an otherwise black frame — through the production chain with
Glowmere's authored settings (`stretch` 10.386, `ghosts` 0.223, `intensity` 0.47, bloom intensity
0.18, threshold 1.0, radius 1.15). Periodicity is the autocorrelation of the mean-removed column
profile at its fundamental lag; a value away from lag 0 means the output contains **shifted copies**
of its input, which no blur can produce.

```
  scene-hdr        512x288  peak 40.0000  lag  0  score 0.000  isolated peaks  1
  bloom/prefilter  256x144  peak  9.0000  lag  0  score 0.000  isolated peaks  1
  bloom/down1      128x72   peak  0.5625  lag  2  score 0.098
  bloom/down2       64x36   peak  0.1170  lag  2  score 0.134
  bloom/down3       32x18   peak  0.0267  lag  2  score 0.105
  bloom/down4       16x9    peak  0.0072  lag  0  score 0.000
  bloom/down5        8x4    peak  0.0015  lag  0  score 0.000
  bloom/up4         16x9    peak  0.0035  lag  2  score 0.274
  bloom/up3         32x18   peak  0.0125  lag  2  score 0.405
  bloom/up2         64x36   peak  0.0537  lag  2  score 0.439
  bloom/up1        128x72   peak  0.2551  lag  2  score 0.416
  bloom/up0        256x144  peak  3.8926  lag  2  score 0.203   <- the wide tier's input
  wide             128x72   peak  0.1382  lag 11  score 0.253   <- first periodic structure
  composite        512x288  peak 40.4085  lag  2  score 0.035
```

Lag 2 at these sizes is the blob's own width — the autocorrelation of a single smooth bump. **Lag 11
at the wide tier is not.** The first stage at which the pattern is measurable is stage 6,
`fs_wide`, and nothing before it carries periodic structure at all.

## 3. The mechanism, as arithmetic rather than as a story

`fs_wide` steps its seventeen taps by `post.texelSize.x * stretch`. If the comb's period *is* the
tap step, the period in wide texels must equal `stretch` exactly and track it linearly. Nothing else
in the chain has that signature: a blur's width scales with stretch but a blur has no period, and an
aliasing artifact of the *source* would keep the source's spacing while stretch moved.

```
  stretch  4.000 -> wide lag  4   (predicted  4)  score 0.843
  stretch  6.000 -> wide lag  6   (predicted  6)  score 0.814
  stretch  8.000 -> wide lag  8   (predicted  8)  score 0.803
  stretch 10.386 -> wide lag 10   (predicted 10)  score 0.394
  stretch 14.000 -> wide lag 14   (predicted 14)  score 0.777
  stretch 20.000 -> wide lag 20   (predicted 20)  score 0.722
```

Exact, over a fivefold range. At Glowmere's authored `stretch` the pass samples a Gaussian of
σ = 3.2 × 10.386 ≈ 33 output texels **every 10.4 texels**, and reaches ±83 texels with 17 samples.
That is undersampled by about twenty. The filter is not a Gaussian; it is a comb whose teeth are
41.5 full-resolution pixels apart at any frame size, because the step is proportional to the
quarter-resolution texel.

Two further measurements bound what the mechanism is *not*:

- **A compact bright rectangle acquires no comb.** Same chain, same settings, an input with no
  isolated energy in it: every stage stays at lag 2. The pass only misbehaves where the input's
  energy sits in isolated texels — which is exactly what the water writes to the emission target.
- **With the ghosts off entirely the comb is still there** (score 0.516). Whatever the two mirrored
  taps do, they are not what turns an impulse into a row. They add structure of their own on top,
  from their own defect: a single unfiltered bilinear tap minifying the half-resolution `bloom` into
  the quarter-resolution `wide` by a further 0.75× and 0.40×, i.e. an effective 2.7× and 5×
  minification with no prefilter.

## 4. Reproducing it

Two independent harnesses, both in `tests/rendering/test_post_artifact_forensics_gpu.cpp`
(`avgen_render_tests "[waterfx]"`, always under `tools/gpu-lock.sh`).

**Synthetic.** `PostBench` drives `PostProcessor` directly with a CPU-written RGBA16Float texture —
no scene, no camera, no clock, no noise. This is the only arrangement in which "the post pipeline
creates the pattern" is falsifiable, because the input provably has no pattern in it.

**The water matrix.** `WaterBench` drives `examples/qa/renderer-qa-water.scene.json` through
`app::Engine` with the camera forced free and placed, the clock held at t = 20 s and the frame index
pinned, and exposure reset per arm. Every configuration is set through the parameters the Glowmere
UI already writes — `post/bloom/enabled`, `post/anamorphic/enabled`, `post/anamorphic/ghosts`,
`post/anamorphic/stretch`, `nodes/<node>/water/sparkle` — and no diagnostic toggle of its own.

The one stage that cannot be isolated through existing controls is the **ghost half of `fs_wide`**:
the ghosts are added to the streak inside a single pass, so "ghosts without streaks" is not a state
the parameters can express. It does not need a bypass — `post/anamorphic/ghosts` at 0 and at 0.223
differ in exactly one number, and the difference of the two `wide` targets *is* the ghost
contribution, measured rather than approximated.

**The band pass is part of the reproduction.** `water.wgsl`'s `sparkleBandFade` only draws a sparkle
cell between about five and twenty-eight pixels across, so whether the sparkle exists at all is a
property of the camera distance and the frame's resolution together. At 960×540 the QA river
sparkles between roughly four and eight metres and nowhere else — from its authored bank view,
thirty-six metres out, there is no sparkle in the frame at all. The `[.probe]` test measures this
rather than assuming it.

## 5. The detector

Three measurements, none of which compares a render against a second render of the same code path:

- **Out-of-region energy** against a *measured* mask. The mask is not "where is the water" but
  "where did the sparkle put light", derived from the one difference that isolates it — the same
  frame with `water/sparkle` at zero — then dilated a few pixels so that "outside" means genuinely
  away from it. Post is off in both arms, so the mask is the shader's own output.
- **Periodicity**, the autocorrelation above. Reported at the *fundamental*: the smallest lag that
  is a local maximum within half the best score. Reporting the argmax instead had an impulse's comb
  come back as its own third harmonic, which reads as a spacing the chain does not have.
- **Isolated peaks**, a count of pixels far brighter than the ring around them. A blur destroys
  isolation and a resample preserves it, so the interesting number is how the count moves from one
  stage to the next.
