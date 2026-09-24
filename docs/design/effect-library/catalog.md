# Effect Catalog: master table

This page is part of the [Effect Library](README.md). It lists every effect once. The design entries,
under headings 2.1 to 2.11, are in the per-family files:

- [Distortion](catalog-distortion.md)
- [Light](catalog-light.md)
- [Motion](catalog-motion.md)
- [Energy](catalog-energy.md)
- [Organic](catalog-organic.md)
- [Particle / Environment](catalog-particles.md)
- [Stylization](catalog-stylization.md)
- [Temporal](catalog-temporal.md)

Dependency tags are defined in [shared-infrastructure.md](shared-infrastructure.md).

## Deduplication

The spec lists 77 names. After merging, there are **73 unique effects**:

| Spec names | Merged into | Why |
|---|---|---|
| Shockwave (Distortion) + Shockwave (Energy) | **Shockwave** (one type; "Energy Blast" is a preset) | Same event-driven expanding front; the Energy reading only adds rim emission and a flash |
| Afterimage (Motion) + Afterimage (Temporal) | **Afterimage** (Geometry and Image modes) | Same look; the two readings are two implementations |
| Floating Spores (Organic) + Spores (Particle) | **Spores** | Same particle preset |
| Falling Leaves (Organic) + Leaves (Particle) | **Leaves** | Same particle preset |

These *near*-duplicates are kept separate, and the difference is stated in each entry:

- **Glow** is the surface. **Aura** is an envelope. **Halo** is glare.
- **Pulse** is emission. **Breathing** is a standing swell of geometry. **Organic Pulsation** is a
  travelling swell.
- **Light Beam** is a cheap cone. **Volumetric Beam** is marched. **God Rays** are occluded shafts.
- **Trail** is continuous. **Afterimage** is discrete copies.
- **Motion Smear** is geometry. **Temporal Smear** is image history.
- **Energy Shield** is around an entity. **Force Field** is a world barrier.
- **Lightning** is a strike event. **Arc** is continuous between endpoints.

These are *one type with several styles*:

- **Light Trail** is a preset of Trail.
- **Sway** is a preset of Wind Response.
- The nine particle effects are styles of `particleEmitter`.
- **Echo**, **Temporal Smear** and **Ghosting** are kernels of `temporalFilter`.

## Legend

**Stages** are ADR-702's `RenderStage`:

| Abbreviation | Stage |
|---|---|
| Geo | Geometry |
| Mat | Material |
| Lit | Lighting |
| Sky | Sky |
| Vol | Volumetric |
| Part | Particles |
| SS | ScreenSpace |
| Post | PostProcess |

"+draw" marks effect geometry drawn in pass 1 (SHELL, RIBBON or REDRAW).

**Targets**:

| Letter | Target |
|---|---|
| W | World |
| E | Entity |
| C | Camera |
| L | Light |
| M | material program (existing, not an owner) |

**Complexity** is implementation effort *given* its dependencies exist:

| Grade | Meaning |
|---|---|
| S | one kind file |
| M | a kind file + shader variant |
| L | new shader logic + CPU generator |
| XL | engine change |

**Modulation** lists the default route source. The full table is in
[parameters-and-modulation.md](parameters-and-modulation.md).

## The table

| Effect | Family | Target(s) | Technique | Stage | Key parameters | Modulation | Cx | Dependencies |
|---|---|---|---|---|---|---|---|---|
| [Space Warp](catalog-distortion.md#space-warp--spacewarp) | Distortion | **E**, W | ellipsoid proxy; radial + bow (velocity) + swirl offset; Sousa mask | SS | radius, strength, bowWeight, swirl, velocityStretch, chroma | `owner.speed` | M | DF, SIGNALS |
| [Gravitational Lens](catalog-distortion.md#gravitational-lens--gravlens) | Distortion | **W**, E | thin-lens remap β=θ−θ_E²/θ; env fallback | SS | einsteinRadius, mode, photonRing | `audio.bass` | M | DF |
| [Heat Shimmer](catalog-distortion.md#heat-shimmer--heatshimmer) | Distortion | **E**, W, C | rising noise gradient × thickness; Haze by depth | SS / Post | strength, scale, riseSpeed | `audio.rms` | S | DF, FXPOST |
| [Shockwave](catalog-distortion.md#shockwave--shockwave) | Distortion + Energy | **E**, W | expanding sphere; derivative-of-Gaussian band; rim; flash | SS, Lit, Part | maxRadius, duration, thickness, rimEmission, trigger | trigger `music.drop` | M | DF, TRIGGER, LIGHTMOD, EMIT |
| [Ripple](catalog-distortion.md#ripple--ripple) | Distortion | **E**, W, M | damped radial wave; membrane or surface | SS / Mat | wavelength, speed, amplitude, decay | trigger `beat` | S | DF, TRIGGER, (Surface waves) |
| [Bubble](catalog-distortion.md#bubble--bubble) | Distortion | **E**, W | thin-film shell + Fresnel + rim lensing | Mat+draw, SS | thickness, iridescence, wobble | `audio.mid` | M | SHELL, DF |
| [Time-Warp Distortion](catalog-distortion.md#time-warp-distortion--timewarp) | Distortion | **E**, W, C | DF with history taps | SS | delayFrames, echoTaps, swirl | `owner.timeRate` | L | DF, TEMPORAL |
| [Portal Distortion](catalog-distortion.md#portal-distortion--portal) | Distortion | **W**, E | disc interior (procedural / env / see-through) + rim + swirl | Mat+draw, SS, Part | open, rimEmission, interiorMode | `audio.bass` | L (Remote: XL) | SHELL, DF, EMIT |
| [Reality Tear](catalog-distortion.md#reality-tear--realitytear) | Distortion | **W**, E, C | polyline SDF crack + shear + void | Mat+draw, SS | open, jaggedness, shear, edgeEmission | trigger `music.drop` | L | BOLT, SHELL, DF |
| [Radial Distortion](catalog-distortion.md#radial-distortion--radialdistortion) | Distortion | **C** | barrel/pinch about a point + zoom blur | Post | amount, center, zoomBlur | `beat.pulse` | S | FXPOST |
| [Glow](catalog-light.md#glow--glow) | Light | **E**, M | emission gain/tint + rim + spill light | Mat, Lit | gain, tint, rim, spill | `beat.pulse` | S | FXL, LIGHTMOD |
| [Pulse](catalog-light.md#pulse--pulse) | Light | **E**, **L**, M | waveform scalar or travelling band | Mat / Lit | waveform, rate, depth, mode | `beat.phase` | S | FXL, LIGHTMOD |
| [Flicker](catalog-light.md#flicker--flicker) | Light | **L**, E, M | seeded noise / dropout schedule on a real light | Lit | model, amount, dropoutRate | `audio.treble` | S | LIGHTMOD |
| [Bloom Source](catalog-light.md#bloom-source--bloomsource) | Light | **E**, M | per-object bloom-weight override | Mat | weight | `beat.pulse` | S | FXL |
| [Light Beam](catalog-light.md#light-beam--lightbeam) | Light | **L**, E, W | spot + additive cone; depth fade; dust | Lit, Geo+draw | length, angle, intensity, dust | `audio.bass` | M | SHELL, LIGHTMOD |
| [Volumetric Beam](catalog-light.md#volumetric-beam--volumetricbeam) | Light | **L**, E | light `volumetricStrength` + local haze medium | Lit, Vol | scatter, hazeDensity | `beat.pulse` | S (occluded: XL) | LIGHTMOD, MEDIUM |
| [God Rays](catalog-light.md#god-rays--godrays) | Light | **W**, L, C | shadow-map sampling in the march; screen radial fallback | Vol / Post | intensity, anisotropy, mode | `audio.rms` | XL | MEDIUM, FXPOST |
| [Aura](catalog-light.md#aura--aura) | Light | **E** | inflated-hull redraw; id-mask screen alternative | Geo+draw, Lit | thickness, noise, riseSpeed | `audio.bass` | M | REDRAW, FXPOST, LIGHTMOD |
| [Halo](catalog-light.md#halo--halo) | Light | **E**, **L**, W | occlusion-faded glare billboard / emissive ring | Geo+draw | mode, radius, intensity | `audio.treble` | S | SHELL |
| [Light Trail](catalog-light.md#light-trail--lighttrail) | Light | **E**, L | preset of Trail | +draw | persistence, emission | `owner.speed` | S | HIST, RIBBON |
| [Trail](catalog-motion.md#trail--trail) | Motion | **E**, L | ribbon over checkpointed transform history | +draw | mode, persistence, width, colour | `owner.speed` | M | HIST, RIBBON |
| [Afterimage](catalog-motion.md#afterimage--afterimage) | Motion + Temporal | **E** | redraw at past transforms, or id-masked history taps | +draw / SS | copies, interval, fade | `owner.speed` | M | HIST, REDRAW / TEMPORAL |
| [Motion Smear](catalog-motion.md#motion-smear--motionsmear) | Motion | **E** | vertex stretch toward −velocity | Geo | amount, smearSeconds | `owner.speed` (implicit) | M | FXL, SIGNALS |
| [Velocity Distortion](catalog-motion.md#velocity-distortion--velocitydistortion) | Motion | **E** | a history ribbon drawn into DF | SS | persistence, strength | `owner.speed` | M | HIST, RIBBON, DF |
| [Wind Response](catalog-motion.md#wind-response--windresponse) | Motion | **E**, M | ADR-360 mesh wind (Flex); pendulum XFORM (Rigid) | Geo | strength, lean, pivot | `audio.rms` | S (migration: M) | wind lanes, XFORM |
| [Orbit](catalog-motion.md#orbit--orbit) | Motion | **E**, L | parametric circle offset | Geo | radius, period, tilt, pivot | `audio.bass` | S | XFORM |
| [Spiral](catalog-motion.md#spiral--spiral) | Motion | **E**, L | helix offset | Geo | r0, r1, h0, h1, turns | `audio.rms` | S | XFORM |
| [Float](catalog-motion.md#float--float) | Motion | **E**, L | incommensurate-sine bob | Geo | height, period, tilt | `audio.bass` | S | XFORM |
| [Shake](catalog-motion.md#shake--shake) | Motion | **E**, **C**, L | trauma² × smooth noise; camera = ADR-098 | Geo | amplitude, frequency, trauma | trigger `music.impact` | S | XFORM, TRIGGER |
| [Bounce](catalog-motion.md#bounce--bounce) | Motion | **E** | abs-sine hop / damped spring; squash & stretch | Geo | height, squash, damping | trigger `beat` | S | XFORM, TRIGGER |
| [Lightning](catalog-energy.md#lightning--lightning) | Energy | **W**, E | seeded midpoint-displacement bolts; leader/return/restrike; flash | +draw, Lit | jaggedness, branches, restrikes, flash | trigger `music.drop` | L | BOLT, RIBBON, LIGHTMOD, TRIGGER |
| [Arc](catalog-energy.md#arc--arc) | Energy | **E**, W | re-seeded bolt between endpoints, cross-faded | +draw, Lit | strands, rate, jaggedness | `audio.bass` | M | BOLT, RIBBON, LIGHTMOD |
| [Electric Field](catalog-energy.md#electric-field--electricfield) | Energy | **E**, (M) | Worley F2−F1 crackle + surface-hopping arcs | Mat, +draw | crackle, arcCount, hopDistance | `audio.treble` | M | FXL, BOLT, RIBBON |
| [Plasma](catalog-energy.md#plasma--plasma) | Energy | **E**, W | emission-only march inside a sphere proxy | +draw, Lit | radius, turbulence, steps | `audio.rms` | M | SHELL, LIGHTMOD |
| [Energy Shield](catalog-energy.md#energy-shield--energyshield) | Energy | **E**, W | Fresnel + hex + impact ripples + depth intersection | +draw, SS | rim, pattern, hits, intersectWidth | trigger `audio.onset` | M | SHELL/REDRAW, TRIGGER, DF |
| [Force Field](catalog-energy.md#force-field--forcefield) | Energy | **W**, E | barrier shell; proximity reveal | +draw, SS | shape, pattern, revealRadius | `audio.rms` | M | SHELL, SIGNALS, DF |
| [Charge-Up](catalog-energy.md#charge-up--chargeup) | Energy | **E**, W | converging emitter + core; publishes charge | Part, +draw | chargeSeconds, radius, rate | trigger `music.build` | M | EMIT, SHELL, TRIGGER, SIGNALS |
| [Discharge](catalog-energy.md#discharge--discharge) | Energy | **E**, W | radial bolt burst + sparks + flash | +draw, Part, Lit | bolts, radius, sparks | trigger `fx.<id>.release` | M | BOLT, RIBBON, EMIT, LIGHTMOD, TRIGGER |
| [Bioluminescence](catalog-organic.md#bioluminescence--bioluminescence) | Organic | **E**, W | cell/spot emission + breathe + wave + proximity flare | Mat, Lit | pattern, breatheRate, flare | `audio.mid` | M | FXL, Worley F2, TRIGGER, ecology lights |
| [Pulsing Veins](catalog-organic.md#pulsing-veins--pulsingveins) | Organic | **E**, (M) | travelling band × vein mask (generalises ADR-376) | Mat | width, pulseSpeed, coordinate | `beat.pulse` | M | FXL, Worley F2 |
| [Growth](catalog-organic.md#growth--growth) | Organic | **E** | reveal-front clip + edge emission; arc-length tube growth; scale-in | Geo, Mat | progress, coordinate, edge | timeline | M | FXL (clip, `fs_depth`), XFORM |
| [Sway](catalog-organic.md#sway--sway) | Organic | **E**, M | preset of Wind Response (procedural oscillator) | Geo | strength | `audio.rms` | S | wind lanes |
| [Breathing](catalog-organic.md#breathing--breathing) | Organic | **E** | normal inflation in a region, asymmetric waveform | Geo | amplitude, rate, region | `audio.rms` | S | FXL (displace) |
| [Pollen](catalog-organic.md#pollen--pollen) | Organic | **W**, E, C | particle preset | Part | rate, buoyancy, sparkle | `audio.treble` | S | EMIT |
| [Tendrils](catalog-organic.md#tendrils--tendrils) | Organic | **E**, W | instanced tube bent along an FK/curl spline in the vertex stage | Geo+draw | count, length, wave, reach | `audio.bass` | L | new instanced tube, noise |
| [Organic Pulsation](catalog-organic.md#organic-pulsation--organicpulsation) | Organic | **E** | travelling normal inflation band | Geo | amplitude, bandWidth, speed | `audio.bass` | S | FXL (displace) |
| [Fireflies](catalog-particles.md#fireflies--particleemitterfireflies) | Particle | **W**, E | scatter-anchored clusters, pulse sync, volume glow | Part | density, blink, sync | `beat.phase` | S | EMIT |
| [Embers](catalog-particles.md#embers--particleemitterembers) | Particle | **E**, W | buoyant curl-noise sparks, blackbody curve | Part | rate, rise, heat | `audio.onset` burst | S | EMIT |
| [Snow](catalog-particles.md#snow--particleemittersnow) | Particle | **W**, C | near/far camera volumes, wind, soft | Part | intensity, size, wind | timeline | S | EMIT |
| [Rain](catalog-particles.md#rain--particleemitterrain) | Particle | **W**, C | stretched streaks + splash rings + mist | Part | intensity, streak, splash | `music.drop` | S | EMIT |
| [Ash](catalog-particles.md#ash--particleemitterash) | Particle | **W**, C | tumbling flakes + embers | Part | intensity, embers | – | S | EMIT |
| [Leaves](catalog-particles.md#leaves--particleemitterleaves--merges-falling-leaves) | Particle + Organic | **E**, W | leaf cards with tumble, canopy source | Part | rate, tumble, colours | `audio.rms` wind | S | EMIT |
| [Spores](catalog-particles.md#spores--particleemitterspores--merges-floating-spores) | Particle + Organic | **E**, W | buoyant glowing motes, vortex attractor | Part | density, glow, drift | `audio.mid` | S | EMIT |
| [Magic Particles](catalog-particles.md#magic-particles--particleemittermagic) | Particle | **E**, W | orbit + attractor + short ribbons, hue sweep | Part | rate, swirl, palette | `owner.speed` | S | EMIT |
| [Cosmic Dust](catalog-particles.md#cosmic-dust--particleemittercosmicdust) | Particle | **W**, C | orbital flow, vortex attractor, size skew | Part | density, flow | `audio.bass` | S | EMIT |
| [Stars](catalog-particles.md#stars--stars--not-a-particle-effect) | Particle (Sky) | **W** | parameterised hashed-cell star field | Sky | density, magnitudeSlope, twinkle | `audio.treble` | S | new Sky uniform block |
| [Fresnel](catalog-stylization.md#fresnel--fresnel) | Stylization | **E**, M | `pow(1−N·V)` additive or opacity | Mat | color, power, bias, mode | `audio.treble` | S | FXL |
| [Rim Light](catalog-stylization.md#rim-light--rimlight) | Stylization | **E** | view × direction rim (kicker) | Mat | direction, threshold, intensity | `beat.pulse` | S | FXL |
| [Dissolve](catalog-stylization.md#dissolve--dissolve) | Stylization | **E** | noise threshold clip + burning edge, in depth/shadow too | Geo, Mat | progress, scale, edge | timeline | M | FXL (clip), EMIT |
| [Hologram](catalog-stylization.md#hologram--hologram) | Stylization | **E** | blended re-route; scan bands, Fresnel, flicker, glitch | Mat, Geo | scan, flicker, glitch | `audio.onsetStrength` | M | FXL, REDRAW variants |
| [Scanlines](catalog-stylization.md#scanlines--scanlines) | Stylization | **C** | beam-width CRT lines, roll, mask | Post | density, strength, roll | `beat.pulse` | S | FXPOST |
| [Chromatic Aberration](catalog-stylization.md#chromatic-aberration--chromaticaberration) | Stylization | **C**, E | existing lens CA + spectral taps; id-masked glitch | Post | amount, center, mode | `beat.pulse` | S | FXPOST (+ existing lens) |
| [Pixelation](catalog-stylization.md#pixelation--pixelation) | Stylization | **C**, E | block-centre sampling; id-masked | Post | blockSize, levels | `beat.pulse` | S | FXPOST |
| [Dithering](catalog-stylization.md#dithering--dithering) | Stylization | **C** | Bayer ordered, display-referred, view-stable option | Post (H2) | levels, palette, stable | – | S | FXPOST (H2) |
| [Toon-ish Edge Treatment](catalog-stylization.md#toon-ish-edge-treatment--toonedges) | Stylization | **C**, E | depth/normal/id Sobel; inverted hull | Post / +draw | width, thresholds | `beat.pulse` | M | FXPOST, REDRAW |
| [Color Cycling](catalog-stylization.md#color-cycling--colorcycling) | Stylization | **E**, C, M | hue rotation with a spatial term | Mat | speed, range, frequency | `beat.phase` | S | FXL |
| [Echo](catalog-temporal.md#echo--temporalfilterecho) | Temporal | **C**, E | sparse FIR over the ring (FrameEcho migrated) | SS | copies, spacing, decay | `beat` spacing | S (E: M) | TEMPORAL (history ids) |
| [Freeze-Frame](catalog-temporal.md#freeze-frame--freezeframe) | Temporal | **W**, **E**, **C** | local-time hold (Pose); ring hold (Image) | Geo / SS | mode, hold | trigger `music.drop` | M | LOCALTIME, TEMPORAL warm-up |
| [Time Dilation](catalog-temporal.md#time-dilation--timedilation) | Temporal | **E**, W | per-owner clock τ on the grid, checkpointed | (simulation) | rate, region | timeline | XL | LOCALTIME |
| [Temporal Smear](catalog-temporal.md#temporal-smear--temporalfiltersmear) | Temporal | **C**, E | dense box/max FIR | SS | length, blend | `audio.rms` | S | TEMPORAL |
| [Ghosting](catalog-temporal.md#ghosting--temporalfilterghost) | Temporal | **E**, C | dense exponential FIR with drift | SS | length, decay, drift | `owner.speed` | S | TEMPORAL (history ids) |
| [Delayed Motion](catalog-temporal.md#delayed-motion--delayedmotion) | Temporal | **E** | HIST delay line; follow a leader; on-twos stepping | Geo | mode, delay, stepRate | `beat` | S | HIST, XFORM |
| [Reverse Playback-Like](catalog-temporal.md#reverse-playback-like--reverse) | Temporal | **E**, **C** | visual HIST rewind with blend-back; reversed ring | Geo / SS | span, speed, return | trigger `music.break` | M | HIST, TEMPORAL |

**Counts by complexity (73):** S 41 · M 25 · L 5 · XL 2. Time Dilation and God Rays are the two XL
effects. Several others become XL only in an optional mode: Portal Remote View and occluded
Volumetric Beam.
