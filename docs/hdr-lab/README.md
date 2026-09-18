# HDR / Exposure / Bloom Lab

> Why is this frame this bright, and where did the glow come from?

Lab #8 of the Engineering Lab Suite. Registered in `src/labs/lab.cpp`; opens on
`examples/labs/hdr-lab.scene.json`; cases in `examples/labs/hdr/cases.json`; tests in
`tests/rendering/test_hdr_lab_gpu.cpp`.

**The map is the deliverable.** §1 is every stage between "a shaded pixel exists in linear space"
and "a byte reaches the file", named by file and symbol, with **what is and is not inspectable at
each step** — because the Volumetric Lab builds on it, because §18 of the specification turns on
intermediate inspectability, and because the lab that ran before this one ended on the sentence this
one exists to make measurable: *any lighting measurement read through the tone curve is a
measurement of the tone curve* (`docs/lighting-lab/README.md` §2.1).

§2 is that sentence as a number.

One boundary, stated before the map: **the Lighting Lab owns the radiance that arrives; this lab
owns what the chain does with it.** That boundary is drawn in the fixture rather than argued about
— every surface in `hdr-lab.scene.json` is *unlit*, so its radiance is the number in the file and a
reading here is never a joint measurement of a BRDF and a tone curve. The next boundary along is the
Volumetric Lab's: the volumetric composite writes into the HDR target *before* stage 4 below, so
in-scatter is bloom's input and not bloom's business.

---

## 1. The chain, stage by stage

Frame width *W*, height *H*. Two sizes matter and they are not the same number: the **output** size
(`--size`, what the file is) and the **scene target** size, which `SceneRenderer::resize`
(`src/rendering/scene_renderer.cpp`) sets to `output × QualitySettings::renderScale`. The whole post
chain runs at the *scene* size. `--supersample 2` makes them differ by a factor of two, and §5.2 is
what that costs.

| # | Stage | Where | Writes | Size | Inspectable today? |
|---|---|---|---|---|---|
| 0 | scene shading | `SceneRenderer::render`, `shaders/pbr_shade.wgsl` | HDR + 4 aux targets | scene | **yes** — `--aov normal,emission,depth,velocity,id`, `--debug-target`, and `renderToImageFloat` |
| 1 | volumetric composite | `VolumeRenderer::encode` | into the **same** HDR target | scene × `volumeResolutionScale` | partly — `--disable volume` is an arm; the march's own target is not exported. **Volumetric Lab.** |
| 2 | debug geometry | `SceneRenderer::render`, the `debug-pass` block | into the **same** HDR target | scene | n/a — and see §4.2: this is *upstream of the bright pass*, so an overlay above the bloom threshold blooms |
| 3 | user shader layers | `shaders::LayerStage::Post`, `shader_layer.cpp` | ping-pong `post_[0/1]` | scene | no separate export |
| 4 | metering | `PostProcessor::encodeMetering`, `fs_meter_prefilter` + `fs_meter_reduce` | a 1×1 texel copied to a readback buffer | W/4 → 1 | **number only** — `PostStats::meteredLuminance`, and now `stages.json`'s `exposure` block. The reduction pyramid is not captured. |
| 5 | exposure | `fs_exposure` | `exposure` | scene | **yes** — `--post-stages`, *when it runs*: the pass is skipped when the scale is within 0.1% of 1, and then the stage is simply absent rather than reported as identity |
| 6 | defocus (DoF + tilt-shift) | `fs_dof` | `dof` | scene | **yes** — `--post-stages` |
| 7 | motion blur | `fs_velocity_tile_max`, `fs_velocity_neighbour_max`, `fs_motion_blur` | `motionblur` | scene (tiles at scene/tileSize) | **the result yes**, the two velocity tile targets **no** |
| 8 | lens distortion + chromatic aberration | `fs_lens` | `lens` | scene | **yes** |
| 9a | bright pass | `fs_prefilter` | `bloom/prefilter` | W/2 | **yes** |
| 9b | bloom downsample ×(levels−1) | `fs_downsample` | `bloom/down1..5` | W/2ⁿ⁺¹ | **yes** |
| 9c | bloom upsample | `fs_upsample` | `bloom/up4..0` | = the matching down level | **yes** |
| 10 | halation pyramid | `fs_halation_prefilter`, then 9b/9c | `halation/prefilter`, `halation/downN`, `halation/upN` | starts W/4 | **yes** |
| 11 | wide tier (halation tint + anamorphic streak + ghosts) | `fs_wide` | `wide` | W/4 | **yes**, as one target. The ghosts cannot be separated from the streak inside it — see §4.3 |
| 12 | composite (bloom + wide + grade + depth layers) | `fs_composite` | `composite` | scene | **yes** |
| 13 | FXAA | `fs_fxaa` | `fxaa` | scene | **yes**, and `--disable fxaa` is its arm (ADR-187) |
| 14 | sharpen | `fs_sharpen` | `sharpen` | scene | **yes** |
| 15 | **tone map**, vignette, grain, sRGB encode | `shaders/tonemap.wgsl` `fs_main` | the LDR target | **output** | **only as the finished frame.** There is no capture between the operator and the encode. |
| 16 | composition overlay (ADR-083) | `SceneRenderer::render` → `overlay_->encodeOverlay` | the LDR target | output | display-referred, and absent from an EXR render |
| 17 | readback and write | `RenderJob::renderOne` → `handleFrame` | PNG (LDR) or EXR (the **pre-tonemap** HDR, resolved by `resolveToOutput`) | output | the file |

Sampler throughout the post chain: one `linearSampler`, `ClampToEdge`, linear min and mag, no mips.
Colour space: scene-linear from stage 0 to stage 14; the tone map is the only transfer.

**There is no temporal history in the post chain** except auto-exposure, which reads back a 1×1
luminance from the previous frame. So one frame is a sufficient reproduction for everything here,
which is what makes the synthetic arm of §3 legitimate.

### 1.1 The exposure scale, and the second one nobody expects

Two multiplications happen before the operator and they come from different places.

* `fs_exposure`'s scale is `scene::updateExposure` (`src/scene/camera.cpp`), GPU-free and
  deterministic: manual is the photographic triangle, automatic walks toward
  `referenceEv100 + log2(L / 0.18)` at `speedUp`/`speedDown` EV per second. **The defaults are a
  no-op** — `referenceEv100` is exactly EV100(f/5.6, 1/50 s, ISO 400) — which is why every number
  in §2 was taken at scale 1.0 without asking for it.
* `TonemapUniforms::exposure` is **`scene.environment.brightness`**
  (`scene_renderer.cpp`, the `TonemapUniforms` block), a second linear scale applied inside
  `tonemap.wgsl` after the whole post chain. It is not part of `ExposureState`, it is not metered,
  and it is *after* the bloom threshold — so raising `environment/brightness` brightens the frame
  without moving what blooms, and raising `camera/exposure/compensation` moves both. Anyone
  measuring "the exposure" has to say which one.

### 1.2 The metering reduction, measured

`fs_meter_prefilter` renders to a quarter-resolution target and takes four bilinear taps one
*full-resolution* texel apart; `fs_meter_reduce` takes sixteen at the source's own texel centres.
Whether those four taps between them cover all sixteen texels of the block decides whether a small
bright source is metered at all, and the specification names a small bright source as a fixture.

They do. Measured (`test_hdr_lab_gpu.cpp`, `[.probe]`): one texel at 4096 in a 64×64 frame, at each
of the sixteen positions inside a 4×4 block, with `meterCenterWeight` 0 so position must not matter
— **1.000000 at every one of the sixteen**, which is the frame's exact arithmetic mean
(4096 / 4096). The four taps land on texel *boundaries*, so each is the average of two texels and
the four together are the unweighted mean of all sixteen. The reduction is exact.

That holds when the dimensions divide. `fs_meter_reduce` steps `nw = ceil(w/4)`, so at a frame
whose chain of quarterings is not exact (1920×1080 goes 480×270 → 120×68 → 30×17 → 8×5 → 2×2 → 1×1)
the taps drift off the source's texel centres and the edge is over-weighted. Not quantified here;
it bounds how precisely a metered luminance can be trusted and is the next thing to measure.

### 1.3 What `--disable post` actually disables

`SceneRenderer::passArms()` has exactly two arms in this territory: **`post`** (the whole chain,
`PostProcessor::run` is not called) and **`fxaa`** (ADR-187, stage 13 only). There is no arm for
exposure, for the bright pass, for the pyramid or for the tone map. Individual stages are switched
through the `post/*` parameters, which is the right thing — they are what the application writes —
but it means an *arm* and a *setting* are not interchangeable here, and `labs::LabCase` can express
only the former (§6).

`--disable post` does **not** disable stage 15. The tone map belongs to the renderer, not to the
post chain, so a frame rendered with post off is still AgX-encoded. The only way to read a
scene-linear value out of this engine is `renderToImageFloat`, an EXR render, or
`post/tonemap/operator` = 4 (clamp).

---

## 2. The tone curve, measured on both sides of one frame

The lab's central instrument, and the reason the fixture is what it is. The same frame is read back
twice — `renderToImageFloat` (the scene-linear HDR the tonemap pass was handed) and `renderToImage`
(the bytes it wrote) — so the operator is the *difference between two readbacks* rather than a CPU
model of AgX sitting beside it. ADR-182: an instrument that cannot disagree with the thing it
measures has measured nothing.

AgX, the default, `chromaRetention` 0, exposure scale 1, **bloom off**:

| authored radiance | into the tonemap | display byte | Δ from the row above |
|---|---|---|---|
| 0.18 | 0.1799 | 128 | — |
| 0.5 | 0.5000 | 175 | +47 |
| 1.0 | 1.0000 | 204 | +29 |
| 2.0 | 2.0000 | 226 | +22 |
| 4.0 | 4.0000 | 241 | +15 |
| 16.0 | 16.0000 | 255 | +14 |
| 50.0 | 50.0000 | 255 | **0** |

Read it as three regions. Below scene white a stop is worth twenty to fifty display levels. Between
1 and 4 a stop is worth fifteen to twenty-two. **Above about sixteen a factor of three is worth
nothing at all** — 16 and 50 are the same byte.

That last row is the Lighting Lab's boundary as a number, and it is why its GPU reach probe disables
post: a 0.76 step in scene-linear radiance became 0.08 of display luminance there, and the table
above says where on the curve that happens. `test_hdr_lab_gpu.cpp`'s *"where AgX stops being able to
tell two radiances apart"* asserts both ends, with the scene-linear side as the control — so a
failure is the curve and not the fixture, the exposure or the composite.

With the chain's **default** bloom on (enabled, intensity 0.2, threshold 1.0) the same patches
arrive at the tonemap lifted: 0.18 → 0.1816, 1.0 → 1.0576, 16.0 → 18.9531. Bloom is not a separate
layer over the picture; it is added to the picture before the curve, and a measurement of "the
curve" taken through the shipped defaults is a measurement of the curve *and* the pyramid.

### 2.1 Hue, and why it is not a footnote

Per-channel, the same frame, AgX:

| patch | authored | display |
|---|---|---|
| neutral | (2, 2, 2) | (226, 227, 227) |
| red | (2, 0, 0) | (243, 86, 86) |
| green | (0, 2, 0) | (115, 236, 115) |
| blue | (0, 0, 2) | (114, 114, 234) |
| cyan | (0, 2, 2) | (143, 228, 228) |
| violet | (1, 0, 2) | (207, 120, 230) |
| blue | (0, 0, 8) | (178, 178, 255) |

A pure blue at radiance 8 arrives on screen as **(178, 178, 255)** — a pale blue-white. That is AgX's
inset matrix doing what it is designed to do, and it is precisely what `post/output/chromaRetention`
(Bioluminescence Phase A) exists to claw back. The measurement belongs here because the *bright
pass* disagrees with it, which is §5.1.

---

## 3. The instruments

### 3.1 `--post-stages <dir>` (new, ADR-277)

`PostProcessor::armCapture` has existed since the water-lattice investigation
(`docs/post-artifact-forensics.md`) and **nothing outside a GPU test could reach it**, which made
"every intermediate stage should be inspectable" true of the class and false of the program. This
is the consumer:

```
avgen --headless --composition examples/labs/hdr-lab.scene.json \
      --size 1280x720 --range 0.5:0.5 --render out --post-stages stages
```

writes one scene-linear EXR per stage at the resolution the chain chose, named by the chain's own
label (`frame_000000.bloom-down3.exr`), plus `frame_000000.stages.json` carrying each stage's
extent, peak and mean luminance and the frame's exposure scale, EV and metered luminance. The
manifest alone answers questions that used to need a test — this is the shipped default chain over
the fixture's two bright sources:

```
  bloom/prefilter     640x360  peak=48.96875 mean=3.560672
  bloom/down1         320x180  peak=48.96875 mean=3.560634
  ...
  bloom/down5          20x11   peak=39.75000 mean=3.528257
  bloom/up4            40x22   peak=40.34375 mean=3.533011
  ...
  bloom/up0           640x360  peak=48.28125 mean=3.556233
  composite          1280x720  peak=59.62500 mean=4.783194
```

The pyramid's mean goes 3.5607 → 3.5562, a loss of 0.12% over eleven passes: ADR-039's
energy-conserving upsample, measured rather than asserted, from a command line.

**Arming changes nothing about the picture.** The only difference is that the pyramid and wide
targets are allocated with `CopySrc`, which nothing samples. Verified the way this repository
verifies that sort of claim — the same render twice, with and without the flag:
`sequence hash 7a3636ff855ad06c` both times.

It is a diagnostic and it says so on stderr: the readback is a synchronous map per stage per frame,
so a sequence rendered with it on is not a sequence whose timings mean anything. Use `--range t:t`.

### 3.2 The two test harnesses

* **`testsupport::PostBench`** (`tests/support/post_bench.hpp`) — `PostProcessor::run` with no
  scene, no camera and no clock, over an RGBA16Float texture the CPU wrote. The only arrangement in
  which "the post chain produced this pattern" is falsifiable. It was the water investigation's and
  now lives in `tests/support/`; the forensics file uses the same copy.
* **`HdrBench`** (`test_hdr_lab_gpu.cpp`) — the whole renderer over the fixture, read back on both
  sides of the tone curve.

### 3.3 What this lab deliberately does not draw

`labs::overlaysFor(LabId::Hdr)` returns an empty `DebugViewOptions`, and stage 2 of the map is why.
Debug geometry is drawn **into the HDR target, before the post chain**. An overlay line brighter
than `post/bloom/threshold` therefore goes through the bright pass, into the pyramid, and back out
over the picture. The Rendering Lab draws nothing because an overlay in a frame it grades becomes an
artifact it reports; this lab draws nothing for the sharper version of the same reason — its overlay
would be *inside* the measurement.

---

## 4. The fixture

`examples/labs/hdr-lab.scene.json`. Two pages, a hundred metres apart, selected by a case's camera.

**Page 1 — the calibrated card.** Sixteen patches in a 4 × 4 grid, every one **unlit**
(`material.unlit`, which takes `pbr_shade.wgsl`'s `object.flags.z` branch: `baseColor + emissive`,
fogged, and the fog is off). No lights matter, no sky, no ambient, no BRDF. A patch's scene-linear
radiance is the number in the file:

| row | patches |
|---|---|
| 0 | 0.0, 0.18, 0.5, 1.0 — black, mid grey, and up to scene white |
| 1 | 2.0, 4.0, 16.0, **50.0** — above scene white to the extreme |
| 2 | red 2, green 2, blue 2, **neutral 2** — one channel at a time, with the control beside them |
| 3 | cyan 2, violet 2, blue 8, **neutral 8** — the two hues Glowmere is made of, and a second control pair |

The camera is 20 m back at a 40° vertical field of view, so the visible plane at z = 0 is
25.8824 × 14.5589 m and each patch is exactly one sixteenth of the frame. **A case samples cell
(col, row) at ((col+0.5)/4, (row+0.5)/4) and needs no projection of its own** — which is the whole
reason for the awkward-looking camera distance.

**Page 2 — the two bright sources.** A 0.3 m emitter and a 4.0 m emitter, both at radiance 64, on
the same black ground: 15 px and 198 px of a 720-line frame. One is smaller than a single texel of
bloom level 4; the other spans thirty of them.

### 4.1 The fixture's own control

`test_hdr_lab_gpu.cpp`, *"the HDR fixture's patches are the radiances it authors"*: post disabled,
every patch read out of the HDR target and compared to the file. Fifteen of sixteen exact, 0.18
reading 0.17993 because that is what a half carries.

The sixteenth is a finding. **A scene file cannot author an emissive above 50.** The card was
written with 256; it renders at exactly 50.0, because `material/emissive` is registered with a hard
maximum of 50 (`src/scene/procedural.cpp`, `registerProceduralParameters`) and the parameter clamps
the authored value with no warning. `baseColor` and `emissiveColor` are clamped to [0, 1] in the
same way, so `emissiveIntensity` is the *only* route above unit radiance from a scene file and 50 is
its ceiling. It is ADR-225's defect in an authoring format — the same shape as the Lighting Lab's
`coneDegrees`, in a different parser — and it is **reported, not fixed**: `procedural.cpp` is not
this lab's file, the ceiling is load-bearing for a UI slider, and raising it changes what an existing
scene means. The fixture's extreme patch is 50 and says so.

### 4.2 Two compromises, written down rather than hidden

* The scene still gets a `defaultKeyLight()`, because it has no rig and no lights. It illuminates
  nothing, because every surface is unlit. The fixture does not pretend the light is absent; it
  makes it irrelevant.
* `environment.brightness` is 1.0 and is a *second* exposure (§1.1). The fixture pins it so that
  the table in §2 is about the operator.

---

## 5. Findings

### 5.1 The bright pass gates on luminance; the tone curve compresses per channel

`fs_prefilter` computes `thresholdWeight(luminance(c), threshold, knee)` with
`luminance = 0.2126 R + 0.7152 G + 0.0722 B`. `tonemap.wgsl` compresses each channel on its own.
They therefore disagree about what "bright" means by the ratio of the luminance weights, and the
ratio between green and blue is 9.9.

Measured — the radiance at which a 16 × 16 emitter's prefilter output first leaves zero, threshold
1.0, knee 0.5:

| hue | luminance of the unit colour | crosses at | ratio to neutral |
|---|---|---|---|
| neutral | 1.0000 | 0.546 | 1.00 |
| cyan | 0.7874 | 0.696 | 1.27 |
| green | 0.7152 | 0.768 | 1.41 |
| **violet** (0.5, 0, 1) | 0.1785 | 3.161 | **5.79** |
| red | 0.2126 | 2.600 | 4.76 |
| **blue** | 0.0722 | 7.607 | **13.93** |

Every ratio is `1 / luminance` to within 1%. So: **a neutral highlight starts to bloom at
scene-linear 0.55, before it has even reached 204 on screen; a pure blue one does not start until
7.6, by which point its blue channel is at the top of the display range and the other two are at
178.** The bright pass admits neutral highlights that are not yet clipping and excludes blue ones
that are.

This is a **finding, not a defect report**, and the distinction is deliberate. The behaviour is what
the code says it does, Glowmere's emissive content is tuned against it, and the one-line change that
would reconcile it — thresholding on `max(c)` instead of `luminance(c)`, or blending the two —
changes every frame that has ever been graded on this engine. It is the owner's decision and not
this lab's. What the lab contributes is the number, and a test that pins the *current* rule
(*"the bright pass gates a hue on its luminance, not on its brightest channel"*) so a change of basis
is a deliberate change with a test to update rather than a silent one.

It also bears directly on Bioluminescence Phases B–E: the species brightness ladder is a ladder of
*hues*, and two species at the same authored intensity in cyan and violet cross the bloom threshold
4.6 stops apart.

### 5.2 The bloom pyramid's reach is a pixel count, so supersampling halves it

Six levels from half resolution means the coarsest level's texel is 2⁶ = **64 output pixels
whatever the frame's size**. The reach is measured in pixels, not in fractions of the picture.

Measured on the synthetic bench, one source of *fixed normalised size* (1/36 of the frame height) at
five resolutions — the normalised radius containing 99% of `bloom/up0`'s energy:

```
   640x360   r99=0.33889      1280x720  r99=0.16667      1600x900  r99=0.13333
  1920x1080  r99=0.11111     2560x1440  r99=0.08333
```

`r99 × height` is 122, 120, 120, 120, 120 pixels. Flat.

And through the real renderer, which is the version that matters, because `render_job.cpp` sets
`quality.renderScale = min(supersample, 2)` and the whole post chain runs at the scene target:

| source | renderScale 1 | renderScale 2 | ratio |
|---|---|---|---|
| 0.3 m (15 px at 720p) | r99 = 0.11944 | r99 = 0.06042 | **0.506** |
| 4.0 m (198 px at 720p) — **the control** | r99 = 0.19028 | r99 = 0.18472 | 0.971 |

The small source's glow covers half as much of the delivered frame with `--supersample 2` as
without; the large source, whose halo is mostly its own width, does not move. The second control is
case 3: the flat card does not change at all, because there is no spatial frequency there for a
resolution change to reach.

This is a real look change hidden inside a sampling flag, and Glowmere's deliverable is rendered
with supersampling on. It is **reported, not fixed**, for the same reason as §5.1 and one more: the
fix is to make the level count follow the resolution — the convention `PostProcessor::run` already
uses for every other spatial post radius is `pixelScale = height / 720`, so
`levels + round(log2(pixelScale))` would be consistent with something documented rather than a new
constant (§28) — and it changes every render at every size other than the one the look was tuned at.
That is a decision, not a bug fix, and it lands in the file the Volumetric Lab is about to enter.

The invariant a fix would have to satisfy, stated so it can be tested later: *the normalised radius
containing 99% of a fixed-normalised-size highlight's halo must not depend on the render scale.*

### 5.3 `resetExposure()` did not discard the metering copy already in flight — **fixed**

**Symptom.** The first frame after a scene swap is exposed for the previous scene.

**Root cause.** `PostProcessor::resetExposure` clears `exposureState_`, `haveMeasurement_` and
`measuredLuminance_`. It did not clear `meterPending_`, the flag saying a copy of the previous
frame's metered luminance is still on its way to the readback buffer, and `takeMeasurement` maps
whatever is pending at the top of the next `run()`. So `updateExposure` was handed
`hasMeasurement = true` with the old scene's reading and walked toward it.

**Measured.** Reset after a frame metered at 8.0, then a black frame: the chain reported **8.0**
where it must report "nothing metered" (−1). The control is a chain that has never metered
anything, through the same call sequence: it reports −1, which is what says the arm was measuring a
leak rather than the default state.

**Fix.** `meterPending_ = false` in `resetExposure`, with the reason in the comment. One line.

**Blast radius: none in a deliverable.** `Engine::resetCameraState` fires at load, before any frame
has been metered, so no shipped render changes. What changes is a process that renders more than
one scene — which is every test process, and the editor.

**Regression.** *"a reset meter reports the frame it was reset for"* (`[hdr][lab][gpu]`), failing
before and passing after.

### 5.4 `renderToImageFloat` read the output's extent out of the scene target — **fixed**

ADR-251 found this in `RenderJob::renderOne` and fixed it there: *"the file was the right size, the
right format, scene-linear and full of the wrong part of the picture"*. The same mistake was still
in `SceneRenderer::renderToImageFloat`, which read back `width × height` — the **output** size —
from `hdrOutput_`, which `resize()` sizes to `output × renderScale`. At `--supersample 2` it
returned the top-left quarter of the frame. Nine test files call it.

**How it was found is the useful part**, because this is the shape of thing that produces a wrong
*finding* rather than a failure. The §5.2 probe first reported the bloom halo four times *wider* at
render scale 2, which would have been a headline. It was a corner of the picture. With the extent
fixed the same probe reports 0.506 — a real result, in the other direction, an eighth the size, and
with a control beside it that does not move.

**Fix.** Read `hdrOutput_.GetWidth()/GetHeight()`. A measurement in normalised coordinates is
comparable across render scales as it stands, and one in pixels was already a measurement of the
scene target rather than of the output.

---

## 6. The artifact classes the specification names

> *Specifically investigate existing classes of bloom ghosting, UV-scaling artifacts, bright-pass
> artifacts, pyramid artifacts.*

**Bloom ghosting — not reproduced, and I do not believe it is there.** A ghost is a second, offset
image of a highlight, and a blur cannot make one. Measured as the radial profile of an impulse's
`bloom/up0` about its own centre, at `bloomRadius` 1.0 and 1.9: **zero rises** in either, out to 60
texels, where a rise is any radius whose mean exceeds the previous one by more than 2%. The halo
falls monotonically from 7.63 to 0.00002. What *is* there, and is authored rather than accidental,
is `post/anamorphic/ghosts` — two flare taps mirrored through the frame centre inside `fs_wide`.
Those are a feature; the defect beside them (a single unfiltered bilinear tap minifying a
half-resolution source by 2.7× and 5×) is already recorded, unfixed, in
`docs/post-artifact-forensics.md` §10, and this lab did not improve on it.

**UV-scaling artifacts — reproduced in the coarse levels, not reproducible in the assembled
pyramid.** The truncation is real: `w = max(1, w / 2)` makes a level slightly *more* than half the
one above it whenever the dimension is odd, and 1080 goes 540 → 270 → 135 → **67** → 33, while the
passes address every level in normalised uv, which assumes exactly half. The coarse levels do
carry a displacement — at 512×288 the 8×4 level's centroid is 3.84 frame pixels off. But the
assembled pyramid does not: measured with a centroid **windowed** to twelve texels about the source,
at 512×256 (halves exactly six times, the control) and 512×288 (truncates), at the centre and at
three quarters across, at radius 1.0 and 1.9 — **every displacement is under 0.2 px**, and the
control is displaced as much as the arm. The energy-conserving upsample blends each coarse level at
`bloomRadius / 2`, so a level *n* displacement reaches `up0` at weight 0.5ⁿ.

This cost a wrong reading first, and it is worth writing down: the *unwindowed* centroid reported a
27-pixel displacement at radius 1.9. That is the frame edge clipping a halo wider than the frame and
pulling the centroid of a truncated symmetric bump toward the middle — the instrument moving, not
the halo. The assertion in the test uses the windowed statistic and says why.

**Bright-pass artifacts — reproduced, and it is §5.1.** Not a sampling artifact: a disagreement of
basis between the bright pass and the tone curve, worth 13.9× on a pure blue.

**Pyramid artifacts — reproduced as §5.2, and the energy claim holds.** The pyramid's own arithmetic
is sound: 0.12% mean loss over eleven passes, the halo within 0.2 px of its source, identical peak
and 90%-radius wherever in the frame the highlight sits (five positions, byte-identical), and total
energy varying by 0.24% only where the frame edge clips it. What is not sound is the *level count*
being a constant while the resolution is not.

**Not investigated here, and named so nobody assumes otherwise:** the halation tier's own thresholds
(the warmth mask is a different question with the same shape as §5.1); the DoF and motion-blur
stages (they need targets the synthetic arm has no business inventing, and neither is accused);
`fs_meter_reduce`'s drift at non-dividing resolutions (§1.2); and the interaction between
`renderScale < 1` in the editor's canvas and everything in §5.2, which is the same mechanism with
the sign flipped and belongs to whoever owns the preview's fidelity.

---

## 7. What this lab's case format cannot say

`labs::LabCase` carries a fixture, a time, a size, a seed, a camera, a supersample factor, a tier,
`--disable` arms, `--quality-arm` arms and `--aov` names. It carries **no way to set a parameter**.
Every arm in this lab that is about exposure, a threshold, an operator or a hue is therefore a test
and not a case, and the seven cases in `examples/labs/hdr/cases.json` are the subset the format can
express: the fixture's two pages, two resolutions, two render scales and the post arm.

That is a real limitation rather than an oversight — a case is meant to be a command line, and there
is no `--set post/bloom/threshold=2` flag for it to be. The Lighting Lab hit the neighbouring
version of it (§5.2 there: no per-light isolation arm). Two labs in a row have wanted the same
thing, which is probably the argument for it.

---

## 8. How to use it

```
avgen --labs                                  # the ownership map, printed
avgen --lab-case hdr:1                        # the calibrated card, post disabled
avgen --lab-case hdr:4                        # the two bright sources

avgen --headless --composition examples/labs/hdr-lab.scene.json \
      --size 1280x720 --range 0.5:0.5 --render out --post-stages stages
      # every intermediate the chain rendered, as scene-linear EXRs + stages.json

avgen ... --debug-target emission             # the mask post/bloom/emissionWeight reads
avgen ... --aov emission --format exr         # the same, exported
avgen ... --disable post                      # the boundary the Lighting Lab drew in code

build/release/tests/avgen_tests "[hdr][lab]"
tools/gpu-lock.sh build/release/tests/avgen_render_tests "[hdr][lab]"
```

The `[.probe]` cases in `test_hdr_lab_gpu.cpp` are Catch2-hidden and are measurements rather than
tests: they print the tables in §1.2, §2, §5.1 and §5.2 and assert only that the GPU reported no
errors. Everything in §5.3, §5.4 and §6 that is stated as an invariant is a case that fails.
