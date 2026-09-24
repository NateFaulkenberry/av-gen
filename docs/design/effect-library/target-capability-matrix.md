# Target Capability Matrix

This page is part of the [Effect Library](README.md). It first establishes what each owner kind can
actually provide in this engine, verified in code. It then gives the per-effect matrix, which is the
proposed `EffectSchema::targets` mask for each type.

## 1. What each target provides today

### World (`EffectTarget::World`)

- **Has:**
  - the transport second;
  - the active camera;
  - the shot spans and heroes;
  - the spectrum;
  - the field bus (wind, vortex);
  - the scene's shared resources: fog and 4 medium slots, sky, the ecology light aggregation, and the
    scatter layers (`EffectContext`, `effect_timing.hpp:137-153`).
- **Has no:** position, mesh or velocity.
- **World-owned types need one of:**
  - an authored position (`SkyAnchor`/`anchorPosition`), or
  - an endpoint (`EffectEndpoint`: a hero, a node, the camera, "whatever the cut is on";
    `effect_stack.hpp:61-65`), or
  - no position at all (screen-scale looks that belong to the world rather than to a camera).

### Entity (`EffectTarget::Entity`), a composition node

- **Transform.** The node's world transform comes from parameter tracks (pure) plus the entity
  layer's `motion_.position + state_.travel` (`entity.cpp:2157-2163`). The effect evaluator can see
  only `nodePosition` and `nodeForward` today (`EffectSceneQuery`, `effect_timing.hpp:123-131`).
- **Velocity and acceleration.**
  - Measured for simulated bodies (`EntityState::velocity/acceleration`, `behavior.hpp:77`, `:129`).
    This is not exposed to effects or to the bus.
  - Track-driven nodes have none. It must come from HIST.
- **Geometry.**
  - One or more `GpuMesh` per entity, with LOD rungs (ADR-351).
  - Bounds come from `scene.meshBounds`.
  - Skinned bodies have a current and previous palette (`skinning.cpp:303-342`).
  - SDF and procedural objects are *not* entities in the draw-item sense. They have their own
    renderers (`sdf_renderer`, `procedural_renderer`). So REDRAW and FXL reach ordinary and skinned
    meshes only. **This is a real limit.** An effect on a procedural or SDF owner needs FXPOST
    (id-masked) or SHELL modes.
- **Per-draw GPU state.** One 512 B object slot with `prevModel`, the ADR-360 wind lanes, ADR-376
  energy lanes and 96 B of free padding (`scene_renderer.hpp:355-386`, `:712`).
- **Identity.** The id target carries `packPickId(Entity, index)`. There are at most 16,383
  addressable entities (`kPickIndexBits`), which is enough for masks.
- **Material.** The material is shared. Per-entity `baseColor`/`emissive` come from that material
  (`scene_renderer.cpp:3041-3042`), and there is no per-entity override of a program.
- **Node parameters.** `nodes/<n>/opacity` (ADR-385; drives Blend promotion and ADR-701 stochastic
  shadow), `nodes/<n>/energy/*`, and wind.
- **Seek.** Simulated state is checkpointed (ADR-700).

### Camera (`EffectTarget::Camera`, empty name = the active camera)

- **Has:**
  - `scene.camera` (position, target, lens, exposure);
  - `CameraShake` (ADR-098);
  - the rig (`camera_rig.*`);
  - `cameraVelocityOnTimeline()`;
  - the whole post chain and the temporal ring;
  - the id, depth, normal, velocity and emission targets as post inputs.
- **Multicam.** Scenes have several named cameras (`glowmere-valley-2-multicam`). **Proposal:** a
  Camera-owned `PostProcess` effect applies only while its camera has the frame. That way a "camera
  look" travels with the cut, and the empty name means "whichever is active".

### Light (`EffectTarget::Light`)

- **Has:**
  - seven types: Directional, Point, Spot, Rect, Disk, Tube and Sphere (`scene_types.hpp:396-434`);
  - parameters `lights/<id>/{enabled, intensity, color, azimuth, elevation, angularSize,
    shadowStrength, position}` (`composition.cpp:3952-3975`);
  - `volumetricStrength` (in the march);
  - `castsShadow` (shadow views are shared, 8 in total);
  - clustered shading with up to 256 scene lights.
- **Has no:** mesh or id. So FXL and REDRAW do not apply, and a light's "visible body" must be a
  SHELL (a halo or beam cone) or an Entity it is attached to.

### Material (not an ADR-702 owner)

`EffectTarget` has four values and **no Material**. That is right, and this design keeps it.
Material-scope looks are **material programs**. A program is a per-fragment op interpreter with
Fresnel, Voronoi, Noise, Ramp, Palette and HueShift, and with inputs for Time, Audio, AudioBands,
BeatPhase and CameraDistance (`material_program.hpp`). It already has parameters, modulation and
presets, and it applies to every user of the material.

A "Material effect" owner would be a second authority over the same fragments. The matrix's Material
column therefore says whether the look is **already expressible as a program** (P), which is the
supported path, or not (–). Where a program falls short, the reason is stated. Usually the program
has no vertex stage and no depth-pass presence.

## 2. The matrix

Legend:

| Mark | Meaning |
|---|---|
| ● | primary target |
| ○ | supported |
| – | not supported (reason in Notes) |
| P | via a material program, existing, no effect needed |
| (P) | a program can approximate it, but with a stated defect |

| Effect | World | Entity | Camera | Light | Material | Notes |
|---|---|---|---|---|---|---|
| **Distortion** | | | | | | |
| Space Warp | ○ | ● | – | – | – | a camera-wide warp is Radial Distortion |
| Gravitational Lens | ● | ○ | – | – | – | |
| Heat Shimmer | ○ | ● | ○ (Haze) | – | – | |
| Shockwave | ○ | ● | – | – | – | the flash light is created, not owned |
| Ripple | ○ | ● | – | – | P (Surface mode = Ground Pulse) | |
| Bubble | ○ | ● | – | – | – | |
| Time-Warp Distortion | ○ | ● | ○ | – | – | |
| Portal Distortion | ● | ○ | – | – | – | |
| Reality Tear | ● | ○ | ○ (transition) | – | – | |
| Radial Distortion | – | – | ● | – | – | an entity is only its centre (endpoint) |
| **Light** | | | | | | |
| Glow | – | ● | – | – | P (Emission op, shared) | |
| Pulse | – | ● | – | ● | P (Time op) | |
| Flicker | – | ○ | – | ● | P | |
| Bloom Source | – | ● | – | – | P (material bloom weight) | |
| Light Beam | ○ | ○ | – | ● | – | |
| Volumetric Beam | – | ○ | – | ● | – | |
| God Rays | ● | – | ○ (Screen mode) | ○ (directional) | – | |
| Aura | – | ● | – | – | – | Hull mode needs a mesh, so not SDF or procedural; Screen mode for those |
| Halo | ○ | ● | – | ● | – | |
| Light Trail | – | ● | – | ○ | – | a preset of Trail |
| **Motion** | | | | | | |
| Trail | – | ● | – | ○ | – | |
| Afterimage | – | ● | – | – | – | Camera-wide is Echo |
| Motion Smear | – | ● | – | – | – | needs a vertex stage; programs have none |
| Velocity Distortion | – | ● | – | – | – | |
| Wind Response | – | ● | – | – | P (procedural Tier 0 sway) | |
| Orbit | – | ● | (rig) | ○ | – | the camera uses `camera_rig` |
| Spiral | – | ● | – | ○ | – | |
| Float | – | ● | – | ○ | – | |
| Shake | – | ● | ● (ADR-098) | ○ | – | |
| Bounce | – | ● | – | – | – | |
| **Energy** | | | | | | |
| Lightning | ● | ○ | – | – | – | |
| Arc | ○ | ● | – | – | – | |
| Electric Field | – | ● | – | – | (P) (crackle via Voronoi; F2 missing) | |
| Plasma | ○ | ● | – | – | – | |
| Energy Shield | ○ | ● | – | – | – | |
| Force Field | ● | ○ | – | – | – | |
| Charge-Up | ○ | ● | – | – | – | |
| Discharge | ○ | ● | – | – | – | |
| **Organic** | | | | | | |
| Bioluminescence | ○ (recipe ladder) | ● | – | – | (P) (no per-entity flare) | |
| Pulsing Veins | – | ● | – | – | (P) (the `tree_veins` program is the mask) | |
| Growth | – | ● | – | – | (P) (program opacity leaves depth and shadow: `fs_depth`) | |
| Sway | – | ● | – | – | P (procedural sway) | a preset of Wind Response |
| Breathing | – | ● | – | – | – | vertex stage |
| Pollen | ● | ○ | ○ (camera volume) | – | – | EMIT preset |
| Tendrils | ○ | ● | – | – | – | |
| Organic Pulsation | – | ● | – | – | – | vertex stage |
| **Particle / Environment** | | | | | | |
| Fireflies | ● | ○ | – | – | – | |
| Embers | ○ | ● | – | – | – | |
| Snow | ● | – | ○ | – | – | the camera volume, `volumeFollow` |
| Rain | ● | – | ○ | – | – | |
| Ash | ● | – | ○ | – | – | |
| Leaves (+ Falling Leaves) | ○ | ● (canopy) | – | – | – | |
| Spores (+ Floating Spores) | ○ | ● | – | – | – | |
| Magic Particles | ○ | ● | – | – | – | |
| Cosmic Dust | ● | – | ○ | – | – | |
| Stars | ● | – | – | – | – | Sky stage |
| **Stylization** | | | | | | |
| Fresnel | – | ● | – | – | P (shared) | |
| Rim Light | – | ● | – | ○ (a later wave) | (P) | |
| Dissolve | – | ● | – | – | (P) (depth and shadow defect) | |
| Hologram | – | ● | – | – | – | pipeline re-route |
| Scanlines | – | – | ● | – | – | Entity scanlines are Hologram |
| Chromatic Aberration | – | ○ (glitch mask) | ● (lens, exists) | – | – | |
| Pixelation | – | ○ (mask) | ● | – | – | |
| Dithering | – | – | ● | – | – | |
| Toon Edges | – | ○ (Hull) | ● (Screen) | – | – | |
| Color Cycling | – | ● | ○ (hueShift route) | – | P (HueShift op) | |
| **Temporal** | | | | | | |
| Echo | – | ○ (needs history ids) | ● (FrameEcho exists) | – | – | |
| Freeze-Frame | ● (Pose) | ● (Pose) | ● (Image) | – | – | |
| Time Dilation | ○ (region) | ● | – | – | – | the camera is never dilated |
| Temporal Smear | – | ○ | ● | – | – | |
| Ghosting | – | ● | ○ | – | – | |
| Delayed Motion | – | ● | – | – | – | |
| Reverse Playback-Like | – | ● (Motion) | ● (Image) | – | – | |

**Summary.** The spec lists 77 effect names. Four are duplicates, so there are 73 unique effects:

| Target | Primary | Supported | Not supported |
|---|---|---|---|
| Entity | 49 | 15 | 9 |
| World | 14 | 20 | 39 |
| Camera | 11 | 11 | 51 |
| Light | 5 | 8 | 60 |

No type is universally attachable. That was the spec's warning, and each "–" above is enforced by the
type's `targets` mask through `effectAllowedOn` (`effect_stack.hpp:53-56`). The Add Effect menu never
offers the combination.
