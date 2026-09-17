# Artifact detection: the detectors, their inputs, their failure modes, and the artifact they may not claim

Status: **implemented** (spec §4, §5, §15, §16, §32) as `tools/quality-lab/metrics/temporal.*` and
`tools/quality-lab/artifacts/masks.*`. §16 requires each detector to carry an algorithm description,
a mathematical definition, inputs, output range, interpretation, known failure modes, test cases and
a debug visualization — this document was that, per detector, *before* any code existed, and the
three ⚠ notes below are where writing the code changed it
([ADR-253](../decisions/ADR-253-the-residual-knows-where-the-pixel-came-from.md)).

---

## 0. The governing lesson

ADR-243 is the reason this document is shaped the way it is. A **correctly implemented** temporal
detector, measuring exactly what it claimed to measure, ranked two anti-aliasing remedies backwards
against a human reviewer on the artifact that viewers actually report. Not because the arithmetic was
wrong — because the artifact it measured was not the artifact being complained about.

So every detector below states three things separately, and the report keeps them separate:

1. **The measurement** — what the arithmetic computes. Always true.
2. **The artifact class it is licensed to speak about** — narrower than the measurement.
3. **What it must not be used to conclude** — including, for two of them, an explicit record of a
   conclusion it has already got wrong.

And §40's rule sits above all of them: **measured fact and inferred cause are different fields.**
A detector reports a number and a region. A *hypothesis* about the renderer is labelled a hypothesis,
with a confidence, and never promoted (§41).

---

## 1. Motion-compensated temporal residual — the missing instrument

**The one detector that would have changed ADR-243's investigation**, because it is the only thing on
this list that separates authored motion from unwanted change.

**Algorithm.** For consecutive frames *t* and *t+1*:

1. Read the candidate's velocity AOV for frame *t+1*. It stores screen motion in **UV units**, RG16F,
   written by the scene pass — exact, from the engine, free.
2. Backward-warp frame *t* by that velocity to predict frame *t+1*, bilinearly.
3. Build the **validity mask**: a pixel is valid when it is not disoccluded and not out of frame.
4. `residual(p) = |frame(t+1)(p) − predicted(p)|` over valid pixels only.

**Mathematical definition.**

```
predicted(p)      = frame(t)[ p − velocity(t+1)(p) · resolution ]
valid(p)          = inFrame(p − v) ∧ ¬disoccluded(p)
residual          = mean over { p : valid(p) } of |luma(frame(t+1)(p)) − luma(predicted(p))|
disocclusionFrac  = |{ p : ¬valid(p) }| / |p|
```

**Disocclusion detection** — the whole validity of the number rests here. Two independent tests,
both from AOVs the candidate already exports:

* **Identifier test**: `id(t+1)(p) ≠ id(t)(p − v)`. The surface at *p* is a different object than the
  one the motion vector points back to. Exact where identifiers are exact.
* **Depth test**: `|depth(t+1)(p) − depth(t)(p − v)|` exceeds a scene-scale-relative threshold. Catches
  the same-object-different-surface case a silhouette produces.

A pixel failing either test is excluded and counted.

**Inputs.** Candidate beauty PNG sequence; `--aov velocity,depth,id`. No reference. No supersampling
(ADR-242 refuses it, and the AOVs are exactly correct at candidate resolution — see
[reference-rendering.md §3.3](reference-rendering.md)).

**Output range.** `residual` in [0, 1] on normalised luma; `disocclusionFrac` in [0, 1].

**Interpretation.** Temporal change the engine's own motion does not explain. Near zero on a smoothly
animating pixel however fast it moves; high on shimmer, flicker, ghosting, specular sparkle and
shading instability.

**Failure modes, stated before it is built:**

* **Disocclusion dominates if the mask is wrong.** A translating camera disoccludes a large fraction
  of the frame; unmasked, the detector reports camera motion as instability. This is the exact shape
  of ADR-243's mistake and it is the first thing the test suite must rule out.
* **`disocclusionFrac` is a validity gate, not a quality number.** A residual computed over 4% of the
  frame is not a statement about the frame. The report shows both or neither.
* **It inherits every velocity-AOV defect.** If motion vectors are wrong — and "motion-vector errors"
  is itself one of §4's artifact classes — the residual attributes the error to the image.
  *Mitigation:* optical flow is retained **solely** as a cross-check. Systematic disagreement between
  flow and the velocity buffer is a **motion-vector finding**, reported as such, not a quality number.
* **Transparency has no single velocity.** The MRT's `auxMask` lets blended geometry leave targets
  1/3/4 to the opaque surfaces behind it, so a blended pixel's velocity is the *background's*.
  Transparent regions must be excluded or declared.
* **Sub-pixel warping is itself a resample.** Bilinear prediction blurs, which floors the residual
  above zero. ⚠ **The floor is now computed directly** rather than inferred from the control:
  `warpFloor` warps a frame back and forward with the same velocity, with no renderer in the loop, so
  what it reports is the instrument's own cost. Measured on the synthetic control, a whole-pixel warp
  costs 1.2×10⁻⁶ luma steps and a half-pixel warp costs 7.8 — the first is the velocity's float
  encoding (and the engine's target is RG16Float, coarser still) and the second is the resample.
* ⚠ **The sampler's edge handling is a disocclusion source if you get it wrong.** Rejecting every
  sample whose `x0 + 1` tap falls off the frame rejects the entire last row and column *at zero
  velocity* — 2.6% of a 96×64 frame reported as disoccluded by a static camera looking at a static
  scene. Clamp the tap, whose weight is zero there; never the coordinate. Found by the zero-velocity
  control arm, which is what that arm is for.

**Test cases (ADR-182 — the probe must be shown capable of failing).**

| arm | construction | must |
|---|---|---|
| **control: smooth motion** | synthetic translation, 1 px/frame, exact velocity | residual ≈ the warp floor; **this is the arm that makes every other one non-vacuous** |
| shimmer | alternate two sub-pixel-shifted renders | residual ↑ sharply |
| disocclusion | translation past a foreground occluder | `disocclusionFrac` > 0 and residual **unchanged** vs. the control. If the residual moves, the mask is broken |
| ghosting | blend frame *t* into *t+1* | residual ↑ |
| **zero-velocity control** | static camera, static geometry | residual ≈ 0 **and** `disocclusionFrac` ≈ 0 |

**Debug visualization.** `temporal-residual.png` per frame (heatmap), `disocclusion-mask.png`, and
`temporal-residual.mp4` for the sequence — plus the **worst-frame index**, because §32's question is
"show me what caused this number" and the answer usually lives in three frames.

---

## 2. Spatial high-frequency energy — the measure that agreed with the reviewer

**Measurement.** Mean absolute spatial Laplacian over luma, interior pixels only:
`|4c − left − right − up − down|`. Whole-frame and on an 8×8 tile grid. Borders are **skipped, not
clamped** — a clamped edge pixel reports a Laplacian that is an artifact of the clamp.

**Licensed to speak about.** The spatial high-frequency content of one view, compared against another
arm of the same view. That is all.

**Must not be used to conclude.** That one scene is better than another. That an absolute value is
good or bad. **That high means aliasing** — it cannot separate aliasing from detail, and its own
header says so.

**Why it is here anyway.** It is the measure that ranked ADR-243's arms in the same order as the
reviewer, on both arms. It is `tools/spatial_stats.py` and it already exists.

**Debug visualization.** `aliasing-map.png` (the per-pixel Laplacian) and the tile grid, which is what
localises a finding to "the grass beds" rather than "the frame".

---

## 3. AOV-gated per-class stability — one residual, four masks

§9 wants shadow, specular and detail as separate dimensions. The honest way to produce separate
numbers is **not** four algorithms — it is §1's residual evaluated over four masks, because four
algorithms measuring the same thing under four names is how a metric suite becomes unfalsifiable.

| dimension | mask | interpretation | the caveat that must travel with it |
|---|---|---|---|
| **specular** | emission AOV above a threshold, **or** roughness (alpha of the decoded normal AOV) below one | unexplained temporal change on shiny or emissive pixels | **emission is not specular.** A rough emissive surface is in the mask and should not be. Roughness is the better half of this mask and is half-precision |
| **geometric vs shading** | normal AOV changed / unchanged between frames | a normal that changed means geometry moved; a normal that did not while colour did means **shading** is unstable | a normal changing by less than the half epsilon reads as unchanged; the threshold is a decision, not a fact |
| **LOD popping** | `id` changed while depth and velocity say the surface did not move | a geometric level swapped under a stationary surface | a genuine object change at a silhouette is indistinguishable from a pop without more information. **Report as a candidate, not a detection** |
| **vegetation / thin geometry** | `id` restricted to vegetation material identifiers | the residual over exactly the content ADR-243's reviewer objected to | needs a material-id → class mapping that **does not exist yet** |

**Shadow is missing and stays missing.** There is no shadow AOV; ADR-242 lists it among the views
deliberately not added because nothing had asked for one. The Quality Lab is the first consumer with
a reason to ask, and that is a [human decision](research.md#10-unresolved-questions-carried-into-the-next-phase),
not one to take here. Until it is taken, `shadowStability` reports
`available: false, reason: "no shadow AOV"`. **An approximation over "regions the lighting model says
are shadowed" is refused**, because it would be a number with a name that promised more than it knew
— which is the failure this whole document is organised around.

**The identifier AOV is what makes any of this non-vacuous**, and ADR-242's test already records why:
without asserting that `id` has more than one distinct value, every mask-based row is vacuous. That
assertion is inherited.

---

## 4. Temporal alternation — kept, re-scoped, never alone

**Measurement.** `|x(t+1) − 2x(t) + x(t−1)|` over luma; **peak** per pixel over the sequence, so a
single-frame pop is not averaged away by its neighbours; plus per-frame counts over a threshold and
an 8×8 tile map.

**Licensed to speak about.** Temporal alternation of the luminance signal.

**Must not be used to conclude.** Which anti-aliasing configuration is better. Which artifact a
viewer will object to. **What to fix.** ADR-243 withdrew exactly this use, with the measurements
intact:

> Every number in it is a correct measurement of temporal alternation. None of them should be used
> again to decide what to fix without a spatial measure beside it and a person having looked.

**Harness rule, not advice:** the report emits `temporalAlternation` and `spatialLaplacian`
**together or not at all**.

⚠ **Phase 6 tested the moving-camera case ADR-243 named and did not close, and the result is a second
kind of failure** ([ADR-257](../decisions/ADR-257-the-eye-ranked-them-the-way-the-spatial-measure-did.md)).
It does **not** invert on a moving camera — nothing about the Glowmere ordering reproduced. It goes
**uninformative, and precisely where the eye is most certain**: on the three `aliasing-dolly` arms at
1280×720 it separates FXAA-off from the baseline on 58 of 58 frames by 0.62%, and calls the
supersampled arm — which a blind reviewer ranked best and called *"immediately obvious"* — **worse on
26 of 58 frames**. Two further things follow, both of which change how this number may be read:

* **Its effect size is a property of the render size.** The same arms over the same window separate
  4× more strongly at 640×360 than at 1280×720. ADR-253's conclusion that "the temporal measure
  agrees" on a moving camera was a statement about 640×360 and does not survive at delivery size.
* **A difference of pooled means is not a ranking.** Two arms of one experiment are paired — same
  camera, same times — so the question is whether the ordering survives frame by frame.
  `avgen_quality analyze --per-frame` writes the series so it can be asked; the mean ordering here is
  "correct" for all three arms and one of the three does not survive the pairing at all.

---

## 5. Banding — CAMBI

**Measurement.** Contrast-Aware Multiscale Banding Index. No-reference, luma, frame-local, 0 to ~24,
with ~5 the threshold where banding becomes *"slightly annoying"*.

**Why it is adopted unmodified.** It is the one artifact class where a standard, validated,
no-reference detector already exists and our content is squarely in its domain — volumetric fog,
atmospheric gradients and bloom falloff are exactly the smooth-gradient-into-steps failure it was
built for.

**Failure modes.** Luma only, so chroma banding in saturated bioluminescent gradients is out of scope.
Frame-local, so a band that *crawls* between frames scores the same as a static one — it needs
§1's residual beside it. Its thresholds assume a display brightness, which must be pinned in the
target profile (§10).

**Availability.** Requires libvmaf. ⚠ **Present since 2026-09-16** (ffmpeg 9.0.1), and measured
rather than assumed: a single-code 8-bit ramp scores **18.95** and its dithered twin **0.00**, which
is the artifact and its remedy in the right order. But the same ramp quantised to 4-code steps or
more also scores **0.00** — a step above its `max_log_contrast` reads as a genuine edge rather than a
band. **CAMBI answers for subtle banding only**; the native `quantisationSteps` proxy answers for the
coarse band, and scores dither as banding. Neither covers the range alone. See ADR-252.

---

## 6. Detail retention and over-sharpening — one signed axis

**Measurement.** Ratio of radially-averaged power spectrum above a cutoff, candidate ÷ reference.
Plus `sharpnessRatio`, the mean gradient magnitude ratio that `tools/sharpness.py` already computes.

**Interpretation, and the reason it is one field rather than two detectors:**

* **< 1** — high-frequency information was lost. Blur, excessive filtering, too-aggressive LOD.
* **≈ 1** — the candidate carries the reference's detail.
* **> 1** — the candidate has *more* high-frequency energy than a better-sampled render of the same
  thing. That is **not** more detail. It is aliasing, or sharpening.

`sharpness.py`'s own header states the discipline this implements: *"a change cannot be accepted on
the flicker number alone: a filter that blurs everything scores perfectly on flicker."*

**Must not be used to conclude** which side of 1 is a defect without `flipMean` beside it. A
deliberate sharpening look and an aliasing artifact are the same number.

---

## 7. Detectors deliberately **not** built as separate things

§4 lists thirty-odd artifact names. Most are not thirty detectors.

| named artifact | where it actually lives |
|---|---|
| pixel crawl, shimmer, edge stair-stepping | §1 residual + §2 Laplacian. Crawl needs the edge to move relative to the pixel grid — which a static camera **can** produce when the *geometry* is animated, as ADR-243 records me getting wrong |
| ghosting, disocclusion artifacts | §1, opposite sign / the mask |
| specular instability, bloom instability | §3, emission/roughness mask |
| LOD popping, impostor swaps | §3, identifier mask |
| foliage instability | §3, vegetation mask |
| particle instability | §3 — and note ADR-015's limit: alive order is **slot order**, so alpha-blended particle systems are unsorted and a residual there has a floor |
| temporal noise, flicker, luminance instability | §4 |
| moire, texture aliasing, undersampling | §2 + §6 |
| gradient discontinuities | §5 |
| over-sharpening, excessive blur | §6, one signed axis |
| **motion-vector errors** | the optical-flow cross-check in §1 — a **finding**, not a quality number |
| HDR clipping | `chromaRetention` + the emission AOV, which is pre-tone-map by construction |
| animation sliding, shadow swimming/popping, volumetric banding/noise | **not detectable with today's AOVs.** Recorded as gaps, not approximated |

**Three names for one measurement is how a suite becomes unfalsifiable.** Each row above is a *view*
of a detector, with its own visualization and its own report line, not its own arithmetic.

---

## 8. Diagnostics are mandatory, not a feature (§17, §32)

Every detector emits an image. This is not presentation — §44 and ADR-243 are the argument:

> A metric is evidence, not truth. Always preserve metric + diagnostic visualization + human
> interpretation.

Per run: `reference.png`, `candidate.png`, `difference.png`, `flip-error.png`,
`temporal-residual.png`, `disocclusion-mask.png`, `edge-map.png`, `aliasing-map.png`,
`specular-instability.png`, `banding-map.png`; and for sequences `candidate.mp4`,
`artifact-overlay.mp4`, `temporal-residual.mp4`. Plus, for every pooled number, the **worst frame's
index** and its tile map.

There is precedent in this repository for why this pays. `docs/post-artifact-forensics.md` localised
the anamorphic lattice to post stage 6 by capturing every intermediate target — `PostProcessor::armCapture()`,
which already exists and which nothing outside a test uses — after **three plausible fixes had been
shipped and reverted** without localising anything. Its opening rule generalises exactly:

> **A post chain judged on its final frame cannot be debugged.**

The same is true of a quality vector judged on its pooled numbers.

---

## 9. Phasing

| phase | detectors | gated on |
|---|---|---|
| **2** | §2, §6, plus the full-reference metrics | the vertical slice |
| **3** | §1 and its disocclusion mask; §4 re-scoped | §1's control arms passing |
| **4** | §3's four masks; §5 if ffmpeg arrives | the material-id → class mapping; the shadow-AOV decision |
| later | directional-energy aliasing (§4 of research.md) | only if the full-reference route proves insufficient |

**No detector ships before its control arm passes.** A probe that cannot be shown to fail proves
nothing, and on this project that rule was bought with a normal pass that looked perfect and was
undecodable.
