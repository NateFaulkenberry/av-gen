# Shared Infrastructure (Phase 4)

This page is part of the [Effect Library](README.md). It describes the small set of primitives that
all 73 unique effects are built from. Each primitive is described in five parts: **what it is**, **what
already exists**, **what is new**, **which effects it serves**, and **how it fails honestly** (its
capacity limits and `EffectStatus::Dropped`).

The principle comes from ADR-702, `effect_registry.hpp:68-73`. It warns that a look which needs a
*new integrator* costs shader, GPU-struct and renderer work, and a registry cannot move that work
into a `.cpp` file. So this library adds **a few integrators, each serving many effects**, instead
of one pass per effect.

Every primitive must keep the engine's **default-neutral** rule. With no instance live, the command
stream and pixels must be byte-identical to a build without the primitive. The skipped-pass
discipline of `post.wgsl` §60/§87 and ADR-372's "proved rather than asserted" apply. Each primitive
below therefore describes its *gate*.

## Tags at a glance

| Tag | Primitive | New GPU work? | Serves (count) |
|---|---|---|---|
| **DF** | Distortion field: scene-colour copy + offset target + depth-aware resolve | yes: 1 copy, 1 proxy pass, 1 resolve | 13 |
| **FXL** | Per-entity effect lanes (material and vertex terms in the lit, depth and shadow passes) | yes: object-layout extension | 20 |
| **XFORM** | Render-transform offset layer (visual-only procedural motion) | no (CPU) | 9 |
| **HIST** | Per-entity transform history on the fixed step grid, checkpointed | no (CPU) | 8 |
| **RIBBON** | CPU-built camera-facing or swept strips with core and glow profiles | yes: 1 pipeline | 8 |
| **SHELL** | Analytic proxy shapes with effect shading and depth fade | yes: 1 pipeline family | 10 |
| **REDRAW** | Draw the owner's mesh again with an effect pipeline and transform | yes: pipeline variants | 5 |
| **BOLT** | Seeded fractal polyline generator (cached) | no (CPU) | 5 |
| **EMIT** | Effect-owned particle system over the existing GPU particles | small (dt override, direction rotation) | 13 |
| **LIGHTMOD** | Light modulation plus a transient light pool | no (CPU; uses clustered lights) | 15 |
| **MEDIUM** | Existing placed-media slots + shadow-map sampling in the march | yes: bind the shadow atlas in the march | 3 |
| **FXPOST** | Camera post hooks (pre-bloom HDR and post-tonemap), with id, depth and normal masks | yes: hook points + passes | 11 |
| **TEMPORAL** | ADR-410 ring + history-id channel + warm-up | yes: ring channel + warm-up | 7 |
| **TRIGGER** | Deterministic event activation (last K trigger times ≤ t) | no (CPU) | 17 |
| **SIGNALS** | Entity-derived and effect-published signals on the bus | no (CPU) | most |
| **LOCALTIME** | Per-owner local clock τ (see [time-dilation.md](time-dilation.md)) | no (CPU) | 5 |

---

## DF: the distortion framework

This is the reusable distortion system the spec asks for. It covers localized volumes, screen-space
effects, depth-aware effects, radial effects, animated fields and refraction.

**What exists**

- The HDR target has `CopySrc | TextureBinding` (`scene_renderer.cpp:1440`), so a scene-colour copy
  is one `CopyTextureToTexture` of RGBA16F.
- The linear-depth target exists (`aux-linear-depth`, R32F, `scene_renderer.hpp:741`).
- Water already refracts. It does this by re-projecting a world-space displacement, and it reads
  depth (`shaders/water.wgsl:245-275`), but it does *not* read scene colour, because it draws inside
  the scene pass.
- **No scene-colour copy exists anywhere today.** The engine has no refraction of colour.

**Design** (after [R1] Sousa and [R2] HDRP):

1. **Offset pass.** Each DF producer draws a proxy (sphere, ellipsoid, disc, quad, ribbon, or a
   screen-covering triangle for camera fields) into two targets:
   - `distortOffset` (RGBA16F): `xy` is the screen offset in UV times the weight, `z` is weight ×
     linear depth of the distorter, and `w` is the weight.
   - `distortAux` (RGBA16F): `rgb` is additive edge emission (HDR), and `a` is chroma spread.

   Blending is additive, so overlapping producers superpose, which is thin-lens superposition. The
   pass is depth-tested against the scene depth (read-only), so a warp behind a wall draws nothing.
   Producers author displacement in **world units** and convert it with
   `proj(p + d) − proj(p)`, which is the lesson from `water.wgsl:251-259`. *Remap* producers
   (Gravitational Lens) write an absolute offset and are ordered last by priority.
2. **Copy.** Copy HDR to `sceneCopy`. This happens only on frames with at least one live producer.
3. **Resolve.** A full-screen pass, scissored to the union of the producers' screen rects, reads
   `sceneCopy` at `uv + offset`. If `linearDepth(uv + offset) < z/w − ε`, the sampled texel belongs
   to something *in front of* the distorter, and the pass falls back to the unoffset sample. This
   is Sousa's foreground-leak mask.
   - Chroma: three taps along the offset direction.
   - Edge emission is added to HDR, and also to the emission target through MRT, so selective bloom
     sees it.
   - Lensing into the sky: when `uv + offset` leaves the screen or hits the far plane, the pass
     samples the environment cube along the bent ray (IBL bind group).
   - **Self-exclusion.** An Entity-owned producer writes the depth of its *centre* (the owner's
     centre), not the depth of the proxy's front face. The resolve applies the offset only where the
     current pixel's own depth is *beyond* that centre. Samples nearer than the centre are rejected.
     - Result: a UFO inside its own Space Warp stays crisp, and what is behind it bends.
     - Without this rule, the owner's front surface is distorted by its own field. The proxy
       encloses the owner, so every owner pixel is covered by the proxy.

**Where in the frame.** After the volumetric composite and the debug pass, and before post layers,
temporal capture and the post chain (`scene_renderer.cpp:3803-3877`). This placement gives:

- fog, media and god rays are distorted;
- post (bloom, DoF, grading) sees the distorted image;
- the temporal ring captures distorted frames, so Time-Warp reads history that already contains
  earlier warps. This is FIR-legal.

**Known limitations**

- Particles and blended surfaces write no depth, so one standing *in front of* a distorter is still
  distorted. HDRP has the same trade-off. A later mitigation is to draw additive particles after the
  resolve, but that costs a split scene pass.
- The velocity target is not distorted, so motion blur uses undistorted vectors. This is negligible
  at these amplitudes.

**Gate.** No producer means no targets touched, no copy and no resolve. The frame is byte-identical.

**Capacity.** Proxies go into one storage buffer with a fixed cap (proposed 64 proxies). An instance
beyond the cap reports `Dropped` and names DF.

**Serves:** Space Warp, Gravitational Lens, Heat Shimmer, Shockwave, Ripple (Membrane), Bubble (rim),
Time-Warp, Portal (swirl), Reality Tear (shear), Velocity Distortion, Energy Shield, Force Field
(refraction), and Chromatic Aberration (local).

**Metal note.** On Apple GPUs a full-resolution RGBA16F copy is a real bandwidth cost: about 16.6 MB
read plus 16.6 MB written at 1080p. That is why the copy and resolve are gated per frame and
scissored. Programmable blending or tile reads would avoid the copy, but WebGPU does not expose them
[R22][R23].

## FXL: per-entity effect lanes

**What exists**

- `ObjectUniforms` is 416 B inside a 512 B dynamic-offset slot (`scene_renderer.hpp:355-386`, `:712`).
  So **96 B of every slot are unused padding**. They are not declared in WGSL: `common.wgsl:180-188`
  (ADR-135) records "no free lane" and says the object-layout work is "Phase E's".
- The vertex stage already applies a deformation *identically in every pass*, including the
  previous-frame clip. That is `meshWindOffset` (`common.wgsl:268`, `:358-372`), gated by
  `windShape.y == 0`.
- Tree energy (ADR-376) is an emissive term gated by `energy0.x`.
- `fs_depth` (depth prepass and shadows) reads object uniforms, but **not** material programs
  (`pbr.wgsl:63-95`).
- ADR-701 added stochastic transparency in `fs_depth` for fading bodies.

**Design**

- Declare two inline lanes in the padding:
  - `fxA = (emissionGain, bloomOverride, fxFlags, fxIndex)`
  - `fxB = (tint.rgb, reserved)`
- `fxIndex` points into a new read-only storage buffer `entityFx: array<EntityFx>` of 256 B records,
  16 × vec4. It is bound at group 1 beside the object uniforms. Record 0 is neutral.
- Each record has fixed sub-blocks:
  - **Rim**: colour, power, bias, direction mode (Glow rim, Fresnel, Rim Light).
  - **Pulse / band**: waveform, speed, width, axis, coordinate (Pulse travelling, Pulsing Veins,
    Bioluminescence wave).
  - **Pattern**: Worley or noise, scale, coverage, colour (Bioluminescence, Electric crackle).
  - **Clip**: mode, threshold, edge width, edge colour and emission (Dissolve, Growth).
  - **Displace**: mode Inflate / Travelling / Smear, amplitude, rate, region, velocity (Breathing,
    Organic Pulsation, Motion Smear).
  - **Hue**: speed, range, frequency (Color Cycling).
  - **Hologram**: scan, flicker, glitch.
- **Composition on the CPU.** The Material/Geometry-stage builder folds every FXL effect on an owner
  into one record, in stack order:
  - gains multiply;
  - additive emission sums;
  - rims sum, and their colours are weighted;
  - clip takes the max;
  - displacements sum, with one slot per mode.

  A second effect that needs an occupied *exclusive* sub-block, such as two different Hologram
  settings, reports `Dropped` and names the conflicting effect.
- **Passes.** Vertex displacement and clip run in the lit pass, `fs_depth` (prepass and shadow), and
  the prevClip path. That last one is fed the previous frame's time, as `windTune.w` already is. The
  shadow of a dissolving or growing owner is therefore correct, unlike a program-driven dissolve
  today.
- **Pipeline routing.** The Hologram flag moves the owner's draw item to the blended list with a
  hologram variant. It uses the same bucket decision ADR-385 and ADR-701 use for fading nodes.
- **Gate.** `fxFlags == 0` makes the shader return before any FXL work, as a uniform branch per draw
  (ADR-118: branch per *draw*, not per lane).

**Why a storage buffer and not all-inline.** Six vec4s of padding cannot hold rim, pattern, clip,
displace and hue together. Raising `kObjectStride` to 768 would cut `kMaxObjectCapacity` by a third
for every entity, including the ones with no effects. An index costs one extra read, and only for
entities that have effects.

**Serves:** Glow, Pulse (Entity), Flicker (Entity), Bloom Source, Fresnel, Rim Light, Dissolve,
Hologram, Color Cycling, Bioluminescence, Pulsing Veins, Growth, Breathing, Organic Pulsation,
Motion Smear, Electric Field (crackle), Delayed Motion (a blur-off flag), Wind Response (Flex mode,
which keeps the ADR-360 lanes), Sway, and Time Dilation (the shutter-true velocity flag).

**Needs added to `noise.wgsl`:** Worley F2, for cell edges. The file today has `pcg3d`, value fbm,
curl and `voronoiF1` only.

## XFORM: the render-transform offset layer

**What exists**

- `CameraShake` (ADR-098, `src/scene/camera.hpp:104-134`) is exactly this idea for the camera. It is
  an offset built from ordinary parameters, with `startSeconds` so it scrubs.
- `floaters` (ADR-099 §13) are the pure-function-of-t precedent for instances.

**Design**

- Geometry-stage effects produce a local offset matrix per owner: translation, rotation and
  non-uniform scale about a pivot.
- The Composition composes this offset into the owner node's transform *before* children and
  attachments are resolved. Children, attached emitters, lights and effect proxies therefore follow
  the offset.
- `prevModel` is captured from the resulting scene transform (`scene_renderer.cpp:2833-2835`), so
  velocity and motion blur include it.
- **Visual-only.** `EntityWorld` state (position, travel, perception) is untouched. A floating UFO's
  AI does not perceive its own bob.
- **Ordering problem.** `Engine::update` runs `controller_->update` (composition) *before*
  `updateEffects` (`engine.cpp:4303`, `:4355`). See rendering-architecture §3 for the two-phase fix.

**Serves:** Orbit, Spiral, Float, Shake, Bounce, Wind Response (Rigid), Growth (Scale), Delayed
Motion (Stepped/Follow), and Reverse (Motion).

## HIST: per-entity transform history

**What exists**

- Only a debug overlay for one selected entity: `rendering::TransformHistory`, 240 samples
  (`src/rendering/transform_history.hpp:72-102`).
- The checkpoint machinery copies each `Entity` whole (ADR-700), and Composition has `capture` and
  `restore` hooks for the host half.

**Design**

- A `HistoryBank`: a fixed ring per *subscribed* owner. It records the owner's resolved world
  transform (before XFORM, so XFORM can be re-applied analytically at any sample time) once per
  simulation step instant.
- An owner is subscribed only while some effect needs history. The needed depth is the maximum of
  the subscribers' `persistence`.
- Samples take 40 B each (position, quaternion, scale). Four seconds at 60 Hz is about 9.6 KB per
  owner.
- The bank is part of the checkpoint, through the host capture and restore hooks. The seek replay
  refills the steps after the checkpoint. **A scrubbed trail therefore equals a played trail.**
- Readers interpolate between samples. The head of every trail is the current transform.

**Honest limit.** HIST is exactly as exact as the simulation steps it records. ADR-700 makes those
exact on the fixed grid for seeks. Whether *realtime* play steps on the same grid at every display
rate is ADR-700's property, and HIST inherits it. It does not add a new one.

**Serves:** Trail, Light Trail, Afterimage, Velocity Distortion, Delayed Motion, Reverse (Motion),
Space Warp's velocity for track-driven owners, and Motion Smear's velocity for track-driven owners.

## RIBBON: strips

**What exists.** Particle ribbons (ADR-040) are per particle and GPU-resident, at most 32 points.
They cannot take an entity's path.

**Design**

- The CPU builds strips from point lists: HIST samples, BOLT polylines, or two-anchor sweeps.
- Points are subdivided with Catmull-Rom and written into a per-frame dynamic vertex buffer, uploaded
  once with `queue.WriteBuffer`.
- The vertex stage expands the strip camera-facing (`cross(tangent, toCamera)`), or uses swept
  anchors. It clamps width to at least 1.5 px so thin bolts do not alias away.
- The fragment stage has two profiles: a hard *core* and a Gaussian *glow*.
- Blending is additive or alpha. Output goes to HDR and emission, velocity is camera-only, ids is 0.
- Ribbons draw in the blended section of pass 1, beside particles.
- **Gate:** no strips means no draw.
- **Capacity:** a vertex budget per frame (proposed 64k vertices). An instance over budget first
  reduces its subdivision (LOD), then reports `Dropped`.

**Serves:** Trail, Light Trail, Velocity Distortion (drawn into DF instead of HDR), Lightning, Arc,
Electric Field (arcs), Discharge, and Reality Tear (edge option).

## SHELL: analytic proxies

**Design**

- Canonical meshes are made once from `src/scene/mesh_generators.*`: sphere, ellipsoid (a scaled
  sphere), capsule, cone, disc, quad, box, torus, and billboard.
- Per-instance records (a transform plus 8 vec4 parameters) live in one storage buffer. One
  instanced draw is issued per shell kind.
- There is **one pipeline per shading kind** (Beam, Shield, Plasma, Bubble, Portal, Halo, Tear) rather
  than an in-shader switch, following ADR-118's measured penalty for lane-varying branches.
- Shells read linear depth for intersection glow and depth fade [R16][R25]. They read the IBL cube
  for reflections.
- They draw after opaque, in the blended section of pass 1. Portal and Tear interiors write depth.
- **Dependency:** linear depth only exists when the depth prepass runs (`needsDepthPrepass`,
  `scene_renderer.cpp:3517-3566`). Without it, depth fade degrades to off, and the panel says so.
- **Capacity:** a proposed 128 shells, beyond which an instance reports `Dropped`.

**Serves:** Bubble, Portal, Reality Tear, Light Beam, Halo, Energy Shield, Force Field, Plasma,
Charge-Up (core), and Aura (Sphere mode).

## REDRAW: draw the owner again

**Design**

- A second draw of the owner's `GpuMesh`, or of every mesh of a multi-mesh body, with an effect
  pipeline, a transform override and a vertex push along the normal.
- Skinned owners use a skinned variant, which reuses `SkinningRenderer`'s palette slice.
- Afterimage needs past palettes: a small GPU ring of K palette snapshots, captured at render. It is
  not exact after a seek, which is documented.
- **Capacity:** redraws per frame are capped (proposed 64 draws). Beyond that an instance reports
  `Dropped`.

**Serves:** Aura (Hull), Afterimage (Geometry), Toon Edges (Hull), Energy Shield (Contour), and
Hologram. Hologram is a re-route rather than a second draw, but it uses the same pipeline-variant
machinery.

## BOLT: fractal paths

**Design**

- A CPU generator: midpoint displacement with branches [R13]. DBM [R14] is an optional quality mode.
- Keyed by `(seed, index)` and cached in an LRU cache. Generation is deterministic across platforms,
  because it uses the engine's own RNG and never `std::` distributions, whose output is
  implementation-defined.
- Output is at most 512 vertices, with LOD by projected size.

**Serves:** Lightning, Arc, Electric Field, Discharge, and Reality Tear (outline).

## EMIT: effect-owned particle systems

**What exists.** The whole of `scene::ParticleSystem` (see
[catalog-particles.md](catalog-particles.md)): about 85 fields, GPU compute, and routes onto `burst`.
It is not in the registry.

**What is new**

1. A registry type `particleEmitter`. Its styles are the ten presets.
2. Owner attachment, including rotating `direction` by the owner. Today only the position, attractor
   and extent follow a parent.
3. `burst` on TRIGGER edges.
4. A per-system `dt` scale, for LOCALTIME.
5. A per-preset warm-up.
6. `Dropped` reporting against a particle-system budget, and against the 8-system volume-glow cap.

**Serves:** Fireflies, Embers, Snow, Rain, Ash, Leaves (with Falling Leaves), Spores (with Floating
Spores), Pollen, Magic Particles, Cosmic Dust, Charge-Up, Discharge (sparks), and Portal (inflow).
Dissolve and Shockwave use it optionally.

## LIGHTMOD: light modulation and a transient pool

**What exists**

- Seven light types. Clustered shading uses a 16×24×8 grid, 32 lights per cluster and
  `kMaxSceneLights = 256` (`src/rendering/light_data.hpp:65-70`).
- Authored lights have `lights/<id>/…` parameters.
- Ecology lights take up to `kMaxEcologyLights = 224` *minus authored lights*
  (`composition.cpp:94`, `:7671-7735`).
- Shadows are limited to `kMaxShadowViews = 8` views in total (`shadow_math.hpp:20-21`).

**Design**

- **(a) Modulate.** A Light-owned effect multiplies the intensity and colour of its owner's record
  at the `Lighting` stage. This happens after parameters apply and before the renderer's
  `updateLights`. Modifications multiply, so Pulse × Flicker compose.
- **(b) Pool.** Entity or World effects request transient point or spot lights from a reserved
  **effect light budget**. The proposal is 16, subtracted from the ecology cap, because ecology
  currently takes everything that is left and a second allocator would fight it.
  - Pool lights are unshadowed by default. A shadowed pool light (the Lightning flash) takes views
    from the 8-view budget only under a quality flag.
  - Requests are ranked by priority, then projected intensity. Losers report `Dropped`.

**Serves:** Glow (spill), Pulse (Light), Flicker, Light Beam, Volumetric Beam, Aura (spill),
Lightning, Arc, Plasma, Discharge, Shockwave (flash), Portal (spill), Bioluminescence (through
ecology aggregation), and Orbit and Spiral when the owner is a light.

## MEDIUM: placed media, and the shadowed march

**What exists**

- The volume march with four placed-media slots (`kMaxMedia = 4`, `atmospherics.hpp:612`).
- Henyey-Greenstein in-scatter from lights with `volumetricStrength`.
- The ADR-570 self-shadow through media.
- Local lights from the froxel grid.
- **What it does not have** is geometry shadowing: "There is no shadowing in the fog"
  (`volume.wgsl:508`).

**What is new.** Bind the directional cascade atlas, and optionally spot maps, to the volume pass.
`inScatterAt` then multiplies by a shadow lookup [R5][R6]. This is one engine change, and it turns on
real God Rays and occluded Volumetric Beams.

**Serves:** Volumetric Beam, God Rays, and Plasma (the large-cloud alternative).

## FXPOST: camera post hooks

**What exists**

- User shader layers at `LayerStage::Post` (HDR ping-pong, `scene_renderer.cpp:3839-3875`).
- The built-in chain (`post_processor.*`) with a lens pass (distortion and CA), bloom, DoF, motion
  blur, the look passes, and sharpen with an **id mask** (its only consumer of `aux-ids`).

**What is new.** Two hook points that run the Camera owner's `PostProcess`-stage effects in stack
order:

- **H1, HDR:** after the temporal stage and before bloom. Covers Radial, Pixelation, Scanlines,
  CA glitch, Toon Screen, Heat Haze, and screen-space God Rays.
- **H2, display-referred:** after tonemap and before the 2D composition pass (ADR-083). Covers
  Dithering and the display-space options of Scanlines and Pixelation.

Each pass gets the standard post inputs: source, depth, linear depth, normal, ids, emission,
velocity, and the per-effect uniform. Masks can be entity id, depth range, or screen region.

**Gate.** No Camera effects means no passes, which is the same encoder-level skip `post.wgsl` §60
uses.

**Serves:** Radial Distortion, Heat Shimmer (Haze), God Rays (Screen), Aura (Screen), Scanlines,
Chromatic Aberration (glitch), Pixelation, Dithering, Toon Edges (Screen), Reality Tear (Camera),
and Color Cycling (routes only).

## TEMPORAL: history ring upgrades

**What exists**

- `TemporalHistory`: up to 32 frames, texture arrays, a Colour channel, and a Motion channel that is
  allocated but never captured (`temporal_history.cpp:378`).
- It is reset on seek.
- One effect, FrameEcho.
- ADR-410's warm-up was **never built**.

**What is new**

1. A **history-id channel**, R16Uint object id at history resolution. It is what makes entity-masked
   temporal effects possible.
2. **Capture stride**, so a 32-frame ring can span 2 s at 15 Hz.
3. The **warm-up**. Each temporal effect declares its *tap set*, meaning which past frames it reads.
   After a seek, the offline tier renders exactly those frames before the target, and the live
   scrub does so under a budget. A freeze needs one frame. An echo needs N.
4. Migrate FrameEcho into the `temporalFilter` type (ADR-441: no alias).

**Serves:** Echo (the migrated FrameEcho), Temporal Smear, Ghosting, Freeze-Frame (Image), Reverse
(Image), Afterimage (Image), and Time-Warp Distortion.

## TRIGGER: deterministic event activation

**What exists**

- `Activation {Always, Window, CameraTravel, HeroFocus}` and `Timing.repeatSeconds`
  (`effect_timing.hpp`).
- `CameraShake.startSeconds` is the precedent.
- The offline `AnalysisTrack` has beats and onsets addressable by time (`analysis_track.hpp`).
- Musical events exist on the bus as `music.drop`, `.impact`, `.build` and so on.

**What is new**

- `Activation::Trigger`, with a source kind:
  - `Onset(threshold)`
  - `Beat(everyN, offset)`
  - `MusicEvent(name)`
  - `TimelineMarker(name)`
  - `Repeat`
  - `EffectEvent(fx id, event)`
  - `Proximity(entity, radius)`
  - `Live(signal)` (live tier only)
- The contract is one pure function: `lastTriggers(t, K) → {t0…}`. It searches backward over the
  analysis track, the marker list or the schedule. Consumers compute `age = t − t0` (or `τ(t) − τ(t0)`
  under LOCALTIME) and keep **no state**.
- `Proximity` triggers are evaluated on the simulation grid and recorded in the checkpointed
  HistoryBank, so they are exact too.
- `Live` triggers scrub like ADR-091's live tier, which is stated in the panel.

**Serves:** Shockwave, Ripple, Bubble (pop), Portal (open), Reality Tear, Lightning, Energy Shield
(hits), Charge-Up, Discharge, Shake, Bounce, Growth, Dissolve, Bioluminescence (flare),
Freeze-Frame, Reverse, and EMIT bursts.

## SIGNALS: entity-derived and effect-published signals

See [parameters-and-modulation.md](parameters-and-modulation.md). The signals are
`entity.<name>.{speed, velocity.x/y/z, acceleration, cameraDistance, screenSize, onScreen,
timeRate}` and `fx.<id>.{age, phase, charge, release}`. The owner alias is `owner.*` in route
sources.

## LOCALTIME

See [time-dilation.md](time-dilation.md). It serves Time Dilation, Freeze-Frame (Pose), Delayed
Motion (Stepped pose), Reverse (skinned pose of track-driven owners), and the EMIT dt scale.

---

## Which effects are merely presets of existing systems

These were verified against the code. "Existing" means no new GPU work: the effect is a registry
type, or a style of one, that writes values an existing system already consumes.

| Effect | Existing system it presets | Verified at |
|---|---|---|
| Fireflies, Embers, Snow, Rain, Ash, Leaves, Spores, Pollen, Magic Particles, Cosmic Dust | `scene::ParticleSystem` (EMIT adds attachment, triggers and dt only) | `src/scene/particles.hpp:123-309`; `examples/weather/*` |
| Ripple (Surface mode) | Ground Pulse `RadialWave` (bucket `Surface`) | `src/world/effects/kinds/ground_pulse_effect.cpp`, `shaders/wave_effects.wgsl` |
| Volumetric Beam (with scene fog) | light `volumetricStrength` in the march | `shaders/volume.wgsl:503-509`, `:621-640` |
| Wind Response (Flex), Sway | ADR-360 mesh wind lanes | `shaders/common.wgsl:268`, `:358-372` |
| Pulsing Veins (height mode) | ADR-376 tree energy | `shaders/common.wgsl:408-465` |
| Bioluminescence (World) | recipe bioluminescence ladder + ecology lights | `src/world/world_composer.cpp:813-835`, `ecology.hpp:240-261` |
| Chromatic Aberration (Camera), Color Cycling (Camera) | `post/lens` CA and `post/hueShift` driven by routes | `shaders/post.wgsl:276-290`, `:345` |
| Echo (Camera) | ADR-410 FrameEcho | `src/scene/temporal_settings.hpp:35-72` |
| Shake (Camera) | ADR-098 camera shake | `src/scene/camera.hpp:104-134` |
| Light Trail | Trail (a preset of a new effect, not of an existing system) | — |
| Sway | Wind Response (a preset of a new effect) | — |

**Honest exceptions.** Stars *looks* like a preset of the skybox star field, but that field is
hard-coded (`shaders/skybox.wgsl:131-141`: fixed density, colour, no twinkle), so it needs a small
Sky-stage uniform block. Fresnel *looks* like a preset of the material-program Fresnel op, but that
op is shared by every user of the material, so the entity form needs FXL.
