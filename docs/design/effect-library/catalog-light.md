# Catalog: Light

Part of the [Effect Library](README.md). The master table is in [catalog.md](catalog.md). Tags such
as FXL, LIGHTMOD, SHELL, REDRAW, MEDIUM, HIST, RIBBON and FXPOST are defined in
[shared-infrastructure.md](shared-infrastructure.md).

## The family decision: what each light effect actually touches

The spec asks, for each light effect, which of five things it does. The answer is:

| Effect | Real light | Emissive geometry | Volumetric light | Feeds bloom | Post |
|---|---|---|---|---|---|
| Glow | optional spill light | **yes** (owner's emission via FXL) | – | yes (emission target) | – |
| Pulse | **yes** on a Light owner | **yes** on an Entity owner | – | yes | – |
| Flicker | **yes** (primary) | yes on an Entity owner | follows the light | yes | – |
| Bloom Source | – | – | – | **yes, only this** | – |
| Light Beam | **yes** (a spot) | **yes** (a cone shell) | – (faked by the shell) | yes | – |
| Volumetric Beam | **yes** (a spot) | – | **yes** (the march) | yes | – |
| God Rays | uses the key light | – | **yes** (the march, shadowed) | yes | fallback mode |
| Aura | optional | **yes** (an inflated hull) | – | yes | alt mode |
| Halo | – | **yes** (a billboard or ring) | – | yes | – |
| Light Trail | – | **yes** (a ribbon) | – | yes | – |

Every emissive path writes **both** the HDR colour and the emission aux target (`aux-emission`,
RGBA16F, alpha = bloom weight). That is the rule `pbr_shade.wgsl` follows
(`result.emission = emissive + rim + fx.radiance`, around lines 344, 402 and 513). Selective bloom
(`post/bloom/emissionWeight`, ADR-039) then treats every light effect as a light source rather than
as a bright surface. No light effect writes to the HDR buffer without also writing to the emission
target.

References used across the family:
- [R10] GPU Gems 1 ch.21, *Real-Time Glow*: glow sources go into a separate buffer that is blurred.
  This is the ancestor of the engine's emission target.
- [R4] GPU Gems 3 ch.13.
- [R5] / [R6] Wronski 2014 and Hillaire 2015 for shadowed in-scattering.
- [R16] / [R25] Depth fade, for beam and halo intersections.

---

## Glow  (`glow`)

**2.1 Definition.** The owner emits light from its surface. The two readings are:
- (a) a uniform emissive boost;
- (b) an emissive boost with a view-dependent rim.

**Chosen:** (a) plus an optional rim term. A companion *spill* light is optional, so a glowing object
also lights its neighbours.

**2.2 References.** [R10]; UE emissive materials with "Use Emissive for Static Lighting" (spill as a
separate concern); the engine's own Fresnel rim (`pbr_shade.wgsl:504`).

**2.3 Visual anatomy.**
- The surface self-illuminates in the tint colour.
- The rim is brighter at grazing angles (optional).
- There is a bloom halo from the emission target.
- An optional pool of light falls on nearby surfaces.

**2.4 Implementation.**
- **Recommended:** FXL (per-object effect lanes).
  - Lane `fxEmission = (tint.rgb, gain)`.
  - `emissive_out = material_emissive·(1 + gain·gainMix) + tint·gain·add`.
  - Lane `fxRim = (color.rgb, power)` adds `pow(1−N·V, power)`.
  - The result goes to HDR and to the emission target, and `bloomWeight` is raised to at least
    `bloomBoost`.
- **Spill:** LIGHTMOD allocates a pool point light at the owner's centre, with intensity
  `gain·spill` and the tint colour, no shadow and range ≈ 3× the owner's radius.
- **Alternative:** a material-program Emission op. Rejected: a program is shared by every user of
  the material (`src/scene/material_params.hpp:3-5`), so it cannot make one UFO of three glow.
- **Needs:** FXL (object lanes) and LIGHTMOD. **Stages:** the lit fragment stage.

**2.5 Targets.**
- **Entity:** primary.
- **Material:** "every user of this material glows" is already a material-program Emission op, so no
  effect is needed.
- **Light:** Glow on a light is its intensity, which is Pulse or Flicker.
- **World / Camera:** no.

**2.6 Parameters.** `gain` (0–50, logarithmic), `tint`, `mix` (tint vs own emission), `rim`,
`rimPower`, `bloomBoost`, `spill`, `spillRange`.

**2.7 Modulation.**
- `audio.bass → gain`;
- `beat.pulse → gain` (the default route);
- `owner.speed → rim`;
- `owner.cameraDistance → bloomBoost`;
- timeline;
- MIDI.

**2.8 Animation model.** Stateless.

**2.9 Compositing.**
- It is added before `applyFog`, so it is fogged like the surface. Glow through fog dims, which is
  correct.
- It passes through the emission target to selective bloom.
- Spill lights surfaces through the clustered path, and the volume through `localInScatterAt` when
  its volumetric strength is above 0.
- It does not change shadows.

**2.10 Stack behaviour.** `Material` stage. It modifies material output and lights (spill →
`Lighting`). Several Glows on one owner **sum** their gains and colour-average their tints (the FXL
composition rule).

**2.11 Performance class.** Very Low. The spill costs one clustered light.

**Presets.** Soft Glow, Neon (strong rim), Radioactive (green, spill), Bioluminescent (low gain,
cyan, pulse route).

---

## Pulse  (`pulse`)

**2.1 Definition.** A periodic intensity waveform. The two readings are:
- (a) a whole-object scalar;
- (b) a *travelling* pulse (a band moving across the object).

Both are kept as modes:
- **Scalar:** CPU, any owner.
- **Travelling:** GPU. This generalises tree energy (ADR-376), which is a band moving up the body by
  normalised height (`common.wgsl:408-465`).

The waveforms are Sine, Triangle, Saw, Square (soft), Heartbeat (a double bump) and Beat-locked.

**2.2 References.** ADR-376's `treeEnergyAt`; the LFO sources already on the signal bus (`lfo.<n>`,
`src/signals/source.cpp`). A Pulse is a *targeted* LFO that belongs to its owner.

**2.3 Visual anatomy.** Emission, or light intensity, swells and falls. In Travelling mode a bright
band moves along an axis of the owner.

**2.4 Implementation.**
- **Scalar mode:** the CPU evaluates `w(t)` and multiplies either:
  - the owner's FXL emission gain (Entity), or
  - the light's intensity through LIGHTMOD (Light).
- **Travelling mode:** lane `fxPulse = (speed, width, axisMode, phase)`. The shader evaluates
  `band(dot(p−origin, axis)/extent − speed·t)`.
  - This reuses the tree-energy body frame where one exists (`windOrigin`/`windShape`).
  - Otherwise it uses the object's bounds.
- **Needs:** FXL, LIGHTMOD.

**2.5 Targets.**
- **Entity:** emission.
- **Light:** intensity.
- **Material:** no. That would be a material-program Time or BeatPhase op, which exists.
- **World / Camera:** no.

**2.6 Parameters.**
- `waveform`;
- `rate` (Hz, or beats when beat-locked);
- `depth` (0–1);
- `phase`;
- `sharpness`;
- `mode`;
- `bandWidth`;
- `bandSpeed`;
- `axis`.

**2.7 Modulation.** `beat.phase` (Beat-locked uses it directly); `audio.rms → depth`; `owner.speed →
rate`; timeline.

**2.8 Animation model.** Stateless. The phase is `t·rate + phase`, with no accumulated phase. An
accumulated phase would break seek. That means a modulated `rate` produces phase jumps. The design
therefore offers `rateSmoothing = false` only and documents the jump, or the author uses the
beat-locked phase.

**2.9 Compositing.** As Glow.

**2.10 Stack behaviour.** `Material` (Entity) or `Lighting` (Light). It **multiplies** gains:
Pulse × Glow.

**2.11 Performance class.** Very Low.

**Presets.** Heartbeat, Breathing Light, Beat Strobe (square, beat-locked), Energy Climb (Travelling,
upward).

---

## Flicker  (`flicker`)

**2.1 Definition.** Irregular intensity variation from a real source. The models are:
- **Fire / Candle:** 1/f noise, correlated with a colour-temperature shift.
- **Fluorescent / Faulty:** random drop-outs and rapid strobing bursts.
- **Electrical:** a buzz with occasional spikes.

**2.2 References.**
- Id Software's Quake light styles: authored brightness strings, the first "faulty light" in games.
  They showed that a *pattern* reads better than white noise.
- Band-limited value noise: fBm for fire.
- Blackbody colour shift for fire: dimmer is redder.

**2.3 Visual anatomy.**
- Intensity jitter.
- For fire, colour warms as it dims.
- Shadows cast by the light shift, since the light is real.
- Nearby fog pulses if the light has volumetric strength.

**2.4 Implementation.**
- **Recommended:** LIGHTMOD, on the CPU.
  - Each frame, `m(t) = model(seed, t)`: seeded value-noise octaves, or a hashed dropout schedule on
    a fixed grid (`floor(t·rate)`).
  - Write `intensity·m` and `colour·blackbody(m)` into the light record before `updateLights`.
- For an Entity owner, the same `m` drives the FXL gain.
- There is no GPU cost beyond the light that already exists.
- **Needs:** LIGHTMOD.

**2.5 Targets.** **Light:** primary. Entity (emission). World: no. A "storm flicker" of the whole
scene is Lightning's flash.

**2.6 Parameters.** `model`, `amount`, `speed`, `dropoutRate`, `dropoutDepth`, `colorShift`, `seed`.

**2.7 Modulation.** `audio.treble → amount`; `music.break → dropoutRate`; timeline.

**2.8 Animation model.** Stateless: seeded noise of t.

**2.9 Compositing.** It changes real lighting, so shadows, fog in-scatter and specular all follow.
Because it changes the light, not the image, it is consistent everywhere.

**2.10 Stack behaviour.** `Lighting`. It **multiplies** light intensity. It stacks with Pulse
multiplicatively.

**2.11 Performance class.** Very Low (CPU).

**Presets.** Candle, Torch, Failing Fluorescent, Sparking Wire.

---

## Bloom Source  (`bloomSource`)

**2.1 Definition.** The owner blooms without being made brighter. It only raises how much of its
emission the selective bloom takes.

**2.2 References.** ADR-039's selective bloom (`post/bloom/emissionWeight`), and the per-object bloom
weight already in `ObjectUniforms.ids.z`.

**2.3 Visual anatomy.** A soft halo from bloom around the owner's emissive parts, with no change to
the surface.

**2.4 Implementation.**
- **Recommended:** FXL writes `bloomWeightOverride`. This is the existing `ids.z` lane, now written
  by the effect rather than only by the material.
- **Alternative:** a masked extra bloom pass keyed on the id target. Rejected as more expensive for
  the same result.
- **Needs:** FXL, which is essentially a write to an existing lane.

**2.5 Targets.** Entity. Material (the existing per-material bloom weight). Nothing else.

**2.6 Parameters.** `weight` (0–1), `threshold bias`.

**2.7 Modulation.** `beat.pulse → weight`; `owner.cameraDistance`.

**2.8 Animation model.** Stateless.

**2.9 Compositing.** It only changes what the emission target tells bloom. With `emissionWeight = 0`
in post it has no visible effect. The panel must say so, following the repository's "built but
unreachable" rule.

**2.10 Stack behaviour.** `Material` (the bloom weight). The maximum wins.

**2.11 Performance class.** Very Low.

**Presets.** Subtle, Hot.

---

## Light Beam  (`lightBeam`)

**2.1 Definition.** A visible cone of light: a searchlight or a stage beam. It is a *cheap* look that
does not depend on fog. Compare Volumetric Beam, which is physically marched.

**2.2 References.**
- The classic "light shaft cone" technique: an additive cone mesh with edge fade (Fresnel/N·V),
  a depth-intersection fade [R16] so it softens where it enters geometry, and noise for dust.
- UE "light beam" materials use exactly DepthFade plus Fresnel [R25].

**2.3 Visual anatomy.**
- A bright core fading to soft edges.
- Brighter near the source.
- Soft where the beam meets the floor or walls.
- Faint drifting dust.
- The real spot lights the ground at its end.

**2.4 Implementation.**
- **Recommended:** LIGHTMOD spot light, plus a SHELL cone.
  - The SHELL cone is additive and does not write depth.
  - Fragment: `intensity·(1−|N·V|)^p` inverted so the core is bright, `·axialFalloff`,
    `·depthFade(linearDepth − fragDepth)`, `·(1 + dust·fbm3(p − wind·t))`.
  - It is written to HDR and the emission target.
- **Needs:** SHELL, LIGHTMOD, linear depth (exists).

**2.5 Targets.** **Light:** a spot owner, where the cone matches its angle and range. **Entity:** a
UFO searchlight, which creates its own spot. World: a placed beam.

**2.6 Parameters.**
- `length`;
- `angle` (read from the light when it is Light-owned);
- `intensity`;
- `color`;
- `coreSharpness`;
- `edgeSoftness`;
- `dust`;
- `dustScale`;
- `depthFadeDistance`;
- `castLight` (bool);
- `lightIntensity`.

**2.7 Modulation.** `audio.bass → intensity`; `beat → sweep` (the owner's rotation lives in Orbit or
the timeline); `owner.speed`.

**2.8 Animation model.** Stateless.

**2.9 Compositing.**
- The cone is blended after opaque, fogged by `applyFog`.
- It does not occlude, and it is not occluded *inside* itself. It is a surface, so a pillar inside
  the beam does not cast a shaft. If that matters, use Volumetric Beam.
- It blooms through the emission target.

**2.10 Stack behaviour.** Geometry (SHELL) + `Lighting`.

**2.11 Performance class.** Low: fragment overdraw of one cone.

**Presets.** Searchlight, Stage Spot, UFO Tractor (green, wide, dusty; see the existing tractor-beam
lab docs).

---

## Volumetric Beam  (`volumetricBeam`)

**2.1 Definition.** A spot light that scatters in participating media, so its beam is real. It is
occluded by media (ADR-570 self-shadow) and, after the God Rays work, by geometry.

**2.2 References.** [R5] / [R6], and the engine's own march: `inScatterAt` / `lightRadiance`
(`shaders/volume.wgsl:510-640`).

**2.3 Visual anatomy.** A beam visible in haze. It is brighter looking toward the source
(Henyey-Greenstein forward scattering), and it is darkened where it passes through denser medium.

**2.4 Implementation.**
- **Recommended:** mostly **existing**.
  - Lights carry a `volumetricStrength` packed into `cone.z`, which is read by the march
    (`volume.wgsl:503-509`).
  - The effect sets that strength on its Light owner, or on its own pool spot through LIGHTMOD.
  - If the scene has no ambient medium, it requests a MEDIUM slot of kind "beam haze": the fog bank
    kind with a box/cylinder primitive (ADR-566) enclosing the cone.
- **Gap:** `volume.wgsl:508` says "There is no shadowing in the fog". Geometry does not occlude the
  beam. The shadow-map sampling that God Rays needs (below) fixes both.
- **Needs:** LIGHTMOD, MEDIUM (one of `kMaxMedia = 4` slots, `src/world/atmospherics.hpp:612`), and
  the march shadow work for occlusion.

**2.5 Targets.** Light: primary. Entity (a UFO beam). World: no.

**2.6 Parameters.** `scatter`, `anisotropy`, `hazeDensity` (when it owns a haze), `hazeNoise`,
`intensity`.

**2.7 Modulation.** `audio.rms → hazeDensity`; `beat → intensity`.

**2.8 Animation model.** Stateless.

**2.9 Compositing.** The march is depth-aware and composited before post, so the beam is depth
correct against geometry at its far end. When the beam's own haze is dropped for capacity, the
instance reports `EffectStatus::Dropped`.

**2.10 Stack behaviour.** `Lighting` + `Volumetric`.

**2.11 Performance class.** Medium–High: fragment (march). Its cost scales with lights that light the
air × steps.

**Presets.** Concert Beam, Lighthouse, Abduction Beam.

---

## God Rays  (`godRays`)

**2.1 Definition.** Crepuscular shafts: the key light is occluded by geometry (trees, clouds,
windows), and the shafts show in the air.

**2.2 References.**
- [R4] Mitchell, GPU Gems 3 ch.13: a screen-space radial blur of an occluder mask from the light's
  screen position. It is cheap, but it vanishes when the sun is off-screen and has no depth.
- [R5] Wronski 2014 and [R6] Hillaire 2015: sample the shadow map at each march step. It is correct
  from any view.
- Rejected: "a radial gradient".

**2.3 Visual anatomy.** Bright shafts separated by shadow wedges from occluders, converging toward
the sun. They are strongest looking toward the light (anisotropic phase) and are modulated by haze
density.

**2.4 Implementation.**
- **Recommended:** make the volume march shadowed.
  - In `inScatterAt` (`volume.wgsl:621`), for the directional key light, multiply by a cascade
    shadow lookup at the sample point.
  - The cascade atlas exists (`kMaxShadowViews = 8`, `src/rendering/shadow_math.hpp:20-21`), but it
    is not bound to the volume pass.
  - Binding it is the engine change, and it serves Volumetric Beam too.
  - The effect then only raises scatter and anisotropy on the scene fog, or requests a haze MEDIUM,
    and flags the key light `castsShadow`.
- **Alternative / fallback:** Camera PostProcess mode [R4]. A mask of sky/bright pixels from depth is
  radially blurred toward the sun's screen position over 32–64 taps at half resolution, then added.
  It is offered for scenes with no fog budget. It is honest about its failure mode: it fades as the
  sun leaves the frame.
- **Needs:** a shadow map in the march (engine change), MEDIUM or scene fog, and FXPOST for the
  fallback.

**2.5 Targets.** World (a scene-wide sky condition), Light (directional), Camera (fallback mode).

**2.6 Parameters.** `intensity`, `anisotropy`, `hazeDensity`, `shadowSteps` (march quality),
`mode` (Volumetric / Screen), `decay`, `samples` (Screen).

**2.7 Modulation.** `audio.rms → hazeDensity`; timeline (the time of day moves the sun anyway).

**2.8 Animation model.** Stateless. The volume march jitter is already frame-index hashed, not wall
clock (`volume.wgsl` header).

**2.9 Compositing.** Volumetric mode is correct against depth and fog. Screen mode is additive before
bloom and ignores depth.

**2.10 Stack behaviour.** `Volumetric` (or `PostProcess`).

**2.11 Performance class.** High. The march adds a shadow-map fetch per step per shadowed light.
Screen mode is Medium.

**Presets.** Forest Shafts, Cathedral Window, Glowmere Canopy.

---

## Aura  (`aura`)

**2.1 Definition.** A glowing envelope standing off the owner's silhouette. It can be flame-like,
smooth or crackling. Compare Glow (the surface itself) and Energy Shield (a hard shell with impacts).

**2.2 References.**
- Inflated hull ("shell" rendering: push vertices along normals and shade additively with rim
  falloff). This is the same trick as inverted-hull outlines, e.g. Motomura GDC 2015 [R20].
- A screen-space alternative: dilate and blur an object-id mask. This is common in "highlight"
  outlines.

**2.3 Visual anatomy.**
- A band of light hugging the silhouette.
- Brightest at the silhouette edge, fading outward.
- Noise-driven licks rising upward.
- The aura's colour spills a little onto the surroundings.

**2.4 Implementation.**
- **Recommended:** REDRAW. The owner's mesh is drawn a second time with an aura pipeline.
  - The vertex stage pushes vertices by `offset·(1 + noise)` along the normal, with skinned and
    wind-deformed paths reused.
  - It is additive with no depth write, and depth-tested against the scene so occluders hide it.
  - Fragment: `pow(1 − |N·V|, p)·noise(p·s + up·t)`.
- **Alternative (FXPOST):** an id-mask glow.
  - A mask where `ids == owner`, then a half-resolution separable blur, then an outside-only
    composite.
  - It works for any geometry, including SDFs and procedural objects that REDRAW cannot redraw.
  - It ignores depth unless it compares depth.
- **Needs:** REDRAW (which needs the owner's GpuMesh and pipeline variant) or FXPOST plus the id
  target.

**2.5 Targets.** Entity only.

**2.6 Parameters.** `thickness`, `intensity`, `color`, `edgePower`, `noiseAmount`, `noiseScale`,
`riseSpeed`, `mode` (Hull / Screen), `spill`.

**2.7 Modulation.** `audio.bass → thickness`; `owner.speed → riseSpeed`; `beat → intensity`.

**2.8 Animation model.** Stateless.

**2.9 Compositing.** Blended after opaque. It blooms through the emission target. The spill is a
LIGHTMOD point light.

**2.10 Stack behaviour.** Geometry (REDRAW draw) + `Lighting` (spill).

**2.11 Performance class.** Medium. It redraws the owner's triangles, plus overdraw.

**Presets.** Power Aura (golden flames), Ghostly (cyan smooth), Corrupted (dark with a red rim).

---

## Halo  (`halo`)

**2.1 Definition.**
- (a) Optical: a glare disc and ring around a bright point (a lamp, an emissive orb). This is the
  default.
- (b) Iconographic: a luminous ring floating above the owner's head.

Both are modes of one effect.

**2.2 References.** Lens glare/halo sprites in games, occlusion-tested with a depth fetch at the
source's screen position (UE lens-flare occlusion, Kawase's glare notes, GDC 2003). The 22° halo ring
has a fixed angular radius.

**2.3 Visual anatomy.**
- Optical: a soft core glow, a faint outer ring, and a fade as the source is occluded.
- Ring: an emissive torus that bobs slightly.

**2.4 Implementation.**
- **Optical:** a SHELL camera-facing billboard.
  - The vertex stage samples linear depth at the projected source (in a 3×3 neighbourhood; textures
    are readable in the WGSL vertex stage through `textureLoad`) to compute visibility.
  - The fragment draws radial profiles.
  - It is added to HDR and emission.
- **Ring:** a SHELL torus, emissive. Float motion comes from Float (XFORM), if stacked.
- **Needs:** SHELL, linear depth.

**2.5 Targets.** Light (optical, at the light's position), Entity (either mode), World (placed).

**2.6 Parameters.** `mode`, `radius`, `ringRadius`, `ringWidth`, `intensity`, `color`,
`occlusionSoftness`.

**2.7 Modulation.** `audio.treble → intensity`; `owner.cameraDistance → radius` (constant angular
size).

**2.8 Animation model.** Stateless.

**2.9 Compositing.**
- Additive after opaque.
- The billboard is not depth-tested per pixel, since it is glare, but it is occlusion-faded.
- The ring is depth-tested.

**2.10 Stack behaviour.** Geometry (SHELL).

**2.11 Performance class.** Very Low.

**Presets.** Lamp Glare, Saint Ring, Moon Corona.

---

## Light Trail  (`lightTrail`)

*A preset of Trail, not a separate type.* It is Trail with an emissive, additive, long-persistence
ribbon: HDR intensity above 4, `blend = Additive`, `persistence` 1–4 s, a narrow width and a colour
taken from the owner's emission. It is listed here because the spec lists it. The implementation is
entirely in [catalog-motion.md#trail](catalog-motion.md#trail--trail) (RIBBON over HIST).

- **Targets:** Entity, Light (a moving light draws its own streak).
- **Presets:** Long Exposure, Neon Streak, Firefly Streak (thin, pulsed).
- **Performance class:** Low.
