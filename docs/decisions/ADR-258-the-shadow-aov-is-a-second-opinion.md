# ADR-258: The shadow AOV is a second opinion, measured at 0.43% of the frame, and it ships saying so

**Status:** Accepted — implemented
**Date:** 2026-09-17
**Context:** Implementing ADR-255's option B, and running the fidelity experiment it required
**Follows:** ADR-255 (the shadow AOV and the tier that has no shadow texture), ADR-087 (the
half-resolution shadow mask), ADR-242 (a target nobody reads is not a feature), ADR-182 (a probe
that cannot fail proves nothing), ADR-250 (the instrument is not the engine)
**Amends:** ADR-255's `validate()` clause, which cannot live where it said

## Context

ADR-255 decided option B — a dedicated full-resolution shadow pass gated on `--aov shadow`, because
`shadowMaskScale = 1.0` at `high` and `offline` means the engine's own mask is never built at exactly
the tiers the Quality Lab measures. It was explicit that option B's weakness must be **measured and
not inherited**: the pass *recomputes* the term rather than capturing it, and ADR-087's claim that
its mask is *"exactly the combined visibility the lit pass would otherwise compute"* is a claim.

It is built. This ADR is what the measurement said, and the two refusals the work produced — one of
which ADR-255 asked for, and one of which it could not have known about because it was found by
running the arm.

## The experiment, and the control that made it mean anything

The mask pass had no configuration in which it ran at full resolution *and* the lit pass read it, so
the experiment needed one: `--quality-arm maskconsume`, a diagnostic no tier sets and nothing ships.
Beside it, ADR-182's positive control: `--quality-arm shadowatlas1k`, the shadow atlas halved, which
**must** move both arms — an agreement measured by an instrument that cannot disagree is not
agreement.

Glowmere (`examples/world/terrain.json`), 640×360, one frame at t = 8 s, offline tier, under
`tools/gpu-lock.sh`. **All four arms hashed differently**, so none was vacuous:

| arm | sequence hash |
|---|---|
| baseline (the term computed inline, as `offline` does) | `efce5e80be9b9223` |
| `maskconsume` (the mask pass at full resolution, consumed) | `f8e566bcd7ff0e2c` |
| `shadowatlas1k` | `418390e27fa6c782` |
| `maskconsume,shadowatlas1k` | `0a3c187916ce0934` |

| comparison | pixels differing | max \|Δ\| of 255 | mean \|Δ\| |
|---|---:|---:|---:|
| **consumed mask vs inline** | **0.43%** | **22** | 0.0074 |
| CONTROL: atlas 1k vs 2k, inline | 1.86% | 27 | 0.0439 |
| CONTROL: atlas 1k vs 2k, masked | 1.93% | 30 | 0.0443 |
| identity | 0.00% | 0 | 0.0000 |

**So the answer is "close, real, and not exact."** The mask agrees with the lit pass on 99.57% of
pixels and disagrees on the rest by up to a twelfth of the range, against a control that moves four
times as many pixels. ADR-255 pre-registered what to do with each outcome, and this is the second
one: **`shadowStability` ships as an approximation that names itself one**, in the metric's own
limitations rather than only in a document.

### Two arms that narrow the cause, and one hypothesis that is labelled as one

| arm | pixels differing |
|---|---:|
| mask vs inline, **PCSS off** (plain PCF) | 0.38% |
| mask vs inline, **contact march off** | 0.73% |

Neither suspect survives. Turning off the PCSS blocker search barely moves the disagreement, so it is
not the blocker search. Turning off the screen-space contact march makes the disagreement **larger**,
which is what you would expect if the march were darkening pixels in both arms and hiding part of a
difference that was already there — it is not the march either. ADR-087 said the march is not in the
mask, and this is that statement showing up as an arithmetic consequence rather than as a comment.

What is left is the one difference the shader's own header flags: **this pass reconstructs its
shading position and normal from the depth buffer, and the lit pass uses the interpolated shading
normal.** The normal feeds the shadow lookup's normal-offset bias, so a reconstruction a degree or
two out moves the sample point, and pixels near the acne threshold flip. That explains a difference
that is concentrated in 0.43% of pixels at high amplitude rather than spread thinly everywhere.
**Confidence: moderate. It is a hypothesis and it is not tested here.**

## What shipped

### 1. The pass runs when asked, at full resolution, and is not consumed

`ShadowMaskRenderer::setExportRequested(true)` runs the pass at scale 1.0 whatever the tier says,
and **`active()` stays false** — so `frame.shadowMaskParams.x` stays 0, the frame bind group keeps
the same 1×1 white texel it binds today, and the lit pass computes the term inline exactly as it did
before. Measured, not asserted: Glowmere at 640×360 renders **`efce5e80be9b9223` with and without
`--aov shadow`.** An AOV that changed the deliverable would not be an AOV *of* it.

`needsDepthPrepass` now asks whether a pass was **encoded**, not whether the lit pass will read it.
`active()` there would have produced a shadow plane computed against a depth target nobody filled —
the same class of bug as everything else in this file.

### 2. ADR-255's `validate()` clause is right and its address is wrong

ADR-255 says *"`validate()` gains a clause: `--aov shadow` on a scene with no directional light must
refuse."* `RenderSettings::validate()` has no scene and cannot have one — it validates a settings
struct that is written into a project file long before anything is loaded. The clause lives instead
in **`shadowAovPreconditions(scene, disabledPasses)`**, a free function beside the settings and
called from `RenderJob::start()`, which is the first point where both the scene and the flags exist.
It is a free function rather than a member so it can be tested without a GPU: **a refusal nobody can
exercise is a refusal nobody knows still works.**

It is also stricter than ADR-255 asked. The condition is not "a directional light" but **"a
directional light that is enabled and casts"**: a key light with `castsShadow` off produces the same
constant 1.0, and refusing only the first case would have left the second silently exporting it.

### 3. The refusal ADR-255 could not have predicted, because it was found by running the arm

`--disable shadows` skips the cascade passes, so the shadow atlas is never drawn. The mask pass
samples it anyway — and **an undrawn depth atlas does not read as "unshadowed". It reads as an
occluder in front of everything.** Measured on Glowmere: the exported plane marks **28.9%** of the
frame shadowed, against **4.6%** in the same render with shadows on.

Read naively that is a finding — "disabling shadows puts the scene in shadow" — and it is an
inversion of the truth, in a valid EXR of the right size and format. It is ADR-182's exact shape
arriving inside a feature written to serve ADR-182's own instrument. The combination is refused, with
the measurement in the error message so the next reader does not have to rediscover it.

**The arm itself was not wasted.** It is the strongest evidence that the exported plane is actually
driven by the shadow atlas rather than by anything else in the frame: switch the atlas off and the
plane changes beyond recognition. It is a good probe and a broken configuration, and those are
different things.

### 4. `shadowStability` is a number, with its coverage beside it and its caveat inside it

On Glowmere at 640×360, 12 frames, against a supersampled reference:

| metric | value | coverage |
|---|---:|---:|
| `temporal.motionCompensatedResidual` (ungated) | 1.114 | the frame |
| **`perClass.shadowStability`** | **3.149** | **4.55%** |

Shadowed pixels are **2.8× less stable** than the frame average. That is the first thing this
dimension has ever said, and the qualification travels in the metric's `limitations`, not in an ADR
nobody reads at the same time as the number: *the shadow AOV is a second opinion, not a capture*,
with the 0.43% and the control beside it.

The mask is the **key light's** visibility below 0.5, and that threshold is a decision: a penumbra is
a continuum and where a soft edge stops being shadow is a number somebody chose. The sky is excluded
by its **depth**, not by its visibility — the pass writes unshadowed white there, so a threshold
alone would be right about the sky by accident and wrong the day the pass writes anything else.

## Consequences

**Nothing changes for a render that does not ask.** `setExportRequested` defaults false, `update()`
returns early exactly as before, and the realtime frame is untouched — which is what ADR-250 asks for
and what option A could not have offered.

**The GPU test asserts what its scene can establish, and says what it cannot.** The render-job
fixture is one convex orb over nothing, and the shadow-map term of a sphere with no receiver is the
constant 1.0 — its own far side is back-facing and `shadowFactor` returns lit before it looks
anything up. The first version of the test asserted the plane varied and **failed**, which is
ADR-254's rule arriving from the test side: a scene that cannot show the artifact cannot exercise the
probe that looks for it. The test now asserts the arm that scene *can* carry — the beauty frames are
byte-identical with and without the flag, and the depth channel varies, which is what separates a
pass that ran from the 1×1 placeholder — and names Glowmere as where the non-constant arm lives.

**What this does not say.** It does not say ADR-087's mask is wrong: 99.57% agreement at full
resolution, on a term that ships at half resolution for a picture nobody differenced, is the claim
holding up rather than failing. It does not say the 0.43% is invisible — nobody has looked at the two
frames side by side, and after ADR-257 that distinction is one this repository takes seriously. And
it does not establish the normal-reconstruction hypothesis: that is an experiment somebody has not
run, named so it can be.
