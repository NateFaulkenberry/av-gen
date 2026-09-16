# ADR-250: The instrument is not the engine, and no single number is allowed to be the verdict

**Status:** Accepted
**Date:** 2026-09-16
**Context:** The Render Quality Lab specification, Phase 0 (reconnaissance) and Phase 1 (research)
**Follows:** ADR-243 (the detector and the eye disagree), ADR-242 (a target nobody reads is not a
feature), ADR-212 (an offline render may spend pixels), ADR-182 (a probe that cannot fail proves
nothing), ADR-170 (the GPU lock does not establish exclusivity), ADR-008 (dependency management)
**Constrains:** ADR-251

## Context

The mandate asks for an engineering instrument that replaces

    render -> human inspects -> human describes problem -> agent guesses a parameter -> render again

with objective, repeatable, machine-readable measurement. It also says, repeatedly and in bold, not
to code until the research is done, not to assume VMAF is the answer, and not to invent a single
quality score.

This project has already run the experiment that most quality-tooling efforts run at the end. **ADR-243
recorded a correctly-implemented temporal detector that was anti-correlated with a human reviewer** on
the artifact viewers actually report — it ranked removing anti-aliasing as a 42% improvement while the
reviewer called it *"more distracting"*, and ranked supersampling 9% worse while the reviewer called it
*"much calmer"*. Both arms, both directions. Nothing was wrong with the arithmetic. The detector
measured temporal alternation; the reviewer was objecting to spatial aliasing on moving geometry; and
the two remedies for the second both raise the first.

That is the failure mode this ADR exists to design around, and it happened here, with correctly
executed non-vacuous arms.

## Decision

### 1. The Quality Lab is a separate binary, and its metric engine is native C++

Not a Python package. The reconnaissance probed the machine rather than assuming it:

| probed | result |
|---|---|
| ffmpeg, libvmaf | **absent** |
| OpenCV, OpenEXR, Imath | **absent** |
| python3 | 3.9.6, system Python, no venv, no `requirements.txt` |
| numpy, Pillow, scipy, scikit-image | **absent, and used nowhere in the repo** |

All thirty Python tools in `tools/` are standard-library only; `image_stats.py` hand-rolls a PNG
decoder out of `zlib` and `struct` specifically to avoid a dependency. Meanwhile `avgen_core` is
GPU-free and already links **tinyexr**, **stb**, **nlohmann_json** and **fmt**, and already defines
`gpu::ImageF` / `gpu::Image8` — the exact types every readback produces.

So `avgen_quality` is a tool target beside `avgen_world_preview` and `avgen_help_lint`, following the
rule `tools/CMakeLists.txt` already states: *"build targets rather than scripts so a world can be
inspected with exactly the code the engine samples, not a reimplementation of it."*

**It adds no dependency to `avgen`, and no dependency to the repository.** A pure-stdlib Python
engine cannot walk 1.8 × 10⁹ pixels; adding numpy to fix that would break the only unbroken
convention the repo has, on a system Python with no lockfile.

### 2. Every external tool is optional, found at runtime, and degrades to "unavailable"

`docs/dependencies.md` already classifies ffmpeg as *"optional, external process… never linked, never
shipped, never downloaded"*. VMAF, PSNR-HVS and CAMBI arrive through that same door or not at all.

**A metric whose tool is absent reports `available: false` with a reason**, which lands in the
report's `limitations`. The run succeeds and the vector is shorter. This is the only way a tool that
depends on an un-installed ffmpeg can be honest on this machine — and it is why the Phase 2 vertical
slice is specified *without* VMAF and CAMBI, against the mandate's own sketch.

### 3. The result is a vector, and every metric is named after what it computes

No composite score, no published weights (mandate §8, §52). And the naming rule is ADR-243's lesson
turned into a convention:

> "Temporal stability" was the label, "temporal alternation of the luminance signal" was the
> measurement, and "spatial aliasing on moving geometry" was the artifact. Three things wearing one
> name is how a correct measurement came to rank two remedies backwards.

So the field is `temporalAlternation`, not `temporalStability`; `spatialLaplacian`, not
`spatialAliasing`. The artifact class a metric is *licensed to speak about* is a separate, explicit
property, and it is narrower than the measurement.

**Two harness rules follow, and they are rules rather than advice:**

* `temporalAlternation` and `spatialLaplacian` are emitted **together or not at all**. ADR-243 is the
  reason; `tools/spatial_stats.py` exists because of it.
* Measured fact and inferred cause are **different arrays with different types** in the report.
  `criticalFindings` holds measurements; `hypotheses` carry evidence and a confidence and are labelled;
  nothing automatic promotes one to the other.

### 4. What was adopted, and what was rejected with the reason

| rejected | the decisive reason |
|---|---|
| **a single "quality score"** | ADR-243: one number is how a correct detector ranked remedies backwards with nothing beside it to contradict it |
| **LPIPS** | ImageNet features on stylized synthetic content; per-frame only when our hardest problems are temporal; named in the rendered-VQA literature as poorly correlated; and it occupies ꟻLIP's slot at the price of PyTorch. The mandate's own last question — *does it provide information unavailable from our other metrics?* — answers no |
| **VMAF as the arbiter of renderer configuration** | trained on **compression and scaling of camera-captured video**; thin temporal model; our distortions are out of domain. **Retained, scoped to compression resilience**, where it is the best tool available and genuinely in domain |
| **Butteraugli / SSIMULACRA2** | still-image compression metrics; duplicate ꟻLIP's slot with a worse domain fit |
| **optical flow as the primary motion source** | the velocity AOV is exact and free; flow is an approximation that is worst in thin, fast, low-texture geometry — which is precisely where our artifacts live. Kept **only** to cross-check the velocity buffer, where a systematic disagreement is a motion-vector *finding* |
| **a "ghosting" / "sharpening" / "particle instability" detector each** | each is the motion-compensated residual under a different mask or sign. Three names for one measurement is how a suite becomes unfalsifiable |
| **approximating shadow stability** without a shadow AOV | it would be a number whose name promised more than it knew. Reported `available: false, reason: "no shadow AOV"` instead |
| **an optimizer now** | the mandate forbids it before metric validation, and ADR-243 is what that instruction is protecting against |

Adopted: MS-SSIM, CIEDE2000, ꟻLIP (BSD-3, graphics-native, and the only one whose primary output is
an error *map*), CAMBI, PSNR **as an alignment rail rather than as quality**, the two existing
`spatial_stats`/`temporal_stats` measures re-scoped, and three new native ones — the
motion-compensated residual, AOV-gated per-class residuals, and spectral detail retention.

### 5. The mandate's AOV-versus-supersampling collision is resolved by splitting the render, not by relaxing ADR-242

The mandate wants the Quality Lab built on AOVs and wants supersampled reference renders. ADR-242
refuses those together, because the auxiliary targets are sized to the scaled resolution and *averaging
two normals is not a normal, averaging two identifiers is a third object, and averaging two depths
across a silhouette is a surface that is not there.*

**The refusal is not relaxed. It is not needed.**

| render | flags | job |
|---|---|---|
| **candidate** | production config + `--aov normal,emission,depth,velocity,id` | the image under test, plus masks and motion **at native resolution, where they are exactly correct** |
| **reference** | `--tier offline --render-limits unlimited --supersample 2.0` | the better-sampled image the full-reference metrics compare against |

Full-reference metrics need only the beauty pass, which the reference has. AOV-driven detectors run on
the candidate, which has AOVs. **Nothing needs an AOV at supersampled resolution.** The single
sanctioned crossing is a masked full-reference metric — a candidate-resolution mask applied to a
reference already at candidate output resolution.

Should the refusal ever be lifted, ADR-242 already names the shape of the fix and this ADR endorses it:
a **resolve per target** — nearest for `id`, decode-average-**renormalise** for `normal`,
silhouette-aware for `depth` and `velocity`, plain average for `emission` — not one filter for five.
It is not scheduled, because the split means the Quality Lab does not need it.

### 6. No metric ships without a control arm

ADR-182 is load-bearing here in a way it is not for most subsystems, because **the product is
measurement**. Every metric faces a distortion ladder (blur, aliasing, over-sharpen, banding, colour
shift, exposure shift, shimmer, disocclusion) with two mandatory controls: an **identity** pair that
must move nothing, and a **smooth-motion** sequence that must move nothing temporal. Without the second,
the shimmer arm is vacuous.

And a second gate the ladder cannot replace: **ADR-243's three arms are the first regression case.**
A metric set that does not rank FXAA-off worse and supersample-2× better than the baseline is wrong,
and the fix is the metric. §44's rule stands: *do not fix the human by changing the weighting until the
human agrees.*

### 7. Cost is reported the way ADR-170 says, or reported pending

The mandate wants a quality-versus-GPU-milliseconds frontier. ADR-170 says the lock serialises agents
and not the device, that a timing is evidence only if the device was also quiet, and that even then two
invocations of one binary have a **~3 ms noise floor** — 10.945 / 11.272 / 13.697 ms on a byte-identical
scene. So:

* arms are **interleaved inside one process** (`--ab`, `PhaseProfiler::setFrameGroup`), never compared
  across invocations;
* `PhaseProfiler` and `FrameTimeline` are reported separately and never summed — one is CPU wall clock
  on one thread, the other is GPU timestamps;
* **min over a long run**, because contention is never negative;
* a `contentionWitness` (`pgrep avgen`, load average) sits beside every timing;
* **structural counters** are the comparable quantity — *"the structural quantities moved 3.6% and the
  timings moved 280%"*.

Until the machine is quiet, the cost axis is reported **pending with its witness**, which is an honest
record rather than a placeholder.

## What was measured

This is a research phase and most of it is properly unmeasured. Two things were run on the device
rather than asserted, both under `tools/gpu-lock.sh` with `pgrep avgen` clean before and after (load
average 9.61 — recorded because ADR-170 requires it, and because neither claim below is a timing):

1. **The supersampled EXR readback writes a top-left crop.** Bit-exact, with three discriminating
   controls. This is ADR-251 and it is a defect on the reference-render path.
2. **ADR-242's refusal still fires**, so `validate()` works and simply has no clause for the EXR case:
   `render: aov export and supersample 2 cannot be combined -- an identifier, a normal and a depth edge
   have no correct downsample`. That is the positive control for (1) being a *gap* rather than a broken
   validator.

**What was deliberately not measured:** every metric in §4. None is implemented, and a research phase
that reported metric numbers would be reporting numbers from an instrument nobody has shown can fail.

## Consequences

**The Quality Lab costs the engine nothing.** No new dependency, no `src/` module, no change to
`avgen`'s link line. The coupling is the filesystem, which is where the renderer's output already is.

**The vertical slice is smaller than the mandate's sketch**, because VMAF and CAMBI do not exist on
this machine. It gains them the day somebody runs `brew install ffmpeg`, which is a recorded human
decision and not a plan assumption.

**Three of the mandate's dimensions cannot be delivered as specified**, and are reported unavailable
rather than approximated: shadow stability (no shadow AOV), HDR-domain full-reference comparison
(ADR-251), and the cost frontier (a contended machine).

**The existing analysis tools are not replaced.** `spatial_stats.py`, `temporal_stats.py`,
`sharpness.py` and `chroma_speckle.py` are cited by ADRs, they work, and replacing a documented
instrument with an undocumented one is how measurement history gets lost. They are re-scoped, not
retired.

**What this does not say.** It does not say the metrics chosen here are correct — none has faced its
control arm yet. It says which instruments are worth the cost of finding out, and which were rejected
before anyone spent a render on them.
