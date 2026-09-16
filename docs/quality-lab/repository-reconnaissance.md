# Quality Lab Phase 0: repository reconnaissance

Status: complete. Spec §39 Phase 0 / §50 STEP 1. **No implementation is proposed here** — this
document records what exists, measured by reading the code rather than by assuming the spec's
conceptual layout (§26: *"Do not assume the current structure"*).

Worktree `/Users/natefaulkenberry/Documents/GitHub/av-gen-wt-qualitylab`, branch `agent/qualitylab`,
from `41e0c76`.

The headline: **AV Gen already has most of a quality lab's plumbing and none of its instruments.**
Deterministic offline rendering, AOV export, HDR EXR output, asynchronous readback, frame and
sequence hashing, a GPU mutex, two profilers, a batch render queue, an in-process A/B harness and
eleven analysis scripts all exist. What does not exist is any image-comparison code in `src/`, any
reference-render workflow, any structured report, and any metric beyond four hand-rolled Python
statistics. The gap is narrower and differently shaped than §26 assumes.

---

## 1. Build system, and what a new component must fit into

| fact | value |
|---|---|
| generator / presets | Ninja; `debug`, `release`, `relwithdebinfo`, `asan`, `tsan`. Output `build/<preset>/` |
| standard | C++23, no extensions, `-Wall -Wextra -Wpedantic -Wshadow -Wold-style-cast …` on engine targets only |
| libraries | `avgen_core` (GPU-free), `avgen_gpu`, `avgen_platform` |
| executables | `avgen` (`build/<preset>/src/avgen`), plus seven `avgen_*` tools |
| tests | `avgen_tests` (unit + integration, GPU-free), `avgen_render_tests` (GPU, `RESOURCE_LOCK gpu`) |
| dependency manager | **CPM**, vendored at `cmake/CPM.cmake`; everything pinned to a tag/commit, archives SHA256-pinned; `SYSTEM` + `EXCLUDE_FROM_ALL`; no vcpkg/Conan/submodules; `CPM_USE_LOCAL_PACKAGES OFF` (ADR-008) |
| CI | **none.** No `.github/`, no workflow files. Everything is run by hand or by an agent |

Third-party libraries already present that a quality tool could use without adding anything:
**tinyexr 3.2.0** (EXR read and write, already linked into `avgen_core`), **stb** (PNG/HDR),
**nlohmann_json**, **fmt**, **spdlog**, **glm**, **Catch2 v3**, **meshoptimizer**, **fastgltf**,
**kissfft**, **SDL3**, **Dawn** (pinned prebuilt), **ImGui/ImPlot**.

`tools/CMakeLists.txt` states the house pattern a Quality Lab binary would follow:

> Command-line tools that link the GPU-free core. They are build targets rather than scripts so a
> world can be inspected with exactly the code the engine samples, not a reimplementation of it.

### 1.1 The environment, probed rather than assumed — and it is the constraining finding

| probe | result |
|---|---|
| `ffmpeg` | **not installed.** Not in `PATH`, not in `/opt/homebrew/bin`, not a brew formula on this machine |
| libvmaf | **absent** (no ffmpeg; `pkg-config` lists nothing matching) |
| `pkg-config` vmaf / opencv / openexr / imath | **zero matches** across 40 visible packages |
| `python3` | **3.9.6**, Apple's system Python. No venv, no `requirements.txt`, no `pyproject.toml` |
| numpy / Pillow / scipy / OpenCV / matplotlib | **not installed, and not used anywhere in the repo** |
| homebrew | 6.0.22, `/opt/homebrew` (Apple Silicon) |

All 30 Python tools in `tools/` are **standard-library only** — `image_stats.py` hand-rolls a PNG
decoder out of `zlib` and `struct` specifically to avoid a dependency. This is an unwritten but
unbroken convention, and it is consistent with ADR-008's pinning discipline: the repo does not have
an un-pinned dependency anywhere, in any language.

`docs/dependencies.md` already records the one precedent that matters here:

> **ffmpeg** — optional, external process… never linked, never shipped, never downloaded.

found at runtime via `VideoSettings::ffmpegPath`, `AVGEN_FFMPEG`, `PATH`, `/opt/homebrew/bin`,
`/usr/local/bin`. So there is precedent for an *optional* external-process dependency and **no
precedent for a hard one, in any language.**

---

## 2. Render entry points and the CLI

One binary, `avgen`; parser `parseArgs` at `src/app/application.cpp:218-575`; options struct
`AppOptions` at `src/app/application.hpp:63-186`.

### 2.1 Flags a quality harness cares about

| flag | meaning |
|---|---|
| `--project <file>` | load an `avgen-project` v4 — **parameters, routes, sources, presets, shaders, timeline, and the `render` block** |
| `--composition <file>` | load an `avgen-scene` v1 — *what is in the scene*, and nothing else |
| `--render <out>` | offline render to a sequence directory or a video file. **Implies `--headless`** |
| `--queue <file>` | run a render queue JSON. **Implies `--headless`**. Batch |
| `--format png\|sequence\|exr\|video` | output kind |
| `--range a:b` | render time range **in seconds**; either side may be empty |
| `--fps`, `--size WxH` | override the project's frame rate and resolution |
| `--tier preview\|realtime\|high\|offline` | quality tier. `RenderSettings::tier` defaults to `offline` |
| `--supersample <1.0–2.0>` | offline only; render above output resolution (ADR-212) |
| `--aov normal,emission,depth,velocity,id` | offline only; export auxiliary passes as EXR (ADR-242) |
| `--render-limits tier\|live\|unlimited` | ADR-186 distance-detail limits |
| `--disable <csv>` | `shadows,ao,volume,post,shadowmask,culling,water,transparency,particles,animation,cameramotion,fxaa` |
| `--quality-arm <csv>` | `shadowrange, contact, pcss, maskfull, volumefull, volumepreview, volumequarter, volumesteps` |
| `--ab <phase>`, `--ab-blocks <n>` | **in-process interleaved A/B**, implies headless. `none` measures the noise floor |
| `--bench-json <f>` | machine-readable run record (percentiles, counters) — ADR-113 |
| `--capture <file>` / `--capture-ui <file>` | last frame to PNG/PPM / the swapchain *after ImGui* |
| `--debug-target <t>` | on-screen auxiliary view. **Explicitly ignored under `--render`** |
| `--profile-cpu`, `--profile-csv <f>` | per-phase CPU frame distribution |

**Traps, recorded because each one would have produced a wrong measurement:**

* **`--frames` does not apply to `--render`.** The render branch returns before the frame loop
  (`application.cpp:3564-3602`). A render's length comes from `--range` and `--fps`, or the project.
* **`--output` is a display window, not a render path.** The render path is `--render`.
* **`--quality` is the *video encoder's* quality**, not the renderer's.
* **`--debug-target` is silently a no-op for renders.** A harness that tried to export AOVs this way
  would get beauty frames and no warning at the point of use.
* The usage text at `application.cpp:122-216` and `docs/help/reference-command-line.md` are both
  **stale**: between them they omit `--supersample`, `--aov`, `--render-limits`, `--ab`,
  `--preview-mode`, `--capture-ui*`, `--viewport-matches-render`, `--bench-json`, `--cluster-stats`.
  Read the parser, not the help.

### 2.2 `--composition` is not `--project`, and the difference is the image

This is the single most important fact for benchmarking discipline, and this repository has been
burned by it twice.

A scene file holds *what is in the scene*. **Every parameter that shapes the image is in the
project's `parameters` map**, keyed by path — not in the scene:

* exposure: `camera/exposure/{mode, aperture, shutterSeconds, iso, compensation, minEv, maxEv, speedUp, speedDown, meterCenterWeight}`
* lens: `camera/lens/{focalLength, aperture, shutterAngle, sensorWidth, sensorHeight, focusDistance, useExplicitFov}`
* **chromatic aberration**: `post/lens/chromaticAberration`; also `post/lens/distortion`
* **FXAA**: `post/output/antialias` — *0 skips the pass entirely*; also `sharpen`, `vignette`, `grain`
* bloom, halation, anamorphic, grade, DoF, tilt-shift, motion blur, `post/tonemap/operator`

…plus the **whole `render` block**, which is where `tier`, `supersample`, `limits` and `aovs` live.

A `--composition` run gets default post values and **no render block at all**. Rendering a scene
alone therefore renders a *different image* from the piece. **Every Quality Lab render goes through
`--project`.** Where a project must be varied, it is varied by writing a derived project file, not by
dropping to the scene.

### 2.3 Output locations

`--render <out>` wins over everything. Otherwise `render.path` from the project, resolved **relative
to the project file's directory**. Shipped projects use `renders/<name>`. `--capture` paths are
relative to the **cwd**, not the project — a different rule in the same tool.

A project-less `--render` snapshots the session to
`std::filesystem::temp_directory_path() / "avgen_render_session.json"` first.

---

## 3. `RenderJob` — the offline engine, and the determinism proof

`src/app/render_job.{hpp,cpp}`. Constructed by `Application::makeRenderJob`
(`application.cpp:3479-3494`) with a fresh `Engine` in `EngineMode::Offline`.

`start()` order, which matters because one ordering bug here already shipped: validate → resolve
output → resolve end time and frame count → apply ADR-186 limits → **2 warm-up frames through a
throwaway `SceneRenderer`** (skippable with `AVGEN_NO_WARMUP`) → real renderer → `setQuality(tier)` →
**`supersample` sets `renderScale` *before* `resize()`** → quality arms → disabled passes → LDR
target → readback rings → output dir / video writer → `FixedStepClock(fps)` → encoder threads.

### 3.1 The hashes — §30's determinism check, already built

`render_job.cpp:464-470`:

```cpp
lastHash_ = frame.format == gpu::ReadbackRing::Format::Rgba16Float ? gpu::hashImage(frame.imageF)
                                                                   : gpu::hashImage(frame.image);
sequenceHash_ = (sequenceHash_ ^ lastHash_) * 1099511628211ull;
frameHashes_.push_back(lastHash_);
```

Per-frame hash is **FNV-1a 64** over the readback bytes (`gpu::readback.cpp:160-179`); the sequence
hash folds the per-frame hashes in frame order. Only the **beauty** frame is hashed; AOVs drain
through a second ring specifically so they cannot perturb it.

This is the alignment and non-vacuity instrument §14 and §20 ask for, and it exists. It is also what
ADR-243's experiment used to prove none of its five arms was vacuous.

### 3.2 Progress counters

`RenderProgress`: `framesRendered` (submitted), `framesReadBack` (hashed), `framesWritten`
(**encoded and on disk — and this counts AOV files too**: the test asserts `10 + 10 * 5` for ten
frames with five AOVs), `framesTotal`, `lastFrameHash`, `sequenceHash`, `renderFps`, `finished`,
`cancelled`, `error`.

Completion log line: `render complete: {} frames in {:.1f}s ({:.1f} fps), {} written, sequence hash
{:016x}, GPU errors: {}`.

---

## 4. Render targets, AOVs, and the colour pipeline

### 4.1 The five scene targets

`src/rendering/scene_targets.hpp:1-43` — one MRT set shared by every scene-pass pipeline:

| # | target | format | contents |
|---|---|---|---|
| 0 | HDR radiance | `RGBA16Float` | scene-linear |
| 1 | normal + roughness | `RGBA16Float` | **rg = octahedral normal**, b = roughness, a = flags |
| 2 | velocity | `RG16Float` | screen motion in UV units |
| 3 | emission | `RGBA16Float` | rgb = emitted radiance, a = bloom weight |
| 4 | identifiers | `R32Uint` | low 16 bits object id, high 16 bits material id |

Plus `linearDepth_` (`R32Float`) and the `Depth24Plus` depth-stencil. **All of them are sized to the
scaled resolution** — `resize()` is the only consumer of `renderScale`, and it rescales before
`createAuxTargets`. That is the mechanical fact behind ADR-242's refusal.

There is **no shadow AOV, no albedo, no metallic, no volumetric-contribution and no exposure
target.** ADR-242 lists them as deliberately not added because nothing had asked for one.

### 4.2 `--aov` export

Five names (`render_settings.cpp:59-72`), all written as **single-part RGBA EXR, scene-linear,
ZIP-compressed**, one file per AOV per frame: `frame_000123.png` → `frame_000123.normal.exr`.
`depth` and `id` are **32-bit float** (a half is exact for integers only to 2048, and would quantise
metric depth); the other three are half. The normal pass is **oct-decoded on the CPU on the way out**
and texels no geometry wrote are left at `(0,0,0,0)` so the pass doubles as a surface matte.

The refusal, `src/app/render_settings.cpp:228-232`:

```cpp
if (supersample != 1.0f) {
    return fail("render: aov export and supersample {} cannot be combined -- an identifier, "
                "a normal and a depth edge have no correct downsample", supersample);
}
```

### 4.3 The colour pipeline, and where each output is taken from

Scene MRT (scene-linear `RGBA16F`) → post shader layers → built-in post chain (DoF, motion blur,
bloom, halation, anamorphic, lens, grade, **FXAA**) — *all still scene-linear HDR* → **tonemap pass**
(`shaders/tonemap.wgsl`: exposure multiply, operator ∈ {ACES, **AgX** default, Reinhard, Khronos PBR
Neutral, clamp}, chroma retention, vignette, grain, then `linearToSrgb`) → `RGBA8Unorm`.

Consequently:

* **PNG / video output is display-referred sRGB**, taken *after* the tonemap.
* **EXR output is scene-linear HDR**, taken from `hdrOutputTexture()` — the tonemap's *input*.
* **They are not the same image graded differently.** The EXR is post-bloom and post-grade but
  **pre-tonemap, pre-exposure-multiply, pre-vignette, pre-grain**. `environment.brightness` is
  applied *inside* the tonemap and is therefore **not baked into the EXR**.
* The PNG's bytes are sRGB-encoded but the texture is `RGBA8Unorm` and `stbi_write_png` writes no
  `sRGB`/`gAMA` chunk — so the file carries no colour tag. Any external tool must be *told* it is
  sRGB.

### 4.4 Two defects found while reading, both of which would corrupt a reference render

**(a) `--supersample` with `--format exr` is unguarded, and silently crops.**
`render_job.cpp:428-433` enqueues `hdrOutputTexture()` with extent `settings_.width ×
settings_.height`, but under supersampling that texture is `width·scale × height·scale`.
`CopyTextureToBuffer` with a smaller extent is legal and copies the **top-left crop**. `validate()`
refuses only the AOV combination. A supersampled HDR reference — the most natural thing a Quality Lab
would ask for — would come back as a quarter of the frame, at the right pixel dimensions, with no
error. **This is a real bug and it sits directly on the reference-render path.**

**(b) The supersample resolve is a bilinear tap, not a box filter.**
There is no resolve pass. The downsample is a side effect of the tonemap sampling the larger HDR
texture into the smaller output with `tonemapSampler_` (Linear min/mag, `mipmapFilter = Nearest`, no
mips). At 2× that reads four texels weighted by the bilinear footprint at each output pixel centre —
**not** an equal-weight average of the four samples. The quality win ADR-212 measured is real and
comes from higher-resolution shading plus that tap; it is not a correct box resolve, and a
"reference" built on it is a *better-sampled* image rather than a properly-filtered one. Documented
in [reference-rendering.md](reference-rendering.md) rather than treated as ground truth.

### 4.5 Readback and image types

`gpu::ReadbackRing` (`src/gpu/readback_ring.{hpp,cpp}`): N staging buffers, copy at the end of the
frame's own command buffer, async map, `poll()` returns completed frames **in submission order**,
`enqueue()` blocks only when every slot is in flight. Formats: `Rgba8, Rgba16Float, Rg16Float,
R32Float, R32Uint`. `blockedSeconds()` is the GPU-bound indicator.

`gpu::Image8` / `gpu::ImageF` (`src/gpu/readback.hpp:18-34`) — width, height, row-major top-left
`std::vector<std::uint8_t>` / `std::vector<float>` RGBA. **These are the image types a Quality Lab in
this repository would already be written against.**

`assets::writeExr / readExr` (tinyexr, RGBA, half or float), `assets::writePng / encodePng /
writeHdr` (stb), `gpu::hashImage` (FNV-1a over both image types), `gpu::writePpm`.

---

## 5. Profiling, benchmarking and the GPU lock

### 5.1 Two profilers that must never be confused

`core::PhaseProfiler` (`src/core/phase_profiler.hpp`) measures **CPU wall-clock on one thread**, per
named phase, in a 2048-frame ring, reporting order statistics — min, median, mean, p95, p99, max —
computed on demand. Its header states the rule this project learned:

> Why `min` matters too: this machine is usually running several builds. Contention is never
> negative, so on a contended machine the minimum over a long run is the honest estimate of the work
> itself and the median walks with whatever else is running.

It also has **groups** (`setFrameGroup`), which are the main thread's version of `--ab`: interleaved
arms inside one process, with the settling frames after a switch excluded rather than smeared.

`gpu::FrameTimeline` (`src/gpu/frame_timeline.hpp`) measures **GPU passes with GPU timestamps** — one
timestamp per pass at its end, so a pass's cost is the interval between consecutive markers and the
intervals partition the frame exactly. It reports `overflowed()` and `unwrittenLabels()` so a missing
mark is visible rather than silently charged to a neighbour.

### 5.2 The GPU lock, and what it does not do

`tools/gpu-lock.sh` is a **wrapper**: `tools/gpu-lock.sh <command> [args…]`. A directory mutex via
atomic `mkdir` at `${TMPDIR:-/tmp}/avgen-gpu.lock`, 5 s poll, 3600 s default timeout
(`AVGEN_GPU_LOCK_TIMEOUT`), exit 75 on timeout, stale locks reclaimed by pid.

**ADR-170 is the rule that matters and it is stronger than "use the lock":**

> the lock establishes exclusivity *among agents*, and a GPU timing is evidence only if the device
> was also free of anything that did not ask for the lock. Concretely: `pgrep avgen` before a timing
> batch, and record the result beside the numbers. A run that cannot say the device was quiet reports
> its milliseconds as a record of having taken them, not as a measurement.

And, decisively, the clause is necessary but **not sufficient** — three runs under the lock with
`pgrep` clean gave 10.945 / 11.272 / 13.697 ms on a byte-identical scene. The protocol that works:

> **Arms must be interleaved inside one process.** A comparison between two invocations of the same
> binary has a noise floor of about 3 ms on this machine.

Also load-bearing: *"the structural quantities moved 3.6% and the timings moved 280%"* — counters are
CPU-computed, deterministic and comparable across sessions; timings are not.

`ctest`'s `RESOURCE_LOCK gpu` only serialises this project's GPU tests against **each other**; it
cannot see a render or a sibling worktree. Timing-sensitive tests are therefore tagged `[.perf]`
(Catch2 hidden), absent from `ctest` entirely, and run deliberately and alone.

### 5.3 Benchmark harnesses that already exist

`--ab` / `--ab-blocks` (in-process interleaved arms), `--bench-json` (ADR-113 run record),
`tools/bench_ab.sh` (interleaved across binaries × scenes, with `@dir` to pin each binary to its own
shader tree — *"the baseline otherwise runs the new shader, which is how an afternoon was spent
chasing a rendering bug that did not exist"*), `tools/bench_world.sh`, `tools/render_bench.py`,
`tools/certify.py`, `tools/flicker_bench.py`, and the `[.perf]` test family.

---

## 6. Determinism: what is guaranteed and what is not

`core::Rng` is **PCG32**, seeded `forFrame(globalSeed, frameIndex, systemId)`. *"`std::random_device`
is banned."* `FixedStepClock` gives `renderTime = startTime + frameIndex / fps`.
`FrameTime::frameNonce()` is derived from **renderTime, not frameIndex** — because *"three renders
covering t=104s agreed on nothing, differing over 83% of the frame by about one value in 255"*.

**Guaranteed** (`docs/testing.md`): analyzer bit-identical regardless of chunking; offline engine runs
bit-identical in parameters and matrices; **renderer identical image hash for identical scene + time
on the same GPU**. Deterministic by construction: wind (pure function of packed parameters, position
and time), procedural instancing (`hashInstance(seed, index, channel)`), terrain/ecology/city
(function of the seed), particles (ADR-015 revision removed atomics after two headless orb runs
diverged after ~100 frames).

**Not guaranteed, and this is the list a full-reference metric must respect:**

* **Cross-GPU equality is not promised.**
* **Bit-identity holds within a mode at a fixed fps** — a 24 fps offline render and a 120 fps live run
  converge to the same envelope, not the same bits (ADR-012).
* Particle determinism is **per GPU family and driver** (fast-math and FMA contraction are compiler
  decisions); alive order is slot order, so alpha-blended systems remain unsorted (ADR-015).
* **ADR-091's live tier** — ambient population, props, background vehicles — is stateful, reset on
  seek, and **explicitly not frame-accurate under scrub**. *"A scrubbed frame is not the frame you
  would have reached by playing to that time — it is the reset frame."*
* **ADR-245's one impurity**: camera event spans accumulate `Run` state frame by frame; a seek clears
  the table.
* The legacy **orbit** camera mode integrates `orbitSpeed · dt`, so it depends on how the playhead got
  there. Free and Spline placements do not.

Practical consequence: a full-reference comparison must render **from the same start time** with the
same fps, not seek into the middle, and must avoid orbit cameras and ADR-091 live-tier content unless
the harness says so.

---

## 7. Scenes, projects and existing quality content

Scene format `avgen-scene` v1 (`src/scene/composition.{hpp,cpp}`), node kinds `Gltf, Orb, Grid,
Particles, Scene, Procedural, Field, Spline, Sdf, Terrain, Group, City`. Only `format` and `version`
are required; the smallest test fixture is
`{"format": "avgen-scene", "version": 1, "name": "fx", "nodes": []}`. 45 scene files under
`examples/`.

**`examples/qa/` is a purpose-built isolation ladder** and is the closest thing to §11's benchmark
suite that exists: `renderer-qa-minimal.scene.json` (three orbs; *"Everything absent here is absent
on purpose — no transparency, no skinning, no particles, no water, no terrain, no sky"*), then
`-character`, `-transparency`, `-water`, and the full `renderer-qa.scene.json`, driven by
`renderer-qa.json` at 1280×720/30.

Real content for §12: `examples/world/glowmere-valley-2.json` (1920×1080, 60 fps, **tier offline,
supersample 2.0, limits unlimited**), `-multicam` (0→30 s, 1800 frames), `glowmere-stylized.json`
(1920×1080, 30 fps, supersample 2.0), `glowmere-atmospherics.json`, `examples/city/night-shift.json`
(0→105 s). **ADR-171: "Glowmere Valley 2 succeeds `glowmere-stylized.scene.json`"** — not
`terrain.scene.json`, despite the index calling the latter "Glowmere Valley".

⚠️ Both Glowmere Valley 2 projects reference audio at `"../../../../../Desktop/Rebuild.mp3"` — an
absolute-ish escape out of the repo that will not relink elsewhere.

`renders/output-preview/` already contains a **two-arm quality comparison** driven by `--queue`:
arm A at 1280×720, `tier offline`, `limits unlimited`, `supersample 2.0`; arm B at 1200×676,
`tier realtime`, `limits live`, `supersample 1.0`, with `MEASUREMENTS.txt`.

---

## 8. Existing analysis code — the prior art, and the gap

### 8.1 Python, `tools/` (stdlib only)

| tool | what it computes |
|---|---|
| `image_stats.py` | **the PNG decoder everything else imports** (`zlib` + `struct`, 8-bit RGB/RGBA only); luminance histogram, tonal bands, highlight mass |
| `image_diff.py` | differing pixel count, largest channel delta, **bounding box** of the difference |
| `spatial_stats.py` | mean absolute spatial Laplacian over luma, whole-frame and per tile; `--compare` between two arms |
| `temporal_stats.py` | **peak** second temporal difference per pixel over the sequence, count over threshold, per-frame series, 8×8 tile map |
| `sharpness.py` | mean gradient magnitude — *"so a change cannot be accepted on the flicker number alone: a filter that blurs everything scores perfectly on flicker"* |
| `chroma_speckle.py` | opponent-chroma `(R−G, G−B)` 4-neighbour speckle; `--mask` visualisation. The metric ADR-212 quotes |
| `post_artifact_stats.py` | the anamorphic comb, measured from outside |
| `flicker_bench.py` | renders every arm through the real `--render` path, **proves non-vacuity**, runs the detector, analyses the tail not the head |
| `certify.py` | scene certification: exact counters, distributional timings, optional run-to-run pixel diff |
| `review_frames.py` | the fixed review frames at 1920×1080 per `docs/visual-quality.md` |
| `experiment.py` | records one controlled look-development experiment — change ONE variable, render, compare, write a record |

Every one of these is a legitimate ancestor of the Quality Lab, and three of them
(`flicker_bench.py`'s non-vacuity proof, `experiment.py`'s one-variable discipline, `sharpness.py`'s
"two numbers that pull in opposite directions") encode exactly the methodology §43 and §44 ask for.

### 8.2 C++

**There is no image comparison, diff or statistics code in `src/`.** What exists is confined to
tests: `meanAbsoluteDifference` in `test_output_preview_gpu.cpp:98`, a local `compare()` in
`test_phase_g_certification.cpp:297` and another in `test_composition_gpu.cpp:1203`. Determinism is
checked with `gpu::hashImage`, not pixel comparison.

**That is the gap the Quality Lab fills.**

### 8.3 No golden images, deliberately

There are no committed reference PNGs and no image-comparison test. `examples/qa/baselines/` holds
three **JSON state snapshots** (`"avgen-frame-snapshot"`) whose own note says *"pre-upgrade baseline:
the state the renderer derives, not the pixels it draws"*. `tools/certify.py` states the position:

> It compares against **the previous run**, not against a committed reference image, and that is a
> decision rather than an omission. A renderer upgrade changes pixels on purpose; an image baseline
> committed at the start of one is discarded on its first day and then teaches everybody to ignore
> the check.

`docs/testing.md` records the unbuilt intention: *"a perceptual comparison is planned for visual
regression."* This is the first request for one.

### 8.4 Stage-level post capture already exists

`PostProcessor::armCapture()` / `takeCapture()` (`src/rendering/post_processor.hpp:117-122`) records
**every intermediate target the post chain renders**, named, for one frame, and disarms itself. This
is how `docs/post-artifact-forensics.md` localised the anamorphic lattice to `fs_wide` stage 6 rather
than guessing — the document whose opening rule is *"a post chain judged on its final frame cannot be
debugged."* It is a §32 diagnostic facility that exists and that nothing outside a test uses.

---

## 9. Documentation and registration obligations

There is **no `CONTRIBUTING.md`, no `AGENTS.md`, no `CLAUDE.md`, and no `docs/README.md`.** The doc
TOC is the `## Documentation` section of the repo-root `README.md`. A new subsystem must be
registered in:

1. **`docs/decisions/README.md`** — the ADR table row. Filenames `ADR-NNN-kebab-slug.md`, zero-padded.
   Convention: *"Status, Context/Problem, Alternatives considered, Decision, Rationale, Consequences,
   Rejected alternatives (with the decisive reason), and Revisit triggers. Records are immutable once
   Accepted."* Modern ADRs additionally carry **"What was measured"** with real numbers and
   **ADR-182 fail-capable probes** — effectively mandatory for anything quality- or
   performance-related. Highest existing: **ADR-246**.
2. **root `README.md` → `## Documentation`**.
3. **`docs/architecture.md`** §2 (modules and ownership) and §8 (directory layout), if a new `src/`
   module appears.
4. **`docs/testing.md`** — the per-subsystem coverage table.
5. **`docs/dependencies.md`** — licence and "why this one" for any new CPM dependency (ADR-008).
6. **`docs/project-format.md`** — any new serialized block. *Currently stale*: it documents the
   retired `autoDirector` `wide`/`hero`/`hold*` keys and omits `tier`/`supersample`/`limits`/`aovs`
   from the render block.
7. **`examples/index.json`** — a new example world is invisible unless registered.
8. The **help system is machine-enforced** (`avgen_help_lint`): a new UI panel or command produces
   `undocumented-panel` / `undocumented-command` warnings until a `docs/help/*.md` topic covers it.
   *(A CLI-only tool with no panel does not trip this.)*

`docs/visual-quality.md` already holds the **human review rubric** — fourteen criteria scored 1–10,
including *"Temporal coherence: stable under motion — no boiling, crawling or popping?"* — and a
fixed-review-frame list. §24's human-validation loop should extend this, not compete with it.

---

## 10. What Phase 0 concludes

| §26 asked about | finding |
|---|---|
| root structure / build | CMake + Ninja + CPM, five presets, three libs, one app, seven tools, two test binaries |
| dependency management | everything pinned; **no ffmpeg, no libvmaf, no numpy, no OpenCV on this machine**; stdlib-only Python convention; one precedent for an optional external process |
| tests | Catch2 v3, `unit`/`gpu` labels, `[.perf]` hidden, **no image goldens by decision** |
| tools | 7 C++ build targets, 7 shell scripts, 30 stdlib Python scripts; eleven are analysis tools |
| renderer APIs | `SceneRenderer`, `PostProcessor` (+ `armCapture`), `QualitySettings::forTier` |
| scene loading | `avgen-scene` v1 / `avgen-project` v4; **the project is where the image parameters live** |
| render entry points | `avgen --project … --render …`, `--queue` for batches; headless and windowless |
| AOV infrastructure | five passes, EXR, ADR-242; **refused with supersampling**; no shadow/albedo/metallic AOV |
| profiling | `PhaseProfiler` (CPU, order statistics, groups) and `FrameTimeline` (GPU timestamps) |
| benchmark infrastructure | `--ab`, `--bench-json`, four shell/Python benchmark drivers, `[.perf]` tests |

**Three things that shape everything downstream:**

1. **The metric engine cannot assume ffmpeg, numpy or a package manager.** The house style is a
   pinned C++ tool linking `avgen_core` — which already has tinyexr, stb, `ImageF`, `Image8` and
   `hashImage`. Anything external must follow the ffmpeg precedent: optional, found at runtime,
   degrading to "this metric is unavailable" rather than failing.
2. **The determinism and non-vacuity instruments already exist** (`sequenceHash`, `--ab`,
   `flicker_bench.py`'s pattern). The Quality Lab should consume them, not reinvent them.
3. **Two defects sit on the reference-render path** — the unguarded `--supersample` + `--format exr`
   crop, and the bilinear-rather-than-box resolve. Both are recorded here before any reference
   strategy is designed on top of them.
