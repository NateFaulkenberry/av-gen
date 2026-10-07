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

**Naming note.** `"environment"` cannot be the JSON key (it is sky/fog). Candidates: `"habitat"`, `"biome"` (taken
by the world system), `"ecosystem"`, `"envRenderer"`. Decided when the block is built (ADR-1200).

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

## 5. The running assessment (brief §18)

Updated at every checkpoint.

**What the generalized renderer does well.**
- Instanced, culled, LOD'd generated meshes, at any density the triangle budget allows.
- Terrain with authored features (the canyon is 3 features).
- Water.
- Bloom with chroma retention.
- Audio fields (travelling onset fronts, per-element bands) on emission.
- Clustered lights.
- Every output path (stills, video, EXR) for free.

**What it does poorly for this Environment** (CP1, to be confirmed by section 4's experiments).
- Micro-scale emitters cost full lit shading.
- Emitters can light their surroundings only through point lights, and the medium's cost grows with them.
- No propagation medium with refractory state (a scalar grid has 1 channel; Gray-Scott is not excitable).
- The generator distribution's ground is its own value noise, not the terrain, so GPU-generated layers cannot stand
  on sculpted land. CP1 used CPU-placed `points` (65,536 cap per object).

**What needs specialized GPU treatment.** Hypotheses until measured:
- micro emitters as points;
- a propagation field;
- a glow field instead of light pools.

**What can remain conventional.** Terrain, water, post, camera, output, controls (expected).

**Visual compromises avoided by specialization.** (Pending.)

**New reusable Environment infrastructure.** (Pending.)

**What should remain specific to Bioluminescence.** (Pending.)
