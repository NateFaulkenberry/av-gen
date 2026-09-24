# Catalog: Spatial Distortion

Part of the [Effect Library](README.md). The master table is in [catalog.md](catalog.md). Every
entry uses the spec's headings 2.1 to 2.11. Shared primitives are named by the tags defined in
[shared-infrastructure.md](shared-infrastructure.md): **DF** (distortion field), **SHELL** (proxy
shape renderer), **TRIGGER** (deterministic event activation), **SIGNALS** (entity-derived signals),
**TEMPORAL** (history ring plus id mask), **FXPOST** (camera post hooks), **BOLT** (fractal path
generator), **EMIT** (effect-owned particle emitter). References `[Rn]` are listed in
[README.md#references](README.md#references).

**The family's one rule.** Every distortion effect here is a *producer* for one shared framework,
DF. None of them owns a render pass. DF works like this:

1. Each effect draws a proxy (a sphere, a shell, a disc or a quad) into a screen-space offset
   target, depth-tested against the scene.
2. One resolve pass samples a copy of the HDR scene at `uv + offset`.
3. The resolve rejects any sample whose linear depth is nearer than the distorter's own depth, which
   is Sousa's foreground mask [R1].

A distortion is authored in **world units**, not UV. The engine's water shader already learned this
lesson: an offset added straight to a screen uv means something different at 2 m and at 50 m
(`shaders/water.wgsl:251-259`). DF works the same way. A displacement `d` at point `p` becomes the
screen offset `proj(p + d) - proj(p)`.

---

## Space Warp  (`spaceWarp`)

**2.1 Definition.** There are three readings:
- (a) a lens-like bending of the view around an object;
- (b) an Alcubierre-style bow wave, where space is compressed ahead of a moving object and stretched
  behind it;
- (c) a swirling "magic" warp.

**Chosen:** a single field with three weighted terms, each of which can be switched on or off:
- *radial* (lens pull toward the centre);
- *bow* (compress/stretch along the owner's velocity);
- *swirl* (tangential twist about the view axis).

The terms are modes of one effect, not separate effects.

**2.2 References.**
- [R1] Sousa, GPU Gems 2 ch.19: perturb lookups into a scene copy and mask foreground leaks.
- [R2] Unity HDRP distortion: effects write a distortion-vector buffer, and one full-screen pass
  applies it.
- [R3] UE translucent refraction / Pixel Normal Offset: the offset is in screen space, but authored
  per material.
- Rejected: plain UV noise. It has no centre, no depth relation and no relation to motion.

**2.3 Visual anatomy.**
- A soft-edged ellipsoidal region around the owner, stretched along its velocity.
- Background lines bend toward the owner and bunch up ahead of it.
- A faint chromatic fringe at peak gradient.
- An optional thin emissive rim at the field's edge.
- Nothing in front of the owner is affected.

**2.4 Implementation.**
- **Recommended:** DF producer.
  - The proxy is an ellipsoid centred on the owner's world bounds, with its major axis along the
    owner's velocity. The velocity comes from the entity's measured velocity (`EntityState::velocity`,
    `src/entity/behavior.hpp:77`), or from a finite difference of the render transform for
    track-driven nodes. The CPU computes it and packs it into the instance record.
  - Per fragment, the proxy computes the view ray's closest-approach vector `b` to the centre,
    analytically.
  - `radial = -normalize(b)·k_r·f(|b|/R)`, where `f` is a smooth bump that is 0 at the centre and at
    R, and peaks near 0.4R.
  - `bow = v̂·k_b·(dot(b̂, v̂))·f`: ahead it pushes toward the centre, behind it pulls away.
  - `swirl = rot90(b)·k_s·f`.
  - The sum is a world-space displacement, which is projected into a UV offset.
  - A small animated perturbation (curl-like noise at low amplitude, `shaders/noise.wgsl`) breaks up
    the symmetry.
- **Alternative:** evaluate analytic warps in one full-screen pass over a list of N primitives. This
  has no proxy draw and suits few, large warps. It is rejected as the default because proxies get
  culling, depth testing and scissoring for free.
- **Needs:** a scene-colour copy (DF), the linear-depth target (exists: `aux-linear-depth`, R32F), and
  the owner's velocity (SIGNALS).
- **Stages:** vertex+fragment for the proxy, fragment for the resolve.
- **Resources:** a DF slot, meaning one proxy record (64 B).
- **Performance:** fragment cost is proportional to screen coverage, plus the shared DF copy/resolve.

**2.5 Targets.**
- **Entity:** primary. It follows the transform and reads velocity.
- **World:** a placed warp at a fixed point.
- **Camera / Light / Material:** not sensible. A camera-wide warp is Radial Distortion.

**2.6 Parameters.**

| Parameter | Range |
|---|---|
| `radius` | m |
| `strength` | 0–2 |
| `radialWeight` | 0–1 |
| `bowWeight` | 0–1 |
| `swirl` | −1..1 |
| `velocityStretch` | 0–4, elongation per m/s |
| `speedForFull` | m/s at which velocity terms saturate |
| `noiseAmount` | 0–1 |
| `noiseScale` | |
| `noiseSpeed` | |
| `chroma` | 0–1 |
| `rimEmission` | HDR |
| `rimColor` | |
| `falloff` | 0.5–4 |

**2.7 Modulation.** Default route `owner.speed → strength` (see
[parameters-and-modulation.md](parameters-and-modulation.md)). Other sources:
- `owner.acceleration → chroma`;
- `audio.bass → swirl`;
- `beat.pulse → strength`;
- a timeline curve;
- MIDI.

**2.8 Animation model.** Stateless. It is a pure function of t, the owner's transform and the
owner's velocity. The velocity is measured on the fixed step grid and rebuilt by the seek replay
(ADR-700), so it scrubs exactly.

**2.9 Compositing.**
- The resolve runs after the volumetric composite, so fog and media behind the owner bend too.
- It runs before the post chain, so bloom, DoF, grading and tonemap act on the warped image.
- Opaque and water surfaces behind the proxy distort.
- Particles do not write depth, so a firefly *in front of* the warp is still bent. This is a known DF
  limitation (see shared-infrastructure).
- The rim emission is added in the resolve into HDR and into the emission target, so selective bloom
  sees it.
- No shadows and no lighting.

**2.10 Stack behaviour.**
- Stage: `ScreenSpace`.
- No geometry, no material and no lighting change.
- Contributes to the DF intermediate targets.
- Several warps add their offsets, which is the thin-lens superposition.

**2.11 Performance class.** Medium: fragment and bandwidth (the DF copy is shared by all
distortions).

**Presets.**
- **UFO Warp:** bow 0.8, radial 0.3, chroma 0.2, speed-driven.
- **Gravitational Warp:** radial only, no rim.
- **Magical Warp:** swirl 0.7, noise 0.5, violet rim.
- **Portal Warp:** swirl 1.0, strong rim, radial 0.5.

---

## Gravitational Lens  (`gravLens`)

**2.1 Definition.**
- (a) Physically motivated point-mass lensing, with an Einstein ring and a secondary image.
- (b) A generic "bulge" magnifier.

**Chosen:** (a). A bulge is Space Warp's radial term. There are two modes:
- **Lens:** the mass is transparent.
- **Black Hole:** it adds an opaque event-horizon disc and an optional photon-ring glow.

**2.2 References.**
- [R17] The thin-lens equation for a point mass, `β = θ − θ_E²/θ`. This is the mapping *Interstellar*
  used to visualise lensing (James et al. 2015). In real time it is one lookup per pixel with no ray
  march.
- [R1] The scene-copy lookup.

**2.3 Visual anatomy.**
- Background stars and geometry are stretched tangentially into arcs near the Einstein radius θ_E.
- There is a mirrored inner image.
- In Black Hole mode, a black disc sits at ~0.4 θ_E (a stylised shadow radius) with a thin bright
  photon ring.

**2.4 Implementation.**
- **Recommended:** DF producer with a *remap* rather than an additive offset.
  - The proxy is a camera-facing disc of angular radius ~3θ_E.
  - The fragment computes θ as the pixel's angular offset from the projected centre.
  - It computes `β = θ(1 − θ_E²/|θ|²)` and sets `offset = proj(β) − uv`.
  - If `uv+offset` leaves the screen, or lands on the far plane, the resolve samples the environment
    (skybox) along the bent direction instead. The sky is the most visible thing to lens, and
    screen-space data cannot supply what is off-screen.
- **Alternative:** ray-march bent rays through a Schwarzschild metric. Rejected: it is Very High cost
  and has no artistic gain at these sizes.
- **Needs:** DF, plus access to the environment cubemap in the resolve. The IBL bind group exists
  (`iblBindGroup_`, `scene_renderer.cpp:3645`).
- **Resources:** a DF slot.

**2.5 Targets.**
- **World:** a placed mass.
- **Entity:** a black-hole object.
- **Camera / Light:** no.

**2.6 Parameters.**
- `einsteinRadius` (degrees, or m at the lens distance);
- `mode` (Lens / Black Hole);
- `horizonScale`;
- `photonRing` (HDR);
- `ringColor`;
- `falloffRadius` (the multiple of θ_E at which the effect is feathered to zero);
- `chroma`.

**2.7 Modulation.**
- `audio.bass → einsteinRadius` (the lens "breathes");
- `beat.pulse → photonRing`;
- timeline;
- MIDI.

**2.8 Animation model.** Stateless. Rotation and flow of an accretion disc belong to a separate Medium
effect (Vortex already exists, ADR-387).

**2.9 Compositing.**
- Same slot as Space Warp.
- Objects nearer than the lens are rejected by the depth test.
- The black disc is written in the resolve before post, so bloom from the photon ring behaves.
- The Vortex medium composited before the DF resolve is lensed too, which is correct.

**2.10 Stack behaviour.** `ScreenSpace`. It is a remap, so when it stacks with additive warps its
offset is added last. The remap is order-sensitive, and DF orders by priority.

**2.11 Performance class.** Medium: fragment plus bandwidth. An off-screen environment sample adds
one cubemap fetch.

**Presets.**
- **Black Hole:** Black Hole mode with a photon ring.
- **Subtle Mass:** a small θ_E, Lens mode.
- **Einstein Ring:** a large θ_E, a feathered edge, no disc.

---

## Heat Shimmer  (`heatShimmer`)

**2.1 Definition.** Refraction through turbulent hot air. There are two modes:
- **Volume:** a box or cylinder above a source (exhaust, fire, lava).
- **Ground Haze:** camera-wide mirage shimmer that grows with distance near the ground.

**2.2 References.**
- [R29] GPU Gems 1 ch.6, "Fire in the Vulcan Demo": heat haze by perturbing a scene copy with
  scrolling noise.
- [R1] Sousa: the heat-haze case explicitly. Mask foreground.
- UE heat-distortion particles: a normal-map-driven refraction offset.

**2.3 Visual anatomy.**
- High-frequency, low-amplitude wobble that rises upward.
- The wobble is stronger through a thicker column of hot air.
- It fades near the column's edges and with height.
- It is achromatic.

**2.4 Implementation.**
- **Recommended:** DF producer.
  - The proxy is the column's box or cylinder, drawn back-faces-last so both entry and exit are
    known. Thickness comes from front and back depth, or analytically for a cylinder.
  - The offset is the gradient of 3D noise (`noise.wgsl`) sampled at `p + up·speed·t`, times
    `thickness/refThickness`, times the height falloff.
  - It is computed in world space and projected, so distant shimmer shrinks correctly.
- **Ground Haze** is an FXPOST pass, not a proxy:
  - `offset = noiseGrad(uv·scale, t)·smoothstep(near, far, linearDepth)·horizonMask`.
  - It is Camera-owned.
- **Needs:** DF (Volume) or FXPOST (Haze), plus linear depth.

**2.5 Targets.**
- **Entity:** the column rides the owner, e.g. a jet exhaust.
- **World:** a placed column.
- **Camera:** Ground Haze.
- **Light:** could attach to a fire light, though that is really Entity or World.

**2.6 Parameters.**
- `strength`;
- `scale` (m);
- `riseSpeed` (m/s);
- `height`;
- `radius`;
- `edgeSoftness`;
- `thicknessRef`;
- Ground Haze only: `nearDistance`, `farDistance`, `horizonBand`.

**2.7 Modulation.**
- `owner.speed` (an exhaust answers throttle);
- `audio.rms`;
- timeline;
- `random.*` for gusts.

**2.8 Animation model.** Stateless: noise time is the transport second.

**2.9 Compositing.** After fog, so the haze bends fog, which is correct. No emission. Bloom follows
from the resolved image.

**2.10 Stack behaviour.** `ScreenSpace` (Volume) or `PostProcess` (Haze). Additive offsets.

**2.11 Performance class.** Low–Medium: fragment. The shared DF copy dominates.

**Presets.**
- **Engine Exhaust:** Entity, narrow, fast.
- **Campfire:** World column.
- **Desert Haze:** Camera, Ground Haze mode.

---

## Shockwave  (`shockwave`)

*Merged: the spec lists Shockwave under both Distortion and Energy. It is one type. The Energy
reading is the "Energy Blast" preset, which has a strong emissive rim and a flash light.*

**2.1 Definition.** An expanding pressure or energy front released by an event.
- (a) A spherical 3D front seen as a refracting ring.
- (b) A ground ring. This is the existing Ground Pulse (a surface wave, ADR-207/702).
- (c) A full-screen ring (2D).

**Chosen:** (a), with sub-effects coupled through presets and routes:
- the ground ring via Ground Pulse sharing the trigger;
- a flash via LIGHTMOD;
- a dust ring via EMIT.

**2.2 References.**
- [R1] / [R2] Screen-space refraction band.
- Common game practice (UE "shockwave" materials on an expanding sphere mesh, sampling scene colour
  with a radial offset band). The front is a *band*, not a disc, and its amplitude decays with radius
  (energy spread over 4πR²).
- Rejected: a 2D screen-space ring centred on a projected point. It ignores depth, so a wave behind a
  wall shows through.

**2.3 Visual anatomy.**
- Idle.
- The trigger: an optional flash.
- A thin compressive refraction band that expands, with an optional emissive leading edge and an
  optional dust ring on the ground.
- The band thins and weakens with radius.
- Fade.
- Complete.

**2.4 Implementation.**
- **Recommended:** DF producer.
  - The proxy is a sphere of radius `R(age)`, where `R = maxRadius·ease(age/duration)` and the ease
    is an ease-out.
  - For each fragment, the view ray's closest approach `b` gives the projected ring coordinate.
  - `offset = b̂·A(age,R)·band((|b|−R)/thickness)`, where `band` is a signed derivative-of-Gaussian:
    compression on the outside, rarefaction on the inside.
  - Emission is `rimColor·rimEmission·gaussian(...)`, added into DF aux.
- **Trigger:** TRIGGER gives `t0 = latest trigger ≤ t`.
  - Up to `maxConcurrent` (≤4) fronts per instance are evaluated from the last K trigger times still
    inside `duration`, all a pure function of t.
- **Needs:** DF, TRIGGER, and optionally LIGHTMOD (the flash light) and EMIT.
- **Resources:** one DF slot per live front.

**2.5 Targets.**
- **Entity:** emanates from the owner.
- **World:** a placed point.
- **Light:** the flash is a light, but the front is not owned by one.
- **Camera:** a "camera shockwave" is Radial Distortion.

**2.6 Parameters.**
- `maxRadius` (m);
- `duration` (s);
- `ease` (choice);
- `thickness` (m);
- `strength`;
- `radiusDecay` (0–2, the exponent on 1/R);
- `rimEmission` (HDR);
- `rimColor`;
- `chroma`;
- `flashIntensity`;
- `flashRadius`;
- `dustAmount`;
- `triggerSource` (choice: beat / onset / music event / marker / manual / repeat);
- `triggerThreshold`;
- `maxConcurrent`;
- `cooldown`.

**2.7 Modulation.**
- Triggers: `audio.onset`, `music.drop`, `music.impact`, `beat` (from the offline analysis track, so
  they are deterministic).
- Continuous: `audio.rms → strength`, `owner.speed → maxRadius`, timeline, MIDI (live tier).

**2.8 Animation model.** idle → trigger(t0) → expand (age < duration·0.8) → fade → complete. All of it
is derived from `age = t − t0`. There is no stored state. See TRIGGER.

**2.9 Compositing.**
- Depth-tested proxy.
- The rim emission feeds bloom through the emission target.
- The flash is a real transient point light (LIGHTMOD), so it lights surfaces and fog and casts
  shadows only if the pool light is flagged to.
- The dust ring is ordinary particles.

**2.10 Stack behaviour.**
- `ScreenSpace` (the front).
- `Lighting` (the flash).
- `Particles` (dust).
- One type evaluated at three stages. Its schema declares its primary stage, and its sub-records
  carry their own.

**2.11 Performance class.** Medium: fragment and bandwidth. The flash light and dust are Low each.

**Presets.**
- **Explosion:** emissive, flash, dust.
- **Sonic Boom:** refraction only.
- **Energy Blast:** cyan rim, strong flash, chroma.
- **Bass Drop:** `music.drop` trigger, large, subtle.

---

## Ripple  (`ripple`)

**2.1 Definition.** A damped *train* of concentric waves in a surface or plane. Compare Shockwave,
which is a single 3D front. There are two modes:
- **Membrane:** an invisible plane or disc in the air, e.g. a force wall that has been touched.
- **Surface:** on scene surfaces. This is the existing Ground Pulse `RadialWave` (a Material-stage
  surface term), exposed as a preset.

**2.2 References.**
- [R2] The distortion buffer.
- The damped radial wave `A·sin(k(r−ct))·e^{−r/λ}·e^{−age/τ}` (classic water-drop synthesis).
- ADR-207's wave term in `shaders/wave_effects.wgsl`.

**2.3 Visual anatomy.** Concentric bright and dark refraction bands that grow outward from a point
and lose amplitude with radius and age. There are optional faint emissive crests.

**2.4 Implementation.**
- **Membrane:** DF producer. A disc proxy is oriented by the owner. The offset lies in the disc's
  plane along the radius, projected.
- **Surface:** the existing `GroundPulse` machinery (bucket `Surface`, `kMaxGpuWaves = 8`,
  `src/world/wave_effect.hpp:59`). This needs no new code, only a preset.
- **Needs:** DF (Membrane) and TRIGGER.

**2.5 Targets.**
- **Entity:** a touched shield or a water orb.
- **World:** a placed membrane.
- **Material:** Surface mode applies to every lit surface near the source. This is the existing
  behaviour.

**2.6 Parameters.**
- `wavelength`;
- `speed`;
- `amplitude`;
- `radialDecay`;
- `ageDecay`;
- `rings`;
- `radius`;
- `crestEmission`;
- `trigger*` (as Shockwave).

**2.7 Modulation.** `beat` or `onset` triggers; `audio.treble → amplitude`; timeline.

**2.8 Animation model.** Triggered or repeating (`Timing::repeatSeconds`). It is a pure function of
age.

**2.9 Compositing.** As Space Warp. Surface mode shades inside the lit pass, so it is fogged and
bloomed like any surface.

**2.10 Stack behaviour.** Membrane is `ScreenSpace`. Surface is `Material` (bucket `Surface`).

**2.11 Performance class.** Low (Surface) or Medium (Membrane).

**Presets.**
- **Water Drop:** Surface.
- **Membrane Touch:** Membrane, triggered.
- **Beat Ripple:** Surface, `beat.pulse`.

---

## Bubble  (`bubble`)

**2.1 Definition.** A thin refracting and reflecting spherical shell. It is one of:
- a soap bubble;
- a protective orb;
- a water orb.

This is not a distortion *region*: it is a surface with an edge-only lensing term.

**2.2 References.**
- Thin-film interference: Belcour & Barla 2017, "A Practical Extension to Microfacet Theory for the
  Modeling of Varying Iridescence". Colour is a function of film thickness × cos θ.
- Fresnel (Schlick; `fresnelSchlick` exists at `shaders/pbr_shade.wgsl:72`).
- [R1] Shell-edge refraction.

**2.3 Visual anatomy.**
- Nearly invisible at the centre.
- Bright iridescent swirling bands and an environment reflection toward the rim.
- A slight lensing of the background at the rim.
- A slow wobble (low-order shape oscillation).

**2.4 Implementation.**
- **Recommended:** SHELL sphere proxy with a bubble material, plus a DF rim term.
  - Film thickness is `base + noise(p, t)·variation`, flowing downward (gravity drainage).
  - Colour comes from a small analytic interference fit in 3 wavelengths.
  - Reflection samples the environment cubemap.
  - Alpha is Fresnel.
  - The DF rim offset is `∝ (1−N·V)^k`.
  - The wobble is done in the vertex stage (displacement of a UV sphere), with the same displacement
    in the depth pass, which is not needed because the shell is blended and does not write depth.
- **Needs:** SHELL, DF, and the IBL cubemap.

**2.5 Targets.**
- **Entity:** encases the owner, sized to its bounds.
- **World:** placed.

**2.6 Parameters.**
- `radius`;
- `thickness`;
- `thicknessVariation`;
- `iridescence`;
- `reflectivity`;
- `refraction`;
- `wobbleAmount`;
- `wobbleSpeed`;
- `tint`;
- `popTrigger`.

**2.7 Modulation.** `audio.mid → wobbleAmount`; `beat → iridescence` flow speed; the `owner.speed`
trigger pops the bubble.

**2.8 Animation model.** Stateless. The pop is TRIGGER-driven: `age` drives a dissolve of the shell,
reusing Dissolve's noise-threshold term, plus a droplet EMIT burst.

**2.9 Compositing.**
- The shell is blended and drawn after opaque, with no depth write.
- It is fogged by `applyFog`, as blended surfaces already are.
- Reflection highlights bloom.
- It does not cast shadows, which is correct for a thin film.

**2.10 Stack behaviour.** The shell is `Material`-like, but it is SHELL geometry drawn in the blended
list. The rim is `ScreenSpace`.

**2.11 Performance class.** Medium: fragment.

**Presets.** Soap Bubble; Protective Bubble (tinted, thicker, hit ripples through Energy Shield's
term); Water Orb (strong refraction, blue tint).

---

## Time-Warp Distortion  (`timeWarp`)

**2.1 Definition.** The *visual* of a region where time runs differently. What is seen through it
lags, stutters or echoes. It is the picture half of Time Dilation, which is the simulation half; see
[time-dilation.md](time-dilation.md).

**2.2 References.**
- ADR-410's temporal ring. A cache of pure-function frames, FIR only.
- [R9] Karis 2014, for history reprojection and the ghosting it produces when it is *not* clamped.
  Here the ghosting is the look.

**2.3 Visual anatomy.**
- A soft region that shows a delayed or echoed version of the scene behind it.
- Concentric "time rings" of slight swirl.
- A chromatic edge.
- Inside, moving things smear and lag.

**2.4 Implementation.**
- **Recommended:** DF producer with a *history tap*.
  - The resolve samples TEMPORAL history frame `k = round(delay·weight)` instead of the current copy,
    wherever the proxy's weight is above zero.
  - It blends in 2–3 taps for the echo.
  - The offset adds swirl.
- This needs the ring bound to the DF resolve. It exists (`temporal_history.hpp`, 32 frames max,
  half resolution by default). The ring is captured **before** post and after the DF resolve in
  frame order, so the resolve reads history from previous frames only. It is FIR and legal under
  ADR-410.
- **Alternative:** a masked Echo in FXPOST (screen-space region). Rejected: it has no depth relation.

**2.5 Targets.** Entity (around a time mage or clock), World, Camera (full-screen "time slip", which
is Echo's FXPOST mode).

**2.6 Parameters.** `radius`, `delayFrames` (≤ ring length), `echoTaps` (1–3), `echoDecay`, `swirl`,
`chroma`, `edgeGlow`.

**2.7 Modulation.** `owner.localTimeRate → delayFrames`: the owner's time dilation drives its visual
(SIGNALS). Also `beat.pulse` and timeline.

**2.8 Animation model.** Stateless per frame. The history is a pure cache that is reset on seek.
Immediately after a seek the ring is short, and the effect clamps `k` to `framesValid`
(`temporal_history.cpp:346-366`), so it grows back in over `delayFrames`. This is documented as the
ADR-410 "settling" behaviour, since no temporal warm-up exists.

**2.9 Compositing.** The history is pre-post radiance, so the delayed image is re-bloomed and graded
consistently. The history holds no DF output from earlier frames, because capture happens after
resolve. Warps are therefore visible in history, which is intended.

**2.10 Stack behaviour.** `ScreenSpace`. Reads TEMPORAL.

**2.11 Performance class.** High: bandwidth. It samples history array layers and requires the ring
to be allocated at N frames.

**Presets.** Slow Zone, Stutter Field (k jumps in steps), Rewind Bubble (k sweeps).

---

## Portal Distortion  (`portal`)

**2.1 Definition.** An oriented opening: a disc or ellipse with a rim, a distorted boundary and an
interior. The interior modes are:
- **Procedural:** a nebula or void, parallax-mapped.
- **Environment:** an environment cubemap seen through the opening, with parallax.
- **See-Through:** the scene, swirled.
- **Remote View:** a second camera's render.

**2.2 References.**
- Portal rendering in games uses stencil and a recursive camera (Valve, *Portal*). WebGPU supports
  stencil, but this engine uses none, and a recursive pass is a second scene render.
- [R1] / [R2] for the boundary.
- Parallax interior mapping: van Dongen 2008, "Interior Mapping". A fake volume behind a plane is
  one ray-box intersection per fragment.

**2.3 Visual anatomy.**
- A noisy, swirling, emissive rim ring.
- A band of strong swirl distortion just outside the rim.
- An interior with depth: parallax and a darker falloff toward the rim.
- Particles drawn in toward the rim.
- Open and close animation.

**2.4 Implementation.**
- **Recommended:** SHELL disc proxy plus DF.
  - The disc fragment shades the interior (Procedural / Environment / See-Through) and writes depth,
    so objects behind the portal plane are hidden by the interior and objects passing through
    intersect it naturally.
  - Emission is written to the emission target.
  - The rim is an analytic ring SDF in disc space: `noise(angle, t)`, animated.
  - The DF producer adds swirl in an annulus.
  - EMIT is an inward spiral preset using the particle attractor and `orbit`
    (`src/scene/particles.hpp:144-152`).
- **Remote View** is a later wave. It is a second, reduced-resolution scene render into a texture.
  That means `SceneRenderer::render` must be callable for a secondary view into an offscreen target,
  which is available because `renderSubmitted`/`renderToImage` exist but have not been tried per
  frame. It is Very High cost.
- **Needs:** SHELL, DF and EMIT. Remote View also needs a secondary view.

**2.5 Targets.**
- **World:** a placed gate.
- **Entity:** a portal carried by an entity, or opened by it.
- **Camera:** no.

**2.6 Parameters.**
- `radius`;
- `aspect`;
- `open` (0–1);
- `rimWidth`;
- `rimEmission`;
- `rimColor`;
- `rimNoise`;
- `swirl`;
- `swirlBand`;
- `interiorMode`;
- `interiorColor`;
- `interiorDepth` (the parallax depth);
- `particleRate`.

**2.7 Modulation.** `audio.bass → rimEmission`; `beat → swirl`; timeline `open`; `owner.speed`.

**2.8 Animation model.** `open` is a parameter keyed on the timeline, or driven by a TRIGGER envelope
(opening → open → closing). Stateless.

**2.9 Compositing.**
- The interior writes depth and occludes.
- The rim blooms.
- Fog is applied to the disc like any surface.
- Portal light spill uses LIGHTMOD, as an area-like point light at the disc.

**2.10 Stack behaviour.**
- The geometry is SHELL (the Material pass).
- `ScreenSpace` (swirl).
- `Particles`.
- `Lighting` (spill).

**2.11 Performance class.** Medium. Remote View is Very High: an extra scene pass.

**Presets.**
- **Magic Portal:** Procedural, violet.
- **Sci-Fi Gate:** Environment, cyan hard rim.
- **Rift to Space:** Procedural starfield, black rim.

---

## Reality Tear  (`realityTear`)

**2.1 Definition.** A jagged crack in space. The image is sheared apart along a fractal line, with
glowing edges and a void or other-world interior. Compare Portal, which is a round, controlled
opening.

**2.2 References.**
- BOLT, a seeded fractal polyline (midpoint displacement, [R13]), used as the crack outline.
- A 2D SDF of a polyline, for the edge band.
- [R1] for the shear.
- Glitch aesthetics: RGB split, row displacement.

**2.3 Visual anatomy.**
- A thin, jagged slit that widens as it opens.
- White-hot edges falling off to colour.
- The two sides of the image displaced apart perpendicular to the crack.
- Chromatic split near the crack.
- An interior void, as in the portal interior modes.
- Occasional flicker.
- Shard particles.

**2.4 Implementation.**
- **Recommended:** SHELL quad proxy in the tear's plane, with the crack as a polyline SDF.
  - The polyline is ≤32 points, generated on the CPU from a seed by BOLT and uploaded per instance.
  - Inside `|sdf| < width(open)`, shade the interior.
  - At the edge band, add emission.
  - The DF producer adds `offset = sideSign·n̂·shear·profile(sdf)` plus chroma.
  - Flicker is `hash(floor(t·rate))`.
- **Needs:** BOLT (polyline), SHELL, DF and EMIT.

**2.5 Targets.** World (placed), Entity (tears around a character), Camera (a full-screen tear
transition, done as an FXPOST variant using the same SDF in screen space).

**2.6 Parameters.** `length`, `open`, `jaggedness`, `branches`, `seed`, `edgeEmission`, `edgeColor`,
`shear`, `chroma`, `flicker`, `interiorMode`, `shardRate`.

**2.7 Modulation.** `music.drop` trigger opens it; `audio.onset → flicker`; timeline `open`.

**2.8 Animation model.** open/close envelope from TRIGGER or timeline. The seed changes per trigger
index (`seed + triggerIndex`), so every tear differs but replays identically.

**2.9 Compositing.** As Portal. The edge emission blooms strongly (HDR > 10 recommended).

**2.10 Stack behaviour.** Material (SHELL quad), `ScreenSpace` (shear) and `Particles`.

**2.11 Performance class.** Medium: fragment. The CPU polyline cost is Very Low.

**Presets.** Glitch Tear (RGB-heavy), Void Rift (black interior, violet edge), Crystal Crack (white
edge, no interior, strong shear).

---

## Radial Distortion  (`radialDistortion`)

**2.1 Definition.** A camera or screen effect: barrel or pinch about a centre, and radial (zoom) blur.
The centre can be the screen centre or the projected position of an entity.

**2.2 References.**
- Brown–Conrady radial lens model. The engine already implements a one-term version:
  `shaders/post.wgsl:269-290` `distort()`, with `post/lens/distortion` about the screen centre.
- Zoom blur: N taps along `uv − centre`.
- [R4] shares the radial-blur kernel with screen-space god rays.

**2.3 Visual anatomy.** The image bulges or pinches around a point. With zoom blur, streaks radiate
from the point. A "punch-in" is a brief pulse of both.

**2.4 Implementation.**
- **Recommended:** FXPOST pass (`PostProcess` stage), Camera-owned.
  - The centre is a screen point, or an entity's projected centre (the CPU projects the owner or
    target entity).
  - `k·r²` barrel/pinch, plus optional N-tap (8–16) zoom blur weighted by distance from the centre.
- The global `post/lens/distortion` stays the lens *property*. This is the *effect*, and it
  composes after it.
- **Needs:** FXPOST only.

**2.5 Targets.** Camera. A World or Entity owner would only supply the centre. That is modelled as a
Camera-owned effect with a "centre on entity" endpoint, using the existing `EffectEndpoint` source
picker (`effect_registry.hpp:541`).

**2.6 Parameters.** `amount` (−1..1), `centerMode` (Screen / Entity), `center` (uv), `entity`,
`zoomBlur` (0–1), `blurTaps`, `falloff`, `chroma`.

**2.7 Modulation.** `beat.pulse → amount` (the punch-in); `music.drop → zoomBlur`;
`camera.speed → zoomBlur` (warp-speed, SIGNALS).

**2.8 Animation model.** Stateless.

**2.9 Compositing.** Post. Runs before bloom, so blurred highlights bloom, or after, as the "lens"
position in the chain. The recommendation is before bloom, alongside the existing lens pass.

**2.10 Stack behaviour.** `PostProcess`. Several Camera effects run in stack order within FXPOST.

**2.11 Performance class.** Low. Medium with 16-tap zoom blur.

**Presets.** Beat Punch, Warp Speed (zoom blur on camera speed), Fisheye.
