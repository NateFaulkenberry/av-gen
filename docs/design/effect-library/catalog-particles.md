# Catalog: Particle / Environment

Part of the [Effect Library](README.md). The master table is in [catalog.md](catalog.md). Tags are
defined in [shared-infrastructure.md](shared-infrastructure.md).

## The family's finding: nine of ten are presets

The existing particle system is broad. `scene::ParticleSystem` has about 85 fields
(`src/scene/particles.hpp:123-309`), simulated on the GPU in `shaders/particles.wgsl` (emit →
simulate → deterministic prefix-sum compaction, ADR-015). It already has:

- **Emission.** Point, Sphere, Disc, Box and Spline shapes, `burst`, and an emission-mask field
  (`emitMaskField`, :276).
- **Forces.** Gravity, drag, curl turbulence, attractor, orbit, up to 4 field forces, and the
  deterministic wind field (`windInfluence`, :178).
- **Leaf cards with tumble.** `shape2d = Leaf`, `tumbleRate`, `leafAspect`, `twoSided` (ADR-370,
  :172-175).
- **Firefly behaviour.** `pulseRate/Depth/Sync/Sharpness`, `clusterCount/Radius`, `pauseRate/Fraction`
  and scatter-anchored clusters (ADR-520, :237-260).
- **Weather.** A camera-carried wrapping volume (`volumeFollow/volumeWrap`, :192-198). Collision can
  be Kill, Bounce or Splash, where a splash is the particle reborn as a ground ring. Both are ADR-520.
- **Look.** Velocity stretch (:287-289); 32-point ribbons (ADR-040, :294-300); size, colour and
  opacity curves (:279-281); soft particles from linear depth (ADR-367, :184); HG sun scattering
  (:267-268); fog coupling and volume glow (:307-308).
- **Presets.** The `examples/weather` scenes hold rain, snow, ashfall, fireflies, pollen, spores,
  seeds, dust motes and seasons. Glowmere and other scenes add leaves, sparks and streaks.

So **Fireflies, Embers, Snow, Rain, Ash, Leaves, Spores, Magic Particles and Cosmic Dust are presets
of one effect type**. Stars is not a particle effect (see below).

### EMIT: the one Particle Emitter type

Particles are **not** in the effect registry today. `RenderStage::Particles` is "(none yet)"
(`src/world/effects/effect_registry.hpp:419`). They live in `Scene::particles` and as composition
`NodeKind::Particles` nodes (`src/scene/composition.hpp:113`, :337), with parameters under
`particles/<name>/<field>`.

The proposal is **one** registry type, `particleEmitter`, whose styles are the presets below. The
Add Effect menu lists each style as its own entry ("Add Fireflies"), so artists never see the generic
type. The instance owns a `scene::ParticleSystem` named `fx:<id>`, and the evaluator
(`RenderStage::Particles`) writes it into the frame's particle list:

- **Rows.** About 25 artist rows are exposed as `fx/<id>/<leaf>` stored fields. The full struct
  round-trips through `writeExtra`/`readExtra`, so nothing is lost.
- **One authority per parameter.** The particle registrar must *not* also register
  `particles/fx:<id>/*` (the one-authority rule).
- **Owner attachment.** An Entity owner sets `position`, `extent` and the attractor. Today the
  composition node does this for a parent chain (`composition.cpp:7015-7026`). The **gap** is that
  `direction` is not rotated by the owner's rotation, and EMIT must add that.
- **Triggers.** A TRIGGER edge adds `burst` on the trigger frame. Today a burst works only through a
  route onto `burst` (`orb_scene.cpp:171`), and there is no scheduled burst.
- **Status.** The instance reports `Dropped` when the frame's particle-system budget is exceeded, not
  silently. Only 8 systems can feed volume glow (`particle_renderer.hpp:200`), and that limit is a
  documented `Dropped`-style report too.

**Seek.** Particles are the engine's one documented determinism relaxation. Pools reset on seek
(`scene_renderer.cpp:1711`), and a warm-up exists but is off by default and capped at 240 frames
(ADR-360/395/521). EMIT sets `warmUpFrames` per preset for ambient weather, so snow is not empty for
one lifetime after a scrub, and states the relaxation in the panel.

**Gaps that limit the presets:**

- No sprite or texture atlas. The only texture is linear depth (`particles.wgsl:191`), so a snowflake
  or leaf silhouette is procedural (Round or Leaf) only.
- No depth sorting for alpha systems (ADR-015).
- Collision against a flat Y plane only.
- Particles cast no light on surfaces and no shadows.

Each preset below lists the fields it sets. The shared entry points are EMIT (2.4), `Particles`
(2.10) and `Additive`/`Alpha` blending.

---

## Fireflies  (`particleEmitter:fireflies`)

- **2.1 Definition.** Slow wandering points of light that blink asynchronously and sometimes in
  synchrony, clustered near vegetation.
- **2.2 References.** Firefly flash synchrony (Buck 1988; Strogatz *Sync*). This is exactly what
  `pulseSync` models. Existing scenes: `fly-chorus`, `fly-scatter`, and Glowmere's `tree-fireflies`.
- **2.3 Visual anatomy.** Tiny HDR points hover and pause, drift in small clusters, and blink on and
  off with a sharp attack. Each lights a soft halo, and the air nearby glows.
- **2.4 Implementation.** EMIT preset:
  - `blend = Additive` and `emissive` 6–12.
  - `pulseRate` 0.3–0.8 Hz, `pulseSharpness` high, `pulseSync` 0.2.
  - `clusterCount`, `pauseRate` and `pauseFraction`.
  - `scatterAnchor` bound to the ecology scatter layer, so flies live where the plants are.
  - `volumeGlow` on, so the air takes their colour.
  - Low `turbulence`.
  - Optional short trails.
- **2.5 Targets.** World (scatter-anchored) and Entity (a swarm that follows its owner, with the
  attractor at the owner).
- **2.6 Parameters.** Density (`spawnRate`/capacity), blink rate, sync, colour, brightness, cluster
  size, wander, trails.
- **2.7 Modulation.** `beat.phase → pulseSync` (fireflies sync to the music, which is the signature
  AV moment), `audio.treble → brightness`, and the time of day.
- **2.8 Animation model.** Particle state (the relaxation above).
- **2.9 Compositing.** Additive and soft, fogged through `fogCoupling`, glowing through `volumeGlow`,
  and blooming.
- **2.10 Stack behaviour.** `Particles`.
- **2.11 Performance class.** Low.
- **Presets.** Meadow, Glowmere Canopy, Synchronous Swarm.

## Embers  (`particleEmitter:embers`)

- **2.1 Definition.** Glowing hot fragments rising from a fire and cooling as they go.
- **2.2 References.** Blackbody colour over life (orange-white → red → dark) as a `colorCurve`.
  Existing: `ash-embers`.
- **2.3 Visual anatomy.** Bright sparks rise erratically, curl in turbulence, dim from orange to red,
  and wink out. They are slightly stretched by velocity.
- **2.4 Implementation.** EMIT: Box or Disc at the fire, negative gravity (buoyant), `turbulence` of
  curl noise, a `colorCurve` blackbody ramp, `emissive` about 8, `velocityStretch` about 0.5,
  `windInfluence`, and `Additive`. Pair with Flicker on the fire light.
- **2.5 Targets.** Entity (a fire or torch) and World.
- **2.6 Parameters.** Rate, rise speed, turbulence, lifetime, heat colour, brightness, and stretch.
- **2.7 Modulation.** `audio.onset → burst` (crackle pops) and `audio.rms → spawnRate`.
- **2.8–2.11.** Particle state. Additive and bloomed. `Particles`. Low.
- **Presets.** Campfire, Forge, Burning Debris.

## Snow  (`particleEmitter:snow`)

- **2.1 Definition.** Falling snowflakes: a gentle drift or a blizzard.
- **2.2 References.** Tatarchuk [R30] layers near and far precipitation. Existing: `snow-fall`,
  `snow-near` and `snow-drift`, with ADR-520's camera volume.
- **2.3 Visual anatomy.** Soft white flakes fall slowly with a side-to-side sway, following the wind.
  Near flakes are large and soft, far flakes are fine.
- **2.4 Implementation.** EMIT, in *two* systems (near and far) created by one preset. Use
  `volumeFollow`/`volumeWrap` (the camera-carried volume), `gravity` at about 0.8 m/s² terminal,
  `drag`, `windInfluence`, `turbulence`, `Alpha`, `softness`, `sizeVariance`/`sizeSkew`, and
  `collision = Kill` at the ground height. Set warm-up on.
- **2.5 Targets.** World, carried by the camera volume. Camera is not a separate owner here, because
  `volumeFollow` already follows the camera.
- **2.6 Parameters.** Intensity, flake size, fall speed, wind response, near/far balance.
- **2.7 Modulation.** Timeline intensity and `audio.rms → wind`.
- **2.8–2.11.** Particle state. Alpha blending with soft particles, unsorted. `Particles`. Low–Medium
  (fill-rate on the near layer).
- **Presets.** Light Snow, Blizzard, Magical Snow (emissive and slow).

## Rain  (`particleEmitter:rain`)

- **2.1 Definition.** Falling streaks with ground splashes and mist.
- **2.2 References.** Tatarchuk [R30]: rain as motion-blurred streaks plus splash particles plus mist.
  Existing: `rain-fall`, `rain-near`, `rain-impacts` and `rain-mist`.
- **2.3 Visual anatomy.** Thin bright streaks aligned with fall velocity, backlit sparkle, splash
  rings on the ground, and low mist.
- **2.4 Implementation.** EMIT, a multi-system preset:
  - Streaks: `velocityStretch` high, `stretchMax`, `collision = Splash` with `splashLifetime` and
    `ringThickness` (the ring second life), and `scatterAnisotropy` for backlighting.
  - Mist: a low, slow alpha system.
  - The ground's wet look is out of scope (a material concern).
- **2.5 Targets.** World (camera volume).
- **2.6 Parameters.** Intensity, drop speed, streak length, splash size, mist, wind slant.
- **2.7 Modulation.** Timeline intensity and `music.drop → intensity` (downpour on the drop).
- **2.8–2.11.** Particle state. Streaks are additive, mist is alpha. `Particles`. Medium (streak
  fill-rate).
- **Presets.** Drizzle, Storm, Neon Rain (tinted, emissive).

## Ash  (`particleEmitter:ash`)

- **2.1 Definition.** Slow grey flakes drifting down, sometimes with embers mixed in.
- **2.2 References.** Existing: `ash-fall`, `ash-embers`, `ash-smoke` and `ash-embers-near`.
- **2.3 Visual anatomy.** Grey-white tumbling flakes drift slowly, with occasional glowing embers.
- **2.4 Implementation.** EMIT: Leaf-shaped cards (`shape2d = Leaf`, small `leafAspect`),
  `tumbleRate`, slow gravity, turbulence, `Alpha`, and the camera volume. An optional companion
  ember system.
- **2.5 Targets.** World.
- **2.6–2.11.** Intensity, flake size, ember fraction, wind. Particle state. `Particles`. Low.
- **Presets.** Volcanic Ashfall, Burned Forest.

## Leaves  (`particleEmitter:leaves`) — *merges Falling Leaves*

- **2.1 Definition.** Leaves detaching and falling with tumble and flutter.
- **2.2 References.** ADR-370 leaf cards with tumble and the canopy-measured emitter. Existing scenes:
  treeisland and seasons.
- **2.3 Visual anatomy.** Two-sided leaf cards fall with a flutter (tumble) and sway downwind, landing
  and fading.
- **2.4 Implementation.** EMIT: `shape2d = Leaf`, `twoSided`, `tumbleRate`, `windInfluence` about 1,
  and a canopy source (`canopySource`/`canopyFrom`, `composition.hpp:321-325`) when the owner is a
  tree. `collision = Kill` at the ground (Bounce for skittering). Colour comes from a palette curve.
- **2.5 Targets.** Entity (a tree: the emitter is its canopy) and World.
- **2.6 Parameters.** Rate, leaf size, colours, tumble, wind response, fall speed.
- **2.7 Modulation.** `audio.rms → wind` gusts shed leaves, and `music.drop → burst`.
- **2.8–2.11.** Particle state, including the seek relaxation. Alpha blending (unsorted, which is
  acceptable for small cards). `Particles`. Low.
- **Presets.** Autumn, Cherry Petals, Glowmere Drift (emissive leaves).

## Spores  (`particleEmitter:spores`) — *merges Floating Spores*

- **2.1 Definition.** Tiny, often luminous motes that hang and drift up or around from fungi.
- **2.2 References.** Existing: `spores` (examples/weather and glowmere-valley-2). ADR-380: motes
  leave the tree and the vortex attracts them.
- **2.3 Visual anatomy.** Slow buoyant specks with soft glow and random walk. They are denser near the
  source, and some are pulled into the vortex.
- **2.4 Implementation.** EMIT: Sphere or canopy source, slight negative gravity, strong `turbulence`,
  `drag`, a soft `pulse`, `emissive` 2–5, and `Additive`. Use `vortexAttractor`
  (`composition.hpp:326-330`) when a vortex exists.
- **2.5 Targets.** Entity (a mushroom) and World.
- **2.6–2.11.** Density, glow, drift, rise, colour. Modulation: `audio.mid → glow` and
  `beat → burst`. Particle state. `Particles`. Low.
- **Presets.** Mushroom Puff, Glowmere Spores, Spore Burst (triggered).

## Magic Particles  (`particleEmitter:magic`)

- **2.1 Definition.** Stylised sparkles that swirl around or trail from a caster.
- **2.2 References.** Curl noise [R12] plus orbit and attractor. Ribbon trails (ADR-040).
- **2.3 Visual anatomy.** Colour-shifting glints spiral around the owner, leave short ribbon trails,
  twinkle and fade.
- **2.4 Implementation.** EMIT: Sphere around the owner, `orbit`, attractor at the owner, `turbulence`,
  `trailEnabled` (short), a `colorCurve` hue sweep, `pulse`, and `Additive`.
- **2.5 Targets.** Entity (primary) and World.
- **2.6–2.11.** Rate, orbit speed, swirl, trail length (not modulatable; ADR-040), palette, and
  sparkle. Modulation: `owner.speed → spawnRate` and `beat → burst`. Particle state. Low.
- **Presets.** Fairy Dust, Arcane Swirl, Healing Motes.

## Cosmic Dust  (`particleEmitter:cosmicDust`)

- **2.1 Definition.** Fine luminous dust in space, swirling in a galactic flow or around a vortex.
- **2.2 References.** Existing: constellation `discDust` and arms, and the vortex attractor. For
  large-scale structure prefer the Vortex medium (ADR-387). This preset is the resolved foreground
  dust.
- **2.3 Visual anatomy.** A dense field of faint coloured points in slow orbital flow, with
  parallax-rich depth and occasional bright grains.
- **2.4 Implementation.** EMIT: Disc or Box, `orbit` around an axis, the vortex attractor, very low
  `sizeStart`, `emissive` 1–3, `sizeSkew` for a few bright grains, `Additive`, and a camera volume for
  fly-throughs.
- **2.5 Targets.** World and Camera (the volume follows the camera).
- **2.6–2.11.** Density, flow speed, palette, brightness. Modulation: `audio.bass → orbit speed`.
  Particle state. `Particles`. Low–Medium (count).
- **Presets.** Nebula Drift, Hyperspace Dust (streaks, as in the `shot-hyperspace` streaks), Vortex
  Motes.

## Stars  (`stars`) — *not a particle effect*

- **2.1 Definition.** A controllable star field: density, brightness distribution, colour
  temperature spread, twinkle, and optional parallax layers.
- **2.2 References.**
  - Procedural star fields by hashed cells: one star per cell with a magnitude drawn from a power
    law. This is what `shaders/skybox.wgsl:131-141` does today, with a fixed density (0.988 cutoff),
    a fixed colour and no twinkle, and only on the analytic night sky.
  - Glowmere draws stars as point geometry at three depths (`glowmere-cosmos.wgsl:29`).
- **2.3 Visual anatomy.** Sharp points with realistic magnitude spread, faint colour variety
  (blue-white to orange), subtle scintillation near the horizon, and a Milky-Way band as an option.
- **2.4 Implementation.** A **Sky**-stage kind. Replace the hard-coded skybox constants with frame
  uniforms owned by this effect (density, magnitude slope, colour spread, twinkle rate, and horizon
  scintillation), evaluated in the same hashed-cell loop. The anti-aliasing footprint logic
  (`fwidth`) already exists. Parallax layers, for camera fly-through, are point geometry as in
  Glowmere. A new `EffectBucket` value is needed, because it does not fit Comet or Aurora. The only
  new GPU data is a small uniform block.
- **2.5 Targets.** World only.
- **2.6 Parameters.** `density`, `brightness`, `magnitudeSlope`, `colorSpread`, `twinkle`,
  `twinkleRate`, `horizonFade`, `band`, `bandTilt`.
- **2.7 Modulation.** `audio.treble → twinkle` and `beat → brightness`.
- **2.8 Animation model.** Stateless (hash plus t).
- **2.9 Compositing.** At the far plane before the atmospheric layer, so comets and auroras draw over
  it. It feeds bloom only if `skyBloom` is above 0 (ADR-049).
- **2.10 Stack behaviour.** `Sky`.
- **2.11 Performance class.** Very Low.
- **Presets.** Clear Night, Deep Space, Twinkling Horizon.
