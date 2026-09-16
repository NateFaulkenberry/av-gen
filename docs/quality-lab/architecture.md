# Quality Lab architecture

Status: **implemented through Phase 5** (spec §26, §27, §28, §29, §36, §48). Built as described,
with the differences recorded in §10 and in ADR-252/253/254.

Inputs: [repository-reconnaissance.md](repository-reconnaissance.md) (what exists),
[research.md](research.md) (what to use and what to reject).

---

## 1. The three constraints that determined the shape

1. **§49.7 / §49.8 — out of the runtime, no Python or ML burden on the app.** The Quality Lab is a
   separate binary and a set of scripts. `avgen`'s dependency list does not change. Nothing here is
   linked into the engine.
2. **The machine has no ffmpeg, no libvmaf, no numpy, no OpenCV, and a system Python 3.9 with no
   venv.** All thirty existing Python tools are stdlib-only. This is not a preference to work around;
   it is the environment, and ADR-008 means nothing may be added un-pinned.
3. **`avgen_core` already has everything an image metric needs, and it is GPU-free** — tinyexr,
   stb, `gpu::ImageF` / `gpu::Image8`, `hashImage`, nlohmann_json, fmt. And `tools/` already states
   the pattern: *"build targets rather than scripts so a world can be inspected with exactly the code
   the engine samples, not a reimplementation of it."*

**Therefore: the metric engine is native C++, in-tree, as a tool target. Every external tool is
optional and is found at runtime, exactly as ffmpeg already is.**

This is the opposite of the usual answer (a Python package with numpy, scikit-image and libvmaf) and
it is the right one *here* — it adds zero dependencies, runs at C++ speed over sequences, and matches
a precedent the repository has already set and written down.

---

## 2. Data flow

```
          project.json ──┐
                         ├──► avgen --project … --render …  ──► candidate frames (PNG)
   derived overrides ────┘        + --aov normal,emission,      + AOV EXRs
                                    depth,velocity,id           + sequenceHash, frameHashes
                                                                + --bench-json (counters, percentiles)
          reference recipe ──► avgen --project … --tier offline
                                 --render-limits unlimited
                                 --supersample 2.0            ──► reference frames (PNG)

   candidate + reference + AOVs
            │
            ▼
   ┌──────────────────────────────────────────────────────────┐
   │  avgen_quality  (C++, links avgen_core — GPU-free)       │
   │    capture/   frame sequence + AOV readers (tinyexr/stb) │
   │    metrics/   psnr ssim msSsim ciede2000 flip-map        │
   │               detailRetention spatialLaplacian           │
   │               temporalAlternation chromaSpeckle          │
   │    artifacts/ motion-compensated residual, disocclusion  │
   │               mask, AOV-gated per-class residuals        │
   │    report/    metrics.json  +  diagnostic PNGs           │
   └───────────────┬─────────────────────────┬────────────────┘
                   │                         │ (optional, if present on PATH)
                   │                         ▼
                   │              ffmpeg -lavfi libvmaf  ──► vmaf, psnr-hvs, cambi
                   │                         │
                   ▼                         ▼
   ┌──────────────────────────────────────────────────────────┐
   │  tools/quality-lab/*.py   (stdlib only)                  │
   │    experiment driver · report.html · Pareto plot         │
   └──────────────────────────────────────────────────────────┘
                   │
                   ▼
        quality-results/<timestamp>/
            manifest.json  metrics.json  report.html
            reference/ candidate/ diagnostics/ logs/
```

**Nothing in this diagram runs inside `avgen`.** The coupling is the filesystem, which is where the
renderer's output already lives.

---

## 3. Repository structure

§26 proposes a layout and says, in bold, *"This is conceptual. Adapt it to the existing repository
rather than mechanically imposing it. Do not unnecessarily move unrelated source files."* So:

```
tools/quality-lab/                  # the C++ tool and its stdlib Python driver
    CMakeLists.txt                  #   avgen_quality_metrics (a library) + avgen_quality (the CLI)
    main.cpp                        #   analyze | compare | validate --ladder | version
    capture/   sequence.{hpp,cpp}   #   frame + AOV sequence readers. EVERYTHING HERE IS A READER
    metrics/   spatial.{hpp,cpp}    #   psnr, ssim, msSsim, ciede2000, laplacian, quantisationSteps
               temporal.{hpp,cpp}   #   alternation, motion-compensated residual, disocclusion, warp floor
    artifacts/ masks.{hpp,cpp}      #   specular / normal-unchanged / id-churn / material-class masks
    report/    vector.{hpp,cpp}     #   the QualityVector and its JSON, and the pairing refusal
               diagnostics.{hpp,cpp}#   difference, heatmap, mask, Laplacian plane, 8x8 tile map
    external/  vmaf.{hpp,cpp}       #   ffmpeg libvmaf as a SUBPROCESS: vmaf, psnr_hvs, cambi
    validation/ladder.{hpp,cpp}     #   the distortion ladder as a library, shared by the CLI and the tests
    report.py                       #   the HTML report; stdlib only, no framework, no CDN
    schemas/   quality-report.v1.json
tests/unit/test_quality_metrics.cpp # the hand-written spatial ladder -- the human-readable gate
tests/unit/test_quality_lab.cpp     # the shared ladder, the temporal detectors, the masks, the report
examples/quality/                   # the benchmark scenes: aliasing, aliasing-dolly
quality-results/                    # .gitignore'd -- run outputs
```

**Two differences from the sketch above it, both deliberate.** There is no `metrics/flip.*`: ꟻLIP was
left unbuilt because the full-reference slot is occupied by MS-SSIM and CIEDE2000 and nothing has yet
needed a perceptual error *map* that those two cannot localise — the tile map does that job. And
there is no `experiment.py`: `--queue` is the batch primitive the experiment system should build on,
and building a second one before an experiment has actually been run would be inventing requirements.

**The validation ladder is a library and not test code**, which is the one structural decision worth
defending. `validate --ladder` and the unit suite run the *same* arms; a ladder that existed only
inside Catch2 could not be shown to a person who is holding a render and wondering whether to believe
the report.

**No existing source file moves.** No `src/` module is added — the Quality Lab is not part of the
engine. The three existing analysis scripts it supersedes in role (`spatial_stats.py`,
`temporal_stats.py`, `sharpness.py`) **stay where they are**: they are cited by ADRs, they work, and
replacing a documented instrument with an undocumented one is how measurement history gets lost.

---

## 4. CLI (§28)

§28 proposes `avgen-quality render|analyze|compare|experiment|report|benchmark`. Adapted to the
repository's naming (`avgen_world_preview`, `avgen_help_lint`, …), the binary is **`avgen_quality`**:

```
avgen_quality analyze    --candidate <dir> --reference <dir> [--aov-dir <dir>]
                         --profile <target-profile.json> --out <run-dir>
avgen_quality compare    --runs <run-dir>... --out <report-dir>
avgen_quality validate   --ladder            # the §34 distortion ladder, self-contained
avgen_quality version
```

**`render` and `experiment` are deliberately not subcommands of the C++ binary.** Rendering is
`avgen --project … --render …` and batching is `--queue`, both of which exist, are tested, and
already write the determinism proof the Quality Lab depends on. §29 asks for a stable programmatic
interface rather than UI automation; **it already exists and it is the CLI.** The experiment driver
is a Python script that composes derived projects, invokes `avgen` under `tools/gpu-lock.sh`, and
then invokes `avgen_quality analyze`.

`validate` is a first-class subcommand rather than a test-only path because §34 and ADR-182 make
metric validation a **product feature**: anyone must be able to ask the tool to demonstrate that its
metrics can fail.

---

## 5. Render capture (§29)

No new engine interface is needed, which is the strongest finding of the reconnaissance. What exists:

| need | existing mechanism |
|---|---|
| headless render | `--render` (implies `--headless`); windowless, GPU context only |
| batch | `--queue <file>` |
| deterministic frame timing | `FixedStepClock`: `renderTime = start + frameIndex / fps` |
| determinism proof | `sequenceHash`, `frameHashes`, printed on completion |
| AOVs | `--aov normal,emission,depth,velocity,id` → EXR beside the frames |
| reference quality | `--tier offline --render-limits unlimited --supersample 2.0` |
| cost record | `--bench-json` (ADR-113), `PhaseProfiler`, `FrameTimeline` |
| serialisation | `--project` + parameter-path overrides in a derived project |

**Two traps the harness must encode**, both found by reading rather than by running:

* **`--frames` does not apply to `--render`.** Length comes from `--range` and `--fps`.
* **`--supersample` with `--format exr` silently crops to the top-left.** References are taken as
  PNG. See [reference-rendering.md §3.2](reference-rendering.md).

---

## 6. Dependency graph, with §27's full field set

Nothing below changes `avgen`'s dependencies.

| dependency | licence | maintained | language | build | platform | Apple Silicon | CPU/GPU | runtime cost | integration | redistribution | optional? | CI-suitable |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| **tinyexr 3.2.0** | BSD-3 | yes | C++ | CPM, SHA256-pinned | all | **yes, in use** | CPU | low | **already linked into `avgen_core`** | source, attributed | no — already required | yes |
| **stb** | MIT / public domain | yes | C | CPM, commit-pinned | all | **yes, in use** | CPU | low | already linked | trivial | no — already required | yes |
| **nlohmann_json 3.12** | MIT | yes | C++ | CPM | all | in use | CPU | low | already linked | trivial | no | yes |
| **Catch2 v3.16** | BSL-1.0 | yes | C++ | CPM | all | in use | CPU | — | already linked | test-only | no | yes |
| Python 3 stdlib | PSF | yes | Python | none | all | 3.9.6 present | CPU | low | subprocess | none | no | yes |
| **ffmpeg + libvmaf** | libvmaf **BSD-2-Clause-Patent**; ffmpeg LGPL **or GPL by configuration** | yes | C | brew | all | yes | CPU | moderate | **external process**, found via `AVGEN_FFMPEG`/`PATH` — the precedent `docs/dependencies.md` already sets | **none — never linked, never shipped** | **yes** | yes, if installed |
| **ꟻLIP** | BSD-3 | yes (NVIDIA) | C++ (+ optional CUDA) | CMake; single header `FLIP.h` | Win/Linux; **macOS undocumented** | **UNVERIFIED — must build** | CPU or CUDA | moderate on CPU | CPM-pinned vendor, or its Python package | source, attributed | **yes** | yes |
| ColorVideoVDP | **reported MIT — read the file** | yes | Python/PyTorch | pip | all | yes (MPS, torch ≥ 2.1) | GPU strongly preferred | **high** | isolated venv, optional | none (not shipped) | **yes** | no — too heavy |
| OpenCV | Apache-2.0 | yes | C++ | brew/CPM | all | yes | CPU | high | optional, flow cross-check only | source | **yes** | marginal |
| LPIPS / PyTorch | BSD | yes | Python | pip | all | yes | GPU preferred | high | — | — | **rejected** | no |

**Degradation is a designed behaviour, not an error path.** A metric whose tool is absent reports
`available: false` with a reason, which lands in §19's `limitations` array and in the HTML report.
The run succeeds; the vector is shorter; the report says why. **This is the only way a tool that
depends on an un-installed ffmpeg can be trustworthy on this machine.**

---

## 7. The AOV / supersampling resolution

Stated here because it is an architectural decision, not a metric one. Full reasoning in
[research.md §7](research.md#7-the-aov--supersampling-collision-and-how-the-lab-resolves-it).

**The candidate and the reference are different renders with different jobs, so ADR-242's refusal
costs the Quality Lab nothing:**

* **candidate** — production configuration, `--aov …`, no supersampling. AOVs at native resolution,
  where they are exactly correct.
* **reference** — `--supersample 2.0`, no AOVs. Full-reference metrics need only the beauty pass.

The single sanctioned crossing is a **masked full-reference metric** (ꟻLIP restricted to vegetation
pixels): a candidate-resolution mask applied to a reference already at candidate output resolution.

**The refusal is not relaxed.** If it is ever lifted — and the Quality Lab is the first consumer with
a reason to want it — the fix is a **per-target resolve**, not one filter: **nearest** for `id`,
decode-average-**renormalise** for `normal`, **silhouette-aware** for `depth` and `velocity`, plain
average for `emission`. ADR-242 names this shape; this architecture endorses it and does not require
it.

---

## 8. Reports

**`metrics.json`** — versioned, per §19: `schemaVersion`, `run {id, timestamp, gitCommit, scene,
targetProfile}`, `renderer {configuration, gpu, frameTimeMs, contentionWitness}`, `metrics`,
`artifacts`, `diagnostics`, `limitations`. Schema committed at
`tools/quality-lab/schemas/quality-report.v1.json` so nothing depends on ad-hoc output.

**`report.html`** — generated by a stdlib Python script; no framework, no CDN, self-contained. Per
§18 it shows the dimensions **separately** and never a composite:

```
Spatial similarity 93.2   Temporal alternation 88.7   Shadow stability  unavailable (no shadow AOV)
Specular residual  82.4   Banding  unavailable (no libvmaf)   Detail retention 0.91
```

alongside the diagnostic images, the worst-frame links, the target profile, the renderer
configuration, the limitations list, and — where they exist — hypotheses **labelled as hypotheses**.

**Candidate ranking appears only inside an explicitly defined experiment**, never across runs, never
across target profiles.

---

## 9. Future: the optimizer (§23) and the human loop (§24)

Neither is built. §23 and §52 forbid an optimizer before metric validation; §25 and §45 forbid a
learned perceptual model before a human-rated dataset exists.

The only thing that must be true now is the serialisation boundary: **a renderer configuration is a
parameter vector and a result is a quality vector**, both JSON, so an optimizer attaches later
without touching anything that produces either. Research notes on which family of optimizer suits
an expensive-evaluation, small-budget, multi-objective constrained problem are in
[research.md §8](research.md).

The human-validation loop (§24) extends `docs/visual-quality.md`'s existing fourteen-criterion
rubric and fixed-review-frame list rather than creating a second review system.

---

## 10. Phasing, and where the gate is

| phase | content | gate |
|---|---|---|
| **0** | reconnaissance | ✅ done |
| **1** | research, this architecture, ADRs | ✅ done — **this is the §50 STEP 11 stopping point** |
| **2** | vertical slice: one scene → candidate + reference → PSNR/SSIM/MS-SSIM/CIEDE2000/Laplacian → JSON + HTML + diagnostics | ✅ done — the §34 ladder passes with its controls, **and with a meta-control that catches a deliberately broken metric**. VMAF, PSNR-HVS and CAMBI are in too; see below |
| **3** | motion-compensated residual, disocclusion mask, temporal re-scoping | ✅ done — §1's control arms pass, including the one that matters: masking is what makes the residual small (ADR-253) |
| **4** | AOV-gated per-class detectors | ✅ **partly.** `specularResidual`, `shadingResidual` and `lodIdentifierChurn` are built and controlled. `vegetationResidual` reports unavailable — the mask machinery is tested, the **material-id → class mapping does not exist**. `shadowStability` reports unavailable — **no shadow AOV**. Both are human decisions |
| **5** | benchmark suite — **aliasing first**, then a dolly temporal scene | ✅ done — `examples/quality/aliasing` and `-dolly`, both vacuous on the first attempt (ADR-254) |
| **6** | real-scene validation against human assessment | **the owner's time** |
| **7** | experiment framework, Pareto | a quiet machine for the cost axis |
| **8** | optimization research | metric validation complete |

~~**Phase 2 does not include VMAF or CAMBI**, against §39's sketch, for the simple reason that neither
exists on this machine.~~ **ffmpeg 9.0.1 with libvmaf was installed, and Phase 2 gained all three
without a design change** — which is the degradation contract in §6 doing exactly what it is for, and
the only interesting thing about it is that nothing had to happen. The metrics arrived as
`available: true` rows where there had been `available: false` rows with reasons.

What they cost was a day of putting them on the ladder, which is not optional for a metric somebody
else wrote: the default VMAF model scores a posterised frame **100.0** against an identical pair's
**97.3**, CAMBI is blind to coarse posterisation, and the native banding proxy scores dither as
banding. All three are pinned as ladder arms. See
[ADR-252](../decisions/ADR-252-the-banding-instruments-are-blind-in-opposite-places.md).

**Phase 6 — validation against human assessment — is the gate that has not been passed, and none of
the above substitutes for it.** Every direction check so far is one instrument agreeing with another.
