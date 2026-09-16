# ADR-252: The banding instruments are blind in opposite places, and VMAF ranks a defect above its own reference

**Status:** Accepted
**Date:** 2026-09-16
**Context:** Quality Lab Phase 2 — wiring the optional external metrics now that ffmpeg 9.0.1 with
libvmaf is installed
**Follows:** ADR-250 (the instrument is not the engine), ADR-243 (the detector and the eye disagree),
ADR-182 (a probe that cannot fail proves nothing)
**Amends:** `docs/quality-lab/metrics.md` §4.1 (the banding arm), `docs/quality-lab/architecture.md`
§10 (Phase 2 ships without VMAF and CAMBI)

## Context

ADR-250 specified the Phase 2 vertical slice *without* VMAF, PSNR-HVS or CAMBI, for the plain reason
that the machine had no ffmpeg. That is no longer true. The degradation contract in §2 of that ADR
did exactly what it was for — the day the tool arrived the metrics became reachable without any
design changing — and the job became wiring them in.

Wiring them in meant first putting them on the ladder, because ADR-182 does not have an exemption for
metrics somebody else wrote. Four arms on a 640×360 eight-bit luminance ramp, ten frames each: the
ramp itself, the ramp dithered, and the ramp quantised to coarser steps.

## What was measured

ffmpeg 9.0.1, libvmaf, `format=yuv420p`, model as stated. `cambi` is no-reference and is computed on
the main input; the VMAF column is the main input scored against the *undithered ramp* as reference.

| arm | CAMBI | VMAF `v0.6.1` | VMAF `v0.6.1neg` |
|---|---:|---:|---:|
| the ramp, against itself (**identity**) | 18.95 | **97.34** | 97.34 |
| the ramp **dithered** (±6, random) | **0.00** | 91.84 | 90.70 |
| quantised to 2-code steps | 11.45 | 96.38 | — |
| quantised to 4-code steps | 0.00 | **100.00** | 90.90 |
| quantised to 8-code steps | 0.00 | **100.00** | — |
| quantised to 16-code steps | 0.00 | **100.00** | 88.59 |

Three findings, each with the control that makes it mean something.

### 1. The default VMAF model scores a posterised frame **above an identical pair**

100.00 against the identity arm's 97.34. The mechanism is visible in the features: `integer_adm2` is
**1.2235** on the posterised arm, above unity, because the detail-loss feature reads added contrast
as added detail. This is the documented *enhancement gain* that the `neg` model family exists to
suppress, and `neg` suppresses it here — the same arm falls to 90.90, below the identity arm, and the
ordering becomes monotone in the amount of quantisation (90.90 / 88.59).

**The control that makes this a finding rather than a curiosity is the identity arm**, which is the
only reason "100" can be read as "better than an exact copy of the reference" rather than as "good".

### 2. VMAF does not reach 100 on an identical pair

97.34, on a static sequence. The motion feature is zero when nothing moves, and it is zero on the
**first frame of every sequence** because there is no previous frame — measured separately on a moving
clip, where frame 0 scored 97.43 and frames 1-9 scored 100.00. So "VMAF 100 means identical" is false
on static content and false on frame 0 of anything, and a pooled mean quietly carries that.

### 3. CAMBI and the native `quantisationSteps` proxy are blind in **opposite** places

CAMBI scores the single-code ramp **18.95** and its dithered twin **0.00** — the artifact and its
remedy, in the right order, by a large margin. That is CAMBI in its domain and it is why it is
adopted unmodified.

But CAMBI scores the *coarsely quantised* ramp **0.00** as well. A step above its `max_log_contrast`
is read as a genuine edge rather than a band, which is correct behaviour and a blind spot at the same
time. **`docs/quality-lab/metrics.md` §4.1 specified the banding ladder arm as "quantise to 5 bits on
a gradient region → `cambi` ↑ sharply". That is backwards**: coarse quantisation drives CAMBI to
zero.

The native proxy has the mirror-image problem and one worse. `quantisationSteps` looks for a 3-to-12
luma step in an otherwise flat neighbourhood, so ordinary single-code banding is invisible to it — and
a ±2-code dither *looks* like such a step. On the ladder's ramp it reports **0.000 for the banded
frame and 0.031 for the dithered one**. It is anti-correlated with the remedy on this artifact class,
which is ADR-243's shape in a third metric.

## Decisions

### 1. `vmaf_v0.6.1neg` is the default model, and the plain model is opt-in

A metric that can rank a defect above the reference is not one to leave on by default in an
instrument whose whole purpose is to be trusted. The plain model stays reachable because the finding
above needs it to be reproducible.

### 2. VMAF's scoping is unchanged and is now demonstrated rather than cited

ADR-250 retained VMAF **scoped to compression resilience** on the grounds that it is trained on
compression and scaling of camera-captured video. That was a literature argument; it is now a
measurement. Even under `neg`, VMAF scores the *dithered* frame 90.70 — worse than the banded frame's
baseline — because dither is noise and VMAF is trained to dislike noise. **The remedy for the
artifact is ranked below the artifact.** VMAF may not arbitrate a renderer configuration, and the
reason is in the table above rather than in a citation.

### 3. The banding ladder arm is corrected, and both metrics are kept with their bands stated

The arm is now a **dither** arm: a single-code ramp against its dithered twin, which is the direction
banding actually has and the direction a renderer's remedy moves in. It runs under CAMBI where
libvmaf exists.

`quantisationSteps` is **kept, not dropped**, because it answers for the coarse band CAMBI cannot
see, and metrics.md §4.3 is explicit about the alternative: *"Does the metric behave sensibly? If
not, document it. Do not force a metric into the system because it is popular"* — and do not quietly
reweight it either. Both of its failures are asserted as ladder arms so they cannot drift, and both
travel in its `limitations` in every report. Where `banding.cambi` is available it is the banding
number and `quantisationSteps` is its coarse-band companion.

### 4. The three libvmaf properties above are pinned as ladder arms

`avgen_quality validate --ladder` asserts them. They are properties of somebody else's library, so
they are marked *recorded, not fixed* — and if a libvmaf upgrade changes one, the ladder is where that
is found and this ADR is what then needs rewriting. Without ffmpeg the arms report unavailable with a
reason and the ladder still passes, which is the degradation contract applied to the validator.

## Consequences

**Phase 2 ships with VMAF, PSNR-HVS and CAMBI after all**, which is `architecture.md` §10's own
sentence — *"it gains them the day somebody runs `brew install ffmpeg`"* — coming true.

**`metrics.md` §4.1 had a wrong arm in it and the implementation found it.** That is the expected
outcome of building the thing rather than a failure of the research phase: a specified ladder arm is
a prediction about an instrument's behaviour, and this is what predictions are for.

**Two of the Lab's metrics are now known to be anti-correlated with a remedy on some artifact class**
— `temporalAlternation` on spatial aliasing over wind-animated geometry (ADR-243), `quantisationSteps`
on single-code banding. Neither is dropped and both carry it in writing. The pattern is now common
enough to state plainly: **a metric that moves in the right direction on the artifact is not the same
as a metric that moves in the right direction on the fix**, and only the second one can choose work.

**What this does not say.** It does not say libvmaf is broken: CAMBI's contrast ceiling and VMAF's
enhancement gain are both documented properties of instruments built for a different job, and both
behave exactly as built. It says what they can and cannot be asked here.
