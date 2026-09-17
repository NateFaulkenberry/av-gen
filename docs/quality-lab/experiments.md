# Experiments: the unit of work, and the discipline that makes one worth recording

Status: research (spec §20, §21, §22, §41, §42, §43). **Not implemented.**

---

## 1. What an experiment is

§43 states the required fields and they are the definition, not a template to fill in afterwards:

> hypothesis, parameter changed, expected effect, benchmark, metrics of interest, performance
> constraint, result, decision.

Two of them do the work. **Expected effect, stated before the render**, is what stops a result being
reverse-engineered into a conclusion. **Decision** is what stops an experiment being a measurement
nobody acted on.

This is not new here. `tools/experiment.py` already exists and already says it — *"records one
controlled look-development experiment (change ONE variable, render, compare, write a record)"* —
and `docs/experiments/` holds three such records. The Quality Lab's experiment system is that
discipline with a metric vector attached, not a new idea.

---

## 2. The manifest

```json
{
  "schemaVersion": 1,
  "id": "aa-comparison-2026-09-16",
  "hypothesis": "FXAA at 0.75 reduces spatial aliasing on wind-animated grass more than it costs in detail",
  "expectedEffect": { "spatialLaplacian": "down", "sharpnessRatio": "down but > 0.95",
                      "temporalAlternation": "up — and that is expected, per ADR-243" },
  "baseline":  { "project": "examples/world/glowmere-stylized.json", "overrides": { "post/output/antialias": 0.0 } },
  "candidates": [
    { "name": "fxaa-0.75", "overrides": { "post/output/antialias": 0.75 } },
    { "name": "ss-2x",     "renderOverrides": { "supersample": 2.0 } }
  ],
  "reference": { "recipe": "supersample-2x-offline-unlimited" },
  "scene":  { "camera": "river-view", "range": [8.0, 18.0], "fps": 30, "size": "1280x720" },
  "targetProfile": "master-1080p30",
  "metricsOfInterest": ["spatialLaplacian", "flipMean", "msSsim", "sharpnessRatio",
                        "temporalAlternation", "motionCompensatedResidual"],
  "performanceConstraint": { "gpuFrameMsMin": 16.7 },
  "provenance": { "gitCommit": null, "gpu": null, "contentionWitness": null }
}
```

§20's required record — git commit, renderer configuration, scene, target, GPU, frame range,
simulation seed, render duration, metrics, artifact results — is the union of the manifest and the
result. `provenance` is filled by the harness, never by hand.

**`overrides` are parameter-path overrides applied to a derived project**, written to disk and
rendered through `--project`. They are **not** applied by dropping to `--composition`, and they are
not applied by mutating the original. Two separate renderer findings in this repository were wrong
because arms rendered the scene alone.

---

## 3. Every candidate must be shown to be a different render

This is the check that makes everything else non-vacuous, and this project already runs it.
ADR-243's five-arm suite records it as a property of the experiment: *"every arm's sequence hash
distinct so none was vacuous."* And ADR-212's supersampling had a version that **logged "2.00×" while
doing nothing**, caught only because the sequence hash came back byte-identical.

So the harness asserts, before computing a single metric:

| expectation | assertion | on failure |
|---|---|---|
| a candidate that changes the image | `sequenceHash != baseline.sequenceHash` | **the experiment is void**, and reported void — not measured |
| a candidate that must not change the image (a control) | `sequenceHash == baseline.sequenceHash` | the same |
| the reference | rendered once per (scene, camera, resolution, commit) and reused | — |

This is ADR-182 applied to the harness rather than to a detector: *a probe must be shown capable of
failing.* An experiment whose arms are identical produces beautifully consistent metrics and means
nothing.

---

## 4. Parameter sweeps (§21)

§21's instruction is explicit: **do not build a giant brute-force combinatorial system.** Three axes
at four values each is 64 renders; at 2.7× for the reference it is a day.

The design constraint that satisfies both §21 and §23 is narrow and is the only thing that must be
true now:

* a renderer configuration is a **serialisable parameter vector**;
* a result is a **serialisable quality vector**;
* the harness takes a **list of candidate vectors from a generator** and does not care how it chose
  them.

A grid generator ships first because it is honest about being a grid. A Bayesian or Pareto generator
attaches later **without touching anything that produces either vector** — and §23 and §52 both
forbid building one until the metrics are validated.

Axes worth sweeping first, all already exposed: `post/output/antialias` (FXAA, 0 skips the pass
entirely), `render.supersample`, `render.tier`, `render.limits`, and the `--quality-arm` set
(`shadowrange, contact, pcss, maskfull, volumefull, volumepreview, volumequarter, volumesteps`).

---

## 5. The performance/quality frontier (§22), and why it is reported pending

§22 wants quality against render cost. ADR-170 says what an honest cost number requires and this
machine currently cannot provide it: **two other agents are on this GPU.**

The rules, which are the project's own:

1. Every GPU render goes through `tools/gpu-lock.sh`. The lock serialises **agents**, not the device.
2. A timing is evidence only if the device was **also** free of anything that did not ask for the
   lock — `pgrep avgen` beside the numbers. A run that cannot say the device was quiet reports its
   milliseconds as *a record of having taken them*, not as a measurement.
3. Even under the lock with `pgrep` clean, three runs on a byte-identical scene gave 10.945 / 11.272 /
   13.697 ms. **Arms must be interleaved inside one process** — `--ab` for the renderer,
   `PhaseProfiler::setFrameGroup` for the main thread. Separate invocations have a ~3 ms noise floor.
4. **Count structure, not milliseconds, under contention.** *"the structural quantities moved 3.6%
   and the timings moved 280%."* Triangles, instances, draw calls, entity full/coarse/skipped are
   CPU-computed, deterministic and comparable across sessions.
5. `core::PhaseProfiler` reports **min over a long run** for exactly this reason, and `gpu::FrameTimeline`
   is the GPU side. They are never added together and never confused.

**Until the machine is quiet, the frontier is reported with structural counters and a `pending` cost
axis.** That is the correct output, not a placeholder — §22 asks for cost *recorded honestly*, and
"we could not measure this here, and here is the witness" is an honest record.

---

## 6. Agent-facing output (§41)

```json
{
  "summary": { "regressions": [...], "improvements": [...] },
  "criticalFindings": [
    { "type": "measured", "metric": "spatialLaplacian", "delta": "+31%",
      "affectedScene": "glowmere-stylized", "affectedRegions": ["tile(3,5)", "tile(4,5)"],
      "worstFrame": 214 }
  ],
  "hypotheses": [
    { "cause": "thin geometry below the sampling rate in the grass beds",
      "confidence": 0.6, "evidence": ["spatialLaplacian +31%", "detailRetentionRatio 1.14"],
      "label": "HYPOTHESIS — not established" }
  ],
  "limitations": [ "cambi unavailable: no libvmaf on this host",
                   "shadowStability is an approximation: --aov shadow recomputes the term (ADR-258)",
                   "gpuFrameMsMin pending: device not quiet (pgrep avgen -> 2 processes)" ]
}
```

**`criticalFindings` and `hypotheses` are different arrays with different types for a reason.** §40
and §41:

> It must clearly distinguish measured fact from inferred likely cause. Never pretend the analyzer
> knows causality when it only knows correlation.

A hypothesis carries its evidence and its confidence, is labelled, and is **never** promoted into
`criticalFindings` by anything automatic.

---

## 7. The workflow (§42), and the one step that is not automatable

§42's loop — baseline → run → largest statistically meaningful regression → **inspect the diagnostic
visualization** → hypothesis → change ONE variable → re-run the affected benchmark → compare →
keep/revert → broader regression suite.

Step three is the one this repository has already proved cannot be skipped. ADR-243's Priority 1
programme consumed *"multiple rounds, two withdrawn results and a harness rewrite"*, and one
afternoon of a person looking at rendered footage overturned the use being made of all of it. Its
own conclusion:

> That is an argument for looking **earlier**, not for measuring less.

So the loop carries a human gate, and it is placed early rather than at the end:

**Before an experiment's conclusion is acted on, a person looks at the worst frame and the
diagnostic overlay, and their judgment is recorded beside the numbers.** Where the two disagree, §44
governs: the metric needs improvement. **Do not fix the human by changing the weighting until the
human agrees.**

`docs/visual-quality.md` already holds the review rubric — fourteen criteria including *"Temporal
coherence: stable under motion — no boiling, crawling or popping?"* — and the fixed review-frame
list. The human-validation loop (§24) should extend that document, not compete with it. **How often,
and how much reviewer time it may cost, is the owner's decision**, not the harness's.

⚠ **Run once, and the protocol it produced is written down.** Phase 6
([ADR-257](../decisions/ADR-257-the-eye-ranked-them-the-way-the-spatial-measure-did.md)) put three
arms in front of a person, blind, with the prediction registered first; it cost one sitting and six
questions, and it both validated `spatialLaplacian` and found a mis-specified control that had been
copied into three files. The rules are [metrics.md §4.4](metrics.md), and the one that is easy to get
wrong is rule 4: **the control for a human comparison is a duplicated arm, not a region of the
frame.**
