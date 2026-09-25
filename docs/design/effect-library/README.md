# Effect Library: research and design

**Status:** design proposal (Phases 1–5). No engine code has changed. The documents were written
against branch `agent/effect-library-research`, whose base is `adbc7319`. That base has ADR-702
(Entity Effects) landed.

## What this library designs

It covers **73 effects**. The spec lists 77 names and four are duplicates. The effects are designed
against ADR-702's Entity Effects architecture:

- effect *types* live in a registry;
- effect *instances* attach to an owner (World, Entity, Camera or Light) in one list;
- every row is a modulatable parameter `fx/<id>/<leaf>`;
- one evaluator (`Engine::updateEffects`) builds per-stage frame blocks that the renderer only
  reads.

The central finding is that the 73 effects reduce to **16 shared primitives**. A dozen of the effects
are presets of systems the engine already has. See [shared-infrastructure.md](shared-infrastructure.md).

## Documents

- [STATUS.md](STATUS.md) -- where the library stands (paused after Wave 3) and how to resume.

| Document | Phase | Contents |
|---|---|---|
| **README.md** (this page) | 1 | Index, engine audit and references |
| [catalog.md](catalog.md) | 2 | Master table: 73 effects, deduplication, legend |
| [catalog-distortion.md](catalog-distortion.md) | 2 | Space Warp, Gravitational Lens, Heat Shimmer, Shockwave, Ripple, Bubble, Time-Warp, Portal, Reality Tear, Radial |
| [catalog-light.md](catalog-light.md) | 2 | Glow, Pulse, Flicker, Bloom Source, Light Beam, Volumetric Beam, God Rays, Aura, Halo, Light Trail, and the family's "what does each touch" decision |
| [catalog-motion.md](catalog-motion.md) | 2 | Trail, Afterimage, Motion Smear, Velocity Distortion, Wind Response, Orbit, Spiral, Float, Shake, Bounce |
| [catalog-energy.md](catalog-energy.md) | 2 | Lightning, Arc, Electric Field, Plasma, Energy Shield, Force Field, Charge-Up, Discharge |
| [catalog-organic.md](catalog-organic.md) | 2 | Bioluminescence, Pulsing Veins, Growth, Sway, Breathing, Pollen, Tendrils, Organic Pulsation |
| [catalog-particles.md](catalog-particles.md) | 2 | Fireflies, Embers, Snow, Rain, Ash, Leaves, Spores, Magic Particles, Cosmic Dust, Stars |
| [catalog-stylization.md](catalog-stylization.md) | 2 | Fresnel, Rim Light, Dissolve, Hologram, Scanlines, CA, Pixelation, Dithering, Toon Edges, Color Cycling |
| [catalog-temporal.md](catalog-temporal.md) | 2 | Echo, Freeze-Frame, Time Dilation, Temporal Smear, Ghosting, Delayed Motion, Reverse |
| [time-dilation.md](time-dilation.md) | 2/3 | Per-entity local time that keeps seek determinism (ADR-700) |
| [shared-infrastructure.md](shared-infrastructure.md) | 4 | The 16 primitives, which effects each serves, and which effects are merely presets |
| [rendering-architecture.md](rendering-architecture.md) | 3 | How new stages plug into `updateEffects` → frame blocks → `SceneRenderer`; capacity and `Dropped`; registry metadata |
| [parameters-and-modulation.md](parameters-and-modulation.md) | 3 | Conventions; the `entity.*` / `owner.*` signal design; the per-effect modulation table |
| [target-capability-matrix.md](target-capability-matrix.md) | 3 | What World, Entity, Camera, Light and Material can provide; the per-effect matrix |
| [roadmap.md](roadmap.md) | 5 | Six waves by shared dependency; Wave 1 as a file-level plan; the UFO stacking demo |
| [performance-risks.md](performance-risks.md) | 5 | Performance class and primary cost per effect; cross-cutting risks (no invented timings) |

---

## Phase 1: engine audit

Every pointer below was read in the code at `adbc7319`. Three read-only sub-audits (particles;
motion, temporal and history; material, geometry, organic and lights) were cross-checked against the
main read of the effect system and renderer. **"Missing"** means it was searched for and not found.

### 1. Effect system (ADR-702)

| Area | What exists | Where |
|---|---|---|
| Instances | `EffectInstance{id, kind, name, owner, enabled, order, style, activation, timing, payloads…, values}`; owners are World, Entity, Camera and Light | `src/world/effects/effect_instance.hpp:47-109` |
| Status | `Disabled, Dormant, Drawn, Dropped, Orphaned` | `effect_instance.hpp:114-120` |
| Registry | `EffectSchema`: key, targets mask, category, `RenderStage` (8 values; Geometry, Lighting, Particles, ScreenSpace and PostProcess are "none yet"), priority, rows, styles, routes, beatLeaf, endpoints, factory, resolve | `effect_registry.hpp:406-559` |
| Buckets | `Comet, Aurora, Medium, Surface`. A new integrator needs WGSL, a GPU struct and a renderer change | `effect_registry.hpp:391-404`, `:68-73` |
| Kinds | 8 of them: comet, aurora, vortex, meteor shower, volumetric fog, tornado, ground pulse, travel beam | `src/world/effects/kinds/*.cpp`; `effect_kind.hpp` (append-only numbering) |
| Parameters | `fx/<id>/<leaf>`, table-driven register, apply and capture; "no audio hook on any effect", and entity-derived modulation is to be a bus signal plus a route | `effect_params.hpp:3-28` |
| Evaluator | `updateEffects`: apply, then one `EffectContext`, then `buildWaveFrame` and `buildAtmosphericFrame`, then orphan check and drop log | `src/app/engine.cpp:4011-4075` |
| Context | transport seconds (the *only* clock), camera, camera velocity (timeline finite difference), shots, heroes, scene query, spectrum, field bus | `effect_timing.hpp:137-153` |
| Scene query | `nodePosition`, `nodeForward` **only**. No matrix, bounds, velocity, history or mesh | `effect_timing.hpp:123-131`; adapter `engine.cpp:3801` |
| Timing | Activations are `Always, Window, CameraTravel, HeroFocus`; `repeatSeconds`. **No event or trigger activation** | `effect_timing.hpp` |
| Frame order | routes, then `controller_->update` (scene built), then post/camera params, then `updateEffects`. Effects are evaluated *after* entity transforms exist | `engine.cpp:4290-4355` |
| Capacities | waves 8, comets 6, auroras 2, media 4 | `wave_effect.hpp:59`, `atmospherics.hpp:82-83`, `:612` |
| **Gap** | **No UI code reads `effectStatus`, `effectKindsFor` or `editEffects` on this branch**. Only `engine.cpp` and GPU tests call them | grep of `src/` |

### 2. Renderer and pass architecture

- **Pass order** (`src/rendering/scene_renderer.cpp`):
  1. object uniforms (2808)
  2. particle compute (3319)
  3. procedural (3377)
  4. SDF (3402)
  5. shadow depth (3410)
  6. background (3481)
  7. depth prepass (3517)
  8. linear depth (3566)
  9. GTAO (3598)
  10. shadow mask (3601)
  11. **pass 1** scene → HDR plus 4 aux targets (3608); inside it, after opaque, come the
      atmospheric sky layer (3743), water (3767), grid, particles (3792) and blended entities
  12. volumetric march and composite (3803)
  13. debug (3815)
  14. user post layers (3839)
  15. temporal (3877)
  16. built-in post chain (3901)
  17. tonemap (3949)
  18. overdraw (3975)
  19. aux debug (4041)
  20. 2D composition (4137)
- **Targets and formats** (`scene_renderer.hpp:732-742`):

  | Target | Format |
  |---|---|
  | HDR | RGBA16F (`CopySrc`, `TextureBinding`, `scene_renderer.cpp:1440`) |
  | depth | Depth24Plus |
  | normal + roughness | RGBA16F (octahedral) |
  | velocity | RG16F |
  | emission | RGBA16F (alpha = bloom weight) |
  | ids | R32Uint (object id low 16 bits, material id high 16 bits) |
  | linear depth | R32F |

- **Scene-colour copy / refraction.** **Missing.** No pass copies HDR for sampling. Water "refracts"
  by re-projecting a world-space displacement and reading the *depth* of the bed. It does not read
  scene colour (`shaders/water.wgsl:245-275`). This is the main gap for the distortion family, and
  DF fills it.
- **Transparency.** Blended entities, water and particles draw inside pass 1 after opaque. Particles
  are unsorted (ADR-015). There is no OIT. ADR-385 promotes fading nodes to Blend, and ADR-701 gives
  them stochastic transparency in the depth/shadow pass.
- **User shader layers.** `LayerStage {Background, Post}`, HDR ping-pong (`src/shaders/shader_layers.hpp:25`,
  `scene_renderer.cpp:3839-3875`).
- **WebGPU/Dawn.**
  - The device requests the adapter's full limits (`src/gpu/context.cpp:157-180`).
  - Bind-group convention: 0 frame, 1 object, 2 material, 3 IBL.
  - WGSL has no geometry shaders.
  - A texture cannot be sampled while it is attached to the same pass. That is why DF must copy.
  - There is a timestamp-query FrameTimeline.
  - There is a transient *texture* pool (`src/gpu/transient_pool.hpp`), but **no general GPU buffer
    pool or ring allocator**.
- **Metal.** WebGPU exposes neither tile memory nor programmable blending. Copies are real
  bandwidth [R23].

### 3. Shader infrastructure

- **Includes.** `ShaderLibrary::resolveIncludes` does textual `#include` with depth ≤ 8 and **no
  de-duplication** (`src/gpu/shader_library.cpp:77-115`). Include order is therefore a real
  constraint (`volume.wgsl` header).
- **Noise.** `shaders/noise.wgsl` has `pcg3d`, `hash01`, `valueNoise`, `fbm3` (3-octave value),
  `fbm3Vec`, `curlNoise` and `voronoiF1`. It has **no Perlin or simplex, no Worley F2, and no domain
  warp**. It is reachable only through `fields.wgsl:29`, and `wave_effects.wgsl` keeps its own hash
  for that reason.
- **The surface wave term.** `wavesAt` in `shaders/wave_effects.wgsl:73-183` is additive radiance
  only, and reaches entities, skinned, procedural and SDF surfaces through `pbr_shade.wgsl:26`.

### 4. Material system

- `src/scene/material_program.*` (ADR-030) is a per-fragment op interpreter:
  - base plus ≤ 4 layers, 8 registers, ≤ 48 ops;
  - ops: Noise, Voronoi, **Fresnel**, Ramp, Palette, **HueShift**, Field, Triplanar, EdgeWear,
    DecalBox, Swizzle;
  - inputs: Time, Audio, AudioBands, BeatPhase, InstanceRandom, ObjectId, CameraDistance;
  - outputs include emission and opacity (`material_program.hpp:1-161`).
- **Limits that matter here:**
  - A program is **shared by every user** of its material (`material_params.hpp:3-5`), so there is
    no per-entity override.
  - A program has **no vertex stage**.
  - **`fs_depth` does not run programs** (`shaders/pbr.wgsl:63-95`). So a program-driven dissolve
    leaves its depth and shadow behind.
- **Per-entity GPU state.**
  - `ObjectUniforms` is 416 B in a **512 B** slot (`scene_renderer.hpp:355-386`, `:712`), which leaves
    **96 B of padding**.
  - The WGSL mirror says "no free lane" (ADR-135, `common.wgsl:180-188`). That is true of the
    declared struct only.
  - The lanes carry `prevModel`, ADR-360 wind (`windOrigin/Shape/Tune`) and ADR-376 energy
    (`energy0-3/A/B`).
- **Built-in rim.** `pow(1−N·V,3)·emissive·0.3` (`pbr_shade.wgsl:504`), with a separate styled rim
  (`:386`).

### 5. Post (`shaders/post.wgsl`, `src/rendering/post_processor.*`)

- **Exists:**
  - exposure and metering;
  - bloom with threshold/knee, selective by the emission target (ADR-039);
  - halation;
  - anamorphic;
  - lens distortion and chromatic aberration (`post.wgsl:269-290`);
  - DoF and tilt-shift;
  - McGuire motion blur (tile-max / neighbour-max over the velocity target, `:526-620`) [R7];
  - FXAA;
  - composite grading (`hueRotate` at :345);
  - sharpen with an **id mask**, the only post consumer of `aux-ids` (:391-403);
  - look passes (atmospheric, local contrast, light wrap);
  - vignette and grain in tonemap.
- **Encoder-level skip.** Passes at their defaults are not encoded, so frames stay byte-identical
  (§60/§87).

### 6. Volumetrics

- **The march.** `shaders/volume.wgsl` has Henyey-Greenstein in-scatter from lights with
  `volumetricStrength` (:500-640), froxel local lights (:640-700), and emission.
- **Media slots.** 4 slots × 16 lanes, dispatched by `MediumSlot::kind` (fog bank, vortex, tornado;
  ADR-562/566/580).
- **ADR-570.** Self-shadow through media.
- **Missing:** geometry shadowing in the march, stated as *"There is no shadowing in the fog"*
  (:508). So there are no occluded god rays today.
- **Grid fields.** ADR-032 sim: `simulate.wgsl`, a compute grid solver (advect, diffuse, dissipate,
  reaction).
- **Fog/tornado.** ADR-560–599 and ADR-570. The fog bank and the vortex share one volumetric
  contention space ("one medium slot" history).

### 7. Particles (`src/scene/particles.hpp:123-309`, `shaders/particles.wgsl`)

- **Simulation.** GPU compute: emit, simulate, then a deterministic prefix-sum compaction (ADR-015).
- **Features (about 85 fields):**
  - Point, Sphere, Disc, Box and Spline emitters;
  - burst and emission-mask field;
  - gravity, drag, curl turbulence, attractor, orbit, up to 4 field forces, and wind;
  - leaf cards with tumble (ADR-370);
  - pulse, sync, clusters, pause and scatter anchors, for fireflies (ADR-520);
  - camera-carried wrapping volume;
  - collision against a *flat plane* (Kill, Bounce or Splash as a ring second life);
  - velocity stretch;
  - 32-point ribbons (ADR-040);
  - size, colour and opacity curves;
  - soft particles (ADR-367);
  - Henyey-Greenstein sun scatter;
  - fog coupling and volume glow (8 systems max).
- **Missing:**
  - sprites or atlases;
  - sorting;
  - mesh collision;
  - surface lighting from particles;
  - rotating the emitter direction by its parent;
  - registry integration.
- **Seek.** Pools reset, and warm-up is opt-in and capped at 240 frames (ADR-360/395/521). This is
  the engine's one documented determinism relaxation.
- **Presets.** Scene JSON only (`examples/weather/*` and others).

### 8. Procedural geometry and SDFs

- **Procedural objects.** Instanced storage records plus GPU culling and `DrawIndexedIndirect`, up to
  256 objects (`procedural_renderer.cpp:33`, `:2201`). There are 8 deformer slots (`procedural.wgsl:68-93`).
  The Tube primitive sweeps along a spline (ADR-043, `procedural.hpp:135-146`). Splines have 16 table
  slots (`spline.wgsl`).
- **SDF tree.** 8 primitives, 6 combinations, 8 domain ops, 4 displacements; 64 nodes, depth 8
  (`src/spatial/sdf.hpp`). It renders by raymarch (256 objects) or as a surface-nets mesh. **No
  example scene uses it.**
- **Growth.** **Missing.** `src/world/hero.hpp:127-132` lists a one-shot reveal as unsupported.

### 9. Animation, timeline, modulation and audio signals

- **The bus.** `src/signals/signal_bus.hpp`, with sources:
  - `audio.*` (bands, onset, beat, …);
  - `time.*`, `beat.*`;
  - `music.*` (`drop`, `build`, `impact`, …);
  - `field.<n>.*`;
  - `lfo`, `env`, `noise`, `random`, `timeline`, `control` and `macro`.
- **Missing signals.** There are **no `entity.*` or `camera.*` signals**.
- **Routes.** `ModRoute` (`src/params/modulation.hpp:19-44`) carries the `fromGraph`, `fromEntity`
  (ADR-088 reactions) and `fromMacro` flags.
- **Offline analysis.** `AnalysisTrack` makes beats and onsets addressable by time
  (`src/analysis/analysis_track.hpp`). That is the basis for deterministic triggers.
- **Camera shake.** ADR-098 (`src/scene/camera.hpp:104-134`) is a pure `startSeconds` envelope, which
  is the precedent for TRIGGER.

### 10. Camera

`scene.camera` holds position, target, lens and exposure. There is also a rig (`camera_rig.*`), shake,
multicam, and a physical lens mirrored into post (`engine.cpp:4308-4350`). Camera velocity exists only
as a timeline finite difference, and only for effects (`engine.cpp:3846`).

### 11. Lighting and shadows

- **Light types.** Seven (`scene_types.hpp:396-434`).
- **Clustering.** Clustered 16×24×8 grid, 32 lights per cluster, 256 scene lights
  (`light_data.hpp:65-70`), built by `clusters.wgsl` compute.
- **Shadows.**
  - A light casts only when `castsShadow` is set.
  - Directional lights get ≤ 4 cascades, spots get 1, point and area lights get a cube.
  - There are **8 shadow views in total** (`shadow_math.hpp:20-21`).
  - A half-resolution shadow mask serves ≤ 3 directional lights.
- **Ecology lights.** These take up to 224 minus the authored lights (`composition.cpp:94`,
  `:7671-7735`).
- **Parameters.** Authored lights register `lights/<id>/*` parameters.

### 12. Depth, linear depth, velocity and motion vectors

- **Depth.** Depth prepass, then R32F linear depth (`scene_renderer.cpp:3517-3597`).
- **Velocity.** Written by pbr, grid, procedural, water, sky, SDF and particles.
- **Per-object previous transforms exist.**
  - `prevModel` per entity (`scene_renderer.cpp:2833-2835`, `3038-3039`).
  - Skinned previous palettes (`skinning.cpp:303-342`).
  - Procedural `prevModel`.
  - SDF is camera-only (`sdf_renderer.cpp:77`).
- **On seek.** Previous transforms clear, so velocity is 0 on the first frame after a seek.
- **Debug toggles.** `cameraMotion`/`animationMotion` are debug freeze toggles, not signals
  (`scene_renderer.hpp:568`, `:573`).

### 13. HDR, bloom and emission

- **HDR and emission.** Both are RGBA16F with no emission clamp.
- **Tonemapping.** AgX by default, with chroma retention (ADR-039, and bioluminescence Phase A).
- **Selective bloom.** The emission target, alpha = bloom weight, and a per-object `ids.z` bloom
  weight.

### 14. Compute, instancing and GPU buffers

- **Compute shaders:**
  - `cull.wgsl`
  - `clusters.wgsl`
  - `simulate.wgsl`
  - `particles.wgsl`
  - `points.wgsl`
- **Fragment passes.** GTAO and linear depth are fragment passes.
- **Draws.**
  - Entities: one `DrawIndexed` per entity with a dynamic offset. **Not instanced.**
  - Procedural and particles: instanced or indirect.
- **Object buffer.** It grows to a 64 MiB budget (ADR-128).

### 15. Wind, tree energy and bioluminescence

- **Wind** is one analytic deterministic field (ADR-055, `src/core/wind.hpp`,
  `shaders/wind_field.wgsl:54`).
  - It is packed in the frame uniform.
  - Mesh wind (ADR-360), procedural sway and particles read it. Volumes do not.
  - The CPU `FieldBus` publishes `wind` and vortex flow to media packers (`field_bus.hpp`).
- **Tree energy (ADR-376).** `treeEnergyAt` (`common.wgsl:408-465`) is an emissive band up the body.
  Its parameters are `nodes/<n>/energy/*` (`composition.cpp:4449-4465`). It is entity path only.
- **Bioluminescence.**
  - A recipe ladder (`world_composer.cpp:813-835`).
  - Ecology glow clusters, turned into point lights.
  - Terrain ground glow.
  - Underwater glow (`water.wgsl:313`).
  - **No per-species emissive pulse and no "light field" symbol exist.**

### 16. Temporal (ADR-410)

- **The ring.** `TemporalHistory` holds ≤ 32 frames as texture arrays at `resolutionScale` 0.5.
  - Colour is RG11B10.
  - Motion is **allocated but never captured** (`temporal_history.cpp:378`).
- **Capture.** Pre-post (`scene_renderer.cpp:3877-3899`).
- **Effects.** Only **FrameEcho** exists (`temporal_settings.hpp:35-72`, `temporal.wgsl`).
- **Whole-frame only.** The id-at-read-time selection the header plans is not implemented.
- **Seek.** The ring resets. The ADR-410 **warm-up was never built**, and the ADR is still "proposed".

### 17. Entity simulation and seek (ADR-091, ADR-700)

- **Two tiers.** The baked tier is pure `Track::evaluate(t)`. The live tier is stateful behaviours.
- **Checkpoints.**
  - Taken every film second, and holding each `Entity` whole plus the director.
  - A seek restores the checkpoint before the target and replays fixed 1/60 steps
    (`EntityWorld::replayStep`, `entity.cpp:1776`).
  - `Composition::ReplayPlacement` records each step's flattening and is checkpointed
    (`composition.hpp:1874-1900`). That is the natural recording point for effect history.
- **Velocity.** `EntityState::velocity/acceleration` is measured by backward difference (ADR-545).
- **Transform history.** **No multi-sample transform history for effects exists.** The only history
  is the one-entity debug `TransformHistory` (`src/rendering/transform_history.hpp:72-102`).

### The biggest gaps, ranked by how many effects they block

1. **No scene-colour copy or distortion pass.** This blocks 13 effects (DF).
2. **No entity-scoped material or vertex terms.** Programs are shared, have no vertex stage, and are
   not in `fs_depth`. This blocks about 20 effects (FXL).
3. **No entity-derived signals, and a scene query without velocity, matrix or history.** This blocks
   the architecture's own promise in `effect_params.hpp:12-13` (SIGNALS).
4. **No multi-sample transform history.** This blocks Trail, Afterimage, Delayed and Reverse (HIST).
5. **No event or trigger activation.** This blocks 17 event-driven effects (TRIGGER).
6. **No geometry shadowing in the volume march.** This blocks God Rays and occluded beams.
7. **Temporal is whole-frame, has no history ids, and has no warm-up.** Entity-masked temporal effects
   are impossible, and image history is not scrub-exact.
8. **Effects evaluate after the scene is flattened.** Transform offsets cannot move children. This
   needs a two-phase evaluator.
9. **No UI consumer of `effectStatus` on this branch.** `Dropped` is currently unreadable outside
   tests.

---

## References

These sources were chosen by family, per the brief, rather than by crawling per effect. Each line
gives the one insight used.

- **[R1]** T. Sousa, "Generic Refraction Simulation", *GPU Gems 2* ch.19 (NVIDIA, 2005). Perturb
  lookups into a copy of the scene, and mask samples that hit foreground objects. Used for water,
  heat haze and lenses in Far Cry.
  <https://developer.nvidia.com/gpugems/gpugems2/part-ii-shading-lighting-and-shadows/chapter-19-generic-refraction-simulation>
- **[R2]** Unity HDRP, Distortion. Artist-driven screen-space distortion. Materials write a distortion
  buffer, which is applied after transparents and is separate from physical refraction.
  <https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@14.0/manual/HDRP-Features.html>
- **[R3]** Unreal Engine translucent refraction and Pixel Normal Offset. The refraction offset is
  authored per material in screen space, sampling scene colour captured before translucency.
- **[R4]** K. Mitchell, "Volumetric Light Scattering as a Post-Process", *GPU Gems 3* ch.13. A
  radial blur of an occluder mask from the light's screen position. It fails off-screen.
- **[R5]** B. Wronski, "Volumetric Fog", SIGGRAPH 2014 *Advances in Real-Time Rendering*. Froxel
  in-scattering with shadow-map sampling.
- **[R6]** S. Hillaire, "Physically Based and Unified Volumetric Rendering in Frostbite", SIGGRAPH
  2015 *Advances*. Energy-conserving integration with shadowed participating media.
- **[R7]** M. McGuire et al., "A Reconstruction Filter for Plausible Motion Blur", I3D 2012. Tile-max
  and neighbour-max velocity. The engine implements it.
- **[R9]** B. Karis, "High-Quality Temporal Supersampling", SIGGRAPH 2014 *Advances*. History
  reprojection. Without clamping it ghosts, and Ghosting here is that artefact used on purpose.
- **[R10]** G. James & J. O'Rorke, "Real-Time Glow", *GPU Gems* ch.21. Glow sources go into a
  separate buffer and are blurred, which is selective rather than threshold-driven.
- **[R11]** T. Sousa, "Vegetation Procedural Animation and Shading in Crysis", *GPU Gems 3* ch.16.
  Layered main bend plus detail flutter.
- **[R12]** R. Bridson, J. Houriham, M. Nordenstam, "Curl-Noise for Procedural Fluid Flow", SIGGRAPH
  2007. A divergence-free noise velocity field.
- **[R13]** T. Reed & B. Wyvill, "Visual Simulation of Lightning", SIGGRAPH 1994. A random walk with
  about 16° mean deviation, branching probability, and glow from segments.
- **[R14]** Niemeyer et al. 1984 (the dielectric breakdown model); T. Kim & M. Lin, "Physically Based
  Animation and Rendering of Lightning", PG 2004. Physically plausible branching, too costly per
  frame, usable as a seeded precompute.
- **[R16]** T. Lorach, "Soft Particles", NVIDIA whitepaper 2007. Fade by scene-depth difference. The
  same term gives shield and beam intersection lines.
- **[R17]** O. James, E. von Tunzelmann, P. Franklin, K. Thorne, "Gravitational lensing by spinning
  black holes in astrophysics, and in the movie *Interstellar*", CQG 32 (2015). Thin-lens point-mass
  mapping `β = θ − θ_E²/θ`.
- **[R18]** Unreal Niagara ribbon renderer; Unity Trail Renderer. Trails are strips over a sampled
  position history, with width and colour as functions of age.
- **[R19]** T. Saito & T. Takahashi, "Comprehensible Rendering of 3-D Shapes", SIGGRAPH 1990. Edges
  from depth and normal discontinuities in G-buffers.
- **[R20]** J. Motomura, "Guilty Gear Xrd's Art Style: The X Factor Between 2D and 3D", GDC 2015.
  Inverted-hull lines, stepped (limited) animation, authored smears and afterimages.
- **[R21]** L. Pope, *Return of the Obra Dinn* devlog (dithering). Screen-space dither swims.
  Anchoring the pattern to the view sphere stabilises it.
- **[R22]** W3C WebGPU and WGSL specifications. Usage-scope rules forbid sampling an attachment of
  the current pass. There are no geometry shaders. Default limits are 4 bind groups, 8 colour
  attachments and 8 storage buffers per stage.
- **[R23]** Apple Metal feature documentation. Tile memory and programmable blending exist in Metal
  but are not exposed through WebGPU, so copies cost real bandwidth.
- **[R25]** Unreal Engine DepthFade material function. Soft intersection against scene depth.
- **[R29]** H. Nguyen, "Fire in the Vulcan Demo", *GPU Gems* ch.6. Heat haze by perturbing a scene
  copy with scrolling noise.
- **[R30]** N. Tatarchuk, "Artist-Directable Real-Time Rain Rendering in City Environments", SIGGRAPH
  2006 course. Rain as streaks, splashes, mist and lightning relighting.
- Others cited inline: B. van Dongen, "Interior Mapping" (2008); L. Belcour & P. Barla, "A Practical
  Extension to Microfacet Theory for the Modeling of Varying Iridescence" (2017); S. Eiserloh,
  "Juicing Your Cameras With Math" (GDC 2016); Crane, Llamas & Tariq, *GPU Gems 3* ch.30.

**Confirmed by lookup during this research:** [R1] (chapter, author, and its heat-haze and
foreground-mask scope) and [R2] (HDRP's artist-driven distortion pass, and its interaction with
refraction). The other references are cited from established knowledge of the literature and were
not re-fetched. Their one-line insights are standard, but their chapter numbers should be spot-checked
before these documents are quoted externally.
