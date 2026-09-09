# Layered materials and the procedural sky (ADR-036)

Probes: `avgen_render_tests "[.perf][material]"` and `avgen_render_tests "[.perf][sky]"`
(Release build; `tests/rendering/test_material_gpu.cpp`, `tests/rendering/test_sky_gpu.cpp`).
Apple M2 Max (12 cores, 64 GB), macOS 26.6.2, headless.

## Material program cost at 1080p

102,400 instanced boxes on a 320 x 320 grid, seen from far enough that they cover the frame, one
directional light, no post chain. GPU time from the frame timer, mean of frames 20..59. The
interpreter runs **once per shaded fragment**, so this measures a frame that is essentially all
covered pixels: it is the worst case, not a typical scene.

Two runs back to back; the machine throttles as the run goes on, so each row is given as a range
and the conclusions are drawn from the ratios, which are stable, not the absolute times.

| Program | GPU ms/frame | Added over no program | Added, per op |
|---|---|---|---|
| none | 9.9 – 10.2 | – | – |
| 3 ops (input, gradient, ramp) | 16.3 – 20.2 | +6.4 – 10.1 | 2.1 – 3.4 ms |
| 16 ops (the pre-ADR-036 budget) | 31.2 – 38.2 | +21.0 – 28.1 | 1.3 – 1.8 ms |
| 48 ops layered (28-op base + 4 layers x 5 ops) | 67.1 – 93.1 | +57.2 – 83.0 | 1.2 – 1.7 ms |

Reading the numbers:

- **Cost is proportional to the ops a program actually enables**, not to the budget. Raising
  `kMaxMaterialOps` from 16 to 48 costs nothing by itself: the interpreter loop breaks at the
  program's own op count, and a 3-op program is as cheap as it was before. The 48-op figure is what
  a program that really spends the whole budget costs.
- **A 48-op layered program costs 57–83 ms per full frame of covered pixels at 1080p** — about
  2.9x the 16-op path, for 3x the ops, plus the layer compositing. Per op it is slightly *cheaper*
  than 16 ops, because the fixed cost of entering the interpreter is amortised over more work: the
  layers themselves are close to free, and what you pay for is the ops inside them.
- The ops in the 48-op program are deliberately expensive ones: eight `triplanar` (three fBM
  samples each), five `microDetail` and four `edgeWear` (one fBM each). A program of the same
  length built from arithmetic and ramps costs a fraction of this. **Noise is the cost, not the
  interpreter.**
- **A program that names no program still pays nothing.** The derivative-based geometric inputs,
  the tangent frame and the ambient-occlusion read the ADR-036 inputs need are all behind uniform
  branches on `materialSelect.program` and the object's texture mask, so the no-program row is
  where it was before ADR-036.
- What this means in practice: at 1080p, a full-screen surface running the whole budget is a
  12–15 fps proposition on this machine, so the budget is for *materials on objects*, not for a
  material on a fullscreen quad. The shipped library sits at 16–23 ops
  (`examples/materials/*.material.json`), i.e. roughly 20–35 ms of the same worst case, and far
  less in a real frame where the material covers a fraction of the screen. ADR-036 anticipated
  this ("48 ops needs masking by material and quality tier").

## Procedural sky build cost

The sky is rendered into the same cube / irradiance / prefiltered chain the HDR path uses:
6 faces x every mip of the source cube (analytic, with the sun disc widened per mip to conserve
its energy), then 6 irradiance faces and 6 x 6 prefiltered faces. Mean of 3 builds after a warm-up.

| Resolution | Build ms |
|---|---|
| 128 cube / 64 prefiltered | 7.8 |
| **256 cube / 128 prefiltered (the default)** | **10.3** |
| 512 cube / 256 prefiltered | 10.8 |

Reading the numbers:

- **10 ms, once.** `SceneRenderer::updateEnvironment` hashes the resolved sky (colours, haze, sun
  size, intensity, and the sun direction the key light gives it) and rebuilds only when that hash
  changes. A scene whose sky parameters are static pays this on the first frame and never again;
  a scene animating `env/sky/*` pays it on the frames that change, which is why the parameters are
  ordinary parameters and not per-frame uniforms.
- The chain is dominated by the fixed prefilter and irradiance passes (128 samples per prefiltered
  texel, 256 per irradiance texel), not by the sky evaluation, which is why 512 costs barely more
  than 256. If the build ever needs to be cheaper, cut `prefilterSamples`, not the cube size.
- For comparison, the equirect HDR path on the same settings costs the same 10 ms plus the upload
  and mip generation of the source map.

## What the sky buys

`tests/rendering/test_sky_gpu.cpp` measures the thing the ADR is actually about, on a 0.95-metallic
sphere with no environment map, at 192 x 192:

| | mean luminance | std dev across the surface | range |
|---|---|---|---|
| sky off (the pre-ADR-036 hemispheric constant) | 0.139 | 0.015 | 0.086 |
| sky on (`intensity` 2.0) | 0.543 | 0.178 | 0.788 |

Twelve times the variation across the surface, and nine times the range. The test asserts the
standard deviation grows by more than 3x and the range by more than 2x, with room for a GPU that
rounds differently: a metal that reflects a gradient sky with a sun in it stops reading as one
flat tone, which is the whole point of ADR-036's first half.
