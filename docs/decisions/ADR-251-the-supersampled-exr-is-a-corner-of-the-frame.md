# ADR-251: The supersampled EXR is a corner of the frame, and it looks like a render

**Status:** Accepted
**Date:** 2026-09-16
**Context:** Quality Lab Phase 1 — designing the reference-render strategy on top of `--supersample`
**Follows:** ADR-212 (an offline render may spend pixels), ADR-242 (a target nobody reads is not a
feature), ADR-182 (a probe that cannot fail proves nothing), ADR-243 (the detector and the eye
disagree)
**Follows from:** ADR-250

## Context

The Quality Lab's reference render is `--tier offline --render-limits unlimited --supersample 2.0`,
and the obvious refinement is to take it as scene-linear EXR so full-reference comparison can happen
**before** the tone map — which separates *the renderer produced different light* from *the tone
mapper mapped it differently*, a separation this repository has repeatedly needed and repeatedly had
to improvise.

Reading the code to check that this was sound turned up a problem instead.

`RenderJob::renderOne` enqueues the HDR readback like this:

```cpp
ring_->enqueue(encoder, renderer_->hdrOutputTexture(), settings_.width, settings_.height, ...)
```

`settings_.width` is the **output** width. Under `--supersample 2.0`, `hdrOutputTexture()` is
**twice** that, because `resize()` is the only consumer of `renderScale` and it sizes every scene
target to `output × renderScale`. `CopyTextureToBuffer` with an extent smaller than the texture is
legal and copies the region at the origin.

`validate()` refuses `--aov` with `--supersample` (ADR-242). It has no clause for this.

## What was measured

Four arms, `examples/qa/renderer-qa.json`, 3 frames each, under `tools/gpu-lock.sh`, `pgrep avgen`
clean before and after. No timing is claimed, so the contended load average (9.61) does not bear on
the result — this is a content check, and ADR-170's own observation is that structural quantities are
the ones that survive contention.

| arm | flags | EXR bytes |
|---|---|---|
| **X** | `--size 320x180 --supersample 2.0 --format exr` | **6,065** |
| **Y** | `--size 640x360 --supersample 1.0 --format exr` | 341,221 |
| **Z** | `--size 320x180 --supersample 1.0 --format exr` | 111,241 |
| **P** | `--size 320x180 --supersample 2.0 --format png` | — (control) |

X and Y render the scene at the **same** resolution — 640×360 — and at the same aspect ratio, so
their HDR targets should agree. The 56× file-size gap between X and the same-dimension arm Z was the
first signal; a 320×180 half-float RGBA frame that compresses to 6 KB is almost entirely constant.

**The claim: X is the top-left quarter of Y.** Decoded and compared per channel:

```
X.R vs Y.R top-left 320x180:  maxAbsDiff=0.000000  differingPixels=0/57600 (0.00%)
X.G vs Y.G top-left 320x180:  maxAbsDiff=0.000000  differingPixels=0/57600 (0.00%)
X.B vs Y.B top-left 320x180:  maxAbsDiff=0.000000  differingPixels=0/57600 (0.00%)
```

Bit-exact, on every channel, on every pixel.

**The controls, which are what make that mean anything (ADR-182).** A comparison that matches proves
nothing unless the neighbouring hypotheses are shown to fail, and a top-left crop is one of several
things "a smaller copy of a bigger frame" could be:

```
X.G vs Y.G top-right      : maxAbsDiff=4.033062  differing  2,708/57,600  ( 4.70%)
X.G vs Y.G bottom-left    : maxAbsDiff=0.490582  differing 43,160/57,600  (74.93%)
X.G vs Y.G centre         : maxAbsDiff=4.033062  differing 29,910/57,600  (51.93%)
X.G vs Z.G (same size)    : maxAbsDiff=3.275249  differing 27,282/57,600  (47.36%)
```

So it is **that** crop and not another, and it is **not** simply the un-supersampled render of the
same size. Note the top-right arm differs on only 4.70% of pixels — that is the measurement that
would have looked like agreement to anyone checking with a tolerance instead of a property, since
both corners are mostly empty background. It is separated by the *magnitude*, 4.03, not by the count.

**What the crop throws away is the entire subject:**

```
Y.G top-left quarter : max = 0.0069
Y.G whole frame      : max = 4.0391          -- a factor of 585
```

Arm X's own channel ranges say the same thing from the other side: `B: min 0.01200 max 0.01357`,
against arm Z's `B: min 0.00060 max 2.88281`. **The file is the right size, the right format,
scene-linear, ZIP-compressed, and contains no scene.**

Two boundary facts, both measured, which scope the defect precisely:

* **The PNG path is correct under supersampling.** Arm P's `bright_centroid_x = 0.4990` — the subject
  is centred and the frame is whole. PNG reads `ldr_`, the tonemap's render target, which is created
  at the *output* size, so the mismatch cannot arise there.
* **`validate()` is not broken, it is silent.** ADR-242's refusal fires normally:
  `render: aov export and supersample 2 cannot be combined -- an identifier, a normal and a depth
  edge have no correct downsample`. This is the positive control that (1) is a missing clause and not
  a dead validator.

## Decision

### 1. The Quality Lab takes its references as PNG

Not as a workaround. PNG is also the correct colour space for every metric that needs one — VMAF,
CAMBI, PSNR-HVS, MS-SSIM, CIEDE2000 and LDR-ꟻLIP are all defined on display-referred content — so the
HDR reference was a refinement, not a requirement. **HDR-ꟻLIP is deferred**, and the reason is
recorded rather than left as an unexplained omission.

### 2. The engine fix is proposed and deliberately not taken here

This ADR is written from a research phase whose mandate is explicitly not to change the renderer, and
two other agents are working in this repository. More importantly, **the right fix is a real design
question and not obviously the same one ADR-242 took.**

ADR-242 *refused* because three of its five targets have no correct downsample. **Radiance is not one
of those cases.** Averaging radiance is exactly what a resolve is. So:

| option | assessment |
|---|---|
| **refuse** the combination in `validate()`, mirroring ADR-242 | two lines, immediately correct, and strictly better than a silent crop — but it removes a capability somebody will want, and removes it for a reason that does not apply to radiance |
| **resolve** the HDR readback down to the output size | the correct fix. It is also a change to the render path and needs a resolve that matches whatever the beauty pass does |
| **leave it** | refused. A file that looks like a render and is a corner of one is the exact failure ADR-242 was written about, in a different target |

The recommendation is **resolve**, with **refuse** as the acceptable interim. The choice belongs to
whoever owns the render path.

> **Resolved, the same day.** The render path took the recommendation: `RenderJob::renderFrame` now
> reads the source texture's own extent, and `resolveToOutput` box-averages the supersampled HDR
> frame down to the output size before the frame is hashed — before, because the sequence hash is the
> deliverable's proof and has to describe the file that is written rather than an intermediate nobody
> receives. A ratio that is not an integer is left unresolved with a warning rather than
> approximated, since a half-pixel box is a different filter.
>
> The regression test measures a **luminance centroid** rather than radiance, and the two attempts it
> took to get there are worth recording because both failures were informative. A peak-brightness
> guard could not discriminate: the fixture's bright orb sits near the top-left, so the quarter's peak
> is 9.81 against the whole frame's 10.08 and a crop would have passed on luck. A channel-by-channel
> radiance comparison then failed at 58% — correctly, because a supersampled render is *supposed* to
> differ at every antialiased edge, and an absolute threshold on HDR values reaching 10.0 flags all of
> them. That measured resolving, not framing. A crop does the one thing a resolve never does: it moves
> the content. The control arm is synthesised inside the test — the crop the defect produced is
> rebuilt from the good frame and measured with the same instrument — so the test proves its own
> discriminating power instead of assuming the fixture supplies it.
>
> **Consequence for §1 above:** HDR references are viable again, so "the Quality Lab takes its
> references as PNG" is now a choice rather than a constraint. It remains the right choice for the
> metrics named there, all of which are defined on display-referred content; HDR-ꟻLIP is no longer
> blocked by the engine.

### 3. The general lesson, which is ADR-242's restated in a second place

ADR-242's normal pass was *"smooth, continuous, varying over every surface, sensible in a viewer"*
and undecodable, and it took an assertion about a **property** — that a normal is a unit vector — to
find it. Nothing about the file's appearance would ever have revealed it.

This is the same defect class in a different target: **a copy that moves bytes without knowing the
size of the thing it is moving.** The file passes every test that asks *is there a file, is it the
right size, does it contain plausible floats*. It fails the only test that matters, which is *does it
contain the frame*.

ADR-242 wrote the lesson as **"a target's meaning is not its layout."** The corollary this ADR adds
is narrower and sharper: **an extent is not a size.** `settings_.width` is a true fact about the
output and a false fact about the texture, and the API accepts it either way.

## Consequences

**A defect on the reference-render path was found before a reference strategy was built on it.** Had
the Quality Lab specified HDR references — the natural choice, and the one the mandate's §13 points
at — every full-reference number in the system would have been computed against the top-left corner
of an empty sky, and the metrics would have been *consistent*, *reproducible* and *wrong*. Both
frames would have looked like data.

**The 56× file-size gap is the cheap version of this check.** Any harness writing EXR sequences should
sanity-check that a frame's compressed size is in the expected range; a frame that compresses too well
contains too little.

**`--format exr` is unsafe with `--supersample` until one of the two options above is taken**, in every
context, not just the Quality Lab's. Nothing else in the repository currently combines them — both
Glowmere projects set `supersample: 2.0` and both render `output: "video"` or a PNG sequence — so
nothing shipped is affected. That is luck, not design.

**What this does not say.** It does not say `--supersample` is broken: ADR-212's measurements stand,
the PNG path is correct, and the reviewer's *"much calmer"* judgment was made on frames that were
whole. It says one readback path takes its extent from the wrong number.
