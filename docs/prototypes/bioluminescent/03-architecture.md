# World / Environment / Hybrid: architecture and the running assessment

Brief §6, §13, §14, §18. This document is kept current; every claim about the general renderer is either
measured here or marked as a hypothesis.

## 1. Where the frame goes today (inspected 2026-10-06, main f56eaab0)

- **The driver.** `Engine::tick` and `Engine::update` (src/app/engine.cpp ~6172-6465) run, in order:
  - analysis;
  - control hub (MIDI);
  - time signals and cues;
  - sources, then scene states;
  - timeline;
  - fields, entity signals and routes (`modulator_.applyRoutes`);
  - effects;
  - the simulation input key;
  - `controller_->update` (the Composition flattens into `scene::Scene`).

  The caller then hands `SceneRenderer::render(encoder, scene, time, target, shaderInputs)` the Scene. The offline
  path is `RenderJob::renderOne` (render_job.cpp:703), the live path is application.cpp:5402, and stills render
  through `renderToImage`.
- **`SceneRenderer::render` (scene_renderer.cpp:2748).** Lights and clusters, then compute (fields, simulation,
  particles, procedurals, SDF). Then shadows, background, depth prepass, AO, fog-sky, and pass 1 (HDR + 4 aux + depth).
  Pass 1 draws entities, procedurals, SDF meshes, the SDF raymarch (its own pass on the same targets), sky, water,
  particles, ribbons and shells. Then volumes, distortion, temporal, post (bloom), tonemap, overlay.
- **Specialised renderers already plug in this way.** `SdfRenderer`, `ProceduralRenderer`, `ParticleRenderer`,
  `WaterRenderer`, `VolumeRenderer` and others are each a `unique_ptr` in SceneRenderer. Each reads its slice of
  `scene::Scene` and draws into the shared HDR + depth targets. `SdfRenderer` is the closest template for an
  Environment renderer: own compute, own pass on the shared targets, and depth-prepass and shadow hooks.
- **Everything downstream of the HDR target is renderer-agnostic.** That covers post, bloom, temporal, tonemap,
  `--render` PNG/EXR/video, AOVs, the live quality ladder (render scale) and the output mapping. A renderer that
  writes `hdr_` + depth (+ the aux targets for emission-weighted bloom and AOVs) gets all of it for free.
- **Parameters.** A subsystem registers `ParamDesc`s into `params::ParameterSet` under a path prefix (the
  `registerXParameters`/`applyXParameters` pattern in `Composition::attach`). Routes, MIDI, presets, the timeline
  and the editor's generic parameter panel then reach it with no further wiring.
- **No scene-level mode exists.** A project's `assets.scene.kind` picks a loader (orb, gltf, composition), not a
  renderer. "Environment" is already taken in the code: `scene::Environment` is sky, IBL and fog, and there are
  `EnvironmentProcessor` and `ui/environment_panel`.

## 2. Where the distinction belongs (the smallest clean seam)

The brief's principle is that the common ground is the audiovisual, control and output infrastructure, not the
renderer. In this engine that common ground is already everything outside `SceneRenderer`'s pass list, plus the
shared HDR/depth targets. So:
- **WORLD** = a composition scene, as today.
- **ENVIRONMENT** = a composition scene whose content is an `"ecosystem"` block (working name; see the naming note
  below). The block is owned by a specialised renderer and simulation, `EnvironmentRenderer` in SceneRenderer,
  modelled on `SdfRenderer`. It brings its own compute and passes, writes the shared HDR + depth + aux, and registers
  its parameters under its own prefix. It gets audio, the signal bus, routes, MIDI, states, the timeline, camera,
  output, live/offline and recording unchanged.
- **HYBRID** = the same scene with ordinary nodes beside the block. Both draw into one depth buffer, so a character
  walks through the ecosystem, and the camera, lights and post are shared.

The mode is then a property of the scene's content, not a switch: with no environment block the scene is WORLD,
with an environment block and nothing else it is ENVIRONMENT, and with both it is HYBRID. A switch would need a
compatibility path for every existing scene, and this needs none.

**Naming.** `"environment"` cannot be the JSON key, because it already means sky and fog. Each Environment names its
own block after what it is: the first is `"ecosystem"` (ADR-1200).

### 2.1 The seam contract (for every Environment; the second user is production Astral Forge)

Three pieces, each small. **If the shape of any of them changes, this section says so.**

**1. A scene block.**
- It is a top-level JSON key, parsed by the Composition (`Composition::fromJsonImpl`), and must be added to
  `kSceneKeys`.
- It is stored on the Composition as authored ("rest") values and copied onto `scene::Scene` every frame with its
  parameter finals applied (`Composition::update`, beside the grids).
- The ecosystem's block: `"ecosystem"` → `scene::Ecosystem` (`src/scene/ecosystem.hpp`).
- Unknown keys are refused, not ignored.

**2. Parameters.**
- `registerXParameters(params, rest, prefix)` / `applyXParameters(p, rest, live)`, called from
  `Composition::attach` and from the block's setter if attached later. This is the same pattern as grids
  (ADR-1122).
- Routes, MIDI, presets, scene states, the timeline and the editor's generic parameter panel then reach every leaf
  by path, with no more wiring. The ecosystem's leaves are `ecosystem/<layer>/<leaf>`.

**3. A renderer.**
- `rendering::EnvironmentRenderer` (`src/rendering/environment_renderer.hpp`) has four members: `name()`,
  `wants(scene)`, `encode(EnvironmentFrame)` and `reload(shaders)`.
- `SceneRenderer` owns the implementations and lists them in `environments_` (one line in `init`).
- Every frame, after the lit pass and before the volumetric medium, each environment that `wants` the scene gets
  `encode` with an `EnvironmentFrame`:
  - the command encoder, the scene and the frame time;
  - the FrameUniforms buffer (`shaders/common.wgsl` `frame`);
  - the field block and the simulated-grid table (`FieldUniforms`, `fields.wgsl`);
  - the prepass's linear depth (R32Float view distance);
  - the HDR and emission targets and the depth buffer (Depth24Plus);
  - the HDR size (render scale applied);
  - the live quality settings;
  - the GPU timeline (mark each pass `"<name>.<pass>"`).
- An environment may run any compute, write HDR and emission, and test against or write depth.
- A `wants` that is true makes the depth prepass run.
- With none wanting the scene, nothing is recorded, and the frame is byte-identical.

**What the seam gives an environment for free:**
- audio, analysis, the signal bus and routes;
- MIDI, scene states and the timeline;
- the camera (any mode);
- post, bloom (through the emission target), temporal and tonemap;
- `--render` to PNG, EXR or video, and AOVs;
- the live quality ladder (render scale);
- conventional content in the same frame (HYBRID: shared depth both ways).

**What an environment brings itself:**
- **Its own GPU state.**
- **Determinism.** Either make it a pure function of time (the ecosystem is: per-point hashes and time) or keep its
  stateful part in a `Simulation` grid (exact seek through ADR-1119 checkpoints; the Rift's medium is ADR-1201). A
  particle state outside `Simulation` gets ADR-360's contract (play exact, a scrub visually equivalent after a
  pre-roll), as Astral Forge's prototype does.
- **Quality tiers.** Read `EnvironmentFrame::quality` (`QualitySettings`: render scale, tier, the live ladder's
  levers). Add a lever there only if the environment needs one the ladder does not already have.

**Not built, on purpose:**
- no registry or plugin loading;
- no shared environment base state;
- no pass scheduling beyond "after the lit pass".

Astral Forge's surface march fits this point: it writes HDR and depth after the lit pass, and the medium and post
follow. An environment that must be lit by the scene's shadows, or must cast into them, would need a second hook in
the shadow pass. Add it when a user needs it (`SdfRenderer`'s `drawShadow` is the model).

Commits to cherry-pick for the seam alone:
- `src/rendering/environment_renderer.hpp`;
- the `environments_` list and the `EnvironmentFrame` call in `scene_renderer.{hpp,cpp}`;
- (optionally) `src/scene/ecosystem.*` and `src/rendering/ecosystem_renderer.*` as the worked example.

## 3. CP1: the general renderer at the Rift's first density (measured)

`examples/bioluminescent/cp1.py` builds the Rift with only existing systems:
- terrain (the canyon) and water (the river);
- seven generated species as `points` distributions of GLB meshes with LOD;
- emissive materials;
- an onset field (travelling kick front) and a spectrum field as emission effectors;
- 139 clustered point lights (lantern and crown pools), the volumetric medium with local lights, particles,
  and bloom with chroma retention.

Stills: `~/Desktop/av-gen-review/40-bioluminescent/cp1-reference-board/stills/`.

**Cost.** `06-flight` at 1920×1080, realtime tier, 228 measured frames, under the GPU lock
(`--headless --frames 240 --bench-json`):

| | p50 |
|---|---|
| GPU frame | **76.1 ms** (13 fps) |
| scene pass (lit, pass 1) | 43.9 ms |
| volume march | 24.6 ms |
| depth prepass | 5.0 ms |
| everything else | 2.6 ms |
| CPU (excluding the queue wait) | ~6.7 ms (lights 2.0, objects 1.2, encode 1.2, submit 1.5) |

Counters:
- 1,196 draws;
- 8.56 M submitted triangles (446 M logical);
- 24,110 visible and 171,385 culled instances;
- LOD distribution 242 / 1,386 / 4,790 / 17,692 across the four levels;
- 140 lights;
- 60,000 particle capacity.

**Reading.** The frame is GPU-bound in two places:
- **The lit pass (44 ms).** The cm-scale emitters (polyps, photophores, chain beads) are tessellated icospheres:
  sub-pixel triangles shaded through the full clustered PBR path with 140 lights. That is the worst case for a
  rasteriser (quad overdraw), and a pure emitter does not need lighting at all.
- **The volume march (25 ms).** The medium is marched with 139 local lights in scope.

The first is a representation problem; the second is a lighting-model problem. Neither has been isolated yet.

**What it looks like.** Better than expected: dense, framed, with real canyon scale and a canopy silhouette. Its
failures against §16 are listed in the CP1 status: the haze washes the darkness out, organisms do not light their
surroundings (except through the expensive light pools), the mats read as pebbles, river artifacts, and no
propagation yet.

### 3.1 What the cost is (A/B, same camera, 1080p realtime, GPU p50 ms, under the lock)

Scene variants of `06-flight` (`cp1/ab-*.json`, built from the still's scene by deleting or changing one thing):

| Variant | Frame | Scene pass | Volume march | Depth |
|---|---|---|---|---|
| base (run twice) | 76.2 / 73.0 | 44.3 / 41.9 | 24.7 / 24.3 | 5.05 |
| emitter parts removed (beads, polyps, chains, tips, pods) | 53.2 | 25.0 | 24.5 | 1.90 |
| emitter parts `unlit` | 74.5 | 42.4 | 24.5 | 5.05 |
| the 139 point lights removed (moon only) | 53.4 | 35.4 | 11.3 | 5.05 |
| `volumeLocalLights` 0 | 68.0 | 41.9 | 19.3 | 5.05 |
| no medium (`volumeDensity` 0) | 48.3 | 41.8 | — | 5.05 |
| emitter parts only (no bodies) | 61.2 | 32.2 | 24.2 | 3.41 |

**Findings:**
1. **The micro-emitters cost about 22 ms** (19 in the lit pass, 3 in the depth prepass), and making them unlit saves
   nothing (42.4 against 44.3). Their cost is the **rasterisation of sub-pixel geometry**, not their shading: a
   20-triangle icosphere per polyp, quad overdraw. No material or light setting can fix it. That is a
   representation problem, so it needs a different representation: points accumulated in compute.
2. **The light pools cost about 22 ms:** 9 in the lit pass (clustered shading with 139 lights) and 13 in the medium.
   They are also capped at 224 lights per scene. "Thousands of small sources collectively lighting the canyon"
   (§3) cannot be lights. It has to be a field the surfaces and the haze read.
3. **The medium alone is 11 ms** at the realtime tier (32 steps to 900 m).
4. **Bodies cost about 12 ms** of the lit pass at this density. They are ordinary instanced meshes, and they can
   stay conventional if they stop paying for 139 lights.

## 4. The next measurements (to decide what is specialised, not to assume it)

| Question | Experiment |
|---|---|
| Is the lit pass the micro-emitters' geometry? | A/B the same frame with the bead parts removed, and with them drawn as unlit point sprites |
| Is it the 140 lights? | A/B lights 140 → 8 at the same content |
| Is the volume march the local lights? | A/B `volumeLocalLights` 1 → 0, and the volume quality arms |
| Can a glow field replace the lights? | Prototype: a low-res emission map (the propagation field's own output) sampled by ground, walls, water and haze, against 139 point lights, for cost and look side by side |
| Does unlit/emissive shading of pure emitters cost what lit shading does? | `unlit` material on the emitter parts |

## 5. The running assessment (brief §18), as of CP4

**What the generalized renderer does well, and keeps doing in the Rift.**
- **Terrain and water:** the canyon is three authored features, and its walls frame every shot.
- **Instanced, culled bodies:** crinoids, sea pens, fans, mats, whips, lanterns. Lean meshes; no decimated LODs
  (vertex clustering turned feathery organisms into blocks).
- **Clustered lights.** The hero crown lights are about 120, and only the nearest matter.
- **The volumetric medium.**
- **Particles:** spores and swarms emitted along the flight spline.
- **Material programs:** the rock reads the medium as a field.
- **The rest of the control and output stack, unchanged:**
  - bloom with chroma retention, post and tonemap;
  - scene states, presets, routes and MIDI;
  - the spline camera with banking;
  - every output path.

**What it does poorly for this Environment (measured, §3.1).**
- **Micro-emitters as geometry:** about 22 ms at CP1 density, raster-bound (unlit saved nothing).
- **Light from many small sources:** only point lights (a 224 cap, and cost by light volume in the lit pass and the
  medium).
- **No propagation medium with memory or refractoriness.**
- **A generator layer cannot stand on sculpted terrain:** its ground is its own value noise. The Rift places CPU
  `points` (a 65,536 cap per node, about 14 MB of scene JSON).
- **Offline renders lift distance culls by policy.** Lean bodies matter more than LOD ladders.

**What needed specialized GPU treatment, and got it.**
- **ADR-1200 (the ecosystem).**
  - Emitters are points attached to host instances and accumulated in compute.
  - It covers photophores, polyps, chain beads, crust, plankton, comb plates and embers.
  - About 1 ms for about 19 M candidate points.
  - The light model: rest, response, wake, travel, iridescence, bob, near fade.
- **ADR-1201 (the medium).** An excitable grid: calibrated wave speed, refractory, energy, wake and conductivity,
  with exact seek.

**What could remain conventional.**
- the bodies, terrain, water, haze, particles, camera and post;
- the hero lights. The giants pool their light in the haze, which the owner liked in CP1. The micro scale never
  does: real small emitters do not light their surroundings.

**What visual compromises specialization avoided.**
- **Micro-scale density:** 19 M candidate emitters a frame, where CP1 had a few hundred thousand beads.
  - A plankton carpet on the river.
  - Walls alive with two crust populations (blue and violet, 600 points per 3 m patch).
  - Sea pens with polyp leaves.
  - Comb jellies as pure light (a mesh read as a beach ball).
- **Music that travels.** Fronts at a speed, branching, refractory: "music traveling through a living ecosystem" in
  place of "bass → emission".
- **Per-species behaviour on the same medium:**
  - flash (u);
  - canopy lag (e);
  - an awakened reach (w);
  - fluorescence (`excitedColor`: fans and the violet crust turn magenta under a front).

**New reusable Environment infrastructure.**
- **The seam** (§2.1): `EnvironmentRenderer` and `EnvironmentFrame`. Production Astral Forge is its second user.
- **The excitable grid mode and grid channel selection.** These are general: any scene can propagate anything.
- **Emitter layers on host instances.** General enough for any instanced organism, city windows, stars on a
  structure...
- **Engine fixes found here:**
  - a second seek landing in the initial state (`StateMachine::reset`);
  - the headless benchmark's `--range` not seeking.

**What should remain specific to Bioluminescence (the Rift).**
- the species, their templates and palettes;
- the arc's stage table (camera behaviour, the medium's character, glow and flash per stage);
- the kick-front-from-the-camera ignition.

All of that is data in `examples/bioluminescent/build.py`, not engine code.

## 6. Performance (M2 Max, Dawn/Metal, under the GPU lock)

### 6.1 Where the time goes (1080p output, realtime tier, at the drop, before the live optimisations)

A/B attribution of the CP4 scene at 97 s (GPU p50, one change at a time; `build/biolum/ab3`):

| Variant | Frame | Lit pass | Volume march |
|---|---|---|---|
| base | 74.1 ms | 35.2 | 34.1 |
| volume steps 32 → 16 | 57.2 | 35.2 | 17.3 |
| volume steps 20 | 61.3 | 35.1 | 21.3 |
| volume noise off | 71.2 | 35.1 | 31.2 |
| volume max distance 400 | 74.0 | 35.1 | 34.1 |
| no volumetric local lights | 70.0 | 35.2 | 29.9 |
| no crown lights | 59.2 | 31.7 | 22.7 |
| no rock material program | 67.2 | 28.6 | 33.8 |
| no swarm / no particles | 73.7 / 73.9 | 35.3 | 33.7 |
| no bodies (emitters only) | 62.0 | 26.1 | 33.7 |

**Readings:**
- **The medium costs about 1 ms per march step.**
  - Noise is about 3 ms of it.
  - The crown lights' halos about 11 ms.
  - Particles are free.
- **The Environment's own parts are about 1 ms each:** the ecosystem (19 M candidate emitters) and the propagation
  medium (0.39 ms a step at 256×1024).
- **Everything else is the general renderer's lit pass:** bodies, terrain, water, 120 lights and the rock's program.
  - At the arc points (realtime tier, 1080p): Dark 59.5, Awake 63.2, Drop 76.6, Body 70.4, Aftermath 31.4 ms.

### 6.2 What was done for live, and what it bought

| Step | Change | Effect |
|---|---|---|
| 1 | live medium: 16 steps, no noise (offline keeps 32, with noise) | halves the march |
| 2 | terrain chunks 40 → 80 m (64 quads), view 1.6 → 1 km, LOD from 100 m | draws 2,224 → 599; CPU work 12.5 → 6.2 ms |
| 3 | crown lights 120 → 72 (the largest crinoids) | the CPU lights stage 5.5 → 3.7 ms |
| 4 | leaner crinoids (stalk 5.4k → 3.1k triangles, arms 13.5k → 8.7k: pinnules on every other segment); fans drawn to 140 m, sea pens to 80 m | 7.7M → 6.0M triangles; GPU at Emergency 23.2 → 18.7 ms |

### 6.3 Live, measured with the engine's own profiler (`--live-profile`)

All at 1920×1080 output, LIVE AUTO (`effects_first`), on the Trench file.

| Target | Mode | Where | Level / scale | Frame median | GPU median | Status |
|---|---|---|---|---|---|---|
| 30 fps | **live** (real loop, Fifo) | the drop (96 s) | Medium / 0.85 | 33.3 | 26.7 | **achieved, 0 deadline misses** |
| 30 fps | headless | the drop | High / 1.00 | 36.9 | 29.3 | achieved |
| 30 fps | headless | 40 s | High / 1.00 | 37.5 | 29.7 | at risk |
| 30 fps | headless | 150 s | High / 1.00 | 37.5 | 30.0 | achieved |
| 60 fps | headless | 40 s, the drop | Emergency / 0.50 | 27 | 18.6-18.7 | over budget |

**60 fps is not reached on an M2 Max.** What remains is the general renderer's lit pass (bodies, terrain, lights),
about 15 ms of the 18.7 at half resolution.

The Environment-specific way past it (§5's "what needs specialized treatment") would be a cheap shading model for
the bodies: they are dark silhouettes lit by the glow, and do not need full clustered PBR with 72 lights. Shading
them like the emitters, from the medium's own light, is the next step if 60 fps is required.

Offline renders the same world with more steps and noise in the medium, the full render scale, and live render
limits. 1080p30 renders at 7.1 fps.
