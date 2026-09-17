# ADR-253: The residual knows where the pixel came from, and that is the whole difference

**Status:** Accepted
**Date:** 2026-09-16
**Context:** Quality Lab Phases 3 and 4 — the motion-compensated residual, the disocclusion mask and
the AOV-gated per-class masks
**Follows:** ADR-243 (the detector and the eye disagree), ADR-250 (the instrument is not the engine),
ADR-242 (a target nobody reads is not a feature), ADR-182 (a probe that cannot fail proves nothing)
**Narrows:** ADR-243's finding, on the axis ADR-243 itself named as unexamined

## Context

ADR-243 is the reason the Quality Lab exists. A correctly implemented temporal detector — the second
difference of luma in time — ranked two anti-aliasing remedies backwards against a human reviewer, on
both arms, with non-vacuous renders and correct arithmetic. It measured temporal alternation; the
reviewer was objecting to spatial aliasing on moving geometry; and both remedies for the second raise
the first.

`artifact-detection.md` §1 names the instrument that was missing: a residual that asks the engine's
own velocity buffer where each pixel came from before differencing, so that **authored motion is
subtracted and only unexplained change is left**. This ADR is that detector, its mask, and what its
control arms established.

## The detector

For consecutive frames *t* and *t+1*: read the velocity AOV of *t+1*, backward-warp *t* by it,
difference against *t+1* over the pixels where the prediction is valid. `screenVelocityAt` writes
`nowUv − beforeUv`, so a pixel's previous position is its own UV minus its velocity; that is read from
`shaders/common.wgsl` rather than inferred, and the smooth-motion arm is what proves it was read
correctly — a sign error there produces a residual of twice the motion, not zero.

Five decisions inside it are worth recording because each one was a choice with a wrong alternative.

**1. The beauty pass is sampled bilinearly and every AOV is sampled *nearest*.** Averaging two
identifiers is a third object and averaging two depths across a silhouette is a surface that is not
there — ADR-242's reason for refusing a supersampled AOV, and it applies just as exactly to a warp
lookup as to a downsample.

**2. Disocclusion is two independent tests, and the report says which one fired.** The identifier test
(`id(t+1)(p) ≠ id(t)(p−v)`) is exact where identifiers are exact, and the identifier target is
R32Uint, so it is exact. The depth test catches the same-object-different-surface case a silhouette
produces, which the identifier test cannot see. A mask with only one of them is one silhouette away
from being wrong, and the ladder asserts that turning the identifier test off leaves the depth test
still firing.

**3. `disocclusionFraction` is a validity gate and is reported beside the residual always.** A
residual computed over 4% of the frame is not a statement about the frame. The same rule governs the
per-class residuals, which report their mask's coverage in the row beneath them: the specular
residual on the Quality Lab's aliasing scene is 22.5 over **0.04%** of the frame, and without the
second number the first one is a headline about forty pixels.

**4. The warp floor is measured, not assumed.** Bilinear prediction is itself a resample, so a
residual has a floor above zero. `warpFloor` measures it directly by warping a frame back and forward
with the same velocity, with no renderer in the loop. On the synthetic control a whole-pixel warp
costs 1.2×10⁻⁶ luma steps and a half-pixel warp costs 7.8. The first number is the velocity's float
encoding — and the engine's target is RG16Float, coarser still — and the second is the resample. A
residual is compared against its control, never against zero.

**5. Per-class stability is one residual under four masks, not four detectors.** Three names for one
measurement is how a metric suite becomes unfalsifiable. The control for the gating machinery is that
a residual gated over *every* pixel must equal the ungated residual.

### The bug the zero-velocity control found

The first bilinear sampler rejected any sample whose `x0 + 1` tap fell off the edge. At zero velocity
that rejects the entire last row and column — on a 96×64 frame, **2.6% of pixels reported as
disoccluded by a static camera looking at a static scene**. The fix clamps the tap (whose weight is
zero there) and not the coordinate. It is a small bug and it is exactly the class this project keeps
writing ADRs about: a mask that fires on a frame where nothing happened will fire on every frame, and
nothing about the number's *appearance* would have said so.

## The control arm that is ADR-243 in miniature

The load-bearing arm is **smooth motion**: a grating translating exactly one pixel per frame, with the
velocity that describes it. Measured:

| | value |
|---|---:|
| motion-compensated residual | **0.000** |
| disocclusion fraction | 0.021 |
| **temporal alternation (same footage)** | **31.3** |

One sequence, two instruments, opposite verdicts. The content is pure authored translation — there is
no defect in it at all — and the second-difference measure reports a large number because the second
difference in time of translating content is not zero. **That is the whole of ADR-243's mechanism,
reproducible in a unit test in under a second, with no renderer and no reviewer.**

The other arms, each with its control: shimmer (residual 19.1, per-frame spatial energy unchanged
within 0.6%, so it is shimmer and not blur); ghosting (5.3× the smooth-motion control); disocclusion
(the mask fires on 3.1% of pixels, and **unmasked the residual is 0.65 against the masked 0.000** — so
masking is what makes it small, which is a property only a working mask has); zero velocity (residual
0.000, disocclusion 0.000, alternation 0.000).

## What the aliasing benchmark then measured, and how it narrows ADR-243

Three arms on `examples/quality/aliasing-dolly.json` at 640×360, 30 frames, all three sequence hashes
distinct, each analysed against the same supersampled reference. Under the GPU lock; **no timing is
claimed** — the load average was 12-15 throughout and ADR-170's rule is that structural quantities
survive contention and milliseconds do not.

| arm | `spatialLaplacian` | vs baseline | `temporalAlternation` | vs baseline |
|---|---:|---:|---:|---:|
| t1 baseline (FXAA 0.75) | 14.458 | — | 17.803 | — |
| t2 FXAA off | 15.703 | **+8.6%** | 18.287 | **+2.7%** |
| t4 supersample 2× | 11.829 | **−18.2%** | 16.832 | **−5.4%** |

And one result from the GPU integration test that is worth more than it looks. Comparing a native
render against its 2× reference, `spatialLaplacian` runs **1.22×** on the aliasing scene — the
candidate carries more, which is the staircase the reference resolved away — and **0.82×** on a
single smooth orb at 96×64, where there is no staircase and supersampling instead brings more genuine
shading detail into the frame. **The comparison changes sign with the scene.** `metrics.md` has said
from the start that this measure *"cannot separate aliasing from detail"*; that sentence is usually
read as a caveat about confidence, and it is not. It is a statement that the number's direction is
not a property of the renderer configuration alone. The test asserts the two differ and deliberately
asserts nothing about which way.

**The spatial measure reproduces ADR-243's ordering on new content with a new instrument**, which is
`metrics.md` §4.2's second gate and it passes: FXAA off is worse, supersampling is better, the
baseline sits between them.

**And the temporal measure agrees, which is not what ADR-243 found.** On Glowmere it ranked FXAA-off
as 42% *better*. Here it ranks it worse, in the same order as the spatial measure, on every arm.

ADR-243 closes by naming exactly this gap: *"a moving camera has not been reviewed at all, and crawl
on static geometry is exactly what a moving camera would produce."* The difference between the two
situations is that one: Glowmere was **wind-animated sub-pixel geometry under a static camera**, and
this is **static geometry under a moving camera**.

**So ADR-243's finding is narrowed, not overturned**, and the narrowing is stated carefully:

* *Measured fact.* On this scene under this motion, the two measures rank the three arms identically.
* *What is not established.* **No human has looked at these frames.** The agreement here is between
  two instruments, not between an instrument and an eye. ADR-243's finding was anchored by a reviewer
  and this one is not, and metrics.md §4.2 does not permit the second to replace the first.
* *Hypothesis, labelled as one.* The anti-correlation lives in the interaction between FXAA's
  per-frame threshold decision and geometry that moves *relative to the sampling grid at the sub-pixel
  scale*, which wind animation produces and a smooth camera translation does not. Confidence:
  moderate. The experiment that would settle it is the same three arms on Glowmere with a person
  watching, which is the owner's time and not an agent's.

## Consequences

**The instrument ADR-243 did not have now exists and is validated against controls that can fail.**
Its first real measurement disagrees with the detector it replaces in the way it was designed to.

**`shadowStability` is still absent and still reports `available: false, reason: "no shadow AOV"`.**
Phase 4 built the four masks it can build; the fifth needs an engine change, and approximating it
from "regions the lighting model says are shadowed" stays refused.

**`vegetationResidual` is `available: false` for a different reason**: the masking mechanism is built
and tested against a synthetic identifier plane, and the **material-id → class mapping does not
exist**. That is a human decision, not a gap in the code.

**What this does not say.** It does not say the motion-compensated residual is correlated with human
judgment. Nobody has tested that, and the single most expensive lesson in this repository is that a
detector can be correct, non-vacuous, well-controlled and still rank remedies backwards. It says the
detector measures what it claims to measure, that its mask is what makes the number valid, and that
the arms which would prove it wrong have been built and run.

---

## Follow-up, 2026-09-17: "the temporal measure agrees" was a statement about 640×360

Appended, not rewritten. The measurement above reproduces exactly and is not withdrawn; the
*conclusion drawn from it* does not survive a change of render size, which this ADR did not test.

This document reports the three `aliasing-dolly` arms at **640×360, 30 frames** and observes that
`temporalAlternation` ranks them in the same order as `spatialLaplacian` — *"which is not what
ADR-243 found."* Re-run at **1280×720, 60 frames**, the size a reviewer actually watched, that
agreement is gone. One variable isolated — same scene, same arms, same 2-second window, same 60
frames, only the resolution:

| | spatial A vs B | spatial C vs B | temporal A vs B | **temporal C vs B** |
|---|---|---|---|---|
| 640×360 | +8.40%, 60/60 frames | −17.65%, 0/60 | +2.31%, 58/58 | −2.97%, **14/58 wrong-direction** |
| 1280×720 | +6.98%, 60/60 frames | −16.90%, 0/60 | +0.62%, 58/58 | −0.74%, **26/58 wrong-direction** |

The spatial measure holds its size and its unanimity at both. The temporal measure's separation of
the supersampled arm shrinks fourfold and degrades to a coin flip. Halving the resolution puts more
of the content below the sampling rate, so supersampling changes more pixels: the effect size is a
property of the render size and not of the renderer.

**The rule that follows is general and this ADR did not state it:** an instrument's effect size is
evidence only at the resolution it was measured at, and the resolution that counts is the delivery
one. [ADR-257](ADR-257-the-eye-ranked-them-the-way-the-spatial-measure-did.md) carries the rest of
it, including the human ranking this ADR correctly said it did not have.
