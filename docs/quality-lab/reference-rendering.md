# Reference rendering: what a reference is, and the four things it is not

Status: research (spec §13, §7A, §14). Companion to
[research.md](research.md) and [architecture.md](architecture.md).

---

## 1. The distinction the whole system rests on

§7A states it and it is the easiest thing here to lose:

> **The reference is NOT necessarily "ground truth."** It is a deliberately high-quality reference
> against which information loss can be measured.

Everything a full-reference metric reports is therefore of the form:

> *how much information did this configuration lose, relative to a better-sampled render of the same
> model?*

and never *how good is this image*. A supersampled render does not correct a wrong shadow bias, a
wrong normal, a temporally-unstable shader, a tone mapper that clips, or an authored look that is
ugly. **It converges the sampling error and nothing else.** Where the model is wrong, the reference
is wrong in exactly the same way, with more samples.

Four things a reference is not, stated so a report can cite them:

1. **Not ground truth.** See above.
2. **Not a target to optimise toward.** A configuration that maximises similarity to the reference
   is a configuration that renders slowly. The frontier (§22) is the deliverable, not the maximum.
3. **Not comparable across scenes.** A reference is a pair-wise instrument. "Scene A scores 0.94 and
   scene B scores 0.89" is not a statement about the renderer.
4. **Not stable across engine versions.** A renderer upgrade changes pixels on purpose —
   `tools/certify.py` already states this as the reason there are no committed golden images. A
   reference is re-rendered per experiment, from the same commit, never committed to Git (§36).

---

## 2. The strategies, and what each one actually buys

| strategy | what it converges | what it does not | available here |
|---|---|---|---|
| **spatial supersampling** (`--supersample`) | the raster sampling rate — thin geometry, silhouettes, specular highlights | shading errors, temporal behaviour, shadow bias, LOD choice | **yes, 1.0–2.0 only** |
| **increased render resolution** (`--size`) | the same, but the output is a different size and must be downsampled by the harness | same | yes — but it changes the frame, see §4 |
| **the offline quality tier** (`--tier offline`) | shadow resolution 4096, 4 cascades, 24 PCF/24 blocker taps, contact 24, AO 6×12 at full resolution, SDF 48, material tiers **off** and forced to Full | sampling rate | **yes, and it is the default for a render** |
| **conservative LOD** (`--render-limits unlimited`) | distance culling, rig rate limits, entity bands | **not the LOD ladder itself** — ADR-191 keeps it | yes |
| **hysteresis-free evaluation** | path-dependence: ADR-146 sets `lodHysteresisAllowed = false` at the offline tier so the frame does not depend on how the camera got there | — | yes, automatic at `offline` |
| **temporal accumulation / jittered multi-sample** | sampling *and* temporal reconstruction error | needs a jitter mechanism the engine does not have | **no — not built** |
| **offline path-traced ground truth** | almost everything | it would be a different renderer | **no, and out of scope** |

**The chosen reference recipe** is the union of what exists:

```
--project <derived-reference-project>.json
--tier offline --render-limits unlimited --supersample 2.0
--format png
```

rendered from the **same commit**, the **same start time**, the **same fps** and the **same output
resolution** as the candidate.

---

## 3. The limits of this reference, stated before anyone relies on it

### 3.1 The supersample ceiling is 2×, and the resolve is a bilinear tap, not a box filter

`--supersample` is clamped to `[1.0, 2.0]` at parse time and again in `validate()`. So the best
available reference is **4 samples per output pixel**, not 16 or 64. §13's "8× resolution" is not
reachable without engine work.

More subtly: **there is no resolve pass.** The downsample is a side effect of the tonemap sampling
the larger HDR texture into the smaller output through `tonemapSampler_` — Linear min/mag,
`mipmapFilter = Nearest`, no mips. At 2× that is a bilinear footprint evaluated at each output pixel
centre, which for an exact 2:1 ratio reads four texels but does **not** weight them equally the way a
box filter would. The shader's own comment is written for the *upscale* case (`renderScale < 1`);
the minification case is unexamined.

So the reference is a **better-sampled image**, not a **correctly-filtered** one. ADR-212 measured
the benefit and a reviewer confirmed it (*"is t4 visibly calmer? yes, much"* / *"worth 2.7× the
render time? absolutely"*), so the improvement is real. The claim that must not be made is that the
reference is band-limited.

**Recorded as a limitation in every report that uses a supersampled reference**, per §18's
"metric confidence/limitations" and §19's `limitations` field.

### 3.2 `--supersample` with `--format exr` writes a crop — **measured**, not inferred

`RenderJob` enqueues `hdrOutputTexture()` with extent `settings_.width × settings_.height`, but under
supersampling that texture is `width·scale × height·scale`. `CopyTextureToBuffer` with a smaller
extent is legal and copies the **top-left crop**. `validate()` refuses only the AOV combination; there
is no guard for this one.

**This was verified on the device rather than left as a reading of the code.** Four arms,
`examples/qa/renderer-qa.json`, 3 frames each, under `tools/gpu-lock.sh`, `pgrep avgen` clean before
and after:

| arm | flags | EXR size | result |
|---|---|---|---|
| **X** | `--size 320x180 --supersample 2.0 --format exr` | **6,065 B** | the defect |
| **Y** | `--size 640x360 --supersample 1.0 --format exr` | 341,221 B | the same scene resolution, written whole |
| **Z** | `--size 320x180 --supersample 1.0 --format exr` | 111,241 B | the same output size, no supersampling |
| **P** | `--size 320x180 --supersample 2.0 --format png` | — | the control: the PNG path |

**X is bit-for-bit the top-left quarter of Y**, on all three colour channels:

```
X.R vs Y.R top-left 320x180:  maxAbsDiff=0.000000  differingPixels=0/57600 (0.00%)
X.G vs Y.G top-left 320x180:  maxAbsDiff=0.000000  differingPixels=0/57600 (0.00%)
X.B vs Y.B top-left 320x180:  maxAbsDiff=0.000000  differingPixels=0/57600 (0.00%)
```

The controls, which are what make that non-vacuous (ADR-182):

```
X.G vs Y.G top-right      : maxAbsDiff=4.033062  differing 2708/57600   (4.70%)
X.G vs Y.G bottom-left    : maxAbsDiff=0.490582  differing 43160/57600 (74.93%)
X.G vs Y.G centre         : maxAbsDiff=4.033062  differing 29910/57600 (51.93%)
X.G vs Z.G (same size)    : maxAbsDiff=3.275249  differing 27282/57600 (47.36%)
```

— so it is *that* crop specifically, and it is not simply the un-supersampled render.

**What is discarded is the entire subject.** The top-left quarter of this frame is empty background;
the orbs are elsewhere:

```
Y.G top-left quarter : max = 0.0069
Y.G whole frame      : max = 4.0391          — a factor of 585
```

Arm X's own range confirms it: `B: min 0.01200 max 0.01357`, against arm Z's `min 0.00060 max 2.88281`.
**The file is the right size, the right format, scene-linear, and contains no scene.**

Two boundary facts, both measured:

* **The PNG path is correct.** Arm P's `bright_centroid_x = 0.4990` — the subject is centred, the
  frame is whole. PNG reads `ldr_`, the tonemap's target, which is at output size.
* **ADR-242's refusal still fires**, so `validate()` is working and simply has no clause for this
  case: `render: aov export and supersample 2 cannot be combined -- an identifier, a normal and a
  depth edge have no correct downsample`.

**Consequence for the design: references are taken as PNG.** That is not a workaround dressed as a
decision — PNG is also the right colour space for every metric that needs one (§5). An HDR reference
is desirable and is blocked on the engine fix.

**The engine fix is proposed, not taken here** (see ADR-251). Refusing the combination the way ADR-242
refuses AOVs would be a two-line `validate()` clause and strictly better than a silent crop. But
radiance, unlike an identifier or a normal, *does* have a correct resolve — averaging radiance is
what a resolve is — so the better fix is to resolve the HDR readback rather than refuse it, and that
is a change to the engine's render path that belongs to whoever owns it.


### 3.3 AOVs are refused with supersampling, and the reference does not need them

ADR-242 refuses `--aov` with `--supersample` because the auxiliary targets are sized to the scaled
resolution and three of the five have no correct downsample. The Quality Lab's split makes this cost
nothing:

| render | flags | job |
|---|---|---|
| **candidate** | production config + `--aov normal,emission,depth,velocity,id` | the image under test, plus masks and motion at their own exactly-correct resolution |
| **reference** | `--tier offline --render-limits unlimited --supersample 2.0` | the better-sampled image the full-reference metrics compare against |

Full-reference metrics need only the beauty pass. AOV-driven detectors run on the candidate.
**Nothing needs an AOV at supersampled resolution.**

The one operation that crosses the boundary is a **masked full-reference metric** — "ꟻLIP error
restricted to vegetation pixels". That is a candidate-resolution mask applied to a reference that is
already at candidate output resolution, which is well-defined. It is the only sanctioned crossing.

### 3.4 The reference is not deterministic in every scene

A full-reference comparison is only meaningful where the two renders describe the same moment. From
the reconnaissance, the systems that break that:

* **ADR-091's live tier** — ambient population, props, background vehicles: stateful, reset on seek,
  **not frame-accurate under scrub**.
* **Legacy orbit cameras** integrate `orbitSpeed · dt`, so the frame depends on how the playhead got
  there. Free and Spline placements do not.
* **Cross-GPU equality is not promised**, and particle determinism is per GPU family and driver.
* Bit-identity holds **within a mode at a fixed fps** — an offline render and a live run converge to
  the same envelope, not the same bits.

**Rules that follow, and they are harness rules, not advice:**

1. Always render from the **same start time**; never seek into the middle to save frames.
2. Always pin **fps** and **size** explicitly, on both arms.
3. Refuse orbit-camera scenes for full-reference work, or declare them.
4. Compare on **one GPU, one session**.
5. **Check the sequence hashes.** Two arms expected to be identical must hash identically; two arms
   expected to differ must hash differently. An arm whose hash matches the baseline when it was
   supposed to change is **vacuous**, and the experiment is void rather than measured — which is
   ADR-182 applied to the harness and the check ADR-243's five-arm suite already used.

---

## 4. Why the reference renders at the candidate's output resolution

There are two ways to get a better-sampled reference: raise `--supersample`, or raise `--size` and
downsample on the CPU. They are not equivalent.

`--supersample` changes **only the internal render scale**; the output frame is the same size, the
camera frames the same content, and post-processing runs at the scene resolution before the resolve.
Raising `--size` changes the output, so the harness must downsample — and it also changes every
resolution-dependent effect in the post chain. `post/lens/chromaticAberration` is documented as
*"pixels scaled by resolution"*; bloom's pyramid has a different number of usable levels; FXAA's
edge search is a fixed texel radius. **A 4K render downsampled to 1080p is not a better-sampled
1080p frame; it is a different look.**

So: **the reference matches the candidate's output resolution and differs only in `--supersample`,
`--tier` and `--render-limits`.** This is the same discipline `tools/bench_ab.sh` already enforces
for shaders — *"the baseline otherwise runs the new shader, which is how an afternoon was spent
chasing a rendering bug that did not exist."*

---

## 5. Colour space: which file the metric reads, and why it matters

From the reconnaissance:

* **PNG** is display-referred sRGB, taken **after** the tonemap (exposure multiply, operator,
  chroma retention, vignette, grain, sRGB encode). The file carries **no colour tag** — `RGBA8Unorm`
  and `stbi_write_png` writes no `sRGB`/`gAMA` chunk — so any external tool must be *told*.
* **EXR** is scene-linear HDR, taken from the tonemap's **input**: post-bloom and post-grade but
  **pre-tonemap, pre-exposure-multiply, pre-vignette, pre-grain**.

They are **not the same image graded differently.**

| metric | reads | why |
|---|---|---|
| VMAF, PSNR-HVS, CAMBI, MS-SSIM, CIEDE2000 | **PNG (sRGB)** | all are defined on display-referred content; VMAF and CAMBI especially assume a display |
| ꟻLIP (LDR) | **PNG (sRGB)** | LDR-ꟻLIP expects sRGB |
| HDR-ꟻLIP | **EXR (linear)** | separates *"the renderer produced different light"* from *"the tone mapper mapped it differently"* — blocked on §3.2 |
| motion-compensated residual, AOV-gated detectors | **PNG + AOV EXRs** | the residual is on the visible image; the masks and motion come from the AOVs |
| spectral detail retention | **PNG** | high-frequency content as delivered |

**The report records which file each number came from.** A PSNR taken on EXRs and a PSNR taken on
PNGs of the same render are different numbers about different images, and a suite that mixes them
silently is a suite that cannot be trusted.

---

## 6. Cost

ADR-212 measured `--supersample 2.0` at **2.7× render time**. A reference is therefore roughly three
times the cost of the candidate it is compared against, and it is rendered **once per (scene, camera,
resolution, commit)** and reused across every candidate in an experiment. That reuse is the reason
the experiment manifest keys the reference by exactly those four things (see
[experiments.md](experiments.md)) — re-rendering it per candidate would triple an already expensive
sweep for no information.

Storage: §36 says do not put render outputs in Git, and nothing here does. A 300-frame 1080p PNG
reference is on the order of a gigabyte; run directories are `quality-results/<timestamp>/` and are
`.gitignore`d.

---

## 7. What is not built, and what it would take

| wanted | blocked on |
|---|---|
| supersample above 2× | a clamp, and memory: targets scale with the square of the factor |
| a correct box/tent resolve | a resolve pass, or a mip-based downsample; today it is the tonemap's bilinear tap |
| an HDR reference | the `--supersample` + `--format exr` crop guard |
| AOVs at reference resolution | per-target resolves — nearest for `id`, decode-average-**renormalise** for `normal`, silhouette-aware for `depth` and `velocity`, plain average for `emission` (ADR-242 names this shape; [research.md §7](research.md#7-the-aov--supersampling-collision-and-how-the-lab-resolves-it) endorses it). **Not needed** given the candidate/reference split |
| temporal accumulation reference | a jitter mechanism; there is no temporal history anywhere in the post chain except auto-exposure |

---

## Verification status

§3.2 is **measured**, on the device, under the GPU lock, with three controls that discriminate the
crop from every neighbouring hypothesis. See ADR-251 and the table above.

§3.1 — the bilinear-rather-than-box resolve — is **read from the code and not separately measured**.
The sampler is `Linear`/`Linear` with `mipmapFilter = Nearest` and no mips, and there is no resolve
pass; that is unambiguous from the source. What is *not* established is how much the difference from
a true box filter matters, which would need a controlled comparison against a CPU-downsampled 2×
render. Recorded as a limitation of the reference, not as a defect.
