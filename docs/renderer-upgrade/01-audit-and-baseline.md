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
| Constellation ~6.09 ms GPU | **3.60 ms** | ⚠️ **materially different** |
| Constellation volumetrics ~3.93 ms | **2.29 ms** | ⚠️ **materially different** |

**Discrepancy 1 — Constellation is 41% faster than the brief states.** Not noise: five runs, 1%
spread. The brief's number predates work in this repository or was taken under different conditions.
Anyone A/B-ing against the brief's Constellation figure would measure a phantom improvement.

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
| Cluster occupancy / overflow | ⚠️ cluster buffer readable; no occupancy statistic |
| Transient/geometry/texture memory | ⚠️ transient texture count only |
| **Fragment invocations, quad utilisation, occupancy** | ❌ **not available — Xcode/Metal only** |
| Streaming activity | ❌ no streaming system exists |
| p95/p99/variance | ❌ harness reports p10/p90/min only |

**The one missing capability that blocks the central architectural decision is fragment-invocation
and quad-utilisation counters, which only a Metal frame capture provides.** This was flagged in the
forensics report as the single unused tool in the repository; it is now decision-relevant.

---

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
