# Render Quality Lab

An engineering instrument for objective, repeatable, machine-readable analysis of AV Gen's rendered
output.

**Phases 0-5 are built.** The metric engine, the CLI, the versioned report, the temporal detectors,
the AOV-gated masks and the first two benchmark scenes exist and are under test. Phase 6 — validation
against human assessment — is not, and it is the gate that matters most; see *What is not validated*
below.

## Using it

```
# 1. render the candidate at production settings, with the AOVs the temporal detectors need
tools/gpu-lock.sh ./build/release/src/avgen --project examples/quality/aliasing-dolly.json \
    --render out/candidate --size 1280x720 --range 0:2 --aov velocity,depth,id,normal,emission

# 2. render the reference: better-sampled, no AOVs (ADR-242's refusal costs nothing -- see §7)
tools/gpu-lock.sh ./build/release/src/avgen --project examples/quality/aliasing-dolly.json \
    --render out/reference --size 1280x720 --range 0:2 --supersample 2.0

# 3. measure. Pass both sequence hashes: two arms that hash identically are void, not equal
./build/release/tools/quality-lab/avgen_quality analyze \
    --candidate out/candidate --reference out/reference --out out/run \
    --scene examples/quality/aliasing-dolly.json --profile master-1080p30 \
    --candidate-hash <hash> --reference-hash <hash>

# 4. read it
python3 tools/quality-lab/report.py out/run          # -> out/run/report.html
./build/release/tools/quality-lab/avgen_quality compare --runs out/runA out/runB

# and at any time, ask the instrument to demonstrate that it can fail
./build/release/tools/quality-lab/avgen_quality validate --ladder
```

`validate --ladder` is a first-class subcommand rather than a test-only path because the product here
*is* measurement: anyone must be able to ask the tool to show its metrics moving on the distortion
they claim to detect, holding still on the distortions they do not, and **failing when a metric is
deliberately broken**. The last of those is the meta-control, and without it "the ladder passes" is a
sentence with no information in it.

## Read in this order

| document | what it settles |
|---|---|
| [repository-reconnaissance.md](repository-reconnaissance.md) | what already exists — and it is most of the plumbing and none of the instruments |
| [research.md](research.md) | every technique considered, the §47 thirteen-question comparison table, and what was rejected with the reason |
| [architecture.md](architecture.md) | the system: a native C++ tool outside the engine, every external dependency optional |
| [metrics.md](metrics.md) | the quality vector, each field's definition, and what each number may **not** be used to conclude |
| [artifact-detection.md](artifact-detection.md) | the detectors, their inputs, their failure modes, and their control arms |
| [reference-rendering.md](reference-rendering.md) | what a reference is, the four things it is not, and the measured defect on its path |
| [benchmark-scenes.md](benchmark-scenes.md) | what exists, what to build, and what must not be optimized against |
| [experiments.md](experiments.md) | the unit of work, the non-vacuity check, and how cost is reported honestly |

Decisions: **[ADR-250](../decisions/ADR-250-the-instrument-is-not-the-engine.md)** (the instrument is
not the engine), **[ADR-251](../decisions/ADR-251-the-supersampled-exr-is-a-corner-of-the-frame.md)**
(the supersampled EXR is a corner of the frame),
**[ADR-252](../decisions/ADR-252-the-banding-instruments-are-blind-in-opposite-places.md)** (libvmaf
measured and put on the ladder),
**[ADR-253](../decisions/ADR-253-the-residual-knows-where-the-pixel-came-from.md)** (the
motion-compensated residual, and ADR-243 narrowed),
**[ADR-254](../decisions/ADR-254-a-benchmark-that-cannot-show-the-artifact.md)** (two vacuous
benchmark scenes, and the rules they bought).

## The three things that shaped it

1. **ADR-243.** A correctly-implemented temporal detector was measured *anti-correlated* with a human
   reviewer on the artifact viewers actually report. Every design choice here is downstream of that:
   a vector rather than a score, metrics named after what they compute, a spatial measure beside
   every temporal one, and a human gate placed early rather than at the end.
2. **Every external tool is optional and degrades to *"unavailable"* with a reason.** The metric
   engine is native C++ linking `avgen_core`, which already has tinyexr, stb and the image types, so
   `avgen`'s dependency list does not change and nothing in `src/` includes anything under
   `tools/quality-lab/`. ffmpeg is spawned as a process and never linked. (Written when the machine
   had no ffmpeg at all; it has one now, and nothing about the design changed — which is the
   degradation contract working rather than being tested.)
3. **ADR-242's refusal did not need relaxing.** The candidate carries AOVs at native resolution; the
   reference carries supersampling and no AOVs. Nothing needs an AOV at supersampled resolution.

## What the instrument has measured

* The §34 distortion ladder and its controls pass — 22 spatial checks, 14 temporal, 7 external — and
  the ladder is shown to **catch a deliberately broken metric** ([ADR-182](../decisions/ADR-182-a-diagnostic-arm-that-cannot-fail.md)
  applied to the validator).
* **ADR-243's ordering is reproduced on new content by a new instrument**: on
  `examples/quality/aliasing-dolly`, FXAA-off is +8.6% worse and supersample-2× is −18.2% better on
  `spatialLaplacian`, which is `metrics.md` §4.2's second gate.
* **The smooth-motion control is ADR-243 in miniature**, in a unit test, in under a second: pure
  authored translation gives a motion-compensated residual of 0.000 and a temporal alternation of
  31.3.
* Three properties of libvmaf that change how its numbers may be read, including the default VMAF
  model scoring a posterised frame **100.0** against an identical pair's **97.3** ([ADR-252](../decisions/ADR-252-the-banding-instruments-are-blind-in-opposite-places.md)).

## What is **not** validated

**No human has looked at anything this instrument has measured.** Phase 6 is the validation against
human assessment and it has not happened. Every direction check so far is one instrument agreeing
with another, or with a judgement a reviewer gave about a *different* scene in ADR-243. The single
most expensive lesson in this repository is that a detector can be correct, non-vacuous,
well-controlled and still rank remedies backwards — so until a person has watched these arms, the
numbers describe the renderer and do not rank it.

## Human decisions collected, and not taken

Listed at [research.md §10](research.md#10-unresolved-questions-carried-into-the-next-phase), minus
the two that have since been answered (ffmpeg is installed; ADR-251 was resolved by fixing the
resolve). Still open: whether the engine gets a **shadow AOV**; the **material-id → class mapping**
that `vegetationResidual` needs; and the **human-validation protocol and its reviewer budget**, which
is the Phase 6 gate.
