# Quality Lab research: what each instrument measures, and what it would tell us here

Status: research phase (spec §1, §39 Phase 1, §50 STEPS 2–9). **No implementation follows from this
document alone.** Its job is to decide which instruments are worth building or adopting, which are
not, and — for the ones adopted — what each one is *not* allowed to be used to conclude.

Companion documents: [repository-reconnaissance.md](repository-reconnaissance.md) (what already
exists), [metrics.md](metrics.md) (the chosen metric set and its definitions),
[artifact-detection.md](artifact-detection.md) (the detectors), [reference-rendering.md](reference-rendering.md)
(how a reference is produced), [benchmark-scenes.md](benchmark-scenes.md),
[experiments.md](experiments.md), [architecture.md](architecture.md).

---

## 0. The two findings that reorganised this research before it started

This project has already run the experiment that most quality-tooling efforts run at the end, and it
has the scar. Both results below are this repository's own, not literature, and both of them change
what the Quality Lab should be.

### 0.1 ADR-243: a correctly-implemented temporal detector was anti-correlated with the eye

`tools/temporal_stats.py` measures the second difference in time, `|x(t+1) − 2x(t) + x(t−1)|`. It is
a correct implementation of a defensible idea: smooth motion scores near zero however fast it moves,
alternation scores high. On the artifact a human reviewer actually reported — wind-animated grass
blades thinner than a pixel, jagged where a blade meets what is behind it — it ranked the arms
**backwards**, on both arms, in both directions:

| arm | temporal 2nd-difference | spatial mean \|Laplacian\| | the reviewer |
|---|---:|---:|---|
| FXAA off | −42% ("better") | +31% (worse) | *"more distracting"* |
| supersample 2× | +9% ("worse") | −9% (better) | *"much calmer"* |

The mechanism is not a bug in either instrument. The reviewer was objecting to **spatial aliasing on
moving geometry**; the detector measures **temporal alternation**; and the two remedies for the first
(edge filtering, higher sampling) both *raise* the second — FXAA's per-frame edge decision flips on
pixels near its threshold, and supersampling resolves more sub-pixel detail into the frame and so
puts more edges near threshold.

Three consequences for everything below:

1. **A detector's name is not its meaning.** "Temporal stability" was the label; "temporal
   alternation of the luminance signal" was the measurement; "spatial aliasing on moving geometry"
   was the artifact. The Quality Lab must name each metric after **what it computes**, and state the
   artifact class it is licensed to speak about, separately.
2. **No metric ships without a human-anchored direction check.** Not calibration — direction. If
   metric and eye disagree on a constructed pair, that is recorded as a limitation of the metric, per
   §44, and never resolved by reweighting until the metric agrees.
3. **A single-signal verdict is how this happened.** Priority 1's whole attribution programme ran on
   one instrument. The structural fix is a quality *vector* with a spatial measure beside every
   temporal one (§8, §9), which is what the spec demands and what this repository learned the hard
   way.

### 0.2 ADR-242: AOV export and supersampling are refused together, correctly

`--aov` exports normal / emission / depth / velocity / id as EXR. `--supersample` renders at a
multiple and resolves down. They are refused together at `validate()`, because the auxiliary targets
are sized to the **scaled** resolution and three of the five have no correct downsample: *averaging
two normals is not a normal, averaging two identifiers is a third object, and averaging two depths
across a silhouette is a surface that is not there.*

The spec walks straight into this: §6 wants the Quality Lab built on AOVs, §13 leans on
supersampling for reference renders, and §15's motion-compensated temporal analysis wants velocity
from the same render. See [§7 below](#7-the-aov--supersampling-collision-and-how-the-lab-resolves-it)
for the resolution — which is a *design* answer, not a request to relax the refusal.

---

## 1. Full-reference video quality metrics

### 1.1 VMAF, and why it is adopted with a narrow licence to speak

VMAF fuses elementary features (VIF at multiple scales, a detail-loss measure, and a motion feature)
through a trained regressor, into a 0–100 scale. **v1 (June 2026)** replaced the v0 family: it folds
normalized viewing distance directly into the feature calculations and trains one consistent model,
re-applied at 3H (standard 1080p), 5H (phone), 1.5H (discerning 4K) and a consumer 4K-at-3H condition
whose range extends to 110 so it can express 4K's benefit over 1080p at the same distance. v1 models
require libvmaf newer than 3.2.0.

**The decisive question is not whether VMAF is good. It is whether AV Gen's distortions are inside
its training domain, and mostly they are not.** VMAF was trained on **compression and scaling**
artifacts in **camera-captured** content. Our primary distortions are aliasing, shimmer, shadow
swimming, specular sparkle, LOD popping and banding in synthetic, stylized imagery. Two independent
lines of published work say this matters: gaming-content VQA studies find general-purpose metrics
weaken on synthetic, high-motion, repetitive content, and the rendered-video VQA literature states
plainly that metrics designed for natural video — VMAF, LPIPS, PSNR and SSIM named together — do not
correlate well with human judgment of *rendered* artifacts, which are disproportionately temporal.
VMAF's own temporal modelling is thin: beyond a frame-difference motion feature it does not model
temporal masking, which is precisely the axis we care most about.

**Decision: adopt, scoped.** VMAF is retained for exactly one job it is genuinely the best available
tool for — **§9's "compression resilience" dimension**, the question "how much of this master
survives delivery encoding?", which *is* compression distortion on the content VMAF was built for
(candidate = encoded master, reference = master). It is reported in the quality vector for
render-configuration comparisons too, because it is cheap once libvmaf is present and a strong
disagreement between VMAF and the graphics-native metrics is itself information. **It is not
permitted to rank AA configurations on its own**, and the report must carry that limitation next to
the number (§18, §19 `limitations`). Pooling: report mean **and** the 5th-percentile/minimum over
frames, never mean alone — a 3-frame pop is invisible in a mean over 300 frames, and popping is an
artifact class we are specifically hunting.

### 1.2 PSNR — kept as a sanity rail, not as quality

Measures mean squared error on a log scale. It does not measure anything perceptual: it cannot
distinguish an error concentrated on one silhouette from the same energy spread over the sky, and it
is dominated by exposure and gamma. **Kept**, because it is free, and because it is the fastest
possible **alignment and determinism check**: two renders that should be bit-identical give infinite
PSNR, and a PSNR that collapses between arms that changed one post parameter is evidence the frames
are misaligned rather than that quality changed. That is a real job and it is the only one PSNR gets.

### 1.3 SSIM / MS-SSIM — kept, with the aliasing caveat stated

Structural similarity over local luminance, contrast and structure windows; MS-SSIM does it over a
pyramid, which makes it markedly better at our scale range. **What it does not measure:** it is
insensitive to a uniform shift in colour, and — the caveat that matters here — a *staircased* edge
and a *resolved* edge differ in local structure, so MS-SSIM does respond to aliasing, but it responds
with the same sign to genuine detail. It cannot separate "more high-frequency structure because the
image is better resolved" from "more high-frequency structure because the image is aliased" any more
than `spatial_stats.py` can. **Kept** as a broad spatial-fidelity signal; **never** used as the
tie-breaker between an AA arm and a no-AA arm, which is the exact comparison it is blind on.

### 1.4 PSNR-HVS / PSNR-HVS-M — marginal, adopted only because it is free

A DCT-domain PSNR weighted by a contrast sensitivity function, with the -M variant adding contrast
masking. It is a real improvement on PSNR and a real step below SSIM-family metrics. It comes free
in libvmaf. **Adopted as a secondary, with no decision authority** — if it ever disagrees with
MS-SSIM in a way that changes a conclusion, that disagreement is a finding to investigate, not a
number to average.

### 1.5 CIEDE2000 — adopted for one specific dimension

Perceptual colour difference in CIELAB with the 2000 corrections. It is the right instrument for
**chroma retention** (§9's HDR/colour dimension) and it answers a question this project has already
asked in anger: the bioluminescence work is explicitly about chroma surviving tone mapping and
bloom. libvmaf provides it. **Adopted**, scoped to colour, with the caveat that it is defined for
surface-colour comparison under a reference illuminant and is being used here on tone-mapped emissive
content well outside that context — so it is a *relative* measure between arms of one view, never an
absolute ΔE claim.

### 1.6 ꟻLIP — the strongest single adoption on this list

`ꟻLIP` (Andersson et al., HPG 2020; NVIDIA) is a difference evaluator **built specifically for
rendered images against a reference**, and it is the only widely-used full-reference metric on this
list whose design context is ours rather than streaming video. It models what a human perceives when
**alternating** between two images — colour difference through a spatial-CSF-filtered opponent-colour
pipeline, amplified by an edge/point-detection term so that differences on *edges and features* count
for more than differences in flat regions. That amplification is exactly the weighting our artifacts
need: our defects live on silhouettes, thin geometry and highlights.

Three properties make it decisive here rather than merely nice:

* **It outputs an error *map* first and a pooled number second.** §17 and §32 demand "show me what
  caused this number" for every metric, and for ꟻLIP that is the primary output, in a magma
  colour-mapping designed to be read. Most metrics on this list need a diagnostic invented for them;
  this one arrives with the diagnostic as the point.
* **HDR-ꟻLIP exists** ("Visualizing Errors in Rendered High Dynamic Range Images"), evaluating over a
  range of exposures. Our EXR path is linear HDR, so we can compare *before* tone mapping, which
  separates "the renderer produced different light" from "the tone mapper mapped it differently" —
  a separation this repository has repeatedly needed and repeatedly had to improvise.
* **BSD 3-Clause**, C++ with a CPU-only build (CUDA optional), a single-header integration
  (`FLIP.h`), a Python/CLI front end, and it already vendors `tinyexr` and `stb_image` — the same
  file formats our renders come out in.

**Risk, recorded honestly:** the repository does not document macOS/Apple Silicon support, and the
C++ path's first-class build is CMake-plus-Visual-Studio. Apple Silicon viability is an **open item
for the vertical slice**, not an assumed fact; the pure-C++ backend has no platform-specific
dependency visible, but that must be *built* before ꟻLIP is written into the architecture as load
bearing. If it does not build, the Python `flip-evaluator` package is the isolated fallback and
costs us nothing architecturally, because the Quality Lab is already permitted to be heterogeneous
(§3) and is already outside AV Gen's runtime (§49.7).

### 1.7 ColorVideoVDP — the most capable temporal instrument, and the one to pilot, not to depend on

ColorVideoVDP (Mantiuk et al., SIGGRAPH 2024) is the first metric to model spatio-temporal **and**
chromatic vision together, built on measured chromatic spatiotemporal contrast sensitivity and
cross-channel masking, and it accounts for display geometry and photometry explicitly. On paper it is
the only instrument on this list that could, in principle, have settled the ADR-243 dispute on its
own, because flicker and spatial aliasing enter it through the same modelled visual system rather
than through two detectors with opposite signs.

Against that: it is **PyTorch**, it wants Python 3.12, it is slow on CPU for video, it requires a
**physical display specification** (size, resolution, peak luminance, viewing distance, ambient
light) as mandatory input, and its output is in JOD units on a scale whose top is 10. It reports MPS
selected by default on Apple Silicon with torch ≥ 2.1, which is the good news.

Two cautions. The mandatory display model is not a nuisance — it is §10's "quality is conditional on
target delivery" made unavoidable, and it is a genuine *argument for* the metric. But it also means
every ColorVideoVDP number is only comparable to another taken under the identical display
declaration, which must be part of the target profile and part of the report. And the licence is
reported by GitHub's detector as MIT; **that must be read from the LICENSE file before adoption**,
because sibling metrics from the same group have historically shipped research-only terms.

**Decision: pilot in Phase 3, do not put on the critical path.** It is the best candidate to become
the Quality Lab's temporal authority, and it is exactly the kind of heavyweight Python/ML dependency
§27 and §49.8 forbid from becoming mandatory. It lives behind an optional-dependency boundary and
earns its place by validated agreement with human judgment on constructed pairs, or it does not get
one.

### 1.8 LPIPS — rejected as a required metric

LPIPS compares deep features (AlexNet/VGG/SqueezeNet, ImageNet-trained) calibrated on the BAPPS
human-preference dataset. **Rejected from the required set**, for reasons the spec asked for rather
than assumed:

* Its features are ImageNet-derived — trained on photographs of natural objects. Our content is
  stylized synthetic imagery with emissive materials and procedural vegetation, which is outside that
  distribution in both statistics and semantics.
* The rendered-video VQA literature names LPIPS alongside PSNR/SSIM/VMAF as failing to correlate with
  human judgment on rendered artifacts.
* It is per-frame. Our hardest problems are temporal, and a per-frame perceptual metric is precisely
  the thing §5 warns about ("a frame can look excellent while the video looks terrible").
* It costs a PyTorch dependency, which ꟻLIP delivers most of the benefit of without.
* **It does not provide information unavailable from our other metrics** — which is §47's last
  question and the one that decides it. ꟻLIP occupies the same slot (perceptually-weighted
  full-reference spatial difference) with a graphics-native design and a BSD licence.

It stays on the list as an *optional* comparator for the human-validation study (§24), where the
question is which objective metric best predicts human preference and LPIPS is a legitimate entrant.
It is not a metric the Quality Lab reports by default.

### 1.9 Butteraugli, SSIMULACRA2 — noted, not adopted

Designed for still-image *compression* quality at the visually-lossless threshold. Same domain
mismatch as VMAF with none of VMAF's video pooling, and they duplicate ꟻLIP's slot. Recorded so the
next person does not re-open the question.

---

## 2. No-reference and artifact-specific measures

§7B is the half that matters most, because "two configurations can be equally different from a
reference while having very different perceptual failure modes" is not hypothetical here — it is
ADR-243's finding restated.

### 2.1 CAMBI — adopted for banding, unmodified

Contrast-Aware Multiscale Banding Index. No-reference by default (full-reference mode exists via
`--full_ref`), 0 to ~24, with ~5 the threshold where banding becomes "slightly annoying". Operates
frame-by-frame with no temporal information, on luma.

It is adopted **because banding is the one artifact class on our list where a standard, validated,
no-reference detector already exists and our content is squarely in its domain** — volumetric fog,
atmospheric gradients and bloom falloff produce exactly the smooth-gradient-into-steps failure CAMBI
was built for. Caveats recorded: it is frame-local (a band that crawls between frames scores the same
as a static one, so it needs a temporal partner), it is luma-only (chroma banding in saturated
bioluminescent gradients is out of its scope), and its threshold parameters assume a display
brightness we must pin in the target profile.

### 2.2 The repository's existing spatial and temporal statistics — kept, renamed, re-scoped

`tools/spatial_stats.py` (mean |Laplacian|) and `tools/temporal_stats.py` (second temporal
difference) both stay, because both are correct measurements and both are already validated *as
measurements*. What changes is the contract around them, per ADR-243 and §44:

* They are named after their computation, not after an artifact.
* Neither is ever reported without the other beside it.
* `spatial_stats.py`'s own header states the limit that decides its use: **it cannot separate
  aliasing from detail**, so it is meaningful only between arms of one view, never between scenes and
  never as an absolute bar. That rule is inherited by every derived measure.

### 2.3 Motion-compensated temporal residual — the detector this project is missing

§15's construction — warp frame *N* by the velocity AOV, compare against actual frame *N+1*, and the
residual is *unexplained* temporal change — is the single highest-value thing on this list that does
not exist yet. It is the direct answer to §5's requirement to distinguish scene motion from unwanted
temporal variation, and it is the thing that would have separated ADR-243's two arms: wind-animated
grass *moves*, and a measure that subtracts predicted motion first stops charging the animation for
the artifact.

Sources of motion, in preference order: **the engine's velocity AOV** (exact, free, already
exported) over optical flow (estimated, expensive, and wrong in exactly the thin-geometry regions we
care about). §6's instruction not to duplicate renderer information with computer-vision
approximations is correct and this is its strongest case. Optical flow is retained only as the
*validation* of the velocity AOV — if a flow estimate and the velocity buffer disagree
systematically, that is a motion-vector defect, which is itself one of §4's artifact classes.

Known failure mode, stated before it is built: **disocclusion**. Where frame *N* has no information
about what frame *N+1* reveals, the residual is large and it is not an artifact. The depth and ID
AOVs are what identify those pixels, and they must be masked out rather than scored — a residual
measure without disocclusion masking would report camera motion as instability, and would be another
correctly-implemented detector that ranks the wrong thing.

### 2.4 AOV-gated per-class stability — how shadow, specular, LOD and vegetation get separate numbers

§9 demands shadow quality, specular quality and detail retention as *separate* dimensions, and the
honest way to produce separate numbers is not four separate algorithms — it is **one residual measure
evaluated over four masks derived from AOVs**:

| dimension | mask source | what the residual then means |
|---|---|---|
| specular stability | emission AOV + roughness (alpha of the normal AOV) | unexplained temporal change on low-roughness / emissive pixels |
| geometric vs shading instability | normal AOV | a normal that changed means geometry moved; a normal that did not while colour did means shading is unstable |
| LOD popping | id AOV | an identifier changing on a pixel where depth and velocity say the surface did not move |
| vegetation / thin-geometry | id AOV restricted to vegetation identifiers | the residual over exactly the content ADR-243's reviewer objected to |
| shadow stability | **no AOV exists** — see below | — |

**Shadow is the gap.** ADR-242 lists `shadow` among the debug views explicitly *not* added because
nothing had asked for one. The Quality Lab is a consumer that would ask. That is a real proposal with
a real cost (a new target or a new pass) and it is recorded here as a **decision for the owner**, not
taken unilaterally — see the report's human-decision list. Until then, shadow stability can only be
measured indirectly, over regions the lighting model says are shadowed, and that limitation is
reported rather than papered over.

### 2.5 Blur / detail retention, aliasing, and the measures deliberately not invented

* **Detail retention** is measurable as radially-averaged power-spectrum ratio between candidate and
  reference — how much high-frequency energy survived — which is well-defined, cheap, and
  interpretable. Adopted.
* **Aliasing**, no-reference, has no standard we should adopt. The literature's directional-energy
  decomposition (partition patch energy into signal and aliasing components along the dominant
  orientation) is sound and independently implementable, and the patent literature in this area is a
  reason to implement from the described principle rather than from any particular implementation.
  **Deferred to Phase 4** behind the full-reference route, which is much stronger: with a
  supersampled reference, aliasing is simply *the high-frequency difference from a correctly-sampled
  image*, and we do not need to infer it from one frame.
* **Not invented:** a "ghosting metric", a "sharpening metric" and a "particle instability metric" as
  separate algorithms. Each is the §2.3 residual over a different mask or a different sign, and
  three names for one measurement is how a metric suite becomes unfalsifiable.

---

## 3. Libraries, and the C++ / Python / CLI boundary — decided by the machine, not by taste

§3 and §27 ask which components should be native C++, external CLI, Python, or optional. The
reconnaissance turned this from a preference into a constraint, and the finding is worth stating
plainly because it inverts what a quality-tooling plan normally assumes:

| probed | result |
|---|---|
| ffmpeg | **not installed on this machine** |
| libvmaf | **absent** — no ffmpeg, nothing in `pkg-config` |
| OpenCV / OpenEXR / Imath | **absent** |
| python3 | **3.9.6**, Apple's system Python — no venv, no `requirements.txt`, no `pyproject.toml` |
| numpy / Pillow / scipy / scikit-image / matplotlib | **not installed, and used nowhere in the repo** |

All thirty Python tools in `tools/` are **standard-library only**. `image_stats.py` hand-rolls a PNG
decoder out of `zlib` and `struct` specifically to avoid a dependency. This is an unwritten but
unbroken convention, and it sits on top of ADR-008's rule that every dependency in any language is
pinned.

Meanwhile the C++ side already has everything an image metric needs, in `avgen_core`, which is
**GPU-free**: **tinyexr** (read and write EXR), **stb** (PNG), `gpu::ImageF` and `gpu::Image8`
(row-major RGBA float / byte images), `gpu::hashImage`, `nlohmann_json`, `fmt`. And `tools/` states
the house pattern for exactly this shape of thing:

> Command-line tools that link the GPU-free core. They are build targets rather than scripts so a
> world can be inspected with exactly the code the engine samples, not a reimplementation of it.

**Decision: the metric engine is a native C++ tool target linking `avgen_core`, and every external
tool is optional.** The reasoning, in order of weight:

1. **A pure-stdlib Python metric engine is not viable at this scale.** A 300-frame 1920×1080
   sequence is 1.8 × 10⁹ pixel visits per pass. `image_stats.py`'s hand-rolled decoder is fine for a
   handful of frames and is the wrong instrument for a sequence. Adding numpy to fix that breaks the
   repo's only unbroken convention, on a system Python with no venv and no lockfile.
2. **The C++ route adds zero dependencies.** tinyexr and stb are already linked; `ImageF` is already
   the type every readback produces.
3. **It keeps §49.7 and §49.8 satisfied by construction.** A separate tool binary is outside AV Gen's
   runtime by definition, and nothing about it touches the app.
4. **It matches the repo's existing precedent for external tools exactly.** `docs/dependencies.md`
   already classifies ffmpeg as *"optional, external process… never linked, never shipped, never
   downloaded"*, found at runtime via `AVGEN_FFMPEG` / `PATH` / `/opt/homebrew/bin`. There is
   precedent for an optional external process and **no precedent for a hard dependency in any
   language**.

Python is retained where it is already good and already conventional: the **report generator**, the
**experiment driver**, and the existing statistics tools — orchestration and text, not per-pixel
arithmetic.

The consequence to be honest about: **VMAF, PSNR-HVS and CAMBI are not available on this machine
today.** They arrive only if someone runs `brew install ffmpeg` (or libvmaf). That is a **human
decision**, recorded as such, not something to be assumed by a plan. Every metric that depends on it
must degrade to *"unavailable on this host"* in the report rather than failing the run — which is
also §19's `limitations` field doing its job.

See [architecture.md](architecture.md) for the resulting layering.


---

## 4. Reference rendering

Full treatment in [reference-rendering.md](reference-rendering.md). The research conclusions:

**A supersampled render is not ground truth, and the document says so in those words.** §13 asks for
this to be documented and it is the easiest thing in this whole programme to get wrong. An 8×
render is a *better-sampled* image of the same model; it does not correct a wrong shadow bias, a
wrong normal, a temporally-unstable shader, or a tone mapper that clips. It converges the *sampling*
error and nothing else. Everything the Quality Lab says against a supersampled reference is
therefore of the form "how much information was lost relative to a better-sampled render of the same
thing" — which is §7A's framing, and is a genuinely useful question, and is not "how good is this".

Its practical limits here are also known in advance: memory and time scale with the square of the
factor; ADR-212 measured 2× at 2.7× render time and the reviewer judged that worth paying; and the
factor cannot be raised indefinitely because the auxiliary targets scale with it.

---

## 5. Determinism and frame alignment

§14 is upstream of every full-reference number, and the reconnaissance answer is better than the spec
assumes: **this is largely already built.** `RenderJob` constructs per-frame hashes and a sequence
hash, and ADR-242 records the design rule that protects them — the AOV readback was deliberately put
on its own ring so that five extra copies per frame could not perturb the ordering the hashes are
built from, *because the sequence hash is the deliverable's proof*.

So the Quality Lab's alignment discipline is: **the sequence hash is the alignment check**. Two arms
that differ only in a post parameter must produce identical *geometry*; where a harness expects two
arms to be identical and the hashes differ, the experiment is void and is reported void rather than
measured. This is also the mechanism that makes ADR-243's "every arm's sequence hash distinct so none
was vacuous" check reusable — it is ADR-182's "show the probe can fail" applied to the experiment
harness itself.

Known-nondeterministic systems are enumerated in the reconnaissance document; anything on that list
is a scene the Quality Lab must not run full-reference metrics against without saying so.

---

## 6. Dependency matrix

Full §27 fields in [architecture.md](architecture.md). The decisions, with the availability column
that the reconnaissance made the deciding one:

| dependency | licence | Apple Silicon | on this machine | role | status |
|---|---|---|---|---|---|
| **tinyexr 3.2.0** | BSD-3 | yes | **already linked** | EXR read/write | in use, no action |
| **stb_image / stb_image_write** | MIT / public domain | yes | **already linked** | PNG | in use, no action |
| **nlohmann_json, fmt** | MIT | yes | **already linked** | report JSON, formatting | in use, no action |
| **Catch2 v3** | BSL-1.0 | yes | **already linked** | metric unit tests | in use, no action |
| Python 3 stdlib | PSF | yes | 3.9.6 | report generator, drivers, existing stats tools | in use, no action |
| **ffmpeg + libvmaf** | libvmaf BSD-2-Clause-Patent; ffmpeg LGPL or GPL by configuration | yes | **ABSENT** | VMAF, PSNR-HVS, CAMBI; and encoding for compression-resilience | **optional; needs a human decision to install** |
| **ꟻLIP** | BSD-3 | **unverified** | absent | graphics-native full-reference difference + error map; HDR variant | proposed; must be built before it is relied on |
| ColorVideoVDP | reported MIT — **verify the file** | MPS with torch ≥ 2.1 | absent | spatio-temporal + chromatic, JOD units | optional, piloted, off the critical path |
| LPIPS / PyTorch | BSD-2 / BSD-3 | yes | absent | comparator in the human-validation study only | **rejected as a metric** |
| OpenCV | Apache-2.0 | yes | absent | optical flow, only to validate the velocity AOV | optional, deferred |
| Optuna / BoTorch+Ax | MIT / MIT | yes | absent | future optimizer (§23) | not now, by §23's own instruction |

Three notes that would otherwise be discovered late:

**ffmpeg's licence is a live question, not a footnote.** ffmpeg is LGPL or GPL depending on
configuration; libvmaf itself is BSD-2-Clause-Patent and imposes nothing. Because the Quality Lab
invokes ffmpeg as a **separate process** and ships none of it — the stance `docs/dependencies.md`
already takes — the exposure is developer tooling, not redistribution. Recorded so nobody later
links libavfilter into a shipped binary on the strength of this document.

**Everything native must go through CPM, pinned, with a `docs/dependencies.md` row** carrying a
licence and a "why this one" (ADR-008). That applies to ꟻLIP if it is vendored.

**Nothing here is permitted to become a hard dependency of `avgen`.** The Quality Lab is a separate
binary; the engine's dependency list does not change.


---

## 7. The AOV / supersampling collision, and how the Lab resolves it

The spec wants both (§6, §13, §15). ADR-242 refuses both together, for correct reasons. **The
resolution is not to relax the refusal.** It is that the two renders serve different purposes and
neither needs to be the other:

| render | flags | what it is for |
|---|---|---|
| **candidate** | production config, `--aov normal,depth,velocity,id,emission`, no supersampling | the image under test, plus the masks and motion that every no-reference and temporal measure needs |
| **reference** | `--supersample N`, no AOVs | the better-sampled image the full-reference metrics compare against |

Full-reference metrics need only the beauty pass, which the reference has. AOV-driven detectors
operate on the candidate, which has AOVs at its own native resolution where they are exactly correct.
**Nothing needs an AOV at supersampled resolution**, and once that is seen the refusal costs the
Quality Lab nothing.

There is one case the split does not cover, and it is named rather than hidden: a **masked
full-reference metric** — "ꟻLIP error restricted to vegetation pixels" — needs the candidate's ID
mask aligned to the reference's pixels. That is a mask taken at candidate resolution and applied to a
reference downsampled to candidate resolution, which is well-defined, and is the operation to use.

**If the refusal is ever to be lifted** — and the Quality Lab is the first consumer with a reason to
want it — ADR-242 already names the shape of the fix, and this document endorses it: not one filter
for five targets, but a **resolve per target**, chosen by what the target *means*:

| target | correct resolve | why the general filter is wrong |
|---|---|---|
| `id` | **nearest** (or majority of the contributing samples) | averaging two identifiers names a third object that is not in the scene |
| `normal` | average the **decoded** vectors, then **renormalise** | the stored value is octahedral, so averaging the encoding is not averaging the normal; and the mean of two unit vectors is not a unit vector |
| `depth` | **silhouette-aware**: nearest-sample, or an average only among samples within a depth threshold of the closest | averaging across a silhouette invents a surface at a depth where nothing exists |
| `velocity` | average, masked the same way depth is | a velocity averaged across a silhouette is two objects' motion attributed to one |
| `emission` | plain average | it is radiance; averaging radiance is what a resolve is |

This is recorded as a **proposal with a named consumer**, which is ADR-242's own bar for adding
anything here. It is not scheduled, because §7's split means the Quality Lab does not need it to
work.

---

## 8. Optimization (§23) — research only, no implementation

§23 says design for it and do not build it, and §52 says do not optimize until measurements are
validated. Both are respected. The recorded findings, for whoever picks this up after metric
validation: the problem is **multi-objective and constrained** (maximise a quality *vector* subject
to GPU frame time and memory budgets), evaluations are **expensive** (a render), and the budget will
be **small** (tens to low hundreds of evaluations), which rules out evolutionary methods that need
thousands and points at Bayesian approaches — TPE (Optuna, MIT, well-suited under ~1000 trials,
NSGA-II available for multi-objective) or Gaussian-process Bayesian optimization with
hypervolume-based acquisition (BoTorch/Ax, MIT, qEHVI/qNEHVI for the Pareto case, at the cost of a
PyTorch dependency).

The architectural requirement that follows, and the only thing that must be true *now*, is that a
renderer configuration is a **serialisable parameter vector** and a result is a **serialisable
quality vector**, so an optimizer can be attached later without touching anything that produces
either.

---

## 9. What this research rejected, and why

| rejected | reason |
|---|---|
| a single composite "quality score" | §8, §52, and ADR-243: one number is how a correctly-implemented detector got to rank remedies backwards with nothing beside it to contradict it |
| LPIPS as a required metric | ImageNet features on stylized synthetic content; per-frame only; named in the rendered-VQA literature as poorly correlated; occupies ꟻLIP's slot at the cost of PyTorch |
| VMAF as the arbiter of render configuration | trained on compression and scaling of camera-captured video; weak temporal modelling; our distortions are out of domain. Retained for compression resilience, where it is in domain |
| Butteraugli / SSIMULACRA2 | still-image compression metrics; duplicate ꟻLIP's slot with a worse domain fit |
| optical flow as the primary motion source | the velocity AOV is exact and free; flow is an approximation that is worst in thin geometry, which is where our artifacts are (§6) |
| training a perceptual model on our content | §25 and §45 forbid it until a human-rated dataset exists. There is no dataset |
| a brute-force parameter sweep | §21 — combinations explode; the design supports intelligent selection instead |
| separate "ghosting" / "sharpening" / "particle" detectors | each is the motion-compensated residual under a different mask or sign; three names for one measurement makes a suite unfalsifiable |
| relaxing ADR-242's refusal to make §13 work | unnecessary — the candidate and the reference are different renders with different jobs (§7) |

---

## 10. Unresolved questions carried into the next phase

Six of these are genuine **human decisions** and are flagged as such rather than decided here.

1. **[HUMAN] Install ffmpeg + libvmaf on this machine?** Without it, VMAF, PSNR-HVS and CAMBI do not
   exist here, and neither does the compression-resilience dimension (§9), which needs an encoder.
   `brew install ffmpeg` pulls roughly eighty transitive formulae; `brew install libvmaf` alone is
   much smaller but gives no encoder. The repo already treats ffmpeg as an optional external process,
   so this breaks no rule — it is a machine-provisioning choice with a real cost.
2. **Does ꟻLIP's C++ backend build on Apple Silicon?** Load-bearing for the architecture. The
   repository does not document macOS support and its first-class C++ path is CMake plus a Visual
   Studio solution. **Must be built, not assumed.**
3. **ColorVideoVDP's actual LICENSE text**, read from the file rather than from GitHub's detector.
4. **[HUMAN] Does the engine get a shadow AOV?** §9 wants shadow stability as a separate dimension
   and there is no shadow target. ADR-242 lists it among the views deliberately not added *because
   nothing had asked for one*; the Quality Lab is the first consumer with a reason to ask. It costs a
   new target or a new pass, in the engine, for a tool outside it.
5. **[HUMAN] What is the human-validation protocol, and how much reviewer time may it cost?** §24
   wants a "which looks better?" workflow. `docs/visual-quality.md` already has a fourteen-criterion
   rubric and a fixed review-frame list. These should be one thing, not two — but the frequency and
   the reviewer's budget are the owner's to set, and ADR-243 is the evidence that this is the most
   valuable input in the whole programme.
6. **[HUMAN] Fix the two reference-path defects now, or work around them?**
   `--supersample` with `--format exr` silently crops (no `validate()` guard), and the supersample
   resolve is a bilinear tap rather than a box filter. Both sit directly under §13. The Quality Lab
   can avoid the first entirely by taking references as PNG, but the bug remains in the engine for
   the next person. See [reference-rendering.md](reference-rendering.md).
7. **Render cost under contention (§22).** Two other agents are on this GPU. ADR-170's rule is that a
   timing is evidence only if the device was also quiet, and that even then two invocations of the
   same binary have a ~3 ms noise floor — arms must be **interleaved inside one process**.
   `core::PhaseProfiler` already reports **min over a long run** for this reason, and its `setFrameGroup`
   is that interleaving for the main thread. Until the machine is quiet, the quality/cost frontier is
   reported **pending** with structural counters beside it, never as estimated milliseconds.

---

## 11. The comparison table (§47)

§47 requires thirteen questions answered per technique. They are split across two tables only so the
rows stay readable; every technique appears in both, and the thirteenth question — *does it provide
information unavailable from our other metrics?* — is the one that decided each adoption, so it gets
its own column and its own prose.

Legend: **FR** full-reference · **NR** no-reference · **S** spatial · **T** temporal · **✓** yes ·
**✗** no · **~** partially / with caveats.

### 11a. What it measures, and whether it is ours to use

| technique | Q1 what it measures | Q2 what it does NOT measure | Q3 FR/NR | Q4 S/T | Q13 information unavailable elsewhere? | verdict |
|---|---|---|---|---|---|---|
| **PSNR** | log mean squared error | anything perceptual; where the error is; colour | FR | S | **no** — but it is the cheapest possible *alignment* test | **keep, as a sanity rail only** |
| **SSIM** | local luminance, contrast, structure agreement | uniform colour shift; aliasing-vs-detail | FR | S | no (MS-SSIM strictly supersedes) | keep, subordinate to MS-SSIM |
| **MS-SSIM** | the same over a pyramid — multi-scale structure | which *kind* of structural change; aliasing vs genuine detail | FR | S | **yes** — broad spatial fidelity across our scale range | **adopt** |
| **PSNR-HVS(-M)** | CSF-weighted DCT-domain error, with masking (-M) | temporal anything; structure | FR | S | marginal — free with libvmaf | adopt as secondary, **no decision authority** |
| **CIEDE2000** | perceptual colour difference in CIELAB | luminance structure; spatial arrangement | FR | S | **yes** — the chroma-retention dimension | **adopt, scoped to colour** |
| **VMAF (v1)** | fused VIF + detail-loss + motion, trained on compression/scaling of camera video | our distortion classes; temporal masking; anything outside its training domain | FR | S + weak T | **yes, for one job**: how much of the master survives delivery encoding | **adopt, scoped to compression resilience** |
| **ꟻLIP / HDR-ꟻLIP** | perceived difference when *alternating* two rendered images — CSF-filtered opponent colour, **amplified on edges and features** | absolute quality; temporal sequence behaviour; causes | FR | S | **yes** — the only FR metric here designed for rendered-vs-reference, and the only one whose primary output is a readable error map | **adopt if it builds** |
| **ColorVideoVDP** | modelled spatio-temporal **and** chromatic visual difference, in JOD, under a declared display | anything without a display specification; causes | FR | **S + T** | **yes** — the only instrument that could arbitrate ADR-243's dispute from one model | **pilot, off the critical path** |
| **LPIPS** | distance in ImageNet-trained deep features, calibrated on BAPPS | temporal anything; out-of-distribution content | FR | S | **no** — ꟻLIP occupies the slot with a better domain fit | **reject as a metric** |
| **Butteraugli / SSIMULACRA2** | still-image compression quality near the visually-lossless threshold | video; our distortions | FR | S | no | **reject** |
| **CAMBI** | contrast-aware multiscale banding index, on luma | chroma banding; temporal band crawl; anything but banding | **NR** | S | **yes** — banding, validated, in-domain | **adopt** |
| **mean \|Laplacian\|** (`spatial_stats.py`) | spatial high-frequency energy — staircase vs resolved edge | **cannot separate aliasing from detail**; meaningless across scenes | NR | S | **yes** — it is the measure that agreed with the reviewer in ADR-243 | **keep, re-scoped** |
| **2nd temporal difference** (`temporal_stats.py`) | temporal *alternation* of the luminance signal | spatial aliasing on moving geometry; anything smooth however fast | NR | **T** | **yes**, narrowly — and ADR-243 is the record of what happens when it is used alone | **keep, never alone** |
| **motion-compensated residual** (velocity AOV) | temporal change **not explained by the engine's own motion** | disocclusions (must be masked); anything the velocity buffer gets wrong | NR | **T** | **yes** — the missing instrument; separates animation from instability | **build** |
| **AOV-gated residual** (emission / normal / id masks) | the residual restricted to specular, shading, LOD or vegetation pixels | shadow (no AOV exists); causes | NR | T | **yes** — it is how §9's dimensions become genuinely separate numbers | **build** |
| **spectral detail retention** | ratio of radially-averaged high-frequency power, candidate vs reference | whether the lost detail mattered; where | FR | S | **yes** — blur / over-sharpening as one signed axis | **build** |
| **directional-energy aliasing** | patch energy split into signal and aliasing along the dominant orientation | — | NR | S | **no**, given a supersampled reference makes it an FR question | **defer to Phase 4** |
| **optical flow** | estimated per-pixel motion | exact motion; thin geometry, where it is worst | NR | T | **no** — the velocity AOV is exact and free | **reject as primary; keep to validate the AOV** |

### 11b. Whether it works here, and what it costs

| technique | Q5 synthetic 3D? | Q6 stylized? | Q7 needs aligned frames? | Q8 failure modes | Q9 cost | Q10 Apple Silicon | Q11 offline? | Q12 integrable? |
|---|---|---|---|---|---|---|---|---|
| **PSNR** | ✓ | ✓ | **✓ strictly** | dominated by exposure/gamma; blind to error *placement* | trivial | ✓ | ✓ | trivial, native |
| **SSIM / MS-SSIM** | ✓ | ✓ | **✓ strictly** | scores aliasing and detail the same way — blind on exactly the AA comparison | low | ✓ | ✓ | native, or libvmaf |
| **PSNR-HVS(-M)** | ~ | ~ | ✓ | CSF assumes a viewing condition it is not told about here | low | ✓ | ✓ | libvmaf only |
| **CIEDE2000** | ✓ | ~ | ✓ | defined for surface colour under a reference illuminant; we use it on tone-mapped emissive content | low | ✓ | ✓ | native, or libvmaf |
| **VMAF (v1)** | **~ weak** | **~ weak** | **✓ strictly** | out of training domain for aliasing/shimmer/popping; thin temporal model; mean pooling hides short pops | moderate | ✓ | ✓ | **needs ffmpeg ≥ libvmaf 3.2.0 — absent here** |
| **ꟻLIP / HDR-ꟻLIP** | **✓ by design** | ✓ | **✓ strictly** | still a per-frame metric; says *where* not *why* | moderate (CPU) / low (GPU) | **unverified — must build** | ✓ | single header, BSD-3, vendors tinyexr + stb |
| **ColorVideoVDP** | ✓ | ✓ | ✓ | mandatory display spec; numbers incomparable across display declarations; slow on CPU | **high** | ✓ (MPS, torch ≥ 2.1) | ✓ | PyTorch + Python 3.12 — a heavyweight optional boundary |
| **LPIPS** | **✗** | **✗** | ✓ | ImageNet features on stylized emissive content; per-frame only | moderate | ✓ | ✓ | PyTorch |
| **CAMBI** | ✓ | ✓ | ✗ | luma only; frame-local, so crawling bands score as static ones; thresholds assume a display brightness | low | ✓ | ✓ | **libvmaf — absent here** |
| **mean \|Laplacian\|** | ✓ | ✓ | ✗ | **cannot separate aliasing from detail**; valid only between arms of one view | trivial | ✓ | ✓ | exists (`tools/spatial_stats.py`) |
| **2nd temporal difference** | ✓ | ✓ | ✗ (same camera) | **ranks AA remedies backwards** on spatial aliasing over moving geometry (ADR-243); needs ≥ 3 frames; loads the sequence into memory | low | ✓ | ✓ | exists (`tools/temporal_stats.py`) |
| **motion-compensated residual** | ✓ | ✓ | ✗ — it *creates* the alignment | **disocclusion**: newly-revealed pixels always score high and are not artifacts; inherits any velocity-buffer defect | moderate | ✓ | ✓ | native; needs `--aov velocity,depth,id` |
| **AOV-gated residual** | ✓ | ✓ | ✗ | ⚠ *amended 2026-09-17:* a shadow AOV now exists and **recomputes** the term rather than capturing it, 0.43% of pixels differing (ADR-255/258); emission ≠ specular exactly; masks are candidate-resolution | moderate | ✓ | ✓ | native; needs `--aov` |
| **spectral detail retention** | ✓ | ✓ | ✓ | a sharpening filter and genuine detail both raise it — the sign must be read with ꟻLIP beside it | low | ✓ | ✓ | native |
| **directional-energy aliasing** | ✓ | ~ | ✗ | patent-encumbered implementations exist; implement from the principle | moderate | ✓ | ✓ | native, deferred |
| **optical flow** | ~ | ~ | ✗ | worst precisely on thin, fast, low-texture geometry — our artifact regions | **high** | ✓ | ✓ | OpenCV — absent here |

### 11c. The thirteenth question, answered as prose

§47's last question is the only one that can reject a popular metric, so it is worth stating what
each adopted instrument is the *sole* source of, because that is its licence to exist in the vector:

* **MS-SSIM** — broad spatial agreement with the reference across scales. Nothing else reports it.
* **CIEDE2000** — colour, separated from luminance. Nothing else does.
* **ꟻLIP** — perceptual difference weighted toward edges and features, *with a map*. No other
  adopted metric tells a human where it is looking.
* **CAMBI** — banding. Nothing else detects it.
* **VMAF** — survival through a delivery encode. Nothing else answers that question.
* **mean |Laplacian|** — spatial high-frequency energy, which is the axis the reviewer's complaint
  lives on and the one the temporal detector is blind to.
* **motion-compensated residual** — temporal change *minus* the motion the engine says it authored.
  Nothing that exists in this repository can currently distinguish those two.
* **spectral detail retention** — how much high-frequency information survived, as a signed axis, so
  blur and over-sharpening are the same measurement in opposite directions.

**PSNR is the exception and is kept anyway**, because its job is not quality: it is the fastest test
that two frames are the same frame.


## Sources

- [Netflix/vmaf](https://github.com/Netflix/vmaf) · [VMAF v1: Good Is Not Good Enough](https://medium.com/netflix-techblog/vmaf-v1-good-is-not-good-enough-60d7e4244ea8) · [VMAF v1 models](https://vmafx.github.io/vmafx/models/v1/) · [CAMBI](https://github.com/Netflix/vmaf/blob/master/resource/doc/cambi.md) · [libvmaf licence (BSD-2-Clause-Patent)](https://github.com/Netflix/vmaf/blob/master/LICENSE)
- [ꟻLIP (NVlabs)](https://github.com/NVlabs/flip) · [ꟻLIP: A Difference Evaluator for Alternating Images, HPG 2020](https://research.nvidia.com/publication/flip)
- [ColorVideoVDP](https://www.cl.cam.ac.uk/research/rainbow/projects/colorvideovdp/) · [paper](https://arxiv.org/pdf/2401.11485) · [repository](https://github.com/gfxdisp/ColorVideoVDP)
- [No-Reference Rendered Video Quality Assessment: Dataset and Metrics](https://arxiv.org/pdf/2510.13349)
- [Subjective and Objective Quality Assessment for in-the-Wild Computer Graphics Images](https://arxiv.org/pdf/2303.08050) · [Towards Deep Learning Methods for Quality Assessment of Computer-Generated Imagery](https://arxiv.org/pdf/2005.00836)
- [LPIPS / PerceptualSimilarity](https://github.com/richzhang/PerceptualSimilarity)
- [Optuna multi-objective / TPE](https://arxiv.org/pdf/2304.11127) · [BoTorch multi-objective](https://botorch.org/docs/tutorials/multi_objective_bo/)
