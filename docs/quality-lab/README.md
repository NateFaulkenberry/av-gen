# Render Quality Lab

An engineering instrument for objective, repeatable, machine-readable analysis of AV Gen's rendered
output.

**Phases 0-6 are done.** The metric engine, the CLI, the versioned report, the temporal detectors,
the AOV-gated masks and the first two benchmark scenes exist and are under test — and **Phase 6, the
validation against human assessment, has been run**
([ADR-257](../decisions/ADR-257-the-eye-ranked-them-the-way-the-spatial-measure-did.md)). What it
established and what it left untested are below, in that order, because the second list is longer.

## Using it

```
# 1. render the candidate at production settings, with the AOVs the temporal detectors need.
#    NOTE: --render is resolved against the PROJECT file's directory, not the working directory,
#    so this writes examples/quality/out/candidate. It is gitignored; it is not in the repo root.
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

# and two things a pooled mean cannot answer:
#   is arm A worse than arm B on THIS frame? (the arms are paired -- same camera, same times)
./build/release/tools/quality-lab/avgen_quality analyze ... --per-frame out/run/frames.csv
#   does an arm change a surface, or only its edges? (ADR-257, the orb that was called a control)
./build/release/tools/quality-lab/avgen_quality control --runs out/armB out/armA out/armC
./build/release/tools/quality-lab/avgen_quality control --object 0 --band 2 --runs out/armB out/armA

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
benchmark scenes, and the rules they bought),
**[ADR-257](../decisions/ADR-257-the-eye-ranked-them-the-way-the-spatial-measure-did.md)** (Phase 6:
the eye ranked them the way the spatial measure did, and the control in the frame was not one).

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
* **ADR-243's ordering is reproduced on new content by a new instrument, and then by a person**: on
  `examples/quality/aliasing-dolly`, FXAA-off is +8.6% worse and supersample-2× is −18.2% better on
  `spatialLaplacian` at 640×360 (+6.5% / −16.9% at 1280×720), which is `metrics.md` §4.2's second
  gate — and a blind reviewer ranked the same three arms in the same order (ADR-257).
* **The smooth-motion control is ADR-243 in miniature**, in a unit test, in under a second: pure
  authored translation gives a motion-compensated residual of 0.000 and a temporal alternation of
  31.3.
* Three properties of libvmaf that change how its numbers may be read, including the default VMAF
  model scoring a posterised frame **100.0** against an identical pair's **97.3** ([ADR-252](../decisions/ADR-252-the-banding-instruments-are-blind-in-opposite-places.md)).

## What Phase 6 established, and what it did not

A person watched three arms of `examples/quality/aliasing-dolly` at 1280×720 — FXAA off, baseline,
supersample 2× — **blind**, as A/B/C with the mapping withheld, with the instrument's prediction
registered in writing beforehand.
[ADR-257](../decisions/ADR-257-the-eye-ranked-them-the-way-the-spatial-measure-did.md) is the record;
[metrics.md §4.4](metrics.md) is the protocol. Three things came out of it.

**1. `spatialLaplacian` ranks anti-aliasing arms the way the eye does, on a moving camera.** The
reviewer ranked C > B > A unprompted, which is the metric's ordering in both directions from the
baseline, and it is not a close call in the data: paired frame by frame, FXAA-off is worse on **60 of
60** frames and supersampling better on **60 of 60**. That is `metrics.md` §4.2's gate with a human
behind it on a second scene and a second kind of motion.

**2. The measure now has one calibration point in each direction, and it is a bracket, not a curve.**
A **6.5%** difference was *not* visible at 1× playback on this moving camera and *was* visible in a
frozen close-up. A **16.9%** difference was *"immediately obvious"*. One shot, one reviewer. Do not
quote it as a threshold.

**3. `temporalAlternation` did not invert — it went uninformative exactly where the eye was
certain.** ADR-243's backwards ranking does not reproduce on a moving camera. But on the
supersampled arm, the one the reviewer called *"immediately obvious"* and *"Best"*, the measure calls
it **worse on 26 of 58 frames** — a coin flip — while separating the two arms the reviewer found
hardest to tell apart on 58 of 58, by 0.62%. And ADR-253's finding that it *did* agree here was a
property of its render size: at 640×360, one variable changed, the same arms separate 4× more
strongly. **An instrument's effect size is evidence only at the resolution it was measured at.**

## What is still **not** validated

* **A static camera on this scene was not reviewed.** ADR-243's inversion was found on a static
  camera, so the configuration that produced the original failure remains untested by this
  instrument.
* **No other scene has been reviewed at all**, and nothing here is transferable between scenes:
  `spatialLaplacian` *"cannot separate aliasing from detail"* and is meaningful only between arms of
  one view.
* **One reviewer, one session, one ranking.** There is no second opinion and no repeat.
* **Every other metric is unvalidated against a person.** `motionCompensatedResidual`,
  `disocclusionFraction`, `msSsim`, the per-class residuals, CAMBI and VMAF have been checked against
  controls and against each other, and never against an eye. ADR-253's warning stands for all of
  them: a detector can be correct, non-vacuous, well-controlled and still rank remedies backwards.
* **The review had no negative control.** The orb was offered as one and is not one — see below.

## The control that was not one

The scene's large smooth orb was described as a control in ADR-254, in `benchmark-scenes.md` and in
the scene files themselves: *"it cannot alias, so a metric that moves on this scene's arms must not
be moving here."* Asked whether it looked the same in all three arms, the reviewer said **no**, and
they were right. A sphere's interior shading is unaffected by anti-aliasing; its silhouette is a
curved edge like any other and is affected exactly as much.

Measured with `avgen_quality control`, over 60 frames: the orb's **interior** is **bit-identical**
with FXAA off (at 1, 2, 4 and 8 px of erosion) and moves 0.15 luma steps under supersampling; its
**silhouette band** moves 2.70 and 5.39 steps, with peaks over 120. Both non-vacuity arms are
reported beside those numbers — the same interior mask moves 2.90 steps between *consecutive frames*
of one arm, and the same split on the fences and blades gives interiors of 2.1 to 6.7, because
sub-pixel geometry has no invariant interior.

So: **the orb's interior is a control. The orb is not.** And the control for a *human* comparison is
not a region of the frame at all — it is a **duplicated arm**, one clip shown twice under two labels,
which is the review-level form of the rule the harness already runs on renders: two arms that hash
identically are void, not equal.

## The two engine gaps, now answered

Both were carried into this phase as questions for a person, and both have answers:

* **The shadow AOV: yes** — and the research changed the size of the job.
  [ADR-255](../decisions/ADR-255-the-shadow-aov-and-the-tier-that-has-no-shadow-texture.md). The
  engine has a screen-space shadow term as its own target (ADR-087), and exporting it looked like a
  texture and a switch case. It is not: `shadowMaskScale = 1.0` at both `high` and `offline`, so the
  mask pass **does not run at all** at the tier the Quality Lab renders its candidate at. An AOV
  built on it would be a constant in exactly the configuration that needs it, and correct everywhere
  else. The decision is a dedicated full-resolution pass gated on `--aov shadow`, with the fidelity
  claim measured rather than inherited.
* **The material-id → class mapping: emitted, never authored.**
  [ADR-256](../decisions/ADR-256-a-material-id-is-not-a-class.md). A material id is a scene-build
  index, so a hand-written list of integers is silently wrong the moment the scene changes -- the
  mask would be well-formed, the residual correct, and the surfaces the wrong ones.
  `scene::Material` gains a surface class, the generators set it where the knowledge already is, and
  `--aov id` writes a `materials.json` beside the frames. A mapping that ships with the frames
  cannot disagree with them.

Neither is implemented. Both are scoped.

## Human decisions collected, and not taken

Listed at [research.md §10](research.md#10-unresolved-questions-carried-into-the-next-phase), minus
the ones since answered (ffmpeg is installed; ADR-251 was resolved by fixing the resolve; the
**human-validation protocol** was run and is written down at [metrics.md §4.4](metrics.md), and the
reviewer budget it cost was one sitting and six questions). Still open: whether the engine gets a
**shadow AOV**, and the **material-id → class mapping** that `vegetationResidual` needs — both
scoped, neither implemented.
