# Renderer professionalization — Deliverables 1, 3 and 4

Prepared 2026-09-13. **Preparation phase only: no implementation.** Measurements taken on Apple
M2 Max, Dawn on Metal, release build, idle machine, nothing else running.

---

# Deliverable 1 — Repository audit

## 1.1 The supplied baseline, verified against source

Rule B requires the brief's figures be checked rather than adopted. Six of them are wrong, and two
of the discrepancies matter.

| Claim in the brief | Verified | Status |
|---|---|---|
| ~15,615 lines under `src/rendering` | **17,634** | stale (this investigation added ~2,000) |
| 35 WGSL shaders | **36** | stale by one (`reference.wgsl`) |
| 197 GPU tests | **259 cases** | stale |
| 5 scene targets | **5** — `scene_targets.hpp:20` | ✅ confirmed |
| clustered forward | confirmed — `light_data.hpp:57-62`, 16×8×24 froxels | ✅ |
| Glowmere ~18.87 ms GPU | **18.55–18.74 ms** over five runs | ✅ within 1% |
| Glowmere ~23.60 ms wall | **21.92–22.57 ms** | close; see §3.3 |
| scene pass ~15.79 ms / ~84% | **15.73 ms / 84.2%** | ✅ confirmed exactly |
| ~141 draws, ~430,233 tris | **141 draws, 430,231 tris** | ✅ |
| ~2,329 visible / ~114,283 culled | ✅ exactly | ✅ |
| CPU scene ~0.67 ms | **0.66 ms** | ✅ |
| Constellation ~6.09 ms GPU | 3.60 ms *in one block* | ⚠️ **withdrawn — see §4.7** |
| Constellation volumetrics ~3.93 ms | 2.29 ms *in one block* | ⚠️ **withdrawn — see §4.7** |

**Discrepancy 1 — WITHDRAWN.** I recorded "Constellation is 41% faster than the brief states" and
justified it as "not noise: five runs, 1% spread". **The 1% spread was Glowmere's and does not
transfer.** Constellation's own within-session GPU spread is **36.97%** — eight block medians in one
session ranged 6.16–10.16 ms, and inside a single 240-frame block p50 is 6.29 ms against p90 10.81
and p99 12.98. The brief's 6.09 ms sits inside that range. There is no discrepancy; there is a scene
whose median is not a stable statistic, which this document had already said of Constellation in a
different sentence and which I then failed to apply to my own measurement of it.

**How it was caught, and why that matters more than the error.** The A/B harness floors its noise
threshold at *the session's own measured baseline spread* rather than at a fixed constant. With the
fixed 2% constant alone, a null A/B on Constellation — the baseline against itself — would have been
certified as a **16% improvement**. The instrument caught the mistake its designer had already made
in prose.

**Discrepancy 2 — a third Glowmere number exists.** `docs/renderer-qa-2026-09-11.md` records
23.79 ms GPU. Today's measurement is 18.6 ms. The gap is 28% and is **not** explained by run-to-run
variance (§3.3). It is unresolved and is recorded as such rather than averaged away.

**Correction to the brief's known-limitations list:** "incomplete rendering determinism under
contention" and "GPU timing tests failing under parallel contention" were two symptoms of one defect,
`SYM-TERRAIN-1`, fixed earlier today: ambient occlusion dropped its temporal history whenever the
frame index did not advance by exactly one — right for a seek, wrong for a *repeat*, so re-rendering
a frame produced a different picture from the one it was re-rendering. Debug and release now agree
and the full suites are green. **The renderer has no known nondeterminism.** This matters for the
upgrade: every image comparison used to validate it now measures the upgrade rather than competing
with a defect underneath it.

## 1.2 Architecture map (source-level)

| Concern | Location |
|---|---|
| Frame orchestration (live) | `src/app/application.cpp` — `runLive`, the headless loop |
| Scene submission, all camera passes | `src/rendering/scene_renderer.cpp` (**3,182 lines**) |
| Scene targets contract | `src/rendering/scene_targets.hpp` — the five-attachment declaration |
| Per-object GPU data | `SceneRenderer::render`'s `makeItem` lambda — one 512-byte slot per drawable |
| Procedural instancing + GPU cull + LOD | `src/rendering/procedural_renderer.cpp` |
| Clustered lighting | `src/rendering/light_data.{hpp,cpp}` — 16×8×24 froxels, exponential Z |
| Shadows | `shadow_renderer.cpp`, `shadow_math.cpp`, `shadow_mask_renderer.cpp` |
| Skinning | `src/rendering/skinning.cpp` — two palettes per rig in one dynamic-offset slice |
| Water / particles / volumetrics / SDF | `water_renderer.cpp`, `particle_renderer.cpp`, `volume_renderer.cpp`, `sdf_renderer.cpp` |
| Post + tone map | `post_processor.cpp`, `output_mapper.cpp` |
| Scene state (authoritative) | `src/scene/composition.cpp` — `applyParameters` |
| Diagnostics | `renderer_diagnostics.hpp`, `renderer_snapshot.cpp`, `transform_history.cpp` |

**Ownership model, already established and load-bearing** (from the forensics work): *the parameter
is authoritative, the node's `*Rest` struct is the authored baseline, and the object hanging off
`Scene` is a per-frame derivation.* The renderer takes `const scene::Scene&` throughout. Any GPU
scene representation must respect this — a persistent GPU scene is a **second** derived copy, and
the invalidation rule has to be explicit or it becomes the tenth boundary defect.

## 1.3 `scene_renderer.cpp` — what is actually mixed together

It is 3,182 lines and does mix concerns. Identified boundaries (**not** refactored):

| Concern | Evidence |
|---|---|
| Frame orchestration | pass ordering, encoder lifetime |
| Resource management | target creation, resize, bind-group caches, per-format pipeline maps |
| Visibility | trusts `entity.cameraCulled` from the composition; does not cull itself |
| Draw submission | `makeItem`, the opaque/blended/grid/water lists |
| Pass implementation | background, depth, linear depth, scene, aux debug, tonemap |
| Diagnostics | per-object diagnostic frame built unconditionally, every entity, every frame |
| Offline | `renderToImage`/`renderToImageFloat` readback paths |

## 1.4 GPU scene representation — audit findings

| Question | Answer |
|---|---|
| Transforms CPU- or GPU-resident? | **CPU.** Uploaded per frame into a 512-byte slot per drawable |
| Stable instance IDs? | **No.** `objectSlot` is submission order, reassigned every frame |
| Identity that *is* stable | pick id = entity index; material id = index + 1 (**one-based, load-bearing** — it is the only thing distinguishing entity 0 from an empty pixel) |
| Assets vs submeshes separated? | **No.** A multi-material glTF arrives as N entities/procedurals — `visitor_m1/_m2/_m3` |
| Multi-material scatter cost | confirmed: culled and counted **once per material** |
| Bounds | `scene::entityCullBounds` (one shared function), conservative pad for posed characters |
| Visibility persisted between frames? | **No.** Rebuilt every frame |
| Scene data rebuilt each frame? | **Yes** — by design (the derived-copy rule) |

## 1.5 Limit inventory

| Limit | Value | Source | Reason | User-visible? | Legitimate? | Replacement needed? |
|---|---|---|---|---|---|---|
| `kMaxObjects` (entity draw slots) | **256** | `scene_renderer.hpp:469` | 512-byte dynamic-offset slots = 128 KB uniform buffer | **Yes** — logs "extra entities skipped" | Architectural limitation | **Yes — highest priority** |
| `kMaxProceduralObjects` | 256 | `procedural_renderer` | same | yes | architectural | yes |
| `kMaxSceneLights` | 256 | `light_data.hpp:62` | packed light buffer | yes | quality setting | later |
| `kMaxLightsPerCluster` | 32 | `light_data.hpp:61` | froxel index list | overflow only | quality setting | measure first |
| `kMaxLodLevels` | 4 | `scene_types.hpp` | LOD ladder width | no | architectural | Phase D |
| `kMaxShadowViews` | 8 | `shadow_math.hpp:17` | shadow uniform array | no | quality setting | no |
| `kMaxRigs` | 64 | skinning | joint buffer slices | yes | architectural | later |
| `kMaxPaletteJoints` | 256 | skeleton | GPU palette | no | API/memory | no |
| `kMaxGpuSplines` | 16 | fields | — | **yes** — Glowmere logs "22 splines; only the first 16 available" | architectural | later |
| `kMaxDrawsInFlight` | 64 | timeline | timestamp slots | no | memory safety | no |

**`kMaxObjects = 256` is the headline limit.** Glowmere flattens to **278 entities** — already over
it. It does not currently fire because only 136 are submitted per frame after culling, but the
margin is negative for a camera that sees the whole valley. It is a hard, silent-ish cap on
simultaneously-visible entities, imposed by a uniform-buffer layout choice, and it is exactly the
kind of limit the brief says must be replaced by a mechanism rather than raised.

---

# Deliverable 3 — Performance baseline and protocol

## 3.1 Protocol (reproducible)

```
hardware      Apple M2 Max
os            Darwin 25.6.0
build         cmake --preset release   (NEVER debug for acceptance — see §3.4)
backend       Dawn / Metal
command       ./build/release/src/avgen --headless --frames 120 --size <W>x<H> \
                --composition <scene>        (or --project for Constellation)
warm-up       first 12 frames discarded by the harness
measured      the remaining 108 "steady" frames
timing        GPU timestamp queries per pass; wall clock per frame
statistic     median, p10, p90, min (harness-reported)
preconditions nothing else on the GPU  (pgrep -f tests/avgen_render_tests)
              no source edits in flight (shaders load from disk at runtime)
```

**A/B rule:** both arms in the same machine session, interleaved, ≥3 runs each. A difference below
**2% GPU / 4% wall** is not a result (§3.3).

## 3.2 Current measurements

> **Superseded for Glowmere as of the LOD0 merge (2026-09-13).** The numbers below are the
> *pre-upgrade* baseline and are kept as the reference point every later A/B is measured against.
> The current figures are in §3.2.1. Nothing in this section is edited in place — a baseline that
> moves is not a baseline.

**Glowmere** (`examples/world/glowmere-stylized.scene.json`), 1280×800:

| | median | p10 | p90 |
|---|---|---|---|
| wall | 22.20 ms | 21.17 | 27.86 |
| GPU | 18.61 ms | — | — |

Pass split: `scene` 15.73 (84.2%), `volume` 0.66, `shadow` 0.66, `ao` 0.59, `shadowmask` 0.46,
`cull` 0.33, `depth` 0.26, `particles` 0.13, `post/bloom` 0.13, rest ≤0.07.
Submitted: 430,231 tris / 2,771 inst / 136 draws camera; 427,353 / 2,761 / 126 depth;
792,206 / 2,199 / 189 shadow over 55 casters. **Logical 15,128,152 tris / 116,978 inst** before LOD
and culling. Binds: 83 pipeline, 795 bind-group, 453 vb, 451 ib (322 redundant avoided).

**Constellation** (`examples/constellation/constellation.json`), 1280×800:

| | median |
|---|---|
| wall | 4.71 ms |
| GPU | 3.60 ms |

Pass split: `volume` 2.29 (64%), `particles` 0.52, `scene` 0.46, rest ≤0.07.

## 3.2.1 Post-LOD0 Glowmere (current)

Measured by me, not accepted from the agent that made the change: five runs under
`tools/gpu-lock.sh`, same session, at the merge commit.

| | pre-upgrade (§3.2) | post-LOD0 | change |
|---|---|---|---|
| GPU median | 18.55–18.74 ms | **14.61–14.81 ms** | **−21%** |
| scene pass | 15.73 ms | **11.86–12.06 ms** | **−24%** |
| submitted tris | 430,231 | **264,305** | **−38.6%** |

Within-session spread 1.4% — above the 1.0% floor in §3.3 but far below the 2% A/B threshold, so
the result stands on its own without needing the threshold argued.

What produced it (ADR-108, ADR-109, ADR-110): `partOf` gives a group one spatial instance instead
of one per part; `Scene::identity` stops distinct-looking objects from being distinct meshes; and
LOD0 runs through meshoptimizer. The triangle reduction is the mechanism and the scene pass is where
it lands — consistent with §4.5's finding that this renderer is fragment-bound and that fragment
cost tracks *triangle size*, not pixel count.

Accepted on two further gates beyond the timing: the `[baseline]` frame-state test passes unchanged
(29 assertions — derived state is identical), and the captured frame is visually correct at full
size, which §50 of the spec requires independently of the measurement.

## 3.3 Reproducibility — investigated, not assumed

Five consecutive Glowmere runs, same session:

| run | wall | GPU |
|---|---|---|
| 1 | 22.20 | 18.55 |
| 2 | 22.27 | 18.74 |
| 3 | 22.57 | 18.61 |
| 4 | 21.92 | 18.55 |
| 5 | 21.97 | 18.61 |

**Within-session spread: GPU 1.0%, wall 3.0%.** So the session-to-session gap the brief asks about
(23.79 ms in the QA doc vs 18.6 ms today, 28%) **cannot** be run-to-run variance, GPU contention or
thermal drift at this magnitude. Candidate causes, none yet eliminated: a different build
configuration, a different scene revision, or genuine improvement between those dates. **Recorded as
unresolved.** The actionable conclusion is the A/B rule in §3.1: never compare across sessions or
against a number from a document.

## 3.4 Measurement capability audit

| Measurement | Status |
|---|---|
| CPU frame time, GPU frame time, per-pass GPU time | ✅ available, per-pass timeline |
| Frame distribution (p10/p90/min) | ✅ |
| Draw / triangle / instance / LOD / culled counts | ✅ `AVGEN_FRAME_COUNTERS=1` |
| CPU stage breakdown | ✅ `AVGEN_CPU_STAGES=1` |
| Cluster occupancy / overflow | ✅ `--cluster-stats` (ADR-114); see §3.5 |
| Transient/geometry/texture memory | ⚠️ transient texture count only |
| **Fragment invocations, quad utilisation, occupancy** | ❌ **not available — Xcode/Metal only** |
| Streaming activity | ❌ no streaming system exists |
| p95/p99/variance, 1% low, machine-readable output | ✅ always on; `--bench-json` (ADR-113); see §3.5 |

**The one missing capability that blocks the central architectural decision is fragment-invocation
and quad-utilisation counters, which only a Metal frame capture provides.** This was flagged in the
forensics report as the single unused tool in the repository; it is now decision-relevant.

---

## 3.5 What the instrument can now do, and what it found out about §3.3

Added 2026-09-13 ([ADR-113](../decisions/ADR-113-a-measurement-carries-its-conditions.md),
[ADR-114](../decisions/ADR-114-cluster-occupancy-is-measured-uncapped.md)).

**Distribution.** The harness reports p95, p99, max, the 1% low, the 0.1% low and the variance
alongside the median it always had. "1% low" here means the mean of the slowest 1% of frames and is
a different number from p99; both are printed and the JSON names carry their definitions.

**`--bench-json <file>`** writes the run as a machine-readable record carrying its conditions —
scene, camera pose, resolution, tier, engine revision, build, adapter, frame range, warm-up, and a
per-process session id. Records may only be compared within one session id.

**`--ab <phase>`** runs baseline and arm interleaved, A/B/A/B, in one process and reports the paired
difference against the noise floor. `--ab none` compares the baseline with itself, which is how the
floor itself is measured.

**`--cluster-stats`** reports froxel-grid occupancy (§3.6). It is CPU work inside the measured
frames, so a record taken with it says so and its wall clock is perturbed (23.29 ms against
22.20 ms on Glowmere; GPU unchanged).

### The Constellation discrepancy of §1.1 is withdrawn

§3.3 measured the within-session spread on **Glowmere** — 1.0% GPU — and §1.1 applied it to
**Constellation** to conclude that Constellation's 3.60 ms was "materially different" from the
brief's 6.09 ms and "not noise: five runs, 1% spread".

Constellation's own within-session spread, measured by a null A/B over three interleaved pairs, is
**36.97% GPU / 26.34% wall**. Eight block medians taken in one session on 2026-09-13: 10.16, 9.04,
8.00, 7.80, 6.88, 6.55, 6.42, 6.29, 6.16 ms. Inside a single 240-frame block its p50 is 6.29 ms
against a p90 of 10.81 and a p99 of 12.98 — the per-frame cost varies by a factor of two, so the
median lands wherever the distribution's mass happens to fall.

**Both 3.60 and 6.09 are inside one scene's own noise.** The 41% "material difference" is not a
finding. Glowmere's stability does not transfer to Constellation, and neither does its noise floor.
Anything Constellation is asked to decide needs a stabilised scene or a statistic that is not the
median.

Glowmere's own stability is confirmed by the same instrument: three interleaved baseline blocks
varied by 1.71% GPU / 1.63% wall, and a null A/B on it reports +0.34% GPU — correctly, no result.
The 28% Glowmere gap of §1.1 remains unresolved, and can no longer recur: a record without its
conditions can no longer be written.

### The A/B mode reproduces §4.1's largest attributed effect

`--ab shadowmask`, two pairs, Glowmere: baseline 19.40 ms GPU, arm 23.79 ms, **−4.06 ms (−20.9%)**,
per-pair deltas −4.65 and −4.06. §4.1 found removing the shadow mask costs +5.8 ms of scene pass;
this is the whole-frame figure for the same thing, found without trusting a number from a document.

## 3.6 Cluster occupancy — the §1.5 "measure first" answered

`kMaxLightsPerCluster = 32` was listed as needing measurement before replacement. Measured, 1280×800:

| | Glowmere | Constellation |
|---|---|---|
| froxels | 3072 | 3072 |
| local lights in the grid | 222 | **0** |
| min / p50 / p90 / p99 / max per cluster | 0 / 0 / 14 / 24 / **29** | 0 / 0 / 0 / 0 / 0 |
| mean | 5.70 | 0.00 |
| empty clusters | 1720 (56.0%) | 3072 (100%) |
| **overflowed clusters** | **0** | **0** |
| **lights dropped by the cap** | **0** | **0** |

**Overflow is not happening.** Glowmere's busiest froxel wants 29 of a possible 32 — a margin of
three, which is not comfortable but is not a defect. **Do not raise the cap**: it would enlarge the
cluster buffer and change nothing drawn. Re-run this after any change that adds local lights to
Glowmere, because nothing reports an overflow at runtime.

The brief named clustered lighting "a large uninstrumented cost". It is now instrumented: 56% of the
grid is empty and the `clusters` pass measures 0.07 ms of an 18.5 ms frame. It is not large. And
Constellation does not use the grid at all — its 3072 clusters are all empty — which is one more
reason the two scenes need separate budgets.


# Deliverable 4 — Scalability gap analysis

## 4.1 The central measurement: what is the scene pass actually doing?

The brief warns against inferring a fragment bottleneck from triangle counts. So it was measured
three ways.

**Resolution scaling** — geometry fixed, pixels varied 8×:

| Size | Pixels | Scene pass |
|---|---|---|
| 640×400 | 0.26 M | 10.62 ms |
| 906×566 | 0.51 M | 12.85 ms |
| 1280×800 | 1.02 M | 15.99 ms |
| 1810×1131 | 2.05 M | 22.15 ms |

Eight times the pixels buys 2.1× the time. Least-squares fit: **≈ 9.0 ms fixed + 6.4 µs per 1000 px**.
At 1280×800, **56% of the scene pass is resolution-independent.**

**Same geometry, three fragment shaders** — the control that isolates vertex/binning from fragment:

| Pass | Triangles | Draws | Fragment work | Cost |
|---|---|---|---|---|
| depth prepass | 427,353 | 126 | none | **0.26 ms** |
| shadow | 792,206 | 189 | trivial | **0.66 ms** |
| scene | 430,231 | 136 | PBR + clustered lights + 5 attachments | **15.73 ms** |

**Vertex processing, binning and draw submission are ≈1.7% of the scene pass.** 792k triangles cost
0.66 ms when the fragment shader is trivial. The scene pass is **fragment-bound**, definitively.

**Subsystem attribution** — each arm removed in turn, scene-pass cost:

| Arm removed | Scene pass | Δ |
|---|---|---|
| (none) | 15.73 | — |
| water | 16.19 | +0.5 |
| transparency | 15.66 | −0.1 |
| particles | 15.60 | −0.1 |
| ao | 15.40 | −0.3 |
| volume | 15.93 | +0.2 |
| **shadow mask** | **21.56** | **+5.8** |

Nothing removable accounts for it: the scene pass is **opaque geometry shading**, essentially alone.
And the shadow mask is a large *win* — removing it costs 5.8 ms, because the lit pass then computes
the directional shadow term per pixel. Do not touch it.

## 4.2 The diagnosis, with its confidence level

**Established fact:** the scene pass is fragment-bound, and most of its fragment cost does not scale
with resolution.

**Leading hypothesis (medium-high confidence): quad overdraw on sub-pixel triangles.** A triangle
smaller than a pixel still forces a 2×2 quad of fragment invocations, so invocation count tracks
*triangle count*, not pixel count — which is exactly a cost that is fixed under resolution change.
Glowmere is dense ecology: 15.1 M logical triangles reduced to 430 k, scattered across a valley, most
of it distant. 430 k triangles × 4 invocations ≈ 1.7 M fragment invocations — **more than the 1.02 M
pixels on screen**, independent of resolution.

**Competing explanations considered and largely excluded:** a fixed per-pass or tile-setup cost would
scale with tile count and therefore with resolution; attachment load/store bandwidth likewise; vertex
and binning cost is excluded by the depth and shadow controls above.

**This hypothesis is not yet confirmed and must not be built on until it is.** The confirming
measurement is a Metal frame capture reading fragment invocations against pixels shaded, and the
quad-utilisation / partial-quad counters. **That is the first task of Phase A.**

## 4.3 Ranked gaps

| # | Gap | Why it matters here | Evidence | Difficulty | Risk |
|---|---|---|---|---|---|
| 1 | **No representation change for distant dense geometry** (no HLOD, no impostors, LOD ladder only 4 deep and per-mesh) | directly causes the ~9 ms fixed fragment cost if the hypothesis holds | §4.1, §4.2 | high | medium |
| 2 | **Five-attachment scene pass** — 32 B/px of colour + depth | multiplies per-invocation cost and pressures Apple tile memory | `scene_targets.hpp` | medium | medium |
| 3 | **`kMaxObjects = 256`** visible entities | hard cap below Glowmere's own entity count | §1.5 | low | low |
| 4 | **Multi-material assets culled/counted per material** | inflates instance and draw work on exactly the scatter content that hurts | brief, confirmed | medium | low |
| 5 | **No stable instance identity, scene rebuilt per frame** | blocks persistent GPU scene, temporal visibility, streaming | §1.4 | high | **high** — collides with the derived-copy rule |
| 6 | **No hierarchical occlusion** | Glowmere is a valley — occlusion potential is real but unquantified | none yet | high | medium |
| 7 | **No world streaming / spatial partition** | blocks large worlds; not a current bottleneck | — | high | high |
| 8 | **Volumetrics dominate Constellation** (64%) | a *different* workload needing a different answer | §3.2 | medium | low |
| 9 | `scene_renderer.cpp` at 3,182 lines | maintainability, not performance | §1.3 | medium | low |

## 4.4 What the measurements say NOT to do first

Three popular first moves are contraindicated **by this engine's own numbers**:

- **GPU-driven submission / indirect everything.** Targets CPU submission cost. Glowmere spends
  0.66 ms of CPU on the scene and issues 136 draws. Even perfect elimination saves ~0.7 ms of a
  22 ms frame, and **nothing** of the 15.7 ms scene pass.
- **Hierarchical occlusion culling / HZB.** Removes *geometry* work. Geometry work is 1.7% of the
  scene pass. Occlusion also removes the fragment work behind occluders — which is real and worth
  measuring — but the depth prepass already suppresses opaque overdraw, so the ceiling is lower than
  it looks.
- **A frame graph.** Delivers transient aliasing, pass culling and dependency validation. None of
  those is a measured problem here; the pass order is fixed, documented and asserted.

None of these is *wrong* — each is deferred pending evidence, and the evidence that would justify
each is named in the roadmap.

## 4.5 The gate measurement — quad overdraw confirmed

Apple's overdraw counter would have answered this in one GPU capture, but this repository does not
use Xcode tooling. The question is therefore answered **from inside the engine**, which is the better
instrument anyway: reproducible, committed, and runnable on demand rather than living in a
screenshot. `tests/rendering/test_fragment_cost_perf.cpp`, `[.perf][fragment]`.

**The experiment.** Hold covered pixels *constant* — one plane filling the viewport — and vary only
how many triangles cover them. Coverage never changes, so if cost is flat in triangle count then
invocations track pixels; if it climbs as triangles go sub-pixel, invocations track triangles.

1280×800, 1.02 Mpx, identical shader and identical coverage throughout:

| Triangles | px/triangle | Scene pass | Depth pass (control) |
|---|---|---|---|
| 2 | 512,000 | 3.02 ms | 0.13 |
| 2,048 | 500 | **1.57 ms** | 0.13 |
| 16,200 | 63 | 2.69 ms | 0.13 |
| 131,072 | 7.8 | 3.21 ms | 0.07 |
| 259,200 | 3.95 | 4.13 ms | 0.07 |
| 524,288 | 1.95 | 6.03 ms | 0.20 |
| 1,036,800 | 0.99 | 7.14 ms | 0.33 |
| 2,097,152 | 0.49 | **7.73 ms** | 0.46 |

**Result: 4.9× the scene-pass cost from triangle size alone, at identical coverage.** Subtracting the
depth control leaves ~4.6× of pure fragment cost, against the **4× a 2×2 quad predicts** for
sub-pixel triangles. The knee falls between 7.8 and 3.95 px/triangle, exactly where the quad
threshold sits. The depth column stays flat throughout, so this is fragment work and not vertex or
binning.

**Quad overdraw on sub-pixel triangles is confirmed as the mechanism behind the
resolution-independent component.** Glowmere submits 430 k triangles over 1.02 Mpx — an average of
2.4 px/triangle, and worse than average for the scattered ecology specifically, since terrain
contributes large triangles that pull the mean up.

**What this does and does not establish.** It establishes the *mechanism* and the *shape* of the
curve. It does **not** establish Glowmere's exact split, because the sweep's shader is simpler than
Glowmere's (one directional light, no shadow mask, no procedural material) so absolute milliseconds
do not transfer. Quantifying Glowmere's own share is the first task of Phase C: measure the scene
pass before and after a representation change on the ecology alone.

**A caveat worth keeping:** the 2-triangle case (3.02 ms) is *more* expensive than the
2,048-triangle case (1.57 ms). Two screen-filling triangles are poor for tile parallelism. The
cheapest point is a few hundred pixels per triangle, not the largest possible triangle — so
"fewer triangles" is not monotonically better, and a representation system should target a band
rather than a minimum.

**The instrument caught its own bug.** The first run was perfectly flat at every tessellation, which
looked like a clean null result for quad overdraw. It was the plane being backface-culled by an
inverted winding — the scene pass was shading nothing. The tell was the depth pass rising while the
scene pass stayed at one timestamp quantum. The draw-call and submitted-triangle columns are printed
now precisely so that failure cannot look like a result.

## 4.7 Clustered lighting is instrumented, and the spec's premise is wrong for these scenes

The spec calls clustered light evaluation "a known large untouched cost". Measured, with counts taken
as **uncapped demand** rather than post-cap list lengths — a post-cap statistic saturates at 32 and
can never show whether the cap binds:

| | Glowmere | Constellation |
|---|---|---|
| froxels / local lights | 3072 / 222 | 3072 / **0** |
| per-cluster min / p50 / p90 / p99 / max | 0 / 0 / 14 / 24 / **29** | all 0 |
| mean | 5.70 | 0.00 |
| empty clusters | 1720 (56.0%) | 3072 (100%) |
| **overflowed** | **0** | **0** |
| **lights dropped** | **0** | **0** |

**Light overflow is not real.** The busiest froxel in Glowmere wants 29 of 32 — a margin of three,
worth re-checking after any change that adds local lights, since nothing reports overflow at runtime.
**Do not raise the cap.** And the cluster build is 0.07 ms of an 18.5 ms frame with 56% of the grid
empty; Constellation never touches the grid at all.

## 4.8 Two counters were wrong, and are corrected

- **`RenderStats::lights` reported 8** while Glowmere shaded 230. It counts slots in the 8-long
  *uniform fallback* array, not scene lights. Left in place for its one existing reader, documented,
  and joined by `shadedLights` and `directionalLights`.
- **The offline CPU frame is not CPU-bound.** 21 ms against 18.5 ms GPU looked like CPU cost; 19.99 ms
  of it is `queueWait`. The stage split now separates finish, submit and queue wait, and the JSON
  record warns about it — otherwise every future reader draws the same wrong conclusion.

## 4.6 Two further probes, and what they ruled out

**Geometry density at fixed resolution.** Terrain `viewDistance` swept 520 → 110 m (temporary scene
variants, since removed):

| viewDistance | Triangles | Scene pass |
|---|---|---|
| 520 m | 430,233 | 15.66 ms |
| 300 m | 458,841 | 15.60 ms |
| 180 m | 515,577 | 15.99 ms |
| 110 m | 534,843 | 16.12 ms |

**24% more triangles cost 3% more time.** Terrain triangles are large, so they add coverage without
adding invocations — which is the behaviour the quad-overdraw model predicts, and which rules out
"triangle count" as the driver in its naive form. The cost is not triangles; it is *small* triangles
and per-invocation shader cost.

**Constellation scales differently and must not be merged into the same conclusion:**

| Size | volume | particles | scene |
|---|---|---|---|
| 640×400 | 1.31 | 0.52 | 0.39 |
| 1280×800 | 2.29 | 0.52 | 0.46 |
| 1810×1131 | 4.00 | 0.52 | 0.46 |

Volumetrics are 64% of that frame and scale sub-linearly (3.05× for 8× pixels — a fixed-size froxel
grid plus a resolution-scaled composite). Particles are **completely flat**: simulation- and
geometry-bound, not fragment-bound. Its opaque scene pass is negligible. **Nothing proposed for
Glowmere's bottleneck will help Constellation, and vice versa.** They need separate budgets.
