# Benchmark scenes: what exists, what is missing, and what must not be optimized against

Status: **two scenes authored** (spec §11, §12, §35) — `examples/quality/aliasing` and
`examples/quality/aliasing-dolly`, both registered in `examples/index.json`.

> **Both were vacuous on the first attempt, in two different ways**, and §4 below has gained two
> rules because of it. See [ADR-254](../decisions/ADR-254-a-benchmark-that-cannot-show-the-artifact.md).

---

## 1. The rule that outranks the scene list

§35, and it is the reason this document leads with it rather than with the eight torture scenes:

> **A renderer optimization that improves the aliasing test while making real scenes worse is not a
> successful optimization.**

and §12:

> Synthetic torture scenes isolate defects; they do not prove the system works on real worlds.

This repository has the receipt. ADR-243's finding came from **Glowmere**, on the documented river
view, with a human watching. No synthetic scene would have produced *"the grass looked jagged in wind
animations"*, because no synthetic scene has Glowmere's wind-animated grass at Glowmere's density
under Glowmere's lighting. The synthetic suite is for **attribution**; the real worlds are for
**judgment**; and the judgment is what decides.

So the suite is ordered **real first**, against the spec's Phase 5-before-Phase 6 ordering. The
synthetic scenes are what you build once a real scene has told you which defect is worth isolating.
Building eight torture scenes before that is the failure §11 warns about from the other side —
optimizing an instrument against content nobody ships.

---

## 2. What already exists, and it is more than §11 assumes

### 2.1 `examples/qa/` — a purpose-built isolation ladder

Five scenes, one project (`renderer-qa.json`, 1280×720/30), designed as exactly the additive ladder a
quality suite wants. `renderer-qa-minimal.scene.json`'s own note states the discipline:

> Everything absent here is absent on purpose — no transparency, no skinning, no particles, no
> water, no terrain, no sky.

| scene | adds | §11 class it already serves |
|---|---|---|
| `renderer-qa-minimal` | three orbs, nothing else | the baseline every arm is differenced against |
| `renderer-qa-character` | skinned geometry | **8. motion/animation** (partially) |
| `renderer-qa-transparency` | blended geometry | the transparency caveat in the residual (§3 of artifact-detection) |
| `renderer-qa-water` | animated water | **2. temporal stability** (partially) — and the river is ADR-243's second subject |
| `renderer-qa` | the full combination | integration |

Three of them already have `examples/qa/baselines/*.snapshot.json` — **derived renderer state, not
pixels**, by an explicit decision recorded in the file and in `tools/certify.py`.

### 2.2 Real content

| project | resolution / fps | notes |
|---|---|---|
| `examples/world/glowmere-valley-2.json` | 1920×1080 / 60 | **tier offline, supersample 2.0, limits unlimited** — already the reference recipe |
| `examples/world/glowmere-valley-2-multicam.json` | 1920×1080 / 60, 0→30 s = 1800 frames | a **bounded** sequence, which the open-ended one is not |
| `examples/world/glowmere-stylized.json` | 1920×1080 / 30, supersample 2.0 | **ADR-171: this is what "Glowmere" means in this repo's evidence base** — not `terrain.scene.json`, despite the index calling that one "Glowmere Valley" |
| `examples/world/glowmere-atmospherics.json` | 1920×1080 / 60, 0→30 s | volumetrics and banding |
| `examples/city/night-shift.json` | 1920×1080 / 30, 0→105 s | a different art direction entirely — the §35 cross-check |

⚠️ **Both Glowmere Valley 2 projects reference audio at `"../../../../../Desktop/Rebuild.mp3"`** — an
absolute-ish escape out of the repo that will not relink on another machine. A quality run that needs
a deterministic audio-driven parameter must fix this or use a scene without it.

### 2.3 `renders/output-preview/` — an existing two-arm comparison

Driven by `--queue`, with `MEASUREMENTS.txt`: arm A at 1280×720, `tier offline`, `limits unlimited`,
`supersample 2.0`; arm B at 1200×676, `tier realtime`, `limits live`, `supersample 1.0`. This is the
shape of a Quality Lab experiment, authored by hand, before the Quality Lab existed. **`--queue` is
the batch primitive the experiment system should build on** rather than reinvent.

---

## 3. The synthetic suite, mapped to what would actually be built

§11's eight scenes, with an honest assessment of each — what it would isolate, whether the engine can
author it, and whether anything already covers it.

| # | §11 scene | isolates | authorable today? | already covered? |
|---|---|---|---|---|
| 1 | **aliasing** | thin wires, fences, diagonal edges, fine repeating texture, small emissive elements | **yes** — `Grid`, `Orb`, `Procedural`, `Spline` nodes; a fence is a spline array | no. **Highest value** |
| 2 | **temporal stability** | dolly, orbit, fast pan, animated foliage, particles, thin geometry | **yes, with a caveat, and it is not the caveat this table first gave.** Use a **Free** camera. The legacy **orbit** mode integrates `orbitSpeed · dt` and is path-dependent — and a **Spline** camera takes its target from a point further along its own path, so it always looks where it is going and a *lateral* dolly aims it off the subject entirely. ⚠ See §4.2 | ✅ `examples/quality/aliasing-dolly` |
| 3 | **shadow** | cascade transitions, swimming, popping, bias, filtering | yes | no — **and there is no shadow AOV**, so this scene can be rendered and only judged by eye and by the ungated residual |
| 4 | **specular** | rough/smooth metal, wet surfaces, moving lights, HDR highlights | yes | no |
| 5 | **vegetation** | grass, leaves, branches, mushrooms, multiple LOD distances | **yes, and it is the class AV Gen actually ships** — but a synthetic version is strictly worse than Glowmere, which already has it at density | **Glowmere covers this better.** Build it only to *isolate*, never to judge |
| 6 | **volumetric** | fog, god rays, gradients, dense atmosphere | yes | `glowmere-atmospherics.json` partly covers it |
| 7 | **HDR/emission** | extreme and subtle emission, dark environment, saturated colour | yes — and it is this project's live art-direction question (the bioluminescence work) | partly |
| 8 | **motion/animation** | walking character, rotating object, camera tracking | yes | `renderer-qa-character` partly |

**Recommended build order, which is not §11's order:**

1. **Aliasing (1).** The artifact the reviewer reported, the one the metrics disagree about, and the
   one with no existing coverage. It is also the cheapest to author and the easiest to make
   unambiguous.
2. **Temporal stability (2)** with a **lateral dolly** — because ADR-243 ends by saying *"a moving
   camera has not been reviewed at all, and crawl on static geometry is exactly what a moving camera
   would produce."* That is a named open question with a named scene that would answer it. ⚠ **Not a
   spline dolly**, against this document's first recommendation: see §4.2. Built as
   `examples/quality/aliasing-dolly`, and the arms it produced are in ADR-253.
3. Everything else, only once a real scene has shown the defect is worth isolating.

---

## 4. Scene authoring rules for benchmark content

Derived from the determinism findings in the reconnaissance, and each one exists because getting it
wrong produces a comparison that is not a comparison:

1. **A benchmark scene ships with a project.** `--composition` alone renders a different image —
   default exposure, default post, no render block. Every quality render goes through `--project`.
2. ⚠ **A benchmark camera must be FREE.** Not orbit — it integrates `orbitSpeed · dt` and is
   path-dependent. And **not spline**, which this document originally recommended: a spline camera
   takes its position from the spline and its target from a point `lookAhead` units *further along
   the same spline*, so it always looks where it is going. The motion a temporal benchmark needs is
   **lateral**, because lateral motion is what produces parallax and parallax is the only thing that
   disoccludes anything — and a lateral spline track therefore aims the camera sideways at empty
   background. The first version of the dolly scene did exactly that and rendered a flat frame,
   `rms_contrast 0.0000`, caught only because the candidate and its supersampled reference came back
   with the **same sequence hash** (rule 8, and reference-rendering.md §3.4 rule 5). A free camera
   with keyframed position and target is equally path-independent — a timeline is a pure function of
   time, which is the property rule 2 was always protecting — and it can look at the subject while
   moving across it. A spline camera stays right for a flythrough, where looking along the path *is*
   the shot.
3. **No ADR-091 live-tier content** — ambient population, props, background vehicles are stateful,
   reset on seek and not frame-accurate under scrub — unless the scene declares it.
4. **Pin the seed** on every procedural node, every particle system and the world recipe.
4b. ⚠ **Pin every parameter the experiment's arms will toggle**, explicitly, rather than inheriting a
   default. `post/output/antialias` defaults to **0.0** — `post_settings.hpp`: *"0 skips the pass
   entirely"* — so a scene that never asks for FXAA never runs it, and `--disable fxaa` on such a
   scene logs *"this is a DIAGNOSTIC render"* and writes a **byte-identical sequence hash**. Read
   naively that is a finding, and it is the reassuring one: "FXAA does not affect this scene." It is
   ADR-182's exact shape — a measurement whose null result is indistinguishable from a broken
   instrument — and it happened on the first attempt at ADR-243's three arms. An anti-aliasing
   benchmark has to have anti-aliasing switched **on** before it can be switched off.
5. **Bounded duration.** `endSeconds = -1` resolves to the audio duration, and the audio may not be
   there. State a range.
6. **Additive.** One behaviour per scene beyond the shared baseline, so a difference has one
   candidate cause — the `examples/qa/` ladder's existing design.
7. **Register in `examples/index.json`**, or the scene is invisible to the engine.
8. **Declare the sequence hash** at authoring time, so a scene that silently changes is caught by the
   thing that already catches it — **and check it between arms**, which is how both of the failures
   above were found before any metric had been computed. An arm that cannot differ is void rather
   than measured.
9. ⚠ **A region of the frame is not a control for an aliasing arm, and a scene may not claim one
   is.** This document and ADR-254 both described the aliasing scene's large smooth orb as a control
   — *"it cannot alias, so a metric that moves on this scene's arms must not be moving there"* — and
   a Phase 6 reviewer handed it as a control reported, correctly, that it differed. A sphere's
   *interior shading* is unaffected by anti-aliasing; its *silhouette* is a curved edge like any
   other and is affected exactly as much. Measured over 60 frames (`avgen_quality control`): the
   orb's interior is **bit-identical** with FXAA off and moves 0.15 luma steps under supersampling,
   while its silhouette band moves 2.70 and 5.39 with peaks over 120. **So the claim a scene may
   make is about an object's interior, never about the object.** Nothing visible is invariant under
   anti-aliasing, because everything visible has an edge — and the control for a *human* comparison
   is therefore a **duplicated arm**, one clip shown twice under two labels (metrics.md §4.4), not
   something in the frame. ADR-257.

---

## 5. Target profiles (§10)

Quality is conditional on delivery and must not be hard-coded. A profile is an explicit document:

```json
{ "name": "youtube-4k-30", "width": 3840, "height": 2160, "fps": 30,
  "colorSpace": "rec709", "bitDepth": 10, "codec": "h264",
  "viewingCondition": "vmaf-4k-3h",
  "display": { "peakLuminanceNits": 200, "diagonalInches": 27, "viewingDistanceH": 3.0,
               "ambientLux": 100 } }
```

The `display` block is not optional padding: ColorVideoVDP **requires** it, CAMBI's thresholds assume
a display brightness, and VMAF v1's whole change is that viewing distance is folded into the feature
calculation — so the phone, 1080p-3H, 4K-1.5H and 4K-3H models are different instruments and a score
from one is not comparable to a score from another. **Two numbers taken under different profiles are
not comparable and the report must refuse to rank them.**

Profiles to define first: `master-1080p30`, `master-1080p60`, `youtube-4k-30`, `vertical-1080x1920-30`
(AV Gen ships music video content, and vertical is a real delivery), `phone-1080p30`.

---

## 6. Storage (§36)

`quality-results/<timestamp>/` with `manifest.json`, `renderer.json`, `metrics.json`, `report.html`,
`reference/`, `candidate/`, `diagnostics/`, `logs/`. **Not in Git** — a 300-frame 1080p PNG pair plus
AOVs is several gigabytes. `.gitignore`d, with the retention expectation documented: the
**manifest and metrics** are the durable record and are small; the frames are reproducible from the
manifest and the git commit, and are deletable.

This follows the position `tools/certify.py` already states — compare against the previous run and
the recorded numbers, not against committed pixels, because *"a renderer upgrade changes pixels on
purpose."*
