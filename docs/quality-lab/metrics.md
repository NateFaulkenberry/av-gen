# The quality vector: what is reported, how it is defined, and what each number may not be used to say

Status: **implemented** (spec §8, §9, §16, §19, §34). The selection reasoning is in
[research.md](research.md); the detector algorithms are in
[artifact-detection.md](artifact-detection.md); the code is `tools/quality-lab/`.

> **Amended by implementation.** Three claims in the sections below were written as predictions about
> instrument behaviour and turned out to be wrong when the instruments were built and run. They are
> corrected in place and marked ⚠, and the measurements are in
> [ADR-252](../decisions/ADR-252-the-banding-instruments-are-blind-in-opposite-places.md) and
> [ADR-253](../decisions/ADR-253-the-residual-knows-where-the-pixel-came-from.md). A specification
> that survives contact with its implementation unchanged is a specification nobody tested.

---

## 1. Three rules that govern every entry

**1. A metric is named after what it computes, not after the artifact it is hoped to detect.**
ADR-243 is the reason. "Temporal stability" was the label; "temporal alternation of the luminance
signal" was the measurement; "spatial aliasing on moving geometry" was the artifact the reviewer
objected to. Three different things wearing one name is how a correct measurement came to rank two
remedies backwards. So the field is `temporalAlternation`, not `temporalStability`, and the artifact
class it is licensed to speak about is a separate, explicit property.

**2. Every metric carries its own limitations into the report.** §18 and §19 require it; ADR-243
requires it more. A number without its caveat becomes a false authority the moment it is read by
someone who was not there.

**3. No composite score.** §8, §52. The primary result is a **vector**. A later optimizer may
combine dimensions under an explicitly stated objective; the Quality Lab does not, and does not
publish weights.

---

## 2. The vector

§8 gives a candidate field list and says, in bold, *"The exact structure must be determined during
research. Do not use these fields blindly."* This is the determined structure. Differences from §8's
sketch are justified in §3.

```
QualityVector
├── spatial            msSsim, ssim, psnr, psnrHvs, flipMean, flipP95, ciede2000Mean, ciede2000P95
├── temporal           temporalAlternation, motionCompensatedResidual, disocclusionFraction
├── perClass           specularResidual, shadingResidual, lodIdentifierChurn, vegetationResidual,
│                       shadowStability (+ a coverage field beside each masked one)
├── detail             spatialLaplacian, detailRetentionRatio, sharpnessRatio
├── color              chromaSpeckle, chromaRetention, ciede2000Mean
├── banding            cambi
├── delivery           vmafMean, vmafP5, vmafMin        (compression resilience only)
├── cost               gpuFrameMsMin, cpuPhaseMsMin, structuralCounters, contentionWitness
└── provenance         gitCommit, sequenceHash, frameHashes, referenceRecipe, sourceFile per metric
```

Every leaf is accompanied, in the JSON, by `{ value, unit, source, pooling, limitations[],
available }` — `available: false` with a reason is how a metric that needs an absent tool reports
itself (§19 `limitations`), rather than being omitted or defaulted to zero.

### 2.1 Spatial fidelity

| field | definition | pooling | limitations |
|---|---|---|---|
| `msSsim` | multi-scale structural similarity, candidate vs reference, on sRGB luma | mean **and** 5th percentile over frames | responds identically to *more aliasing* and *more genuine detail*; must not decide an AA comparison alone |
| `ssim` | single-scale SSIM | mean | superseded by `msSsim`; reported for continuity with external tooling |
| `psnr` | 10·log₁₀(MAX²/MSE) | mean | **not a quality metric here.** Its job is the alignment test: identical renders give ∞ |
| `psnrHvs` | CSF-weighted DCT-domain PSNR | mean | assumes a viewing condition it is not told; no decision authority |
| `flipMean`, `flipP95` | ꟻLIP error, mean and 95th percentile over pixels, then over frames | pixel P95 first, then frame mean and frame P95 | per-frame; edge-weighted by design, so a change that moves only flat regions is under-reported *on purpose* |
| `ciede2000Mean`, `ciede2000P95` | ΔE₀₀ between candidate and reference | as above | defined for surface colour under a reference illuminant; used here on tone-mapped emissive content, so **relative between arms only** |

**Percentiles are not decoration.** A three-frame pop in a three-hundred-frame sequence is invisible
in a mean, and popping is an artifact class we are specifically hunting. Every pooled metric reports
a tail statistic beside its mean.

### 2.2 Temporal

| field | definition | limitations |
|---|---|---|
| `temporalAlternation` | mean and **peak** of \|x(t+1) − 2x(t) + x(t−1)\| over luma, per pixel then pooled — `tools/temporal_stats.py`'s measure | **ADR-243: anti-correlated with human judgment on a static camera over wind-animated sub-pixel geometry**, where it ranked both remedies backwards. **ADR-257: on a moving camera it does not invert and does not discriminate** — a coin flip (26 of 58 frames) on the arm a blind reviewer called *"immediately obvious"*. Never reported without `spatialLaplacian` beside it. Never used alone to choose work |
| `motionCompensatedResidual` | \|frame(t+1) − warp(frame(t), velocity(t+1))\| over pixels **not** masked as disoccluded | inherits every velocity-buffer defect; the disocclusion mask is the whole validity of the number |
| `disocclusionFraction` | the fraction of pixels excluded by that mask | **a diagnostic, not a quality number.** A high value means the residual covers less of the frame, and a residual computed over 4% of the frame is not a statement about the frame |

### 2.3 Per-class stability (AOV-gated)

One residual, four masks. See [artifact-detection.md](artifact-detection.md) §3.

| field | mask | limitations |
|---|---|---|
| `specularResidual` | emission AOV above threshold, or roughness (normal AOV alpha) below threshold | emission is not specular; a rough emissive surface is in the mask and should not be |
| `shadingResidual` | pixels whose **normal did not change** but whose colour did | a normal that changed by less than the half-float epsilon reads as unchanged |
| `lodIdentifierChurn` | pixels where the `id` AOV changed while depth and velocity say the surface did not move | a genuine object swap at a silhouette is indistinguishable from a LOD pop without more information |
| `vegetationResidual` | ⚠ `id` AOV restricted to the object ids `materials.json` classifies as vegetation — **not** to "material identifiers", which the identifier AOV does not carry (ADR-259) | a scatter layer whose asset has no library category falls back to a keyword table over its filename; on Glowmere that reads `pebbles` and `beacons` as vegetation |
| `shadowStability` | the shadow AOV's key-light visibility below 0.5, excluding the sky by its depth | ⚠ **the AOV is a second opinion, not a capture**: at `offline` the engine builds no shadow mask, so `--aov shadow` recomputes the term. Measured at **0.43% of pixels differing** from the lit pass's own, peak 22/255, against a control that moves 1.86% (ADR-258) |

**Both of these were "absent, with a reason" until 2026-09-17 and both are now numbers** — with the
reasons converted into limitations that travel inside the metric rather than sitting in a document
beside it. On Glowmere at 640×360, against an ungated residual of 1.114: `shadowStability` **3.149
over 4.55%** of the frame, `vegetationResidual` **2.800 over 18.74%**. Shadowed pixels are 2.8× less
stable than the frame average and vegetation is 2.5×, which is the first thing either dimension has
said. Neither is validated against a person; §4.2 is the only metric here that is.

**Read the coverage or do not quote the number.** `perClass.shadowCoverage` and
`perClass.vegetationCoverage` are emitted beside them for the reason every masked metric here has
one: a residual over 4.55% of the frame is a statement about 4.55% of the frame.

### 2.4 Detail, colour, banding

| field | definition | limitations |
|---|---|---|
| `spatialLaplacian` | mean \|4c − left − right − up − down\| over luma, interior pixels only — `tools/spatial_stats.py` | **cannot separate aliasing from detail.** Meaningful only between arms of one view; never between scenes; never as an absolute bar. **ADR-257: validated against a blind human ranking** of three anti-aliasing arms on a moving camera, matching on both arms and on 60 of 60 frames — with one sensitivity point in each direction and no more: 6.5% invisible at 1× playback, 16.9% *"immediately obvious"* (§4.2) |
| `detailRetentionRatio` | ratio of radially-averaged power above a cutoff, candidate ÷ reference | > 1 means *more* high-frequency energy than the reference — which is aliasing or sharpening, not detail. **The sign must be read with `flipMean` beside it** |
| `spatialLaplacianRatio` | `spatialLaplacian(candidate) ÷ spatialLaplacian(reference)` | ⚠ **the sign depends on the scene, measured both ways.** On `examples/quality/aliasing-dolly` — 2 cm fence slats crossing the sampling limit — the candidate is **1.22×** the reference, which is the aliasing the reference resolved away. On a single smooth orb at 96×64 it is **0.82×**, because there is no staircase to resolve and supersampling instead brings more genuine shading detail into the frame. Both are correct measurements of the same quantity. This is why "it cannot separate aliasing from detail" is not a hedge in a header: it flips the comparison's sign |
| `sharpnessRatio` | mean gradient magnitude ratio — `tools/sharpness.py` | *"a filter that blurs everything scores perfectly on flicker"*; this is the counterweight, and it is a ratio, never an absolute |
| `chromaSpeckle` | opponent-chroma (R−G, G−B) 4-neighbour speckle — `tools/chroma_speckle.py` | the metric ADR-212 quotes; comparable only between arms of one view |
| `chromaRetention` | mean saturation of the candidate ÷ the reference in highlights | measures the tonemap's chroma path, which is an authored look as much as a defect |
| `cambi` | Contrast-Aware Multiscale Banding Index | luma only; frame-local, so a *crawling* band scores as a static one; thresholds assume a display brightness that must be in the target profile. ⚠ **Measured: it detects SUBTLE banding and is blind to coarse posterisation** — quantising a ramp to 4-code steps or more takes it to 0.00, because a step above its `max_log_contrast` reads as a genuine edge (ADR-252) |
| `quantisationSteps` | the fraction of pixels showing a 3-to-12 luma step in an otherwise flat neighbourhood — the native proxy, named for what it computes | ⚠ **Two measured failures, both kept in writing rather than reweighted (§4.3).** It is blind to ordinary single-code 8-bit banding, which CAMBI scores 18.95; and it scores a ±2-code **dither** — the remedy — at 0.031 against the banded frame's 0.000, so it is anti-correlated with the fix. Kept because it answers for the coarse band CAMBI cannot see. Where `cambi` is available, `cambi` is the banding number |

### 2.5 Delivery

`vmafMean`, `vmafP5`, `vmafMin` — **candidate = the encoded delivery file, reference = the master**.
This is the one place VMAF is in its training domain, and the only place it is permitted to rank
anything. It is *not* used to compare renderer configurations; see [research.md §1.1](research.md).

### 2.6 Cost, and why it is reported the way it is

§22 wants render cost recorded. ADR-170 says what an honest cost record looks like on a shared
machine:

* `gpuFrameMsMin` — **min over a long run**, from `gpu::FrameTimeline`. Contention is never negative,
  so the minimum is the honest estimate of the work and the median walks with whatever else is
  running.
* `cpuPhaseMsMin` — the same, per phase, from `core::PhaseProfiler`, which already reports min for
  exactly this reason. **These two are never added together and never confused**: the header is
  explicit that one is GPU timestamps and the other is CPU wall clock on one thread.
* `structuralCounters` — triangles, instances, draw calls, entity full/coarse/skipped, rig
  posed/rate-limited/culled. ADR-170's decisive observation: *"the structural quantities moved 3.6%
  and the timings moved 280%."* **These are comparable across sessions; the timings are not.**
* `contentionWitness` — the `pgrep avgen` result and the load average, taken beside the numbers. A
  run that cannot say the device was quiet **reports its milliseconds as a record of having taken
  them, not as a measurement.**

**And the protocol, not just the fields:** a comparison between two invocations of the same binary
has a ~3 ms noise floor on this machine. Where a real cost comparison is needed, arms must be
**interleaved inside one process** — `--ab` for the renderer, `PhaseProfiler::setFrameGroup` for the
main thread. A quality/cost frontier built from separate invocations is a frontier built from noise.

---

## 3. What was dropped from §8's sketch, and why

§8 names nineteen fields. This structure keeps most of them and changes these:

| §8 field | disposition |
|---|---|
| `lpips` | **dropped.** ImageNet features on stylized synthetic content; per-frame only; named in the rendered-VQA literature as poorly correlated; ꟻLIP occupies the slot without PyTorch |
| `temporalStability` | **renamed** `temporalAlternation`. ADR-243: the name was the error |
| `edgeStability` | **merged** into `motionCompensatedResidual` + `spatialLaplacian`. A separate "edge stability" number would be a third name for one of these two |
| `shadowStability` | ⚠ **was absent, now present and labelled.** `--aov shadow` exists (ADR-255/258) and recomputes the term rather than capturing it, so the number ships with its 0.43% disagreement stated |
| `bloomStability`, `hdrStability` | **merged** into `specularResidual` (emission-masked) and `chromaRetention`. Separate detectors for these would be the same residual under the same mask |
| `spatialAliasing` | **renamed** `spatialLaplacian`, because that is what it computes, and it cannot separate aliasing from detail |
| `gradientQuality` | **replaced** by `cambi`, which is a validated instrument for the same question |
| `compressionResilience` | **kept**, as the `delivery` group |
| — | **added**: `disocclusionFraction`, `detailRetentionRatio`, `lodIdentifierChurn`, `contentionWitness`, per-metric `available` |

---

## 4. Validation: every metric must be shown capable of failing (§34, ADR-182)

This is the load-bearing section, because the product is measurement. **No metric enters the vector
until it has both arms below.**

### 4.1 The distortion ladder

For each metric, a set of deliberately constructed image pairs, generated on the CPU from one
rendered frame so the only variable is the distortion:

| arm | construction | the metric that must move | the metrics that must **not** |
|---|---|---|---|
| **identity** | the same file twice | none — `psnr` = ∞, `msSsim` = 1, `flipMean` = 0 | all of them |
| slight blur | 3×3 Gaussian, σ = 0.5 | `detailRetentionRatio` < 1, `sharpnessRatio` < 1, `flipMean` > 0 | `cambi` |
| severe blur | σ = 3.0 | the same, much further | `cambi` |
| aliasing | nearest-neighbour ½ downsample then ×2 up | `spatialLaplacian` ↑, `detailRetentionRatio` **> 1** | `chromaSpeckle` should barely move |
| over-sharpen | unsharp mask | `sharpnessRatio` > 1, `detailRetentionRatio` **> 1**, `flipMean` > 0 | `cambi` |
| banding ⚠ **corrected** | ~~quantise to 5 bits on a gradient region~~ → **a smooth single-code ramp against its dithered twin** | **`cambi` 18.95 → 0.00 for the dither** | `sharpnessRatio` ≈ 1. The original arm had the direction backwards: coarse quantisation drives CAMBI to **zero**, not up (ADR-252). The dither arm is also the direction a renderer's own remedy moves in |
| banding blind spot ⚠ **added** | the same pair, under `quantisationSteps` | **nothing — and that is the assertion.** 0.000 banded, 0.031 dithered | pinned so the blind spot cannot drift into a trusted zero |
| colour shift | +2 ΔE₀₀ in a\*b\* | **`ciede2000` ↑** | `msSsim` ≈ 1, `spatialLaplacian` ≈ 1 |
| exposure shift | ×1.05 linear | `psnr` collapses | `msSsim` barely moves — **this arm is why PSNR is not a quality metric, demonstrated rather than asserted** |
| compression | encode at a low bitrate and decode | `vmaf` ↓ | — |
| **shimmer** | a synthetic sequence alternating two sub-pixel-shifted renders | **`temporalAlternation` ↑** | `spatialLaplacian` ≈ 1 per frame |
| **smooth motion** ⚠ **corrected** | a synthetic sequence translating one frame by 1 px/frame | `motionCompensatedResidual` ≈ **0.000**; but `temporalAlternation` is **31.3**, not ≈ 0 | this is the control that makes the shimmer arm non-vacuous — **and the correction is the whole point.** The second difference in time of translating content is not zero, so pure authored motion reads as large instability to the measure ADR-243 was using. The prediction that both would be zero was the same mistake ADR-243 records, written into its own control arm (ADR-253) |
| **disocclusion** | a translating sequence with a foreground occluder | `disocclusionFraction` > 0 and `motionCompensatedResidual` **unchanged** after masking | if the residual moves, the mask is broken |

**And the ladder itself has a control.** `runSpatialLadder` takes its metrics as bindings so a
deliberately broken metric — `spatialLaplacian` replaced by a constant — can be substituted and the
ladder required to fail. Without it, "the ladder passes" is a sentence with no information in it, and
a validator that passes whatever it is given is ADR-182's failure one level up from where ADR-182
found it. The test asserts the *targeted* arms fail while the identity arm still passes, so it is a
sabotage and not a global break.

The identity arm and the smooth-motion arm are the **controls**. ADR-182's rule is that a probe must
be shown capable of failing; ADR-242's test is the precedent — every assertion is a property only the
right answer has, *"because a pass that writes a plausible-looking wrong buffer is the failure this
repository keeps writing ADRs about."*

### 4.2 The direction check against a human — **and it has now been run twice**

Separate from §4.1 and not replaceable by it. ADR-243's arms are the first regression case the
Quality Lab must reproduce, because the answer is already known:

| arm | the reviewer said | a metric set that is fit for purpose must |
|---|---|---|
| FXAA off | *"more distracting"* | rank it **worse** — `spatialLaplacian` does (+31%) |
| supersample 2× | *"much calmer"* | rank it **better** — `spatialLaplacian` does (−9%) |
| baseline | *"probably not"* ship it | sit between them |

Any composite, any weighting, any new detector that ranks these three in a different order is
**wrong**, and the fix is the detector. §44: *"Do not 'fix' the human by changing the weighting until
the human agrees."*

⚠ **This gate is no longer a regression case inherited from another scene.** Phase 6
([ADR-257](../decisions/ADR-257-the-eye-ranked-them-the-way-the-spatial-measure-did.md)) put the same
three arms of `examples/quality/aliasing-dolly` in front of a reviewer at 1280×720, **blind**, with
the prediction registered beforehand, and they ranked **C > B > A** — supersample, baseline, FXAA
off — which is `spatialLaplacian`'s ordering in both directions:

| arm | `spatialLaplacian` | vs baseline | paired per frame | the reviewer |
|---|---:|---:|---|---|
| FXAA off | 9.5991 | **+6.5%** | worse on **60 of 60** frames | *"I would object to the worst clip"* |
| baseline | 9.0103 | — | — | *"Better"* than FXAA off, in the close-up |
| supersample 2× | 7.4912 | **−16.9%** | better on **60 of 60** frames | *"Immediately obvious"*, *"Best"*, shippable |

Two things that gate now carries and did not before:

**A sensitivity bracket, with one point in each direction.** The **6.5%** difference was *not* visible
at 1× playback on a moving camera and *was* visible in a frozen close-up; the **16.9%** difference was
*"immediately obvious"*. One shot, one reviewer, one sitting. It is a bracket and not a threshold, and
quoting it as a threshold is the failure §44 is about.

**A resolution qualifier, which is general.** `temporalAlternation` passes this gate at 640×360 and
fails it at 1280×720 on the same arms and the same window — it calls supersampling worse on 26 of 58
frames there, a coin flip on the arm the reviewer was surest about. **A metric set must meet §4.2 at
the delivery resolution.** An instrument's effect size is evidence only at the size it was measured
at, and a metric validated at preview size has been validated for preview size.

### 4.3 Where a metric fails the ladder

It is **recorded as a limitation and kept or dropped on the merits**, never quietly reweighted. §34:
*"Does the metric behave sensibly? If not, document it. Do not force a metric into the system because
it is popular."*

### 4.4 The human protocol

§4.2 is only worth what the review that produced it was worth, so the review has rules of its own.
These are what Phase 6 ran; ADR-257 is the record of what each one caught.

1. **The prediction is registered before the reviewer looks.** In writing, with numbers. A metric
   that is consulted afterwards agrees with everything.
2. **Blind labels, mapping withheld.** A/B/C, disclosed after the answers are written.
3. **Distinct sequence hashes.** Two arms that hash identically are void, not equal — and the arms
   the reviewer watches are the arms that were measured, which is worth checking by re-rendering and
   comparing the hash rather than assuming.
4. ⚠ **The control is a duplicated arm, not a region of the frame.** One arm shown twice under two
   labels: a reviewer who reports a difference between a clip and itself has told you the review's
   resolution, and nothing else calibrates the other answers. **A region of the frame cannot do this
   job in an aliasing review** — anything visible has a silhouette, and a silhouette is an edge. The
   first version of this protocol offered the aliasing scene's smooth orb as the control; the
   reviewer correctly reported that it differed, and it does (ADR-254's correction).
5. **Four questions about the footage, asked separately and in this order**: the *ranking*; the
   *magnitude* and how long it took to be sure; *where* in the frame it was seen; and the *shipping
   decision* — would you object to any of these. With rule 4's control question and one open field at
   the end, that is six, which is what Phase 6 cost. The magnitude question is what produced the
   sensitivity bracket above, and the location question is what makes a ranking checkable against a
   scene built to put the artifact somewhere specific.
6. **An invitation to contradict the instructions**, and it must be a real one. Phase 6's control
   question told the reviewer what it expected them to see and asked them to say so loudly if they
   disagreed. They did, and they were right, and the instructions were wrong. A review that cannot
   produce that answer is a review nobody can learn from.

---

## 5. Pooling and frame selection (§31)

* Default: **every frame** for sequences under 300 frames; every 2nd above that, with the sampling
  recorded in the manifest.
* **Final validation runs are exhaustive.** A subsampled run may not be cited as a regression check.
* Reported per metric: `mean`, `p5` or `p95` (whichever is the bad tail for that metric), `min`/`max`,
  and the **index of the worst frame**, so §32's "show me what caused this number" has somewhere to
  start.
* **Never mean alone.** The worst frame is usually the finding.
