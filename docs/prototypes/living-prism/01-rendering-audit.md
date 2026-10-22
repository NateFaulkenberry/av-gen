# The Living Prism 01: rendering architecture and GPU performance audit (Sub-Agent A)

- **Author:** Sub-Agent A (rendering and GPU performance), for the lead agent's integrated report.
- **Brief:** `00-brief.md`. This file covers §3 "Sub-Agent A", the rendering half of §4, §5, §6, the rendering half of §8,
  §9 A/B/C and the rendering half of §10.
- **Worktree:** `av-gen-prism`, branch `audit/living-prism`, from main `5e0f5b02`. Release build in `build/render`.
- **What this is not:** no scene was built and no production behaviour was changed. The probes and temporary artifacts
  are listed in §11.
- **Status labels** are exactly the brief's §2.2: Verified existing, Existing but limited, Experimental, Duplicated or
  fragmented, Missing, Unknown.

## 0. Summary for the lead

**Facts (verified in code or measured):**
1. **The live renderer is mature and well instrumented.** `SceneRenderer::render` is a single-encoder forward renderer
   with about 24 labelled stages:
   - a five-target MRT lit pass;
   - clustered lighting, CSM/PCSS, GTAO, a volumetric medium with four media slots, and an Environment seam;
   - a long post chain and FIR-only temporal effects.
   There is no CPU wait in the live path. The live profiler, per-frame traces, removal arms and A/B verification
   exist and work (§1, §2.18).
2. **Three of four representative scenes hold 1080p30 live with at most 6 misses per 900 frames** (Rainbow World,
   THE RIFT, Event Horizon). DIGITAL MOSH misses 6-13 per 900 even at Emergency (§4.3).
3. **The lit scene pass is 55-75% of every frame; post is 0.3-1.7 ms; volumes cost about 7 ms at Ultra.** Material
   programs cost up to 11 ms (THE RIFT, measured A/B) and have no ladder lever (§4.2, §4.4).
4. **A geometry-heavy world has a large fixed cost the ladder cannot remove.** Rainbow World goes only from 23.9 to
   15.5 ms GPU across the whole ladder; THE RIFT goes from 38.0 to 15.7 (§4.2). A dense crystal landscape will sit on
   the Rainbow World side.
5. **The quality controller works:** no sustained oscillation, bounded probation reverts, no measurable stall from a
   level change, graceful degradation in THE RIFT. It does not record *why* it dropped (§5).
6. **Genuine rendering gaps for this scene:**
   - procedural surfaces cannot be translucent or refractive (Blend is drawn opaque);
   - no SSR outside water, and no probes;
   - no TAA or temporal upscaler (bilinear + FXAA);
   - no occlusion culling;
   - no propagation along a structure (all fronts are Euclidean or on a 2D grid) (§2).
7. **Most "VFX vocabulary" already exists and is shared:** `Onset`/`Wave`/`Spectrum` fields, seven grid modes
   including an excitable propagation grid, GPU particles with trails, effectors, compiled material programs and
   48 Effect Library kinds (§3).
8. **Two measurement defects invalidate parts of existing tooling output:**
   - timestamps are quantised to 65.5 µs (432/432 labels);
   - the headless profile pays a 6.2 ms per-frame sky rebuild that live does not (0.03 ms) (§7 C1-C2).

**Recommendations (mine, for reconciliation):**
- **P0, before any Living Prism work (all S):** C1, C2, C3, C4: make the instruments honest.
- **P1:**
  - screen-space refraction on procedural materials (reusing ADR-1286's copy);
  - material-gated half-resolution SSR;
  - a spatial upscaler instead of bilinear;
  - a ladder field per new effect, and an effects-first priority for the scene;
  - a committed perf baseline with the §9 thresholds.
- **P2:** sorted blended procedural objects; a TAAU spike decided on blockout evidence.
- **Defer or reject:** probes, MetalFX, FSR 2 port, OIT, Hi-Z, pass reorganisation for bandwidth.
- **Readiness (rendering side):** ready for the scene specification *after* the four P0 instrument fixes. The
  refraction/SSR work can proceed in parallel with the art specification because it is gated per material. The
  specification should state how much of the frame is translucent, reflective and particulate, because those three
  numbers decide R1-R3.


## 1. The verified render pipeline (feeds final report §2)

### 1.1 Frame lifecycle, live

Traced in code, not taken from `docs/rendering.md`. Where that doc disagrees, the code is authoritative; §1.4 lists the
disagreements.

| Step | Where | What happens |
|---|---|---|
| Loop | `src/app/application.cpp` `Application::runLive` (l. 4745) | One iteration per presented frame. The loop is paced to the target by `liveFrameCapPeriodMs`/`livePaceStep` (`src/app/interactive_resolution.hpp`, ADR-1107): a whole number of vsyncs, a fixed deadline grid. |
| Clock | `Engine::tick` (`src/app/engine.cpp` l. 5955) | Transport time, `FrameTime{renderTime, deltaTime, frameIndex}`. |
| Control | `Engine::update` (l. 7100) | Timeline automation, then `modulator_.applyRoutes(bus_, params_, dt)` (l. 7247), palette, behaviours, sequence events, then the composition is flattened into a `scene::Scene` and the Effect Library runs (`updateEffects`, l. 6894). Sub-Agent B owns everything up to "a parameter has a value". |
| Quality | `Application` l. 7488, `applyLiveRung` | The live controller (`app::InteractiveResolution`) chooses a level; the rung is applied as ceilings on `rendering::QualitySettings`. A scale change resizes the HDR targets (`SceneRenderer::resize`, l. 5126 of application.cpp). |
| Record | `rendering::SceneRenderer::render` (`src/rendering/scene_renderer.cpp` l. 2794-5167) | One command encoder for the whole frame; passes listed in §1.2. Each pass writes one end-of-pass timestamp (`gpu::FrameTimeline::mark`). |
| Submit | application.cpp l. 5700 | `queue.Submit`, one command buffer. No CPU wait on the GPU anywhere in the live path: timestamps and stats come back through non-blocking mapped rings (`gpu::FrameTimeline`, `gpu::ReadbackRing`). |
| Present | `OutputManager::presentAll` (`src/app/output_manager_gpu.cpp` l. 115), editor swapchain | The tonemapped `finalTexture_` is copied/mapped into each output (projection window, Syphon/IOSurface share, NDI). A slow output is paced by `PresentPacer` (ADR-1089). Swapchain: BGRA8Unorm, Fifo (`docs/rendering.md`, `gpu::Surface`). |

### 1.2 The pass list inside `SceneRenderer::render` (submission order)

Labels are the `FrameTimeline` labels that the profiler reports. Each line names the owning class and file.

| # | Timeline label | Pass(es) | Owner | Runs when |
|---|---|---|---|---|
| 1 | `shaderlayer` | background user shader layers' own passes | `ShaderLayer` (`shader_layer.cpp`) | a scene has GPU shader layers |
| 2 | `sim` | grid-field sub-steps (scalar, vector, reaction-diffusion, agents, excitable, bake, oscillator) | `Simulation` (`simulation.cpp`, `shaders/simulate.wgsl`) | `scene.grids` non-empty |
| 3 | `particles`, `particle-density` | emit, simulate, stable compaction, indirect args | `ParticleRenderer` (`particle_renderer.cpp`) | particle systems |
| 4 | `effectors`, `cull`, `generators` | per-instance effector compute, GPU frustum/distance/screen-size cull + LOD compaction, camera-window generators | `ProceduralRenderer` (`procedural_renderer.cpp`, `shaders/points.wgsl`, `cull.wgsl`, `generator.wgsl`) | objects with effectors / `lod` / generators |
| 5 | `sdf` | SDF compute (density volumes, occupancy) | `SdfRenderer` | SDF objects |
| 6 | `clusters` | froxel light lists, 16x8x24 | `SceneRenderer::updateLights` | clustered lighting on |
| 7 | `shadow` (one per view) | CSM cascades, spot and point maps into one atlas; entities, procedural instances, meshed and raymarched SDFs | `SceneRenderer` l. 4120-4215 | a visible lit surface (ADR-1096) |
| 8 | `background` | HDR clear + background shader layers | `SceneRenderer` | always |
| 9 | `depth` | depth prepass, then linear depth (R32Float) | `SceneRenderer` l. 4255-4330 | AO, contact shadows, shadow mask, water, an Environment, or SDF shards need depth |
| 10 | `ao` | GTAO half resolution + temporal | `AoRenderer` | `ambientOcclusion` |
| 11 | `shadowmask` | half-resolution directional shadow term | `ShadowMaskRenderer` | shadow mask on |
| 12 | `fogsky` | fog-coloured sky / atmosphere background | `SceneRenderer` l. 4360 | a fog sky |
| 13 | `scene` (split around SDF raymarch and the cinematic-water copy) | lit forward pass into 5 MRT: HDR RGBA16F, normal+roughness RGBA16F, velocity RG16F, emission RGBA16F, ids R32Uint; entities, procedural, meshed SDFs, raymarched SDFs, skybox, grid, wire, particles, ribbons, shells, water, blended entities | `SceneRenderer` l. 4400-4615 | always |
| 14 | `<env>.*` (e.g. `astral.march`, `ecosystem.*`) | Environment seam | `EnvironmentRenderer` subclasses (`ecosystem_renderer.cpp`, `astral_renderer.cpp`), ADR-1200 | the scene holds the Environment block |
| 15 | `clouds` | cloud tiles | `CloudRenderer` | clouds |
| 16 | `volume.march`, `volume.composite` | volumetric medium: fog, height fog, up to `world::kMaxMedia` = 4 placed media | `VolumeRenderer` | fog/medium on |
| 17 | `debug` | debug geometry | `DebugDraw` | debug |
| 18 | distortion offset/resolve (own label) | screen-space distortion proxies (offset + resolve) | `DistortionRenderer` | distortion effects |
| 19 | `shaderlayer` | post user shader layers | `ShaderLayer` | post layers |
| 20 | `temporal/*` | FrameEcho, Mosh, Feedback, Slit over a ring of clean radiance (FIR only, ADR-410) | `TemporalEffects`, `TemporalHistory` | any temporal effect on |
| 21 | `post/*` | exposure, outline, look-atmos, DoF, motion blur, lens (distortion, CA, fisheye), glitch (ADR-1065), bloom + halation + anamorphic pyramids, wide, composite, look (grade), display, FXAA, sharpen | `PostProcessor` (`post_processor.cpp`, `shaders/post.wgsl`) over a transient pool | per-stage gates |
| 22 | `tonemap` | AgX/ACES, sRGB encode, **bilinear upscale** from the render-scaled HDR target to the output (ADR-137) | `SceneRenderer` | always |
| 23 | `overdraw`, `auxdebug` | diagnostic views | `SceneRenderer` | when selected |
| 24 | `composition` | 2D text/shape layers (ADR-083) | `CompositionRenderer` | layers |

**Render targets and formats:** HDR RGBA16Float at the render scale, Depth24Plus, the four auxiliary MRT targets above,
R32Float linear depth, half-resolution AO (`aoResolutionScale` 0.5) and shadow mask (0.5), volume march at
`volumeResolutionScale` (0.5 at the live tiers), post at the render-scaled size, tonemap writes the output size. The
transient pool (`gpu::TransientPool`) recycles post intermediates.

**Resolution and presentation:** `QualitySettings::renderScale` sets the internal size; the tonemap filters it up
bilinearly (`shaders/tonemap.wgsl` l. 15, `scene_renderer.cpp` l. 481). There is **no temporal upsampler and no TAA**
(`src/scene/post_settings.hpp` l. 215, `shaders/post.wgsl` l. 787); antialiasing is FXAA with a live floor
(`QualitySettings::antialiasFloor`, ADR-1024).

### 1.3 Where data meets the renderer (the boundary with Sub-Agent B)

Everything below is the rendering side. How the values are produced is B's.

| Consumer | Reads | Update rate | Path |
|---|---|---|---|
| Material programs | per-op constants written by routes; `input` ops (time, position, instance lanes) | per frame, uniform/storage upload; structure is fixed per scene | `FieldUniforms`/`MaterialPrograms::update` (`scene_renderer.cpp` l. 3956-3959); compiled to WGSL live (ADR-1493) |
| Fields (16 slots) | packed every frame from `Scene::fields`, incl. `Wave`, `Onset` (8 newest onsets x 4 sources), `Spectrum` (delayed by distance), `Grid`, `Resonance` | per frame (grids per sub-step, ADR-1119) | `FieldUniforms` -> `FieldBlock` uniform (5136 B), bound in procedural vertex+fragment, effectors, particles, PBR `field` op, SDF raymarch, volume |
| Effectors | field samples, per instance | per frame, compute | `cs_effectors` (`shaders/points.wgsl`) -> live record buffer -> cull -> draw |
| Entity FX lanes (FXL) | Glow, Pulse, Bloom Source per entity | per frame | `world/effects/entity_fx.*`, `ensureEntityFxCapacity` |
| Particles | per-frame uniforms (spawn, burst, forces, field forces) | per frame | `ParticleRenderer::update` |
| Quality | `QualitySettings` from the ladder | per decision (>= 30 frames apart) | `applyLiveRung` |

### 1.4 Documentation that disagrees with the code

| Doc claim | Code | Authoritative |
|---|---|---|
| `docs/rendering.md` "Frame" still shows the 0.1 frame (one scene pass + tonemap) above the current list, and says ACES in one place and AgX in another | `SceneRenderer::render` has ~24 labelled stages; the tone operator is a post setting | code |
| `docs/rendering.md`: `ObjectUniforms` "up to 256 objects" | `ensureObjectCapacity` grows by doubling to a byte budget (`kObjectBufferByteBudget`) | code |
| The coordinator's memory note "one medium slot: fog and vortex share one slot, `dropped` is never read" | `world::kMaxMedia = 4` (`src/world/atmospherics.hpp` l. 629, ADR-562); `mediaDropped` is counted (`atmospherics.cpp` l. 861) and read by `Engine` (l. 7012) and the workload line (`application.cpp` l. 6977) | code: the note is stale |
| ProceduralRenderer header: "blend materials are drawn opaque" | true (`procedural_renderer.hpp` l. 349); only Mask is honoured (`procedural_renderer.cpp` l. 2701) | code; nothing user-facing says so |

## 2. Rendering capability matrix (feeds final report §3)

Each row gives the brief's ten fields. "Cost and risk" uses the effort scale S (under 2 days), M (up to a week), L (up to
three weeks), XL (more). Runtime costs marked *measured* come from §4; everything else is an estimate and says so.

### 2.1 Opaque and transparent geometry
- **Capability:** forward-lit opaque geometry (glTF entities, instanced procedural nodes, meshed and raymarched SDFs);
  alpha-masked cutouts; blended surfaces.
- **Status:** opaque: Verified existing. Transparency: **Existing but limited.**
- **Evidence:** `SceneRenderer::render` scene pass (l. 4400-4615); entities sorted back to front for blend
  (`docs/rendering.md`, scene pass); `ProceduralRenderer::draw` "Opaque objects only in this phase (blend materials are
  drawn opaque)" (`procedural_renderer.hpp` l. 349); only `AlphaMode::Mask` reaches the procedural shader
  (`procedural_renderer.cpp` l. 2701). Blended paths that do exist: particles, ribbons, shells (`shell_renderer.cpp`),
  wire lines, water (`water_renderer.cpp`), blended glTF entities.
- **Current use:** 40 of the 43 live projects draw through procedural nodes (scan of `examples/index.json`, §11 script);
  none of them can make a procedural surface translucent.
- **Limitation:** no blended or refractive *procedural* or *SDF* surface; no order-independent transparency; sorting is
  per entity, not per instance. Translucent crystal can today only be faked with emissive/fresnel/thin-film on an opaque
  surface, or built from shells.
- **Living Prism relevance:** High (the brief names "reflective or translucent surfaces").
- **Proposed action:** Research further, then improve (see §6 R1): a *refractive opaque* material path first (the
  water's frame copy generalised), real blending second.
- **Expected benefit:** crystals that bend what is behind them, the single largest look gap for this scene.
- **Cost and risk:** M for screen-space refraction on procedural materials reusing ADR-1286's copy (one extra
  full-frame copy, ~0.1-0.3 ms at 1080p *estimated*); L for sorted blended instances; regression risk low if gated per
  material.
- **Confidence:** High on the gap (read in code); medium on the cost.

### 2.2 Emissive materials and bloom/glare
- **Capability:** emission target (MRT 3) with a bloom weight; selective bloom weighted by emitted radiance (ADR-039);
  bloom, halation and anamorphic pyramids sized from one reference height (ADR-917); Effect Library glow/halo/glare
  shells; FXL Glow/Pulse/Bloom Source lanes (ADR-703); SDF rim emission (ADR-1052).
- **Status:** Verified existing.
- **Evidence:** `post_processor.cpp` l. 780-1000; `SceneRenderer` hands `emission_` to post (l. 4780); live scenes
  with emissive material programs (Rainbow World, the Resonator scenes, THE RIFT).
- **Current use:** effectively every live scene.
- **Limitation:** the bloom is screen-space, so an emitter just off screen contributes nothing; the reach is a pixel
  count (ADR-279). No lens-flare ghosts beyond the glare shell.
- **Relevance:** High. **Action:** Reuse. **Benefit:** none needed. **Cost/risk:** none.
  **Confidence:** High.

### 2.3 Lighting and shadowing
- **Capability:** clustered forward (16x8x24 froxels, 32 lights per cluster, `light_data.hpp` l. 69), LTC area lights,
  colour temperature, CSM (up to 4 cascades), spot and point maps in one atlas, PCSS, contact shadows, a half-resolution
  shadow mask (ADR-087), SDF shadows from the light's view at a fraction of the map (ADR-1160/1165), caster floor
  (ADR-1095), no shadow pass when nothing visible is lit (ADR-1096).
- **Status:** Verified existing.
- **Evidence:** `docs/lighting.md`; `SceneRenderer::updateLights`; the shadow loop l. 4120-4215; `QualitySettings`
  shadow fields; ladder levers `cascadeCount`, `shadowResolution`, `reducedShadowFiltering`.
- **Current use:** every lit scene; Rainbow World's shadow pass measured in §4.
- **Limitation:** shadow maps re-render every frame (no cached/static cascades, no update-rate reduction); light count
  per cluster is capped at 32 and the overflow is counted (`ClusterOccupancy::dropped`) but not shown in the live HUD.
- **Relevance:** High. **Action:** Reuse; consider "static caster cache" only if the scene's measured shadow cost
  warrants it (§6). **Benefit:** see §4 for what shadows cost. **Cost/risk:** cached cascades M, moderate risk
  (invalidation with moving geometry, wind). **Confidence:** High on capability.

### 2.4 Reflections and indirect-lighting approximations
- **Capability:** image-based lighting always present (HDR map or the analytic procedural sky, ADR-036), split-sum
  specular with a bent-normal lookup, GTAO; reflection-only light bands (ADR-1151); thin film and anisotropy
  (ADR-1143); grating/dispersion detail (`pbr_shade.wgsl` `SurfaceDetail`); screen-space reflection **only in cinematic
  water** (ADR-1286).
- **Status:** **Existing but limited.**
- **Evidence:** `SceneRenderer::updateEnvironment`, `processSky`; `reflection_bands.hpp`; `material_optics.hpp`;
  `water.wgsl` l. 77, 744-760, 1038. Searched for reflection probes, local/parallax cubemaps, SSR outside water,
  planar reflections: none (grep over `src/` and `shaders/`).
- **Current use:** IBL everywhere; bands in the Astral scenes; SSR in the water scenes only.
- **Limitation:** every non-water surface reflects only the global environment cube: a crystal does not reflect the
  world next to it, and there is no diffuse GI or emissive light transfer except through placed lights.
- **Relevance:** High (reflective crystal). **Action:** Research further; prefer reusing the water's SSR against the
  existing linear depth for a material flag (M) over probes (L) — §6 R2.
- **Benefit:** neighbouring emitters visible in crystal faces, which is most of what "a living prism" reads as.
- **Cost/risk:** SSR at half resolution typically 0.5-1.5 ms at 1080p on this class of GPU (*estimate*, to be measured
  in a probe before committing). **Confidence:** High on the gap, low on the cost.

### 2.5 Fog and atmospheric depth
- **Capability:** distance fog = the medium's own air under Beer-Lambert (ADR-705); height fog with ground follow
  (ADR-568/715); a ray-marched volume at `volumeResolutionScale` with self-shadow (ADR-570), particle glow (ADR-040) and
  up to four placed media (ADR-562); fog sky; clouds (`CloudRenderer`, cloud tiles); god rays in the
  Astral Environment.
- **Status:** Verified existing.
- **Evidence:** `VolumeRenderer`, `VolumeStats` (march and composite timed separately, ADR-140), `world/atmospherics.*`.
- **Current use:** many live scenes; ladder levers `volumeResolutionScale`, `volumeStepScale`.
- **Limitation:** no froxel volume (fog is lit by the march's own light sampling per pixel, not cached in a 3D grid);
  the coordinator's "one medium slot" note is stale (four slots now, §1.4).
- **Relevance:** High. **Action:** Reuse. **Benefit/cost:** — . **Confidence:** High.

### 2.6 Colour grading and palette control
- **Capability:** exposure (physical camera ADR-037), AgX/ACES tone map, "look" grade, the project-level `palette`
  block with a selector (ADR-1420, `Engine::update` l. 7250), material-program colour ops, field colour kinds.
- **Status:** Verified existing.
- **Evidence:** `post_processor.cpp` stages `exposure`, `look`, `display`; Rainbow World's palette transitions (they
  rebuild the procedural sky 261 times in 300 s: `17-slow-frames.md` §1).
- **Limitation:** a palette change that touches the sky rebuilds the IBL chain (hashed, built on change); in Rainbow
  World it did not measurably cause spikes (9% vs 5% chance coincidence, 17-slow-frames §1), but it is a per-change CPU
  and GPU cost no profile names.
- **Relevance:** High. **Action:** Reuse; add the sky rebuild to the per-frame trace (S). **Confidence:** High.

### 2.7 Distortion, chromatic aberration, scan effects, image-space treatments
- **Capability:** lens stage (distortion, CA, fisheye ADR-1540); glitch stage (shock, tears, blocks, split, radial
  blur, sort: ADR-1065); screen-space distortion proxies (`DistortionRenderer`: heat shimmer, gravitational lens, space
  warp); temporal effects (FrameEcho, Mosh, Feedback, Slit: ADR-410/1049/1066); user WGSL shader layers before and after
  the scene; outline (ADR-1072).
- **Status:** Verified existing.
- **Evidence:** `post_processor.cpp`; `distortion_renderer.cpp`; `temporal_effects.cpp`; Digital Mosh and the Sonic VFX
  scenes.
- **Limitation:** "scan effects" in world space are not an image-space effect here: Rainbow World's scan is authored as
  route-driven constants on each material program's `remap` ops (`examples/rainbow-world/blender/music.py`
  `front_targets`), i.e. per program, by the scene's build script.
- **Relevance:** Medium (the brief rules out "a static environment with an audio-reactive post filter").
  **Action:** Reuse. **Confidence:** High.

### 2.8 Particles and GPU/CPU simulation
- **Capability:** GPU particles: deterministic emit/simulate/compaction without atomics, indirect draw, velocity
  stretch, trails (ring history, 64 MiB/system budget), lifetime curves, field forces, curl noise, atmosphere coupling
  (ADR-015/040), latent SDF binding (ADR-1140/1145), spawn and distance levers (ADR-1098). GPU grid simulation in seven
  modes (`spatial::GridMode`: Scalar, Vector, ReactionDiffusion, Agents, Excitable, Bake, Oscillator), deterministic and
  seek-exact with GPU checkpoints (ADR-1119/1120). The Astral and ecosystem Environments run their own particle sims.
- **Status:** Verified existing. **Duplicated or fragmented** in one respect: the Astral Forge (`shaders/astral/sim.wgsl`)
  and the ecosystem (`ecosystem.wgsl`) each carry their own particle system beside `ParticleRenderer`, with their own
  quality knobs (`astralTier`).
- **Evidence:** `docs/rendering.md` "Particles"; `particle_renderer.cpp`; `simulation.cpp`; `grid_field.hpp` l. 79.
- **Current use:** 25 live scenes have particle nodes; 14 have grids.
- **Limitation:** the particle system's only live quality levers are spawn scale and cull distance; there is no
  per-system cost in the profile beyond the `particles` label total.
- **Relevance:** High. **Action:** Reuse; do not add a particle framework. Note the Environment duplication (§7 C8).
- **Confidence:** High.

### 2.9 Instancing, culling, LOD and geometry batching
- **Capability:** procedural instancing (one `DrawIndexed`/indirect per object per LOD level), GPU frustum + distance +
  screen-size cull with per-level compaction (ADR-029), LOD ladders with meshoptimizer simplification, lockBorder
  (ADR-1546), shell thinning (ADR-1548), flat far rungs (ADR-1576), the material tier following the rung (ADR-155),
  LOD bias and draw distance as live levers (ADR-1094), entity LOD, camera-window generators (ADR-1117).
- **Status:** Verified existing. **Occlusion culling: Missing** (`rendering/visibility.hpp` l. 187: "This engine has no
  occlusion culling. No Hi-Z, no depth pyramid").
- **Evidence:** `docs/gpu-culling-lod.md`; `cull.wgsl`; Rainbow World's leaf-card finding (`17-slow-frames.md` §2: far
  alpha-tested cards were 12 ms of a 32 ms hold, 9 ms of it in the depth prepass).
- **Limitation:** no occlusion culling, so a dense, enclosed crystal landscape pays for everything inside the frustum;
  glTF entities draw one call each.
- **Relevance:** High. **Action:** Reuse; research Hi-Z occlusion only if a Living Prism layout measures as
  geometry-bound after LOD (§8, rank 13). **Confidence:** High.

### 2.10 Procedural geometry and surface deformation
- **Capability:** generators, sources, a deformer stack in the vertex stage (incl. Field deformer, wind, Path), the
  analytic normal for rigid draws (ADR-1591), finite-difference normals otherwise (ADR-1561), SDF trees compiled to WGSL
  (ADR-1003), fibers/strands (ADR-1180/1181).
- **Status:** Verified existing.
- **Evidence:** `procedural.wgsl`; `procedural_renderer.cpp`; `docs/procedural-geometry.md`.
- **Limitation:** a deformed draw pays the finite-difference normal (the whole chain several times per vertex); this is
  why ADR-1591 exists.
- **Relevance:** High. **Action:** Reuse. **Confidence:** High.

### 2.11 Animation and skinned geometry
- **Capability:** GPU skinning (ADR-086), rigs, motion matching; procedural animation through deformers and effectors.
- **Status:** Verified existing.
- **Relevance:** Low (no characters in the concept). **Action:** Leave alone. **Confidence:** High.

### 2.12 World-level and entity-level visual effects
- **Capability:** the Effect Library (`src/world/effects/kinds/`, 48 kinds: beams, halos, arcs, lightning, ripple,
  shockwave, ground pulse, portal, aurora, ...) with triggers, timing, owners and FXL lanes; world macros (B's side).
- **Status:** **Existing but limited** (implemented and reachable; limited in reach): 12 of 43 live projects use it, and only 14
  kinds appear in live projects (arc 12, pulse 11, lightBeam 6, stars 6, ...) (§11 script).
- **Limitation:** effects are owner-anchored singletons rather than "N impacts at N places"; a percussive impact
  *somewhere in the world* is easier through an `Onset` field than through the library.
- **Relevance:** Medium. **Action:** Reuse selectively (beams, glare, ripple). **Confidence:** Medium (usage counted;
  per-kind cost not measured).

### 2.13 Reusable event-driven VFX
- **Capability:** `Onset` fields (an expanding, decaying ring per onset, 8 newest x 4 sources: `field.hpp` l. 85,
  `audio_history.hpp` l. 46/58), field triggers with their own clocks (ADR-906), effect triggers, particle bursts,
  effectors with enables, latched response envelopes (ADR-1574, B's side).
- **Status:** Verified existing.
- **Limitation:** 16 field slots per frame (`FieldBlock`), shared by every use; 8 onsets per source on the GPU.
- **Relevance:** High. **Action:** Reuse. **Confidence:** High.

### 2.14 Traveling waves, ripples, impact effects and spatial propagation
- **Capability:** `Wave` fields (planar, radial, spherical, cylindrical; sine, pulse, triangle), `Onset` and
  `Spectrum` fields delayed by distance (`audioSpeed`), the excitable propagation grid (ADR-1201: refractory fronts at
  m/s on an XZ plane), resonance fields (ADR-1400), the ripple/shockwave/ground-pulse effects, water ripples.
- **Status:** Verified existing for Euclidean propagation. **Missing** for propagation along a *structure* (a graph
  of connected crystals, a branching vein): every distance above is Euclidean from a point/axis or on a 2D grid.
- **Evidence:** `spatial/field.hpp` l. 61, 84-90, 127; `simulate.wgsl` l. 482; THE RIFT's design (kick -> excitable
  grid -> mats, fans, canopy) in `examples/bioluminescent/build.py`.
- **Limitation:** a front that follows connections needs a per-instance "distance along the structure"; the
  `InstanceRecord` has exactly one spare lane (`emissive.a`, `spatial/point_cloud.hpp` l. 48) that a build script could
  fill, and a material program or effector can compare it to a route-driven front. No shared primitive does this.
- **Relevance:** High. **Action:** Integrate (scene-level first: baked graph distance in the spare lane; promote to a
  shared primitive only if a second scene needs it). **Cost/risk:** S-M, low risk. **Confidence:** High.

### 2.15 Persistent trails and accumulated effects
- **Capability:** particle trails; ribbons (`RibbonRenderer`, Effect Library trail); grid fields as decaying memory
  (Scalar mode: inject, advect, diffuse, decay; reaction-diffusion; agents); the excitable grid's wake channel;
  temporal effects (echo, feedback) on the image.
- **Status:** Verified existing.
- **Limitation:** grids are 2D or small 3D, sampled at a world position; world-space "paint" on arbitrary surfaces is
  only as fine as the grid. Image-space feedback is screen-locked (it smears when the camera moves).
- **Relevance:** High. **Action:** Reuse. **Confidence:** High.

### 2.16 Scene transitions and layered compositing
- **Capability:** scene states with timed transitions (B's side), background/post shader layers, 2D composition layers
  (ADR-083), temporal effects. Switching *projects* is a load (`project_switch.hpp`, ADR-1390).
- **Status:** **Existing but limited:** there is no visual crossfade between two projects, and no render-to-texture of
  a second scene.
- **Relevance:** Low for one scene. **Action:** Leave alone. **Confidence:** High.

### 2.17 Adaptive quality and dynamic resolution
- **Capability:** the five-level live ladder (Ultra..Emergency), three strategies and data-defined priorities
  (ADR-1083/1084/1105), profiles as ceilings (ADR-1099), project overrides (ADR-1101), a minimum level and
  "UNSUSTAINABLE" (ADR-1103), paced decisions on missed deadlines (ADR-1362/1363), the frame cap (ADR-1107), probation
  and backoff (ADR-1104), render scale down to 0.38 with bilinear upscale.
- **Status:** Verified existing (tested in §5).
- **Limitation:** see §5; the upscale is bilinear with no temporal reconstruction, so a low rung looks soft and
  shimmers on thin geometry.
- **Relevance:** High. **Action:** Reuse; see §6 R3 (temporal upscaling) for the main improvement.
- **Confidence:** High.

### 2.18 Profiling, diagnostics and automated performance validation
- **Capability:** `gpu::FrameTimeline` (one end-of-pass timestamp per pass, intervals partition the frame, ADR-077),
  `RenderStats` and `CpuFrameBreakdown`, `avgen --live-profile` (headless and live, JSON `avgen.liveprofile/1`, critical
  path verdict, GPU categories, Dawn memory accounting, candidate levers with `--verify-candidates` A/B, `--optimize`),
  per-frame traces (ADR-1571, ADR-1362), pass and quality arms (`--disable`, `--quality-arm`), `tools/gpu-lock.sh`,
  `AVGEN_PIPELINE_TIMING`, `[performance]` tests.
- **Status:** Verified existing; **Existing but limited** for bandwidth (no counter source: WebGPU exposes no GPU
  performance counters, so bandwidth is only inferable from removal arms such as `auxstore`, ADR-119) and for CI (no
  automated perf regression gate on a live scene).
- **Relevance:** High. **Action:** Improve (S-M): a committed baseline-and-threshold check over the profile JSON
  (§8 rank 4, §9). **Confidence:** High.

## 3. Reusable VFX: rendering side (feeds final report §6)

The brief's five grades: (1) reusable and production-ready; (2) reusable but missing useful controls; (3)
scene-specific only; (4) experimental; (5) absent. "Receives its signal" names the boundary with Sub-Agent B: in every
case below, the effect reads a *parameter or field*, and routes, macros and envelopes are B's to describe.

| Category | Existing implementation(s) | Grade | Signal enters through | Quality control | Notes for the Living Prism |
|---|---|---|---|---|---|
| Localized impacts and bursts | `Onset` field (a ring per onset, decaying), particle `burst`, Effect Library `shockwave`/`groundPulse`/`ripple`/`discharge`, FXL `Pulse` | 1 | fields read the audio history directly (ADR-1116); bursts and effects via route-driven parameters | particles: spawn scale, cull distance; fields: none (cost is per-pixel in whatever samples them) | The `Onset` field is the right primitive: one GPU-side definition, sampled by materials, effectors, particles and volumes alike |
| Traveling fronts and spatial waves | `Wave` field (4 geometries, 3 shapes), `Spectrum` field delayed by distance, excitable grid (ADR-1201), Rainbow World's scan (material-program constants moved by routes) | 1 for Euclidean fronts; **3** for "a front along a structure" (Rainbow World's scan is per-program authoring; no graph distance anywhere) | fields; the scan through `remap` op constants | grid: sim rate and size are authored, not on the ladder | A graph-propagation front is the one missing *architectural* capability (§2.14). Smallest form: a per-instance "path distance" baked into the spare `InstanceRecord` lane and a front compared against it |
| Ripples on surfaces or through a world | Field deformer (vertex), water ripples, `Wave`/`Onset` fields, the RD and oscillator grids (Phase Wall, RD Pins) | 1 | fields / grids | none on the ladder | The deformer pays the finite-difference normal (ADR-1561/1591): budget it |
| Glints, sparkles, emissive accents | material-program `threshold`/`noise` ops with emission (Rainbow World's hat glints, `music.py` l. ~120), the bloom weight, glare shells, `stars` effect | 2 (every scene re-authors its glint program) | route -> op constant | none | A shared "glint" material snippet would save re-authoring; it is a material-program *library* question, not an engine one |
| Beams and light trails | `lightBeam`/`travelBeam` shells, ribbons, particle trails (ADR-040), `arc`/`lightning` | 1 | effect parameters | trails: a 64 MiB/system memory budget; no ladder lever | Beams are additive shells: no shadow, no volume interaction except through the medium's own glow lane |
| Particles and persistent trails | `ParticleRenderer` (§2.8) | 1 | per-frame uniforms, field forces | spawn scale, cull distance | Two Environments carry their own particle systems (§7 C6) |
| Surface/material modulation | material programs (34 op kinds, compiled live: ADR-1493), FXL lanes, thin film, anisotropy, bands | 1 | route -> op constants and FXL | `materialTiers`, flat far rungs (ADR-1576); `noprograms` exists only as a diagnostic arm | The largest single GPU cost in the Resonator scenes (ADR-1493's table) and 6.4 ms in Rainbow World (17-slow-frames §2). The ladder has no "cheaper program" lever except via LOD rungs |
| Color and palette transitions | `palette` block + selector (ADR-1420), grade, field colour kinds | 1 | palette selector (B), parameters | none needed, but a sky-moving palette rebuilds IBL (§2.6, §7 C1) | |
| World-scale environmental movement | wind, Path/Field deformers, effectors (incl. per-point axes ADR-1500), camera-window generators, clouds | 1 | fields / parameters | LOD bias, draw distance | |
| Temporary accumulated effects that decay | Scalar grids (inject/advect/diffuse/decay), the excitable grid's wake, RD, agents; `Onset` decay; temporal echo/feedback (image space) | 1 | fields / grids | grids: none on the ladder; temporal: reset by a scale change (§7 C5) | World-space memory exists. Image-space memory is screen-locked and resets on a ladder scale change |

### 3.1 Abstraction boundaries (rendering side)

- **Low-level effect (engine):** fields (incl. `Onset`, `Wave`, `Spectrum`, `Grid`), grids, particles, effectors,
  material ops, shells, post stages. They are already the vocabulary, and they share one GPU representation
  (`FieldBlock`, `shaders/fields.wgsl`).
- **Response/event primitive (B's side):** latching, envelopes, onsets, macros.
- **Scene-authored:** which field drives which material op on which node, the palette, the composition.
- **The one candidate to promote to engine level** is propagation along a structure (§2.14). Promote it only after a
  scene-level prototype proves it, and only if a second scene would use it.
- **Resource use and the ladder:** the effects with no ladder lever are material programs, grids, `Onset`/`Wave`
  sampling, temporal effects, shells and ribbons. A Living Prism that leans on them can only shed cost through
  resolution. §5 shows what that does to the picture.
- **Independent testing:** the parity tests (`test_fields_gpu.cpp`, `test_points_gpu.cpp`, ADR-1121) hold the GPU to the
  CPU reference. There is no per-effect *cost* test; C2 (timestamp quantisation) is the prerequisite for one.

## 4. Performance baseline (feeds final report §5)

### 4.1 Conditions

- **Machine:** Apple M2 Max (Metal via Dawn `v20260907.201642`), macOS 26.7.1, Release build of `5e0f5b02` + nothing
  (`build/render`), **AC power** before and after every run (logged in `conditions.log`, all 48 + 20 + 8 entries "AC
  Power"), every run under `tools/gpu-lock.sh`.
- **Contention:** Sub-Agent B was compiling during the first runs (load average 108 at 16:02, falling to about 20 by
  16:05). The GPU lock excludes GPU work, not CPU load, so the **CPU** columns of rep 1 are the least trustworthy. The
  reps agree closely (ranges below), so no window was discarded.
- **Target:** the project's live target is **1920x1080 at 30 fps** (`kLiveTargetChoices`, ADR-1341; owner's standing
  rule). Frame period 33.33 ms. The live controller's GPU budget is 29.33 ms (`liveBudget`, 12% headroom,
  ADR-1080). The display was 60 Hz, so a missed deadline costs one 16.7 ms vsync.
- **Audio:** every run used `--audio assets/audio/feline-footwear.wav --no-audio`: the same track drives analysis in
  every scene (ADR-1408), muted at the output. This keeps the audio input identical across scenes.
- **Windows:** `--start 60 --warmup 5 --measure 30`: 900 measured frames per run, **3 interleaved repeats**
  (rep 1 of every scene and mode, then rep 2, then rep 3).
- **Two modes, because they measure different things.** *Headless Ultra* is a fixed 30 fps step with no present and the
  GPU never idle, which gives clean per-pass costs at full quality. *Live auto* is the real editor loop: a projection
  window at 1920x1080, Fifo, the frame cap, and the ladder deciding. Live GPU spans are paced, and Apple's clock
  governor stretches them (ADR-1362), so **live GPU numbers are not comparable to headless ones**. Use live for misses,
  pacing and the ladder; use headless for where the time goes.
- **Scenes** were chosen for different workloads: **Rainbow World** (procedural world: 44M logical triangles, LOD, leaf
  cards, one shadowed sun, material programs); **THE RIFT LIVE** (the ecosystem Environment, the excitable grid,
  72 clustered lights, 1,021 entities, a volume); **DIGITAL MOSH LIVE** (seven raymarched SDFs, a volume, temporal echo,
  three cascades); **Sonic VFX - Event Horizon** (particle- and post-heavy: 10 systems, 95,300 capacity, distortion,
  33 post passes).
- **Commands:** `docs/prototypes/living-prism/probes/A1-rendering-profiles/baseline.sh` (and `ladder.sh`, `verify.sh`, summarised by `summarize.py` and `transitions.py`); raw JSON, traces
  and logs in `~/Desktop/av-gen-review/80-living-prism-audit/A-data/`.

### 4.2 Headless Ultra, 1080p: where the time goes (medians of 3 reps, [min-max of the reps])

| | Rainbow World | THE RIFT LIVE | DIGITAL MOSH LIVE | Event Horizon |
|---|---|---|---|---|
| **GPU p50** | **19.86** [19.86-19.92] | **38.47** [37.88-38.54] | **40.57** [40.50-40.83] | **10.62** [10.35-10.88] |
| GPU p95 / p99 / max | 29.75 / 49.55 / 59.11 | 45.61 / 51.90 / 59.18 | 54.85 / 57.28 / 60.29 | 16.58 / 17.96 / 19.20 |
| Fits the 29.33 ms GPU budget at Ultra? | p50 yes, p95 marginal | no (needs the ladder) | no (needs the ladder) | yes, with 2.8x headroom |
| CPU work p50 / p95 | 10.57 / 13.62 (6.19 of it is a headless-only sky rebuild, §7 C1) | 9.40 / 12.80 | 4.65 / 8.04 | 3.13 / 4.02 |
| scene pass | 12.85 | 25.95 | 22.74 | 7.14 |
| depth prepass + linear | 2.88 | 2.62 | 1.05 | 0.85 |
| shadow (+mask) | 1.70 (+0.20) | — (no lit caster) | 0.26 | — |
| GPU cull / effectors | 1.70 / 0.07 | 0.00 | 0.13 / 0.39 | — |
| volume march / composite | — | 7.21 / 0.07 | 6.62 / 0.07 | — |
| SDF compute | — | — | 3.34 | — |
| particles / grids | — | 0.20 / 0.46 | 3.01 / 0.07 | 0.66 / — |
| AO | — (off) | 0.66 | 0.46 | 0.33 |
| distortion | — | — | — | 0.72 |
| post total (all `post/*`) + tonemap | ~0.5 | ~0.3 | ~0.3 | ~1.7 |
| draws / triangles drawn (logical) | 122 / 0.98 M (44 M) | 1,042 / 4.0 M (103 M) | 394 / 0.27 M | 30 / 0.09 M |
| instances visible / GPU-culled | 2,297 / 163,856 | 693 / 100,417 | — / 2,753 | — |
| lights (clustered) / shadow views | 2 (0) / 1 cascade | 73 (72) / 0 | 2 (1) / 3 cascades, 121 casters | 3 (2) / 0 |
| particles capacity / systems | 0 | 280,000 (Environment) | 32,000 / 2 | 95,300 / 10 |
| render passes / compute dispatches | 28 / 3 | 33 / 5 | 42 / 12 | 53 / 11 |
| GPU memory (Dawn's estimate, total / render targets) | 588 MB / 302 MB | 589 / 266 | 498 / 330 | 504 / 365 |

Readings that matter for the plan:
- **The lit scene pass is 55-75% of every frame.** It contains the geometry, the material programs and the lighting,
  and the timeline cannot split them. Splitting needs the removal arms (`noprograms`, culling arms): see §4.4.
- **Post is cheap everywhere (0.3-1.7 ms).** An image-space treatment is not where a live budget is lost.
- **Volumes cost 7 ms at Ultra** in the two scenes that use them (half-resolution march, 16-24 steps). The ladder's
  first lever, a quarter-resolution volume, brings that to about 1.1 ms live (see the live label medians).
- **The frame's fixed (pixel-independent) cost is large in a geometry-heavy world.** Rainbow World pinned at each level
  (`A-data/ladder/rw-pin-*`), GPU p50: Ultra 23.9, Medium (0.71) 18.6, Low (0.5) 16.1, Emergency (0.5) 15.5 ms. A
  quarter of the pixels removes only about 35% of the cost. THE RIFT scales better: 38.0 / 25.3 / 20.3 / 15.7 ms.
  **A dense crystalline landscape will behave like Rainbow World, so its geometry budget has to be met with LOD,
  culling and instance counts, not with the ladder.**
- **Timestamps are quantised.** All 432 label medians in the baseline are exact multiples of 65.536 µs
  (`timestamp_quantization`, §6 R5). Labels shown as 0.07 ms mean "one quantum or less".
- **Cold costs.** First-ever launch of DIGITAL MOSH: first frame after **113 s** (SDF variants compiling into a cold
  Metal cache; 0.8 s on later launches, ADR-1560's cache). THE RIFT loads in 24-26 s. Its compiled material variant
  arrived **13.5 s** after load, and until then the interpreter draws (ADR-1493). No run compiled anything during its
  measured window (`compilesDuringMeasure` 0 everywhere).

### 4.3 Live, 1080p30, auto quality: what a performer gets (3 reps x 900 frames)

| | Rainbow World | THE RIFT LIVE | DIGITAL MOSH LIVE | Event Horizon |
|---|---|---|---|---|
| Frame interval p50 / p95 / p99 / max (ms) | 33.34 / 35.78 / 39.59 / 42.63 | 33.32 / 34.60 / 37.13 / 51.21 | 33.32 / 35.28 / **48.41** / 53.85 | 33.35 / 35.24 / 39.02 / 58.65 |
| **Deadline misses per run** (of ~900) | 1, 5, 1 | 1, 2, 6 | **6, 10, 13** | 2, 2, 1 |
| Levels held (share of measured frames) | Ultra 90%, High 10% | High 23%, Medium 29%, Low 48% | Ultra 3, High 11, Medium 43, Low 24, Emergency 20% | Ultra 100% |
| Level changes in measured windows | 2 | 12 | 19 | 0 |
| Critical path (profiler's verdict) | sync/present | GPU | GPU | sync/present |
| CPU work p50 | 13.26, of which **UI 7.79** | 11.85, of which "lights" 5.39 | 7.89 | 5.91, of which UI 2.44 |

- **Three of four scenes deliver 30 fps with at most 6 misses per 900 frames** (≤0.7%). DIGITAL MOSH does not: 0.7-1.4%
  misses and a p99 of 48 ms, even though it descended to Emergency in every rep. The descent tracks the song: the
  scene gets heavier from about 75 s on.
- **The profile's "over budget" count is misleading live.** About half of all frames are "over" because a paced frame
  lands at 33.34-33.36 ms against a 33.33 ms budget (`budgetStats`, vsync jitter). The deadline-miss count is the
  meaningful number. *Observability fix (S):* compare live frames against the period plus a tolerance, or report
  only misses in live mode.
- **The editor UI costs 7.8 ms of main-thread CPU in Rainbow World** while projecting (ImGui build and record; 1-2.5 ms
  in the other scenes). It does not cost frames today, because the critical path is the GPU or present. But it is the
  largest CPU item in a live frame and is invisible to the GPU ladder (`gpuShareToAct`). Unknown: which panel costs
  it (no per-panel timing).
- **THE RIFT's 5.4 ms "render submission: lights"** is a coarse bucket. It also contains the per-entity pass over
  1,021 entities (`scene_renderer.cpp` l. 2966-3060) before the light packing of 73 lights. The split is not
  measurable without a probe (*missing observability*).

### 4.4 Measured lever savings (the profiler's own interleaved A/B, headless Ultra)

Short interleaved A/B (2 counterbalanced pairs of 120 frames per lever, `--verify-candidates 5`, from 60 s;
`A-data/verify/*.json`). Only "a saving, outside the noise" verdicts are results. A scene whose cost moves with the
song drifts during the A/B: every DIGITAL MOSH lever came back VOID or implausible (`castercull` "saved" 9.8 ms from a
2.2 ms shadow category), so it is excluded.

| Scene (GPU p50 in this run) | Lever | Measured saving | Noise floor | Verdict |
|---|---|---|---|---|
| Rainbow World (17.5 ms) | `noprograms` (all 14 material programs off) | **2.56 ms (14.6%)** | 2.0% | result |
| Rainbow World | `scale85` (render scale 0.85) | **2.23 ms (12.8%)** | 4.1% | result |
| Rainbow World | `drawdist75` | 1.11 ms | 7.0% | inside the noise |
| THE RIFT (37.8 ms) | `noprograms` (1 program, 12 ops) | **11.08 ms (24.6%)** | 11.5% | result |
| THE RIFT | `scale85` | **9.18 ms (20.5%)** | 2.0% | result |
| THE RIFT | `volumequarter` | **8.19 ms (18.4%)** | 15.2% | result |
| Event Horizon (10.9 ms) | `lodbias2` | **3.21 ms (25.1%)** | 19.0% | result |
| Event Horizon | `noprograms` | 0.52 ms (4.9%) | 2.5% | result |

What it says for the plan:
- **Material programs are a first-order GPU cost wherever they cover the frame.** They cost 11 ms in THE RIFT (one
  12-op program, presumably on the ground that fills the lower frame; screen coverage is not measured) and 2.6 ms in
  Rainbow World. ADR-1493 measured 14.5-33.9 ms in the Resonator scenes before compilation. The ladder has no lever for
  them. A crystal look driven through programs must budget ops against screen coverage.
- **Resolution buys 13-21% for 28% fewer pixels**, consistent with the affine pixel law in
  `interactive_resolution.hpp` (l. 35).
- **The volume's quarter-resolution lever is worth about 8 ms** where a volume exists, which is why it is the ladder's
  first step.

### 4.5 What the tooling cannot measure (missing observability)

| Metric the brief asks for | Status | What would provide it |
|---|---|---|
| GPU memory **bandwidth** | Not measurable: WebGPU exposes no performance counters. Only removal arms (`auxstore`, ADR-119) can infer it | Instruments' Metal System Trace (manual), or a Metal-counter side channel (out of scope) |
| Per-pass cost below 65.5 µs | Quantised | §7 C2: disable `timestamp_quantization` |
| Split of the lit pass into geometry / material / lighting | Only by removal arms (`noprograms`, culling arms), one A/B at a time | Acceptable; no new tool needed |
| End-to-end audio-to-photon latency | Not measured by the renderer. Rainbow World's documents 29-30 measured onset-to-peak on rendered video (a steady one-frame lag) | B's domain plus a capture rig; the renderer contributes one frame of pipelining |
| Procedural shadow casters and draws | Reported as 0 (§7 C3) | Count procedural `drawShadow` |
| CPU split of the "lights" bucket; per-panel UI cost | Coarse | Two more `CpuFrameBreakdown` stages |
| Sky/IBL rebuilds per second | Only in the log | A trace column |
| Particle alive counts | "alive counts live on the GPU and are not read back" | Read the counters back through the existing ring (non-blocking) |

## 5. Adaptive quality: assessment and tests (feeds final report §5)

### 5.1 What it is (code)

- **Pure decision logic:** `app::InteractiveResolution` (`src/app/interactive_resolution.{hpp,cpp}`), fed one sample
  a frame through `note(gpuMs, wallMs)`, and unit-tested (`tests/unit/test_interactive_resolution.cpp`).
- **What drives it:**
  - *Unpaced* (cap off): the median GPU span of the last 20 frames against the budget. It drops up to two rungs at
    once and climbs one rung only when the higher rung is predicted to fit within 90% (learned step ratios, ADR-1085)
    for 120 frames (`raiseHoldFrames`).
  - *Paced* (the default live case, ADR-1362/1363): **deadline misses**, at least 3 of the last 300 frames, each with
    a GPU span near the period (so a CPU-late frame or a lone stall does not count). A raise is on probation; a raise
    that misses is reverted, and its hold doubles up to 16x (ADR-1104).
  - In both modes it holds when the GPU is not the binder (`gpuShareToAct` 0.7).
- **What it moves:** one `LiveQualityRung` of ceilings, applied by `applyLiveRung` (application.cpp l. 7488). The
  fields are render scale (to 0.38-0.5), volume resolution and steps, motion blur, DoF, cascades, shadow resolution and
  filtering, caster floor, particle cull distance, LOD bias, SDF shadow scale and Astral tier. Three tables
  (`balanced`, `resolution_first`, `effects_first`) or a data-defined priority (ADR-1105); profiles and project
  overrides as ceilings (ADR-1099/1101); a project minimum level and "LIVE TARGET UNSUSTAINABLE" (ADR-1103).
- **What it does not move** (no lever): material programs, AO, grids, `Onset`/`Wave` field sampling, temporal effects,
  shells and ribbons, the Environment renderers (except Astral), and post beyond motion blur and DoF.
  `postEffectQuality` and `particleSpawnScale` exist but are set only by profiles and priority ladders, not by the
  three tables.

### 5.2 Tests run

**T1. Natural behaviour at the real target** (the §4.3 runs, 12 live runs x 30 s): see §4.3 for levels and changes.

**T2. A target the scene cannot reach** (Rainbow World at 60 fps on a 60 Hz display, 2 x 90 s,
`A-data/ladder/rw-live-60-*`): it reached Emergency 6.8 s into the measured window in both runs (about 12 s after the start). It then held Emergency (0.5 scale) for 95% of the measured frames, still missing **11% of deadlines** (519 and 535 misses in about 4,870 frames). Run 1 probed upward once (at +22.5 s: Emergency -> Low, reverted 0.8 s later). Run 2 probed twice (+22.4 s and +74.3 s, each reverted within 0.9 s). The second probe waited about 52 s, longer than the first: the growing backoff of ADR-1104/1362. It never oscillated. Whether the "UNSUSTAINABLE" flag showed is not in the record (the profile JSON does not carry it: *unknown*).

**T3. Oscillation.** Across the 12 runs at 30 fps:
- Rainbow World made 2 changes (one drop and a return to Ultra 9 s later).
- THE RIFT made 5-6 changes per run (warm-up included).
- DIGITAL MOSH made 6-8 per run, including **a raise and its revert within 0.4-1 s in all three reps, at the same
  piece time** (about 14.5 s into the window: Medium -> High -> Medium). In rep 1 it also did Low -> Medium -> Low
  within 0.2 s.

These are probation reverts working as designed: each one doubles that level's hold. No run showed sustained flapping.
On a scene whose cost tracks the music (DIGITAL MOSH), the controller follows the song's load *after* misses happen.
It cannot anticipate a heavy section.

**T4. Stalls on a level change.** A drop is caused by slow frames, so only *raises* can show a change's own cost:
- Across all 10 raises, 1 frame over 40 ms occurred in the 10 frames after the change, against a base rate of 0.75
  per 10 frames over all measured live frames. That is consistent with ADR-1086 (a scale change costs at most one
  vsync).
- After drops, the frames over 40 ms (21 in the 10 after, 24 in the 10 before) are the content that caused the drop.
- **Verdict: no measurable stall from the change itself.**

**T5. What the levels do to the picture.** THE RIFT, headless, pinned, at the same piece time (60 warm-up frames each),
`A-ladder-rift-levels.jpg` and `A-ladder-rift-crops.jpg` (centre crops 1:1):
- GPU 38.0 / 25.3 / 20.3 / 15.7 ms at Ultra / Medium (0.85) / Low (0.71) / Emergency (0.5).
- The scene keeps its identity at every level. What goes is the crispness of the fine particulate floor and the
  glints, which turn to soft blobs at 0.5.
- The Rainbow World pins are not time-matched: the warm-up ended at different frames, because the profile's capture is
  "the last measured frame after an adaptive warm-up". They were discarded. *Observability fix (S): a fixed warm-up
  frame count for captures.*

**T6. A visible cost the ADRs do not cover:** a level that changes the render scale resets every screen-space history,
including the temporal echo, mosh, feedback and slit rings (§7 C5). This is inferred from the code path, not filmed.

### 5.3 Assessment against the brief's questions (§6.3)

| Question | Answer | Evidence |
|---|---|---|
| What drives changes? | Paced: deadline misses attributed to the GPU. Unpaced: median GPU span vs budget | §5.1; ADR-1362/1363 |
| Stable measurements or spikes? | Stable: 3 misses in 300 frames, each confirmed by a GPU span; a lone stall is ignored | `missesToDrop`, `missWindowFrames`; T2-T3 |
| Can it oscillate? | Bounded: a failed raise is reverted and its retry delayed up to 16x. Seen in T3, never sustained | T2, T3 |
| Can it lower resolution or individual effect costs? | Both: scale plus 12 effect fields; the order is per project | §5.1 |
| Do scenes degrade gracefully? | THE RIFT yes (T5). Pixel-heavy scenes gain a lot per level; geometry-heavy Rainbow World gains little below Medium (23.9 -> 15.5 ms over the whole ladder) | §4.2 |
| Does it explain *why*? | Partly: the trace has level, scale, cost and pacing per frame, and the Live panel shows level and UNSUSTAINABLE (ADR-1087/1103). It does **not** record *which misses* caused a drop, or which pass grew | trace columns (`live_profile_run.cpp` l. 715) |
| Can expensive systems offer their own trade-offs? | Only by adding a field to `LiveQualityRung` and a table entry (the Astral tier is the precedent). There is no registration interface | `interactive_resolution.hpp` l. 252-273 |
| Do changes stall? | No measurable stall (T4); but scale changes reset temporal history (C5) | T4, T6 |

**Conclusion.** Do not replace or redesign the controller. For the Living Prism:
1. Give each new expensive shared effect (SSR, refraction, any temporal reconstruction) a field on `LiveQualityRung`,
   following the Astral precedent.
2. Choose the scene's priority so effects go before resolution.
3. Add a "drop reason" (the pass whose median grew most, and the miss count) to the trace and the Live panel. This is
   the one observability gap that would let a performer understand a drop.

## 6. Research findings and options with cost (feeds final report §7, §9 A/B/C)

The research covers only topics where the code audit found a real decision. Each block gives the problem, the
options, the sources (with dates), the fit with AV Gen and a recommendation. Unless marked *measured*, costs are
estimates from the sources, from the closest existing AV Gen measurement, or both, and each estimate names its basis.
Published technique is cited; everything about fit, effort and cost is my inference from this codebase.

### R1. Translucent and refractive crystal on procedural nodes (the transparency gap, §2.1)

**Problem.** `ProceduralRenderer` draws `AlphaMode::Blend` as opaque (§2.1). Procedural nodes are the path nearly every
live scene uses, so the engine cannot draw a translucent crystal there today. Water is the only material that
refracts: ADR-1286 copies the frame mid-pass (`scene_renderer.cpp` l. 4540-4570) and samples the copy.

| Option | What it is | Look | GPU cost at 1080p (estimate, basis) | Engineering | Risk |
|---|---|---|---|---|---|
| **A. Refraction from a scene-colour copy** (ADR-1286's mechanism, generalised) | A material flag puts the node in a phase after one `CopyTextureToTexture` of the HDR target. The shader samples the copy at a normal-offset UV, with absorption over the thickness the linear depth gives. This is the "colour pyramid / opaque texture" refraction many engines ship | Reads as glass or crystal over whatever is on screen. One layer only; it cannot show what is behind *and* off screen | One RGBA16F copy, 16.6 MB at 1080p, about 0.1-0.3 ms (from the copy size against the M2 Max's published 400 GB/s; not measured here). The refractive pixels then shade as normal | **M**: share the water's split as "one refraction phase per frame" for water and procedural; add a procedural pipeline variant | Low: gated per material, so a scene with no refractive material never pays for the copy |
| **B. Sorted blending per object** | Blended procedural objects draw after the opaque phase, back to front by object centre, without depth write, the way blended glTF entities already draw | Correct between objects. Instances inside one object stay unsorted, so a crystal cluster can composite in the wrong order | Overdraw-bound: small for sparse crystals | **M** per object; **L** with a per-instance GPU sort (a sort key in the cull pass's compaction, `cull.wgsl`) | Medium |
| **C. Weighted blended OIT** (McGuire and Bavoil, "Weighted Blended Order-Independent Transparency", JCGT 2(2), 2013) | Accumulation (RGBA16F) and revealage targets, plus a full-screen resolve. No sorting | Good for glow veils, haze and particles. A dense crystal stack goes muddy, because blending follows a depth weight and not order. No refraction | Two more MRT targets (about 12 MB/frame more bandwidth) and one resolve, about 0.2-0.4 ms (estimate). Fits WebGPU's 8 colour attachments (`maxColorAttachments=8`, startup log); ADR-119 measured tile use at 32 of 128 B/px | **M** | Low-medium |
| D. Depth peeling or per-pixel linked lists | Exact order | Exact | Several geometry passes, or unbounded memory with atomics | L | High: **rejected** |

**Recommendation.** **A first.** A living prism needs refraction and reflection on mostly solid crystal far more than
stacked transparency, and A reuses a shipped, tested path. Add **B** only if the scene specification asks for crystals
seen through crystals. Use **C** only for soft layers, which shells and particles already provide. Do not build D.

### R2. Reflections: screen-space reflection vs probes (§2.4)

**Problem.** Every non-water surface reflects only the global environment cube, so a crystal next to an emissive vein
does not show the vein.

| Option | Sources | Covers | GPU cost (estimate) | Engineering | Fit |
|---|---|---|---|---|---|
| **SSR from the existing linear depth and HDR** | McGuire and Mara, "Efficient GPU Screen-Space Ray Tracing", JCGT 3(4), 2014; Stachowiak, "Stochastic Screen-Space Reflections", SIGGRAPH Advances in Real-Time Rendering, 2015 | Whatever is on screen, *including moving, music-driven emitters*. Falls back to IBL and bands off screen | Half resolution, 16-32 steps: about 0.5-1.5 ms at 1080p (published figures are for other GPUs; AV Gen has measured SSR only inside the water's total) | **M**: the water already marches view-streaked SSR rays against `aux-linear-depth`; generalise it into a material-gated pass. Velocity (MRT 2) allows reprojection of last frame's HDR | Good: every input exists; it scales with the ladder (resolution, steps, off) |
| **Local cubemap probes**, parallax-corrected | Lagarde and Zanuttini, "Local Image-Based Lighting with Parallax-Corrected Cubemap", SIGGRAPH 2012 talk | Off-screen surroundings, stable | A static probe is cheap to sample. A *dynamic* one (the light changes with the music) re-renders up to six faces of the scene per update | **L**: placement, blending, scheduling | Poor for a world whose light is the music |
| Reflection bands (exists, ADR-1151) | in repo | Stylised strips | ~0 | — | Already available |

**Recommendation.** Material-gated, half-resolution SSR with a ladder field (fewer steps at Medium, off at Low with an
IBL-plus-bands fallback). **No probes.** Measure the cost on the Living Prism blockout before committing, because SSR's
cost is proportional to the reflective share of the frame.

### R3. Temporal antialiasing and upscaling at low ladder rungs (§2.17)

**Problem.** The ladder goes down to render scale 0.42-0.5. The tonemap upsamples bilinearly and the only
antialiasing is FXAA. THE RIFT's pinned levels (`A-ladder-rift-crops.jpg`) show the cost: the fine particulate floor
and the glints turn to soft blobs at 0.5. Thin bright crystal facets will crawl and shimmer at the same rungs.

| Option | Sources | Quality | Cost at 1080p output (estimate) | Engineering | Fit and risk |
|---|---|---|---|---|---|
| **A. In-engine TAA with upsampling (TAAU):** jittered projection, velocity reprojection, neighbourhood clamp, history at output size | Karis, "High Quality Temporal Supersampling", SIGGRAPH Advances 2014; Yang, Liu and Salvi, "A Survey of Temporally Antialiasing Techniques", Computer Graphics Forum 39(2), 2020 | Large gain on edges and specular shimmer, and recovers much of native sharpness at 0.6-0.7. **Ghosts on fast, unpredictable content** (particles, feedback, mosh, flashes), which audio-reactive scenes produce on every beat | One full-screen pass, about 0.3-0.6 ms, plus a 16.6 MB history | **L**: the velocity target exists, and particles write velocity. Needed: jitter in every `viewProj` consumer (GTAO's and the shadow mask's temporal paths already reproject); correct motion for deformers and effectors (ADR-422 says some deformers' velocity is not derivable); disocclusion and reactive masks; exemptions for emissive particles | Medium-high regression risk; live only (offline determinism, ADR-091/410) |
| **B. AMD FSR 2 ported to WGSL** | AMD GPUOpen, "FidelityFX Super Resolution 2" (MIT, 2022) | As A, with a tested reactive mask | Around 1 ms (AMD's published tables are for its GPUs) | **L-XL**: port and maintain a large HLSL/GLSL compute codebase | Portable, but the same input work as A |
| **C. MetalFX temporal scaler through Dawn interop** | Apple, "Boost performance with MetalFX Upscaling", WWDC22 | Best per ms on Apple silicon | Low (Apple gives no per-ms figure) | **L**: Dawn has no MetalFX; it needs IOSurface-shared textures (`SharedTextureMemoryIOSurface` is already negotiated, `gpu/context.cpp` l. 123) and a Metal command buffer synchronised by shared events, outside WebGPU | Platform-locked; same inputs as A |
| **D. Spatial upscale and sharpen (FSR 1-style EASU + RCAS) in place of bilinear** | AMD GPUOpen, "FidelityFX Super Resolution 1", 2021 | Crisper edges at 0.7-0.85; does nothing for temporal shimmer | 0.1-0.3 ms (one or two passes at output size) | **S-M**: it replaces the tonemap's bilinear fetch; a `sharpen` stage already exists | Low risk, deterministic, no history |
| **E. Spend effects before resolution** | — | No softness | — | **S**: project data (`live.priority`, ADR-1105) | None |

**Recommendation.** Do **E + D** first, which help every scene. Run **A** as a P2 spike only if the measured blockout
spends long stretches below scale 0.7, and judge it on reactive content, not on a still. Prefer A to B (control, no
port) and to C (portability).

### R4. Bandwidth on a tile-based GPU

**Sources.**
- Apple's TBDR guidance: load and store actions are the main consumers of system bandwidth, and attachments that are
  never read back should be memoryless ("Tailor your apps for Apple GPUs and tile-based deferred rendering", Apple
  Developer Documentation).
- AV Gen's measurement, ADR-119: storing none of the four auxiliary targets (24.6 MB a frame) changed the Glowmere
  frame by -0.99%, inside the timestamp quantum: "not this frame's constraint". Dawn's `TransientAttachments`
  (memoryless) is negotiated and logged (`gpu/context.cpp` l. 131-155).

**Decision.** Do not reorganise passes for bandwidth now. Re-test only if the blockout adds a refraction copy, SSR and a
TAA history at the same time.

### R5. Frame-time measurement and quality control (§9 C)

**Quantised timestamps (measured, §4.2).**
- All 432 pass medians in the baseline are exact multiples of 65.536 µs (2^16 ns).
- Dawn's `timestamp_quantization` toggle is described as "Enable timestamp queries quantization to reduce the
  precision of timers that can be created with timestamp queries" (Dawn `src/dawn/native/Toggles.cpp`, main, read
  2026-10-22).
- Chrome documents the browser-side equivalent (100 µs), which it disables only under its developer flag ("WebGPU
  developer features", Chrome for Developers).
- ADR-119 noticed "the quantum is 0.066 ms" and left it in place. A native application has no fingerprinting concern.

**Recommendation (S):** list the toggle in `disabledToggles` in `gpu::Context` (beside `AVGEN_DAWN_TOGGLES`,
l. 181-202).

**Decide on misses when paced.**
- The controller already does this (ADR-1362). Its premise, that a capped frame's GPU span reads the clock governor and
  not the work, is AV Gen's own measurement (`interactive_resolution.hpp` l. 104-112).
- My tests in §5 found no oscillation beyond designed probation reverts and no stall from a level change.
- Nothing in the research argues for replacing the controller. The gap is explanation: a drop reason.

## 7. Refactor and removal candidates, rendering side (feeds final report §8)

Each candidate gives evidence, consequence, the smallest fix, benefit, risk and timing. None is a rewrite.

| # | Candidate | Evidence | Consequence | Smallest fix | Benefit | Risk / migration | When |
|---|---|---|---|---|---|---|---|
| C1 | **The headless profile does not run the live sky schedule** | `setLiveSkyLighting` is called only in `Application::runLive` (application.cpp l. 4878), not in `runLiveProfileHeadless` (live_profile_run.cpp l. 394). *Measured:* Rainbow World headless rebuilt the procedural sky on **697 of about 1,060 frames** ("procedural sky built" lines, `A-data/baseline/rw-headless-ultra-r1.log`), and "resource uploads" read **6.19 ms CPU** (median of 3 reps). Live: **0.03 ms**, with ADR-1070's amortised, rate-capped schedule | The headless profile, which `--verify-candidates` and `--optimize` use, measures a different frame from the one performed. Its CPU numbers for any scene whose palette moves the sky are wrong | Set the same `LiveSkyLighting` in the headless profile, or record which sky path ran in the profile's conditions | Headless predicts live | Low; re-baseline the sky-moving scenes | **Before** |
| C2 | **Timestamps quantised to 65.536 µs** | 432/432 label medians on the grid (§4.2); ADR-119 named the quantum | Post stages, tonemap, clusters, shadow mask, small effects read 0 or 0.066 ms; the brief's "new shared effects can be profiled independently" is not achievable below 0.07 ms | `disabledToggles = {"timestamp_quantization"}` in `gpu::Context` | ns-resolution pass costs | None | **Before** |
| C3 | **The profile under-counts procedural shadow work** | `stats_.shadowDraws = stats_.shadows.entityDraws` (scene_renderer.cpp l. 5263); `shadowCasters` counts entities (l. 3899). Rainbow World prints "0 casters ... 0 draws" beside a 1.7 ms shadow pass | The resources section contradicts the GPU section | Count procedural `drawShadow` draws and casters into `RenderStats` | A trustworthy shadow line | None | Before |
| C4 | **Live "over budget" counts vsync jitter** | About 50% of every live run's frames are "over budget" at 33.34-33.36 ms vs 33.33 (`budgetStats`), against 1-13 real misses | A reader concludes the scene fails when it does not | Tolerance on the comparison, or report misses only in live mode | An honest headline | None | Before |
| C5 | **A render-scale change clears every temporal effect** | A rung that changes scale resizes the targets; `resize` calls `resetScreenHistory()`, which calls `temporal_->reset()` (scene_renderer.cpp l. 1740-1742, 2072-2086). ADR-1086 measured the *time* cost of a change, not its *image* cost | On scenes with echo, mosh, feedback or slit (DIGITAL MOSH changed level 6-8 times per 30 s, §5), each scale change empties the ring for up to 32 frames. *Inferred from code, not filmed* | Resample the ring into the new size; or give such scenes a ladder that keeps the scale until Low | Ladder moves that do not erase the look | Low | During, if the Living Prism uses temporal effects |
| C6 | **"TEMPORARY" instrumentation is load-bearing** | 34 `TEMPORARY` markers (`grep -rn TEMPORARY src`), mostly `probe2` (`core/phase2_probe.hpp`). The live profiler's CPU categories read them (`live_profile_run.cpp` l. 73-75; `application.cpp` l. 5977-5979) | Deleting "temporary" code would silently blind the profiler; the names describe a past investigation, not what is measured | Move the fields the profiler reads into `CpuFrameBreakdown` (or a named `FrameProbe`); delete the rest | One CPU breakdown, not two | Low, mechanical | During |
| C7 | **Coarse CPU buckets** | THE RIFT's "lights" bucket (5.4 ms) also holds the per-entity pass over 1,021 entities (scene_renderer.cpp l. 2966-3060); the editor UI (7.8 ms in Rainbow World) has no per-panel split | The largest CPU items in two live scenes cannot be attributed | Two more stages | Attribution | None | During |
| C8 | **Two more particle systems inside Environments** | `shaders/astral/sim.wgsl`, `flakes.wgsl` and `ecosystem.wgsl` simulate particles beside `ParticleRenderer`, with their own knob (`astralTier`, ADR-1222) | Duplicated maintenance; different levers | None now. Do not add a third for the Living Prism | Avoid more duplication | — | After, if ever |
| C9 | **Documentation drift** in `docs/rendering.md` | §1.4 | Readers trust a two-generation-old frame diagram | Replace the Frame block with §1.2's table | Onboarding | None | After |
| C10 | The Effect Library's breadth vs live use | 48 kinds; 12 of 43 live projects use any; 14 kinds appear (§2.12) | Maintenance of kinds no live scene uses | **Leave alone**: the offline and cinematic work uses them, and the brief says unused-in-live is no reason to remove | — | — | — |

## 8. Ranked cost/benefit list, rendering (feeds final report §10)

The ranking is visual or reliability benefit for the Living Prism per unit of engineering effort and runtime cost.
Effort: S under 2 days, M up to a week, L up to three weeks, XL more. The priority is my proposal for the lead to
reconcile.

| Rank | Change | Problem it solves (evidence) | Benefit | Runtime cost | Effort | Risk | Proposed priority |
|---|---|---|---|---|---|---|---|
| 1 | **Disable `timestamp_quantization`** (C2) | Every pass time is a multiple of 65.5 µs (§4.2, 432/432 labels) | Per-effect GPU cost becomes measurable, which the brief's §13 "new shared effects can be profiled independently" requires | none | S | none | **P0** |
| 2 | **Headless profile = live sky schedule** (C1) | Rainbow World headless pays 6.2 ms CPU of sky rebuilds that live does not (0.03 ms) | The A/B tool measures the frame a performer runs | none | S | low | **P0** |
| 3 | **Profile record fixes** (C3, live "over budget", capture warm-up, drop reason in trace) | §4.3, §4.5, §5.2 T5 | A developer can identify the cost when performance degrades (§13) | none | S-M | low | **P0** (observability) |
| 4 | **A committed perf baseline and a regression check** over `avgen.liveprofile/1` JSON for 3-4 live scenes | No live-scene perf gate exists (`tools/ci`: none; 13 `[performance]` unit tests only) | Regressions caught by misses and p99, not by mean fps | none | M | low (GPU CI runs are private: `docs/development/gpu-ci-private-assets.md`) | **P1** |
| 5 | **Screen-space refraction on procedural materials** (R1-A), with a ladder lever | No translucent or refractive procedural surface (§2.1) | The defining crystal look | one HDR copy (~0.1-0.3 ms, *estimate*) plus shading | M | low (gated per material) | **P1**, prerequisite for the art spec's glass |
| 6 | **SSR, material-gated, half resolution, on the ladder** (R2) | Crystal reflects only the sky cube (§2.4) | Neighbouring emitters and music-driven light in reflections | ~0.5-1.5 ms (*estimate*; probe first) | M | medium | **P1** (after a cost probe on the blockout) |
| 7 | **Spatial upscaler instead of bilinear** (R3-D) | Low rungs at 0.5-0.71 are soft (T5) | Crisper picture at every reduced level, in every scene | ~0.1-0.3 ms (*estimate*) | S-M | low | **P1** |
| 8 | **Structure-following propagation at scene level** (baked path distance in the spare instance lane) | Fronts are Euclidean only (§2.14) | "Events that propagate through connected structures" (requirement 8) | ~0 (one lane read) | S-M | low | **P1** if the art spec asks for it; promote to an engine primitive only on a second user |
| 9 | **Ladder fields for new effects + a project priority for the scene** | New effects have no lever (§5.1) | Graceful degradation that keeps identity | none | S per effect | low | with 5-7 |
| 10 | **Resample, not reset, temporal rings on a scale change** (C5) | A scale change empties echo/feedback | Ladder moves stop erasing temporal looks | none | M | low | P2 (only if the scene uses temporal effects) |
| 11 | **Sorted blended procedural objects** (R1-B) | Crystals seen through crystals | Layered translucency | overdraw | M-L | medium | P2 |
| 12 | **TAA/TAAU research spike** (R3-A) | Shimmer on thin facets at low rungs | Large, if the scene sits low on the ladder | ~0.3-0.6 ms + history | L | medium-high (ghosting on reactive content; determinism) | P2: decide on blockout evidence |
| 13 | Hi-Z occlusion culling | No occlusion culling (§2.9) | Only if an enclosed layout measures geometry-bound after LOD | compute pass | L | medium | Defer |
| 14 | Cached / lower-rate shadow cascades | Shadows re-render every frame | Rainbow World's shadow pass is 1.7-1.8 ms: small | — | M | medium | Defer |
| 15 | Weighted blended OIT (R1-C) | — | Soft layers, which shells and particles already cover | 2 targets + resolve | M | low-medium | Defer |
| 16 | Reflection probes, MetalFX, FSR 2 port, depth peeling, pass reorganisation for bandwidth (R4) | — | Small or platform-locked for this scene | — | L-XL | — | **Reject** for this milestone |

## 9. Proposed performance acceptance criteria (feeds final report §11; numbers for the owner's approval)

Derived from the project target (1080p30, Auto ladder, ADR-1341) and the baselines above. None of them is an
invented ideal: each threshold is one the existing scenes already meet, or say how far they miss.

**Measurement protocol (fixed):**
- `avgen --live-profile --mode live --quality auto --target-fps 30 --size 1920x1080 --start S --warmup 5 --measure 30`;
- 3 interleaved repeats, AC power, under `tools/gpu-lock.sh`;
- one fixed muted audio file (`--audio <file> --no-audio`);
- the scene's representative windows, which the scene specification must name: at least its heaviest and a typical one.

| # | Criterion | Proposed threshold | Basis (measured today) |
|---|---|---|---|
| P1 | Deadline misses, live auto, per 900-frame run (median of 3) | **≤ 5 (≤ 0.55%)** | RW 1, RIFT 2, EH 2 meet it; MOSH 10 does not (§4.3) |
| P2 | Frame interval p99, live auto | **≤ 40 ms** (one 60 Hz vsync over the period, plus jitter) | RW 39.6, RIFT 37.1, EH 39.0 meet it; MOSH 48.4 does not |
| P3 | Quality level held, live auto, at the representative windows | **High or better ≥ 90% of frames** (owner to choose: Medium would be the RIFT's reality) | RW Ultra 90%; RIFT High 23%; MOSH High+ 14% |
| P4 | Headless Ultra GPU p50 at the heaviest window | **≤ 29.3 ms** (the controller's budget, so Ultra is reachable) and **p95 ≤ 1.5x p50** | RW 19.9 / 29.8 (1.50x); RIFT 38.5 and MOSH 40.6 exceed it and rely on the ladder |
| P5 | Fixed cost: headless GPU p50 at Emergency | **≤ 50% of the budget (14.7 ms)**, so the ladder has room | RW 15.5 ms (fails narrowly: geometry-bound); RIFT 15.7 |
| P6 | A level change's own cost | No more frames over 40 ms in the 10 frames after a *raise* than the scene's base rate | 1 in 10 raises vs 0.75 base (§5.2 T4) |
| P7 | Compiles while performing | `compilesDuringMeasure` = 0; compiled-material variant ready ≤ 15 s after load (warm cache) | 0 everywhere; RIFT 13.5 s |
| P8 | Each new shared effect | Its own timeline label, a ladder field, and a measured cost at 1080p on the blockout, with unquantised timestamps (C2 first) | — |
| P9 | Regression | The P1/P2/P4 numbers of the 3-4 baseline scenes, kept in the repo; a change that worsens misses or p99 beyond the reps' spread fails review | §4 ranges |

## 10. Extension points for the Living Prism (rendering side, no implementation)

| Need | Extension point | Precedent |
|---|---|---|
| A crystal surface's look and its musical modulation | a procedural node's material program (34 op kinds, compiled live) + thin film / anisotropy / bands + FXL lanes | Rainbow World, the Resonator scenes |
| Refraction (if approved) | a material flag + the scene pass's split that the cinematic water already makes (`scene_renderer.cpp` l. 4540-4570) | ADR-1286 |
| Impacts, fronts, memory | `scene.fields` (`Onset`, `Wave`, `Spectrum`, `Grid`) and `scene.grids` (Scalar decay, Excitable) | THE RIFT, RD Pins, Phase Wall |
| Structure-following fronts | the spare `InstanceRecord` lane (`emissive.a`, `spatial/point_cloud.hpp` l. 48) filled by the build script; a front parameter compared in the program or an effector | none yet |
| A specialised renderer (only if the art spec demands one) | `rendering::EnvironmentRenderer` (ADR-1200): one class, one block, one constructor line | ecosystem, Astral |
| A new ladder lever | a field on `LiveQualityRung`, an `applyLiveRung` line, table entries | `astralTier` (ADR-1222) |
| Per-effect cost | a `FrameTimeline::mark` label per pass, and a GPU category rule in `gpuCategoryTable()` | every renderer |

**Readiness inputs for the lead's §11/§13 matrix (rendering rows):**
- **Requirement 1, a richly layered 3D environment:** procedural + LOD + GPU cull exist. Demonstrated in Rainbow
  World. Test: the blockout meets P4/P5.
- **Requirement 2, emissive and reflective response:** emissive yes. Reflective is limited (IBL + bands only). Needs
  R2. Test: SSR cost probe on the blockout.
- **Requirement 3, lighting and atmospheric depth:** yes (clustered, CSM, volume). Demonstrated in THE RIFT and
  DIGITAL MOSH.
- **Requirement 8, propagation through structures:** Euclidean yes; along structures no (scene-level work).
- **Requirement 9, temporary memory and decay:** yes (grids, `Onset` decay).
- **Requirement 10, particles and trails:** yes.
- **Requirement 11, a smooth camera:** the camera is B's/the lead's. On the rendering side, motion blur exists, and
  there is no TAA to smear.
- **Requirement 13, stable pacing:** yes for 3 of 4 scenes. Test: P1/P2.
- **Requirement 14, graceful reduction:** yes, but geometry-bound scenes gain little. Test: P5.
- **Requirement 15, diagnostics for rendering cost:** exist, but need C1-C4 first.

## 11. Probes and temporary artifacts created by Sub-Agent A

Nothing in `src/`, `shaders/` or `tests/` was changed. No production behaviour was touched.

| Artifact | Where | Purpose | Keep? |
|---|---|---|---|
| Release build | `build/render/` (gitignored) | my binaries | delete when done |
| Symlink `build/release -> render` | `build/release` (gitignored) | `examples/bioluminescent/land.py` hard-codes `build/release/tools/avgen_world_preview` | delete when done (the coordinator approved it) |
| Generated Rainbow World scene | `examples/rainbow-world/scene/`, `build/rainbow-world/` (gitignored) | `python3 examples/rainbow-world/build.py` (Blender 5.x); the committed project was rewritten identically (git status clean) | regenerable |
| Generated THE RIFT scene | `examples/bioluminescent/rift.scene.json` (+ stamp, `rift.json`; gitignored), `build/biolum/` | `python3 examples/bioluminescent/build.py`; `rift-live.json` rewritten identically | regenerable |
| Measurement scripts | `docs/prototypes/living-prism/probes/A1-rendering-profiles/`: `baseline.sh`, `ladder.sh`, `verify.sh`, `summarize.py`, `transitions.py`, `scan_live_scenes.py` | reproduce §4-§5; `scan_live_scenes.py` is the live-scene node census used in §2 | keep with the report (untracked until the lead commits) |
| Raw data | `~/Desktop/av-gen-review/80-living-prism-audit/A-data/` (`baseline/`, `ladder/`, `verify/`, `baseline-summary.txt`, `smoke*.json`) | JSON, traces, logs, `conditions.log` (power and load per run) | review folder |
| Review media | `~/Desktop/av-gen-review/80-living-prism-audit/A-ladder-rift-levels.jpg`, `A-ladder-rift-crops.jpg` | THE RIFT at Ultra/Medium/Low/Emergency, same piece time | review folder |

## Appendix A. Key files and symbols (rendering)

- `src/rendering/scene_renderer.{hpp,cpp}`: `SceneRenderer::render` (l. 2794), `resize` (l. 1700),
  `resetScreenHistory` (l. 2072), `updateLights` (l. 2569), `updateEnvironment` (l. 610), `serviceLiveSky` (l. 750),
  `PassToggles` (hpp l. 690-713), `setCompiledMaterials` (l. 1960).
- `src/rendering/procedural_renderer.{hpp,cpp}`: `draw` (opaque only, hpp l. 349), `drawShadow`, `drawDepthOnly`,
  alpha flags (cpp l. 2701).
- `src/rendering/post_processor.cpp` (stages l. 581-1163); `temporal_effects.cpp`; `volume_renderer.{hpp,cpp}`
  (`VolumeStats`, `volume.march`/`volume.composite`); `environment_renderer.hpp` (ADR-1200 seam);
  `field_uniforms.*`; `simulation.*`; `particle_renderer.*`; `water_renderer.*` + `shaders/water.wgsl` (SSR,
  refraction).
- `src/gpu/frame_timeline.{hpp,cpp}` (end-of-pass partition), `src/gpu/context.cpp` (features l. 120-160, toggles
  l. 181-202).
- `src/app/interactive_resolution.{hpp,cpp}` (ladder, controller, pacing); `application.cpp` `runLive` (l. 4745),
  `applyLiveRung` call (l. 7488), present (l. 5815); `live_profile*.{hpp,cpp}`.
- `src/spatial/field.hpp` (`FieldKind`, `WaveGeometry`), `audio_history.hpp` (`kOnsetHistory`, `kOnsetSources`),
  `grid_field.hpp` (`GridMode`), `point_cloud.hpp` (`InstanceRecord`).
- `src/rendering/visibility.hpp` l. 187 (no occlusion culling).
- ADRs: 029, 035, 036, 039, 040, 059, 077, 086, 087, 119, 137, 155, 410, 562, 705, 917, 1024, 1070, 1080-1089,
  1094-1105, 1107, 1143, 1151, 1200, 1201, 1221, 1222, 1286, 1362, 1363, 1493, 1546, 1548, 1560, 1571, 1576, 1590,
  1591.
- Tests: `tests/unit/test_interactive_resolution.cpp`, `test_live_profile.cpp`, `tests/rendering/test_fields_gpu.cpp`,
  `test_points_gpu.cpp`, `test_culling_gpu.cpp`, `test_material_compiler_gpu.cpp`.

## Appendix B. Benchmark commands

```sh
# headless, per-pass costs (Ultra pinned)
AVGEN_GPU_LOCK_TIMEOUT=14400 tools/gpu-lock.sh ./build/render/src/avgen --live-profile --mode headless \
  --project <p> --audio assets/audio/feline-footwear.wav --no-audio --target-fps 30 --size 1920x1080 \
  --start 60 --warmup 5 --measure 30 --quality ultra --json <out>.json --trace <out>.csv
# live, the ladder and misses
... --mode live ... --quality auto ...
# measured lever savings
... --mode headless ... --measure 10 --quality ultra --verify-candidates 5
```

## Appendix C. Research references (with dates)

- McGuire, M., Bavoil, L. "Weighted Blended Order-Independent Transparency." JCGT 2(2), 2013.
  https://jcgt.org/published/0002/02/09/
- McGuire, M., Mara, M. "Efficient GPU Screen-Space Ray Tracing." JCGT 3(4), 2014. https://jcgt.org/published/0003/04/04/
- Stachowiak, T. "Stochastic Screen-Space Reflections." SIGGRAPH 2015, Advances in Real-Time Rendering.
  https://advances.realtimerendering.com/s2015/
- Lagarde, S., Zanuttini, A. "Local Image-Based Lighting with Parallax-Corrected Cubemap." SIGGRAPH 2012 talk.
- Karis, B. "High Quality Temporal Supersampling." SIGGRAPH 2014, Advances in Real-Time Rendering.
  https://advances.realtimerendering.com/s2014/
- Yang, L., Liu, S., Salvi, M. "A Survey of Temporally Antialiasing Techniques." Computer Graphics Forum 39(2), 2020
  (Eurographics STAR).
- AMD GPUOpen. "FidelityFX Super Resolution 1" (2021) and "FidelityFX Super Resolution 2" (2022, MIT).
  https://gpuopen.com/fidelityfx-superresolution/ , https://gpuopen.com/fidelityfx-superresolution-2/
- Apple. "Boost performance with MetalFX Upscaling." WWDC 2022, session 10103.
  https://developer.apple.com/videos/play/wwdc2022/10103/
- Apple. "Tailor your apps for Apple GPUs and tile-based deferred rendering." Apple Developer Documentation (undated,
  read 2026-10-22).
- Dawn. `src/dawn/native/Toggles.cpp`, toggle `timestamp_quantization` (main, read 2026-10-22).
  https://dawn.googlesource.com/dawn/+/refs/heads/main/src/dawn/native/Toggles.cpp
- Chrome for Developers. "WebGPU developer features" (timestamp quantisation at 100 µs, disabled by the developer
  flag). https://developer.chrome.com/docs/web-platform/webgpu/developer-features

