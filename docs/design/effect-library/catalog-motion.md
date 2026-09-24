# Catalog: Motion

Part of the [Effect Library](README.md). The master table is in [catalog.md](catalog.md). Tags are
defined in [shared-infrastructure.md](shared-infrastructure.md).

**The family's rule: use real motion.** The engine already has what honest motion effects need:
- **Per-object previous transforms.** `ObjectUniforms::prevModel` (`src/rendering/scene_renderer.hpp:358`)
  is written for every entity every frame (`scene_renderer.cpp:2833-2835`, `3038-3039`). Skinned
  meshes carry `previousPalette` (`src/rendering/skinning.cpp:303-342`).
- **A velocity target.** RG16F, written by pbr, grid, procedural, water, sky, SDF and particles.
- **Motion blur.** A McGuire-style tile-max / neighbour-max reconstruction over that target
  (`shaders/post.wgsl:526-620`) [R7].
- **Measured entity velocity and acceleration.** `EntityState::velocity/acceleration`
  (`src/entity/behavior.hpp:77`, `:129`), a backward difference rebuilt by the seek replay (ADR-700).

What is **missing** is a *multi-sample* history of an entity's transform that effects can read. The
only transform history is a debug overlay for one selected entity (`rendering::TransformHistory`,
240 samples, `src/rendering/transform_history.hpp:72-102`). HIST (see shared-infrastructure) is the
proposed primitive. Trails are ribbons built from HIST [R18]. They are not duplicated meshes.

Procedural motion (Orbit, Spiral, Float, Shake, Bounce) all goes through XFORM. XFORM is a
render-transform offset evaluated as a pure function of t. The camera already follows this pattern:
`CameraShake`, ADR-098, `src/scene/camera.hpp:104-134`, is "a camera-space offset derived from
ordinary parameters", with a `startSeconds` so a decaying shake scrubs.

---

## Trail  (`trail`)

**2.1 Definition.** A persistent streak that the owner leaves behind as it moves. It has two modes:

- **Ribbon.** A camera-facing strip through the owner's past positions. Use it for a comet tail or
  a firefly streak.
- **Blade.** A swept surface between two owner-local anchor points, such as a sword arc or a UFO's
  rim wake.

*Light Trail (Light family) is a preset of this effect.*

**2.2 References.**

- [R18] Niagara ribbon renderer and the Unity Trail Renderer: a strip over sampled positions, with
  width, colour and alpha as functions of age. It is rebuilt every frame from history.
- The engine's own particle ribbons (ADR-040, `trailLength ≤ 32`, `src/scene/particles.hpp:294-300`)
  are per *particle* and GPU-resident, so they cannot follow an entity.
- Rejected: duplicated meshes. That is Afterimage, a different look.

**2.3 Visual anatomy.** A smooth ribbon starts at the owner and follows its path. Width tapers with
age, colour moves along a gradient, and opacity fades. It is brighter at the head. A trail can bloom
if it is emissive.

**2.4 Implementation.**

*Recommended:* RIBBON over HIST.

1. On the CPU, read the owner's HIST samples inside `persistence` seconds. HIST samples on the fixed
   1/60 grid.
2. Catmull-Rom subdivide the samples to ≤128 points.
3. Build 2 vertices per point into a per-frame dynamic vertex buffer: position, age, and a
   side-sign.
4. The vertex stage expands the strip by `normalize(cross(tangent, toCamera))·width(age)`.
   Blade mode instead uses the two transformed anchors, so no expansion is needed.
5. The fragment stage applies gradient × fade and a soft edge across the strip. It writes HDR and
   the emission target, with additive or alpha blending, depth-tested and without depth write.

The head is the owner's *current* transform, and the samples are grid instants. The result is
identical at 30 and 120 fps and between a play and a scrub (see HIST).

*Alternative:* for a particle-borne streak, attach a particle emitter (EMIT) with `trailEnabled`.
Use it for many small streakers, like fireflies. It does not suit a single entity.

*Needs:* HIST and RIBBON.

**2.5 Targets.**

- **Entity:** the primary target.
- **Light:** a moving light.
- **Camera:** no. A camera trail is Temporal Smear.
- **World:** no.

**2.6 Parameters.**

| Parameter | Range |
|---|---|
| `mode` | Ribbon or Blade |
| `persistence` | s |
| `widthStart` | |
| `widthEnd` | |
| `colorStart` | |
| `colorEnd` | |
| `opacity` | |
| `emission` | HDR |
| `blend` | |
| `minSpeed` | the trail fades out when the owner stops |
| `anchorA` | local, Blade mode |
| `anchorB` | local, Blade mode |
| `smoothing` | |
| `noiseWiggle` | |

**2.7 Modulation.** `owner.speed → opacity` is the default route. Also `audio.treble → emission`,
`beat → widthStart`, the timeline and MIDI.

**2.8 Animation model.** Stateless given HIST. HIST is the state, and it is checkpointed with the
entity (see HIST).

**2.9 Compositing.** The trail draws in the blended list after opaque, and `applyFog` fogs it.
Emission blooms. The trail writes camera-only velocity, because its points are static in world
space, so motion blur treats it as scenery. It is not lit and casts no shadow.

**2.10 Stack behaviour.** Geometry (RIBBON). No material or lighting.

**2.11 Performance class.** Low. CPU cost is O(points), vertex cost is small, and fragment cost is
the overdraw of a strip.

**Presets.** Comet Tail, Light Trail (Long Exposure), Blade Arc, UFO Wake (Blade at the rim).

---

## Afterimage  (`afterimage`)

*Merged. The spec lists it under both Motion and Temporal, and it is one type with two modes.*

**2.1 Definition.** Discrete fading copies of the owner at past moments. Compare Trail, which is a
continuous strip.

- **Geometry mode.** The mesh is redrawn at K past transforms.
- **Image mode.** Masked samples of the owner are taken from the frame history.

**2.2 References.** The fighting-game and anime "zanzō" look: Motomura, GDC 2015 [R20], on authored
afterimages in Guilty Gear Xrd. ADR-410 temporal history supplies the image mode.

**2.3 Visual anatomy.** Evenly spaced (in time) translucent silhouettes trail the owner. They are
tinted and grow fainter, and are often flat-shaded or emissive.

**2.4 Implementation.**

*Geometry mode (recommended):* REDRAW the owner K ≤ 8 times at HIST transforms `t − i·interval`.

- Use an unlit tinted pipeline with alpha `fade^i` and a Fresnel edge.
- Blend it and draw back-to-front by age.
- Rigid owners are exact.
- Skinned owners need pose history. A GPU palette ring holds K snapshots × joints × 64 B, captured at
  render. After a seek, that ring is rebuilt only as frames play, so skinned afterimages hold the
  current pose until the ring refills. This is a documented relaxation, the same kind as particle
  pools (ADR-360).
- The exact alternative stores joint local transforms in HIST, which is checkpointed. It is more
  memory, and it is listed in the roadmap as optional.

*Image mode:* TEMPORAL. Sample K history layers and keep the pixels whose *history id* equals the
owner.

- This needs an id channel in the history ring. Today the ring stores only Colour, and its Motion
  channel is allocated but never captured (`temporal_history.cpp:378`).
- It is cheaper for dense or complex meshes, and it works for SDF and procedural owners.

*Needs:* HIST + REDRAW (Geometry mode), or TEMPORAL + a history id channel (Image mode).

**2.5 Targets.** Entity. Camera, as whole-frame Echo, is a separate effect.

**2.6 Parameters.** `mode`, `copies` (1–8), `interval` (s), `fade`, `tint`, `emission`, `fresnel`,
`minSpeed`, `holdOnStop`.

**2.7 Modulation.** `owner.speed → copies` or opacity, `beat → interval` (one copy per beat), and
`music.drop`.

**2.8 Animation model.** Stateless given HIST, or the history ring in Image mode.

**2.9 Compositing.** Geometry mode draws in the blended list and is fogged. It does not write depth
or cast shadows. Image mode is composited before post, so it blooms and is graded.

**2.10 Stack behaviour.** Geometry mode uses REDRAW draws. Image mode is `ScreenSpace`.

**2.11 Performance class.** Medium. Geometry mode costs K × the owner's vertex work. Image mode costs
K history taps.

**Presets.** Speed Ghosts, Beat Echoes, Spectral (cyan, Fresnel).

---

## Motion Smear  (`motionSmear`)

**2.1 Definition.** Stylised geometric smearing: the trailing side of the mesh stretches back along
the path, as in 2D smear frames. It is not photographic motion blur, which already exists in post.

**2.2 References.** Smear frames in limited animation (Motomura, GDC 2015 [R20]). The stretch-to-
previous-position vertex technique is common in stylised games. Squash-and-stretch preserves volume.

**2.3 Visual anatomy.** When the owner moves fast, its back half elongates toward where it was, and
its front stays crisp. It snaps back when the owner stops.

**2.4 Implementation.**

*Recommended:* FXL vertex lane `fxSmear = (v_world.xyz, smearSeconds)`.

- In the vertex stage: `w = saturate(dot(normal_world, −v̂))^sharpness`, then
  `p' = p − v·smearSeconds·w·amount`.
- `v` is the owner's measured world velocity in m/s. It comes from SIGNALS or HIST, not from
  `prevModel`. The one-frame delta is frame-rate dependent, which is the same lesson the engine's
  `cameraVelocityOnTimeline` records (`effect_timing.hpp:142-144`).
- The same displacement must run in the depth prepass, the shadow pass and the `prevClip` path, or
  depth fighting and wrong velocity appear. `meshWindOffset` is the precedent: it is applied in
  `vs_main` for all of those (`common.wgsl:268`, `358-371`).

*Needs:* FXL (vertex lanes) and SIGNALS.

**2.5 Targets.** Entity only.

**2.6 Parameters.** `amount`, `smearSeconds`, `sharpness`, `minSpeed`, `maxStretch` (m), and
`volumePreserve`.

**2.7 Modulation.** `owner.speed` is implicit. `beat → amount`.

**2.8 Animation model.** Stateless.

**2.9 Compositing.** The smear displaces real geometry, so it is lit, shadowed, fogged and depth
correct. It writes velocity correctly when the prevClip path applies the previous frame's smear. To
do that, pack the previous velocity in the lane's spare w, or accept a small error.

**2.10 Stack behaviour.** `Geometry`. It modifies vertex positions and sums with other FXL
displacements.

**2.11 Performance class.** Low. It costs a few vertex ALU instructions, in every pass that draws the
owner.

**Presets.** Cartoon Smear, Speedster, Subtle.

---

## Velocity Distortion  (`velocityDistortion`)

**2.1 Definition.** A refractive *wake* behind a fast owner: space ripples along its recent path.
Space Warp's bow term is around the owner. This effect is along the path.

**2.2 References.** [R1] and [R2]. A ribbon drawn into the distortion buffer is how HDRP and UE
particles make "heat trail" and "shock trail" wakes.

**2.3 Visual anatomy.** A band of refraction follows the path. It is strongest just behind the owner
and relaxes with age, with fine ripples perpendicular to the path.

**2.4 Implementation.**

*Recommended:* build a RIBBON over HIST, as Trail does. Draw it into the **DF offset target** instead
of HDR.

- The offset is along the ribbon's screen-space normal, times `sin(k·acrossCoord)·ageFade`.
- The ribbon is *not* camera-facing. It is expanded in the plane of motion so that it reads as a
  sheet.

*Needs:* HIST, RIBBON and DF.

**2.5 Targets.** Entity.

**2.6 Parameters.** `persistence`, `width`, `strength`, `rippleFrequency`, `chroma`, and `minSpeed`.

**2.7 Modulation.** `owner.speed → strength` is the default route. Also `owner.acceleration`.

**2.8 Animation model.** Stateless given HIST.

**2.9 Compositing.** As DF: after fog and before post.

**2.10 Stack behaviour.** `ScreenSpace`.

**2.11 Performance class.** Medium, because it shares the DF copy.

**Presets.** Jet Wake, Warp Contrail.

---

## Wind Response  (`windResponse`)

**2.1 Definition.** The owner answers the scene's wind field. It has two modes:

- **Flex.** Mesh bending: trunk, branches and foliage flutter.
- **Rigid.** The whole object sways, drifts or leans, as a hanging lantern or a balloon does.

**2.2 References.** [R11] Sousa, GPU Gems 3 ch.16, on layered vegetation bending (main bend + detail
flutter). The engine implements this as ADR-360 mesh wind: `meshWindOffset`, `common.wgsl:268`, with
`windOrigin/windShape/windTune` lanes. The wind is one deterministic analytic field (ADR-055,
`src/core/wind.hpp`, `shaders/wind_field.wgsl:54`).

**2.3 Visual anatomy.** Flex mode bends more with height and flutters at the tips. Rigid mode is a
pendulum-like lean downwind with gust response.

**2.4 Implementation.**

*Flex:* this **already exists** as node parameters that pack into the ADR-360 lanes. It needs
`wind.height > 0`, `scene_renderer.cpp:3082-3096`.

The effect must *become* that authority, or it must not exist. Two writers to the same lanes is the
"two hooks, one question" defect ADR-580 §68 removed. The design therefore:

1. Moves node wind into a `windResponse` effect instance. This is a content migration with no
   compatibility shim (ADR-441). Old scenes are rewritten by the loader's migration, not aliased.
2. Keeps the GPU path untouched.

*Rigid:* XFORM. On the CPU, sample `wind.hpp` at the owner's position, which is pure in t. Feed it
through a damped pendulum whose response is computed analytically from the field's own phase terms,
so no integrated state is needed. Apply a rotation about the pivot plus a drift offset.

*Needs:* the existing wind lanes (Flex), XFORM (Rigid), and `FieldBus` `"wind"`
(`field_bus.hpp:124-150`) for the flow sample.

**2.5 Targets.** Entity. Material is no, because per-material wind is the procedural-vegetation Tier
0 path, which already exists.

**2.6 Parameters.** Flex mode reuses ADR-360's rows: strength, branch, foliage, trunk, flutter, lag.
Rigid mode adds `pivotHeight`, `lean`, `drift`, and `gustResponse`.

**2.7 Modulation.** `audio.rms → strength` makes gusts on loud passages. Also the timeline.

**2.8 Animation model.** Stateless. Flex takes the previous time from `windTune.w` for velocity.

**2.9 Compositing.** Real geometry. It is lit and shadowed, and velocity is correct because the
prevClip path already includes the wind offset (`common.wgsl:370-372`).

**2.10 Stack behaviour.** `Geometry`.

**2.11 Performance class.** Very Low (Flex exists) or Very Low on the CPU (Rigid).

**Presets.** Tree, Grass Clump, Hanging Lantern, Balloon.

---

## Orbit  (`orbit`)

**2.1 Definition.** The owner circles a pivot: its own base position, another entity, or a world
point. It may keep facing its direction of travel or keep facing the pivot.

**2.2 References.** Parametric circular motion. Tilt is a rotation of the orbit plane.

**2.3 Visual anatomy.** A smooth circular or elliptical path with an optional tilt and banking.

**2.4 Implementation.** XFORM, evaluated on the CPU:

`offset = R·(cos θ·u + sin θ·ecc·w)` with `θ = 2π(t/period + phase)`.

The pivot comes through an `EffectEndpoint` (`effect_registry.hpp:541`), which can be the owner's
base or another entity. The orientation can be the tangent or the pivot direction. The offset is
applied to the render transform *after* simulation. See XFORM for why it is visual-only.

**2.5 Targets.** Entity, Light (an orbiting light, which moves the light record through LIGHTMOD),
and Camera (a camera orbit is the existing camera rig: `src/scene/camera_rig.*`, which should be
preferred).

**2.6 Parameters.** `radius`, `eccentricity`, `period`, `phase`, `tilt`, `axis`, `pivot`, `face`,
`bank`.

**2.7 Modulation.** `audio.bass → radius`. `beat` does *not* go to period, because phase must stay a
pure function of t (see Pulse 2.8). Use `beat.phase` as the angle source instead.

**2.8 Animation model.** Stateless.

**2.9 Compositing.** It moves real geometry. Shadows, velocity and motion blur all follow, because
`prevModel` captures the offset transform.

**2.10 Stack behaviour.** `Geometry`. XFORM offsets compose in stack order. Rotations compose
left-to-right, top of the stack first.

**2.11 Performance class.** Very Low.

**Presets.** Moon, Satellite, Firefly Circle, Guardian Orb.

---

## Spiral  (`spiral`)

**2.1 Definition.** A helical path. It is an orbit whose radius and height change with phase,
rising or falling, looping or ping-ponging.

**2.2 References.** Parametric helix and conical spiral.

**2.3 Visual anatomy.** The owner corkscrews up or down around an axis.

**2.4 Implementation.** XFORM: `r(s) = mix(r0, r1, s)` and `h(s) = mix(h0, h1, s)`, with
`s = fract(t/period)` or ping-pong. The angle is `turns·2π·s`.

**2.5 Targets.** Entity, Light.

**2.6 Parameters.** `r0`, `r1`, `h0`, `h1`, `turns`, `period`, `loopMode`, `phase`, `face`.

**2.7 Modulation.** `audio.rms → r1`, the timeline, and `beat.phase`.

**2.8 Animation model.** Stateless.

**2.9 Compositing.** As Orbit.

**2.10 Stack behaviour.** `Geometry` (XFORM).

**2.11 Performance class.** Very Low.

**Presets.** Ascension, Vortex Drift, Seed Fall.

---

## Float  (`float`)

**2.1 Definition.** Gentle buoyant idle motion: a bob plus a slight tilt and yaw drift.

**2.2 References.** A sum of sines at incommensurate frequencies avoids a visible loop. Band-limited
value noise is the alternative.

**2.3 Visual anatomy.** A slow vertical bob with a small wobble, never exactly periodic.

**2.4 Implementation.** XFORM:

`y = A·(sin(ω t + φ) + 0.35 sin(2.31ω t + φ2))`. Tilt comes from similar pairs. The phases are seeded
per instance, so two floaters never sync unless asked.

**2.5 Targets.** Entity, Light.

**2.6 Parameters.** `height`, `period`, `tilt`, `yaw`, `seed`, `sync` (0–1, which pulls the phase
toward the beat).

**2.7 Modulation.** `audio.bass → height`, `beat.phase` through `sync`.

**2.8 Animation model.** Stateless.

**2.9 Compositing.** As Orbit.

**2.10 Stack behaviour.** `Geometry` (XFORM), additive.

**2.11 Performance class.** Very Low.

**Presets.** Buoy, Hovering Craft, Dream Float.

*Not to be confused with `src/scene/floaters.*` (ADR-099 §13).* Those are instances of procedural
geometry carried along a water surface. They share Float's principle, which is a pure function of t
with no integration, but they are not entity motion.

---

## Shake  (`shake`)

**2.1 Definition.** Jittery displacement of an object or a camera, from trauma, impact or a rumble.

**2.2 References.** Squirrel Eiserloh, *Juicing Your Cameras With Math* (GDC 2016): trauma-based
shake with `amplitude ∝ trauma²` and smooth (Perlin-style) noise rather than random per-frame
offsets. The engine already has a camera shake: ADR-098, `src/scene/camera.hpp:104-134`, a finite
envelope with `startSeconds`.

**2.3 Visual anatomy.** A fast, decaying, smooth-noise jitter in translation and rotation.

**2.4 Implementation.**

- **Entity:** XFORM offset = `envelope(t − t0)²·noise3(seed, t·frequency)·amplitude`, plus
  rotation. `t0` comes from TRIGGER.
- **Camera:** do not build a second shake. The Camera-owned Shake effect *is* the ADR-098 camera
  shake, which should migrate to an effect instance with no shim (ADR-441). Until then the effect
  writes `camera/shake/*` through a route.

*Needs:* XFORM and TRIGGER.

**2.5 Targets.** Entity and Camera. Light is also possible, for a swinging lamp's jitter.

**2.6 Parameters.** `amplitude` (m), `rotation` (°), `frequency`, `decay`, `trauma`, `trigger*`,
`seed`.

**2.7 Modulation.** `music.impact` or `audio.onset` as the trigger. Also `audio.bass → trauma` and
`owner.acceleration → trauma`.

**2.8 Animation model.** idle → triggered → decaying → idle, all a function of `t − t0`.

**2.9 Compositing.** As Orbit. Motion blur is automatic.

**2.10 Stack behaviour.** `Geometry` (XFORM).

**2.11 Performance class.** Very Low.

**Presets.** Impact, Rumble (continuous, bass-driven), Nervous.

---

## Bounce  (`bounce`)

**2.1 Definition.** A springy hop or squash-and-stretch response, either continuous (hopping) or
triggered (a beat or impact recoil).

**2.2 References.** The squash-and-stretch animation principle with volume preservation
(`s_xz = 1/√s_y`). The damped spring impulse response is `A·e^{−ζω a}·sin(ω_d a)`, which is analytic
in the age `a`.

**2.3 Visual anatomy.** The owner compresses, launches, stretches in flight, lands and settles.

**2.4 Implementation.**

- **Continuous:** `y = H·|sin(π t/period)|`, with a squash factor derived from the phase.
- **Triggered:** use the spring impulse response with `a = t − t0`.

Both are XFORM, with a translation and a non-uniform scale about the owner's base.

**2.5 Targets.** Entity.

**2.6 Parameters.** `height`, `period`, `squash`, `stretch`, `damping`, `frequency`, `mode`,
`trigger*`.

**2.7 Modulation.** `beat` as the trigger (the default route) and `audio.bass → height`.

**2.8 Animation model.** Continuous mode is stateless. Triggered mode is a function of `t − t0`.

**2.9 Compositing.** As Orbit. The scale changes shadows correctly.

**2.10 Stack behaviour.** `Geometry` (XFORM), which includes scale.

**2.11 Performance class.** Very Low.

**Presets.** Beat Hop, Jelly, Landing Recoil.
