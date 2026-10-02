# AV Gen: an evaluation of the live render path

*Measured 2026-10-02.*
- **Machine:** Apple M2 Max, macOS, Metal through Dawn, a release build of main `8ae10a64`.
- **Display:** 120 Hz, so the frame budget is **8.33 ms**.
- **Changes:** no code was optimised. Temporary instrumentation is on the local branch `investigate/live-render-perf`, commit `8f953f53` in `../av-gen-qa-coord`. See the appendix for how to reproduce.

## Executive summary

**The verdict depends on the scene, but the order is consistent: the GPU workload comes first, then CPU "figuring out what to render" in scenes with many characters. WebGPU/Dawn overhead is a distant third.**

| scene | live (0.5 Mpx canvas) | the same scene at 1920×1080 | bound by |
|---|---|---|---|
| benchmark (simple) | 119 fps; CPU 0.9 ms, GPU 3.9 ms | GPU 2.1 ms | vsync, idle |
| sonic-live (representative live) | 95 fps; CPU 2.0 ms, GPU 9.0 ms | GPU 16.5 ms | **GPU** (the lit scene pass) |
| Liminal *All You Got* (SDF) | 60 fps; CPU 2.8 ms, GPU 15.8 ms | GPU 48 ms (~20 fps) | **GPU** (SDF ray march plus volumetrics; pixel-bound) |
| Glowmere Valley 2 (heavy world) | 60 fps; CPU 8.3 ms, GPU 15.3 ms | GPU 41 ms (~24 fps) | **GPU first, CPU second.** CPU alone is already over the 120 Hz budget. |

**Five findings matter most:**
1. **The GPU workload, not submission, is what decides the frame rate on every real scene.** Disabling every optional subsystem still leaves 7-9 ms of GPU work at 0.5 Mpx. And the live canvas is far smaller than what a projector asks for: at 1080p the same scenes cost 2.7-3× more GPU.
2. **AV Gen has a partly optimised live path.**
   - **What's optimised:** resource lifetime, caches, distance limits, deferral budgets and GPU-driven instancing.
   - **What isn't:** the scene-to-GPU data flow is immediate mode. Every frame re-applies every parameter, re-composes every transform, rebuilds and re-uploads every visible object's uniforms, recreates 11 bind groups, and re-encodes every draw unsorted.
3. **One measured CPU defect stands out: posed skinned bounds are computed by CPU-skinning every vertex, up to 4 times per character per frame.** Twice is for culling and twice is for a debug-only diagnostic record that is built every frame unconditionally. On Glowmere that's **18.6% of all main-thread time, about 3 ms per frame**.
4. **Dawn's own CPU work is real but secondary:** about 14% of the Glowmere frame, about 6% of Sonic's. Most of it is `Queue::Submit` translating about 1,000 bind-group sets into Metal. **Rewriting in Metal is not justified by anything measured here.**
5. **"Structural" work driven by modulation happens every frame, but it's cheap today.**
   - Four procedurals regenerate every frame in both Sonic and Glowmere.
   - In Sonic it's because audio routes write generator inputs; in Glowmere it's moving single-instance objects whose placement is part of their hash.
   - It costs about 0.05-0.06 ms per frame now; the risk is that it scales with scatter size.

## Current live architecture (as implemented)

**The main loop is single-threaded,** in `Application::runLive`, `src/app/application.cpp`. All WebGPU work is on the main thread. The swapchain is Fifo, and the acquire happens *before* input and update, so the wait is taken first:

```
poll events -> resize/canvas sizing -> acquireSurfaceView (THE WAIT) -> poll input
-> engine.tick / seek service -> engine.update -> AI pump -> ImGui build (panel.draw)
-> debug geometry -> SceneRenderer::render (records the command encoder) -> ImGui record
-> Finish + Queue::Submit -> collectFrameTimings (non-blocking timestamp readback)
-> viewport pick (only on a click) -> present -> projection/outputs/Syphon -> ProcessEvents
```

**When each part runs:**

| part | when | thread |
|---|---|---|
| audio capture / playback | device buffer (2.7-10 ms) | audio callbacks (lock-free SPSC ring) |
| analysis (FFT, bands, onset, tempo, drums) | hop rate (10-23 ms) | analysis `jthread` → `TripleBuffer` |
| publish to the signal bus | every frame | main |
| timeline automation, `resetFinals` (all parameters), routes | every frame, all of them | main |
| states, sources, palette, behaviours, entity world | every frame | main |
| composition flatten (`rebuild()`) | **only when dirty** | main |
| `Composition::applyParameters` (every node: transform, material, lights) | **every frame, all nodes** | main |
| camera culling of entities (`cullEntityNodes`) | every frame | main |
| rig posing | every frame (distance-rated, ADR-186) | main |
| procedural regeneration | when its hash changes (4 per frame in two scenes) | main |
| mesh / texture upload | when the version changes (0 per frame measured) | main |
| object uniforms, frame uniforms, light packing, cascade fit | every frame | main |
| frame bind groups (3 + 8) | **re-created every frame** | main |
| SDF tree packing | every frame per visible ray-marched object | main |
| SDF shader variant compile | on a structural change (0 during playback) | main |
| ecology instancing / culling / LOD | every frame, **on the GPU** (indirect) | GPU |
| command encoding → Metal translation | every frame, in `Queue::Submit` (Dawn) | main |
| present | every frame | main |

**Live and offline share the renderer.**
- **Live adds:** the realtime tier, live `DetailLimits` (distance cull, LOD, rig rate and entity bands on), interactive deferral budgets (sky IBL 2 ms, seek 2 ms, expensive procedural regeneration), an adaptive canvas scale, and FXAA.
- **Offline lifts the distance limits, keeps the LOD ladder (ADR-191), and blocks on the queue.**

## Is there already a dedicated live path?

**Partially.** What exists, and works:
- **Persistent resources:** meshes and textures are uploaded only on a version change (0 per playback frame measured).
- **Caches:** material bind groups are a cached map; pipelines are cached; compiled SDF variants are cached by tree key; the LOD hysteresis state persists.
- **GPU-driven ecology:** indirect draws, a GPU cull, a LOD ladder, and empty-draw skipping.
- **Live-only policy:** the realtime tier, detail limits, deferral budgets, the adaptive canvas scale, and suspending the viewport while a render runs.
- **Non-blocking GPU timing readback,** and no CPU/GPU fences in the frame. The only blocking waits are the swapchain acquire and the click pick.

What it does not have:
- **No incremental scene update.** Parameter → scene → GPU is recomputed whole every frame. There are no dirty transforms, no dirty materials, and no persistent per-object GPU state keyed by "changed".
- **No draw sorting or redundant-state elision** for entity draws. Only the procedural renderer dedups binds. There's no instancing for repeated entity meshes.
- **Debug and diagnostic work is unconditionally in the hot path:** the per-entity `RenderObjectDiagnostic` build.

So live playback is "the normal renderer run continuously, with live-specific *policy* (tier, distance and deferral) and well-cached *resources*, but an immediate-mode *update*".

## Representative scene measurements

**Method:**
- **The live editor:** `--play --size 1440x900 --adaptive-scale off --frames 600`. The first 120 frames are discarded, leaving 480 frames per arm.
- **Isolation:** every arm of a scene ran back to back inside one hold of the GPU lock (`tools/gpu-lock.sh`), so other agents' GPU work was excluded. CPU-only work by other processes was not excluded; the load average was 3-6.
- **Each scene was bracketed by a baseline at both ends.** The baseline-to-baseline drift was ≤0.7 ms CPU and ≤1.1 ms GPU, so smaller deltas are noise.
- **The live canvas was 492×1024 px (0.50 Mpx),** the viewport in this worktree's saved editor layout.
- **The 1080p numbers** come from headless runs of the same projects (`--headless --tier realtime --size 1920x1080 --frames 240`). At the matched size, headless GPU equals live GPU (Glowmere: 14.0 vs 14.9 ms), so they transfer.

**Frame pacing, live, both baselines:**

| scene | fps (mean) | interval mean / sd | 1% low (mean of the slowest 1%) | frames over one vsync | CPU work mean / max | GPU mean / max |
|---|---|---|---|---|---|---|
| benchmark | 119 | 8.4 / 1.0-1.4 ms | 13-17 ms | 13% | 0.9-1.1 / 4.8 ms | 3.9-4.5 / 7.3 ms |
| sonic-live | 94-98 | 10.3-10.6 / 3.9-4.1 | 20-21 ms | 29-31% | 2.0 / 5.8 | 9.0 / 11.8 |
| liminal | 55-61 | 16.4-18.2 / 4.9-5.7 | 28-33 ms | 77-97% | 2.8-3.0 / 9-18 | 15.8-17.4 / 20-30 |
| glowmere | 60-61 | 16.3-16.7 / 5.2 | 27-28 ms | 82% | 7.6-8.3 / 9.6-10.9 | 15.1-15.3 / 21.6-22.7 |

**Frame time is not stable in the GPU-bound scenes.** Fifo quantises a 9-16 ms GPU frame into 8.3, 16.7 and 25 ms intervals (sd 4-6 ms): visible judder at 120 Hz, not a smooth lower rate.

## CPU vs GPU (live, base1, medians)

| | benchmark | sonic-live | liminal | glowmere |
|---|---|---|---|---|
| frame interval | 8.31 | 8.40 (p99 19) | 16.6 | 16.6 |
| **CPU work** (frame minus the waits) | **0.64** | **1.93** | **2.58** | **8.30** |
| swapchain acquire wait (synchronisation) | 7.6 | 6.5 | 14.0 | 8.3 |
| present | 0.01 | 0.01 | 0.01 | 0.01 |
| engine.update | 0.04 | 0.53 | 0.89 | 3.50 |
| …applyParameters | 0.03 | 0.49 | 0.58 | 0.59 |
| …modulation (routes) | 0.005 | 0.04 | 0.04 | 0.40 |
| UI build (ImGui) | 0.10 | 0.14 | 0.36 | 0.32 |
| render preparation + encoding (`render.record`) | 0.17 | 0.45 | 0.79 | 2.95 |
| …of which the per-entity diagnostic | 0.00 | 0.00 | **0.46** | **1.51** |
| submission (`Finish` + `Submit`, Dawn → Metal) | 0.27 | 0.64 | 0.41 | 1.32 |
| **GPU frame** | **3.8** | **8.9** | **15.6** | **14.9** |
| draws / bind-group sets / VB binds / passes | 2 / 23 / 5 / 10 | 30 / 263 / 126 / 11 | 5 / 17 / – / 11 | 180 / 1,020 / 597 / 15 |

**Notes:**
- CPU and GPU overlap: Glowmere's 8.3 ms of CPU plus 15 ms of GPU gives a 16.6 ms interval, not 23 ms. So the slower of the two sets the rate.
- GPU "frame" is the span from the first pass starting to the last pass ending. In the Liminal no-AO arm it exceeded the frame interval (15 ms at an 8.9 ms p50 interval), which means consecutive frames overlap on the GPU. Treat it as an upper bound, and per-pass deltas as the attribution.

**GPU by pass, live, 0.5 Mpx (median ms):**
- **Sonic:** scene 5.8, shadow 1.4, shadow mask 0.33, DoF 0.26, particles 0.20, bloom 0.20, AO 0.13, exposure 0.13.
- **Liminal:** sdf 9.8, motion blur 0.85, scene 0.52, depth 0.13, bloom 0.07.
- **Glowmere:** scene 8.4, motion blur 1.6, DoF 0.52, shadow 0.46, particles 0.33, cull 0.20, AO 0.13, shadow mask 0.20.

**GPU by pass, 1920×1080, realtime tier (median ms):**
- **Sonic, 16.5:** scene 12.2, shadow 1.3, DoF 0.85, AO 0.52.
- **Liminal, 48.0:** sdf 29.4, volume march 13.0, motion blur 4.8.
- **Glowmere, 41.0:** scene 23.2, volume march 7.9, motion blur 4.3, DoF 1.4, shadow 0.6, AO 0.5.

## Controlled experiments (live, 0.5 Mpx; delta vs the mean of the two baselines)

**These are removal arms through the existing `--disable` and a temporary `AVGEN_X_NOMOD`.** "≈" means inside the baseline-to-baseline drift.

| arm | sonic GPU | sonic CPU | liminal GPU | liminal CPU | glowmere GPU | glowmere CPU |
|---|---|---|---|---|---|---|
| shadows off | **−1.1** | −0.4 | ≈ | ≈ | ≈ (−0.4) | **−1.0** (encode, submit) |
| AO off | ≈ | ≈ | ≈ (−0.6) | ≈ | ≈ | ≈ |
| volumetrics off | ≈ | ≈ | **−5.1** | ≈ | **−1.9** | ≈ |
| post off | **−0.7** | −0.3 | ≈ | ≈ | **−2.2** | −0.6 |
| particles off | ≈ | ≈ | ≈ | ≈ | ≈ | ≈ |
| animation off | ≈ | ≈ | ≈ | ≈ | ≈ | ≈ |
| everything optional off | **−1.7** | **−0.8** | **−6.1** | −0.4 | **−5.6** | **−1.4** |
| modulation off | (picture changes) | −0.1 | (picture changes) | ≈ | (picture changes) | **−0.4** (routes) |
| half resolution (¼ of the pixels) | **−2.6** | ≈ | **−6.8** | ≈ | **−5.7** | ≈ |

**What the arms say:**
- **Even with everything optional off,** there's 7.2 (Sonic), 10.1 (Liminal) and 9.2 (Glowmere) ms of GPU left. That's the core lit scene pass or SDF march: the geometry and shading itself.
- **The fixed versus pixel split:**
  - **Liminal is pixel-bound:** the SDF pass falls from 9.8 to 3.7 ms at ¼ the pixels.
  - **Sonic is split:** about 3.5 ms is per-pixel and about 5.4 ms is fixed per vertex or per pass.
  - **Glowmere at 0.5 Mpx is mostly per-pixel,** but at half resolution the CPU (7.5 ms) becomes the limit: a 13.8 ms interval rather than 8.3.
- **The modulation-off arm is valid for CPU only.** Removing routes puts the scene at different parameter values, a different picture, and made the GPU *slower* in two scenes.
- **Not isolated:** audio analysis (it is on its own thread; its main-thread share is the bus publish, ≤0.01 ms), dynamic lights, the UI overlay (measured directly instead: 0.1-0.4 ms CPU), and scene traversal. Culling was measured through the sample rather than an arm.

## Biggest costs, ranked by measured cost only

**GPU, at projector size (1080p, realtime):**
1. The SDF ray march (Liminal): 29.4 ms.
2. The lit scene pass: Glowmere 23.2 ms, Sonic 12.2 ms.
3. The volumetric march: Liminal 13.0 ms, Glowmere 7.9 ms.
4. Motion blur: 4.3-4.8 ms.
5. Shadows: Sonic 1.3 ms.
6. DoF: 0.9-1.4 ms.

**CPU, live (Glowmere, the only scene where CPU is near a limit):**
1. The swapchain wait (not work): 8.3 ms.
2. **CPU-skinned posed bounds (`scene::entityCullBounds`): 18.6% of main-thread samples, about 3 ms.** It's split between the diagnostic record (1.5 ms, timed) and `Composition::cullEntityNodes` (9.8% of samples, about 1.6 ms).
3. Dawn submit, translating to Metal: 1.3 ms.
4. The rest of render preparation and encoding: about 1.4 ms.
5. `applyParameters`: 0.59 ms.
6. Modulation routes: 0.40 ms.
7. UI build: 0.32 ms.
8. Entity-world behaviours: about 0.4 ms (2.6% of samples).
9. Procedural regeneration: 0.05 ms.
10. Frame bind-group re-creation: 0.02 ms.

## Cacheability: work done every frame that doesn't need to be

**Each item is classed by when it actually needs to run:**
- **A:** every frame.
- **B:** when state changes.
- **C:** when the scene changes.
- **D:** could be amortised.

| work done every frame today | class | evidence | measured cost |
|---|---|---|---|
| **The per-entity `RenderObjectDiagnostic` record:** a name string copy, an FNV hash of the material program bytes, `entityCullBounds` twice and 6 plane margins. It's consumed only by debug views, probes and tests. | B, or only when a consumer is attached | `scene_renderer.cpp`, the loop before `FrameUniforms frame{}` | **0.46 ms** (Liminal, 1 skinned figure), **1.51 ms** (Glowmere) |
| **The CPU skinning of every vertex for a skinned entity's posed bounds,** with no cache, though `rig.paletteVersion` exists to key one. EntityWorld-driven entities are culled twice (two loops), plus a linear `findNode` and node scan per entity. | B (on a palette change) / D (a bone-sphere bound would do) | `scene.cpp` `entityCullBounds`; `composition.cpp` `cullEntityNodes` | about 1.6 ms in culling (Glowmere), plus the row above |
| **`applyParameters`:** every node's transform recomposed and material and light values written into the flat scene, whether or not any parameter moved | B | `composition.cpp` `Composition::applyParameters` | 0.03 / 0.49 / 0.58 / 0.59 ms |
| **`resetFinals` on every parameter, plus every route run once per op priority,** even when its source didn't change | A for routes whose source moved, B otherwise | `parameter_set.cpp`, `modulation.cpp` | 0.04-0.40 ms |
| **Object uniforms for every visible entity,** including an inverse matrix, re-staged and re-uploaded whole: 512 B per object | B (static entities) | `makeItem`, `WriteBuffer(objectUniforms_)` | inside `render.record`; small (0.10 ms on Glowmere) |
| **`prevModelsNext_`,** a motion-vector history in a map keyed by the entity's name `std::string`, rebuilt for every entity | B | `scene_renderer.cpp` | 0.014 ms (Glowmere) |
| **`rebuildFrameBindGroups()`:** 3 + `kMaxShadowViews` (8) `CreateBindGroup` calls per frame. Its comment says the AO output and shadow atlas may change during setup. | B (on a target or view change) | `scene_renderer.cpp:930` | 0.02 ms |
| **A copy of the LOD rung vector per entity per frame** (`chooseGeometry`), and a `std::string` built per draw in `MaterialPrograms::slotOf` | B / A-cheap | `scene_renderer.cpp`, `material_programs.cpp` | not separately measurable |
| **SDF tree re-pack (`sdfCompileKey` + `sdfCompileTable`)** per visible ray-marched object | B (on a tree change) | `sdf_renderer.cpp` | 0.04 ms (Liminal) |
| **Procedural regeneration from modulated inputs** (Sonic: facets, halos, meridians, shards) and from moving single-instance objects (Glowmere: visitor ×4) | B / C: a moving placement should be a transform, not a regeneration | `procedural.cpp`, `rebuild` hash changes every frame | 0.05-0.06 ms; scales with instance count |
| **Unsorted entity draws:** pipeline, 2 bind groups, VB and IB set per draw, in the depth, scene and every shadow view; no redundant-state skipping | A, but much is redundant | the `drawItems` lambda | part of Dawn's submit cost (1.3 ms on Glowmere) |

## Scene-specific performance factors

**Why projects differ:**
- **Pixel-shaded content dominates GPU time,** and it multiplies with resolution. Contributors are SDF ray-march objects, volumetric media and the lit scene pass's material programs. Liminal and Glowmere cost 2.7-3× more at 1080p than in the editor's canvas. **A project that is fine in the editor can be too slow when projected,** because ADR-1026 projection renders at the projector's size.
- **Characters cost CPU.** Each skinned entity is CPU-skinned up to 4× per frame. Glowmere, with 256 entities, many of them farm animals and aliens, pays about 3 ms; scenes without rigs pay nothing. Expect this to bite the pass 4 Liminal city, which adds rigged walkers.
- **Entity count drives submission, not GPU.** Glowmere's 180 draws and 1,020 bind-group sets cost 1.3 ms of Dawn submit. GPU-instanced ecology (62k instances culled to 357 on the GPU) costs almost nothing on the CPU.
- **Parameter count drives `applyParameters`, but only weakly:** 330 parameters → 0.03 ms; 3,700-6,600 → 0.5-0.6 ms.
- **Routes drive modulation cost:** 452 routes → 0.04 ms, while 58 routes in Glowmere → 0.40 ms. So cost depends on what the routes write: per-node writes trigger work downstream, not the route count itself.
- **Modulation that touches structure** (procedural generator inputs) turns into per-frame regeneration.
- **The offline tier is not paid by live** (Glowmere's offline volume march is 69 ms against 7.9 at realtime), and no offline-only preparation was found in the live frame.

## WebGPU/Dawn assessment

- **Sonic:** Dawn's non-waiting CPU work is about 6% of the main thread (submit 5%, `CreateBindGroup` 0.6%, `WriteBuffer` 0.6%).
- **Glowmere:** about 14%, about 2.3 ms per frame.
  - **Breakdown:** `Queue::Submit` (Metal command translation) 8.2%; encoder API calls 3.8%; `WriteBuffer` 1.1%; `CreateBindGroup` 0.3%.
  - **It scales with the command count:** 180 draws, 1,020 bind-group sets and 600 VB binds.
- **No pipeline creation, shader compilation, buffer creation or texture upload happens during steady playback.** The only per-frame object creation is the 11 frame bind groups, at 0.02 ms.
- **The renderer is GPU-bound in every real scene, so Dawn's CPU overhead isn't what sets today's frame rate.** In Glowmere at 120 Hz it would be one of several CPU items: it's half the size of the skinned-bounds cost.
- **Validation:** whether Dawn validation is on in release was not checked. It's worth confirming, because it inflates submit.
- **There is no evidence that would justify a Metal rewrite.** Halving the command count (sorting, elision, instancing) would recover most of what Dawn costs.

## Potential live renderer improvements (not implemented)

| opportunity | evidence | mechanism | confidence | complexity |
|---|---|---|---|---|
| Build `RenderObjectDiagnostic` only when a consumer is attached (debug view, probe, test), or lazily | 0.46-1.51 ms measured | removes the debug work from the hot path | **measured / high** | low |
| Cache posed skinned bounds per rig by `paletteVersion`, or bound by joint spheres; cull each entity once; replace the linear node scan | 18.6% of Glowmere's main thread | removes 3 of 4 CPU skinnings and makes the 4th cheap | **measured / high** | low-medium |
| Resolution: render live and projection at a scale chosen for the GPU budget. The adaptive canvas scale exists; confirm it applies to the projection output, and consider upscaling | 2.7-3× GPU from 0.5 to 2 Mpx; half resolution measured at −2.6 to −6.8 ms | fewer shaded pixels in the pixel-bound passes | **measured / high** | low (policy) to medium (upscaler) |
| SDF and volumetric march cost (steps, epsilon, half-resolution march with an upsample) in live | the SDF pass is 29 ms and volume 13 ms at 1080p | GPU fill | **strongly indicated** (the pass is pixel-bound) | medium |
| Lit scene pass: material program cost and overdraw, per scene | the largest pass everywhere; it remains with everything off | GPU shading | **strongly indicated**, needs a per-material breakdown | medium-high |
| Dirty-state propagation: parameter → node → entity → object slot. Upload only changed object uniforms; skip `applyParameters` for untouched nodes | `applyParameters` 0.5-0.6 ms; uniforms re-staged for every visible object | less CPU and fewer uploads per frame | **plausible but unverified as an fps gain** (the scenes are GPU-bound) | medium-high (touches the composition contract) |
| Sort entity draws by pipeline and material, elide redundant binds, instance repeated meshes | 1,020 bind-group sets and 600 VB binds; Dawn submit 1.3 ms on Glowmere | less Dawn encode and submit CPU | **strongly indicated for CPU**, unlikely to matter for fps today | medium |
| Treat moving placements as transforms, not procedural regenerations; separate structural from continuous procedural inputs | 4 regenerations per frame in two scenes, 0.05 ms | avoids a regeneration cost that grows with scatter size | **plausible** (cheap now) | medium |
| Cache the frame bind groups keyed by their views | 11 creations per frame, 0.02 ms | negligible | **unlikely to matter** | low |
| Name-keyed motion-vector map → index-keyed | 0.014 ms | negligible | **unlikely to matter** | low |

## Answers to the six questions

1. **Does AV Gen already have a genuinely optimised live path? Partially.** It has live-specific policy (tier, distance limits, deferral budgets, adaptive scale) and well-cached resources (mesh, texture, pipeline, material, SDF variant, GPU-driven ecology). Its scene-to-GPU *update* is immediate mode: there's no incremental or dirty-state path, and debug diagnostics run in the hot path.
2. **How much is unavoidable rendering work versus overhead?** The frame rate is set by the GPU, and the arms found no wasted GPU passes. 60-80% of the GPU frame remains with every optional subsystem off, and most of the rest is optional features doing visible work. **On the GPU side, essentially all of it is rendering work.** On the CPU side, of Glowmere's 8.3 ms, about 3 ms (36%) is identified overhead (CPU skinning for bounds and diagnostics), about 1.3 ms is Dawn translation, and the remainder is genuine update and encoding. Uncertainty: ±0.7 ms run drift; the CPU attribution comes from 1 ms sampling and timers.
3. **CPU-bound, GPU-bound or synchronisation-bound? Scene-dependent, but GPU-bound in every real scene measured.** The large acquire wait is the CPU waiting on the GPU (backpressure), not a synchronisation defect. Glowmere is also CPU-limited at 120 Hz once the GPU is relieved (CPU 7.5-8.3 ms against an 8.33 ms budget).
4. **What is done every frame that is unnecessary or cacheable?** The cacheability table above: the diagnostic records; CPU-skinned bounds (4×); whole-scene `applyParameters`; every object's uniforms; 11 bind groups; SDF re-packs; per-frame procedural regenerations; unsorted, unelided draw state.
5. **Are there architectural opportunities for a persistent or incremental live path?** Yes:
   - dirty propagation from parameters to entities to object slots;
   - persistent per-object GPU state;
   - cached posed bounds;
   - sorted and instanced entity submission;
   - structural versus continuous separation for procedural inputs;
   - cached frame bind groups;
   - diagnostics on demand.
6. **Would they plausibly bring a meaningful fps improvement?** For today's scenes, only the GPU-side items would: resolution policy, the SDF and volume march, and the scene pass. Those are **measured / strongly indicated**. The CPU items are **measured** as CPU savings (about 3 ms on Glowmere), but they become fps only once the GPU is under budget, or at 120 Hz. Dirty propagation and draw sorting are **plausible but unverified** as fps wins.

**In the brief's terms:** AV Gen is mostly **"rendering efficiently, but the GPU workload itself is too heavy"** (worst at projector resolution). On character-heavy scenes it is also **"doing too much work to figure out what to render"** on the CPU, though today that's hidden behind the GPU. It is **not "rendering too much"**: culling and LOD are effective (62k ecology instances culled to 357).

## Recommended next investigation

**The smallest experiment that most reduces uncertainty:** measure the *projection* path at the projector's actual resolution, live, on Glowmere and Liminal. Use Start projection on a 1920×1080 output, with this branch's `AVGEN_LIVE_FRAME_CSV`, and run it with and without the adaptive canvas scale.

Every scene's verdict changes between 0.5 and 2 Mpx, and live projection is the case the owner performs with. That run answers two questions:
- whether the existing adaptive scale already keeps projection at frame rate;
- which GPU pass to attack first.

**The cheapest CPU follow-up:** gate the diagnostic build on a consumer and cache posed bounds by `paletteVersion`, then re-run Glowmere's interleaved baseline. The prediction is about −3 ms of CPU work.

## Appendix: reproducing

**Location:** the worktree `../av-gen-qa-coord`, branch `investigate/live-render-perf`, commit `8f953f53`, with TEMPORARY instrumentation.
- `AVGEN_LIVE_FRAME_CSV=<f>` writes per-frame GPU passes, counters and probe timers.
- `AVGEN_X_NOMOD=1` turns modulation off.
- `AVGEN_X_PROCLOG=1` names the regenerating procedurals.

**Scripts and raw CSVs/samples:** in the session scratchpad `liveperf/`.
- `all.sh` / `scene.sh` run the live arms under one lock hold per scene.
- `batch2.sh` runs the procedural log and the headless 1080p and offline runs.
- `cmp.py`, `an.py`, `samp.py` and `hl.py` are the analysis tools.

**Gaps:**
- heap allocations were not counted (`AVGEN_ALLOC_COUNTERS` is off by default);
- the projection output itself was not measured;
- Dawn validation was not checked;
- dynamic lights, culling and traversal had no removal arm; their costs came from the sample and the timers.
