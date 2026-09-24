# Parameters and Modulation

This page is part of the [Effect Library](README.md). It covers three things:

- the parameter conventions every new effect follows;
- how entity-derived modulation is published without an effect-specific hook;
- a per-effect table of beat leaf, default routes and recommended modulation targets.

The full row lists are in each catalog entry, under 2.6.

## 1. Conventions (inherited from ADR-230/500/702, restated because they bind)

- **One row is one parameter.** A row in the schema is a parameter path `fx/<id>/<leaf>`. It is
  also a slider, a timeline key, a preset member, a JSON key and a modulation target
  (`effect_registry.hpp:127-133`). A new effect declares rows; it never registers parameters by hand.
- **Row types.** Rows are `storedFloat`, `storedColor`, `storedBool` or `storedChoice`, and live in
  `EffectInstance::values`. A new kind does not edit the shared header (`effect_instance.hpp:25-30`).
- **Ranges.** Each row has two ranges:
  - the *soft* range, which is the slider;
  - the *hard* range, which is the route clamp.

  A row that is a divisor declares `floorAt`. This applies to `period`, `persistence`, `radius` and
  `duration`.
- **Choices are stored by name in JSON.** This applies to `mode`, `pattern`, `waveform`, `kernel`,
  `interiorMode` and every other "which of these".
- **Main page.** Each type puts at most 6 rows on the Main page. Everything else goes on Advanced.
- **Shared rows** (`sharedEffectFields`) come free. They cover lifecycle and timing, and they are
  gated per type by `sharedFieldApplies`. **Proposed addition:** the TRIGGER rows (`triggerSource`,
  `triggerThreshold`, `triggerEvery`, `maxConcurrent`, `cooldown`). Only types whose
  `triggerSources` metadata is non-empty get them, through the same `sharedFieldApplies` gate.
- **No audio hook on any effect.** `effect_params.hpp:7-14` says: *"Entity-derived modulation (a
  UFO's velocity driving its Space Warp) belongs in the same place for the same reason: a signal on
  the bus and a route onto `fx/<id>/<leaf>`, not a field an effect reads off its owner."* This
  document is the design for that sentence.

## 2. Entity-derived signals

### 2.1 What exists

The bus is `src/signals/signal_bus.hpp`. It has hierarchical names, `declare(name, min, max,
isEvent)`, and one value per frame. The sources today are:

| Family | Signals |
|---|---|
| `audio.*` | `rms`, `peak`, `bass`, `lowMid`, `mid`, `highMid`, `treble`, `spectralCentroid`, `spectralFlux`, `onsetStrength`, `onset` (event), `tempo`, `tempoConfidence`, `beat` (event), `beatPhase`, `beatCount` |
| `time.*` | |
| `beat.*` | `phase`, `pulse`, `count`, `bpm`, `bar`, `phrase`, … |
| `music.*` | `beat`, `downbeat`, `bar`, `phrase`, `section`, `energyRise`, `energyDrop`, `build`, `break`, `drop`, `impact` |
| `field.<name>.*` | `occupancy`, `enter`, `exit` |
| other | `lfo.*`, `env.*`, `noise.*`, `random.*`, `timeline.*`, `control.*`, `macro.*`, `state.*` |

**No `entity.*` or `camera.*` signals exist.** `Engine::cameraVelocityOnTimeline()` (`engine.cpp:3846`)
computes the camera's velocity, but it reaches only `EffectContext::cameraVelocity`.

**A precedent for per-entity routes exists.** An entity's `reactions` block is compiled into routes
marked `fromEntity` (ADR-088, `src/scene/composition.cpp:1680-1690`). The owner alias below is built
the same way.

### 2.2 Proposed signals

The engine publishes these once per frame, before routes run, from the **last completed simulation
step** (see rendering-architecture §3).

**Per entity**, for every entity that has at least one effect attached or that some route
references:

| Signal | Range (declared) | Source |
|---|---|---|
| `entity.<name>.speed` | 0–50 m/s | `EntityState::velocity` (simulated) or HIST finite difference over the grid (track-driven) |
| `entity.<name>.velocity.x/y/z` | ±50 | same |
| `entity.<name>.acceleration` | 0–100 m/s² | `EntityState::acceleration` / HIST second difference |
| `entity.<name>.angularSpeed` | 0–4π rad/s | HIST |
| `entity.<name>.cameraDistance` | 0–500 m | the previous step's camera position |
| `entity.<name>.screenSize` | 0–1 (projected radius / viewport height) | bounds + the previous step's camera |
| `entity.<name>.onScreen` | 0/1 | the same |
| `entity.<name>.timeRate` | −4–4 | LOCALTIME |
| `entity.<name>.moving` | event (edge of speed above a threshold) | derived |

**For the camera:** `camera.speed`, `camera.velocity.x/y/z` (exposing the existing
`cameraVelocityOnTimeline`), and `camera.fov`.

**Per effect**, declared at install from the schema's `publishes` list:

| Signal | Meaning |
|---|---|
| `fx.<id>.age` | seconds since the last trigger |
| `fx.<id>.envelope` | 0–1 |
| `fx.<id>.phase` | 0–1 |
| `fx.<id>.charge` | Charge-Up |
| `fx.<id>.release` | event, Charge-Up |
| `fx.<id>.trigger` | event |

**Cost.** One `declare` per signal at install. Per frame, each signal costs a few floats. Signals are
declared only for entities that are subscribed, so a scene with 278 entities and 5 effect owners
publishes about 45 values, not 2,500.

### 2.3 The `owner.` alias

A preset cannot name its owner, because it is written before it is attached. So a default route in
a schema may use the source prefix **`owner.`**. For example, Space Warp's
`{"owner.speed", "strength", 0.8, …}`.

`defaultEffectRoutes(effectId, kind)` becomes `defaultEffectRoutes(const EffectInstance&)`, and it
resolves the prefix against the instance's owner:

| Owner | `owner.speed` becomes |
|---|---|
| Entity | `entity.<owner name>.speed` |
| Camera | `camera.speed` |
| Light | `light.<name>.*`: speed and cameraDistance, taken from the light record's position (published only for lights that own effects) |
| World | not resolved. The route is **refused, by name**, at registration, matching `checkRegistry`'s "named failure" style. |

When the owner is renamed (`renameEffectOwner`), the resolved routes are regenerated. This is why
they are resolved at install time and not stored resolved. It is the same lifecycle as `fromEntity`
reaction routes, and the flag `fromEffectDefaults` marks them. It is the fourth member of the
`fromGraph`/`fromEntity`/`fromMacro` family (`params/modulation.hpp:30-38`). That family exists
because a route a subsystem installs is otherwise erased by the next project reload.

### 2.4 Determinism of each modulation source

| Source | Exact under seek? | Why |
|---|---|---|
| `audio.*`, `beat.*`, `music.*` in playback/offline | yes | the offline `AnalysisTrack` is addressable by time (`analysis_track.hpp`) |
| `time.*`, `lfo.*`, `timeline.*`, the timeline | yes | pure functions of t |
| `entity.*` | yes | the last completed step; ADR-700 replays it exactly |
| `camera.*` | yes, when the camera is a pure function of t or of exact entity state | |
| `fx.*` | yes, when the effect's schedule is pure (TRIGGER) | |
| `random.*`, `noise.*` | yes if seeded by t (verify per source); no if wall-clock seeded | |
| `control.*`, MIDI, live audio input | **no** | live tier, ADR-091. The panel says so. |

**Events vs TRIGGER.** A bus *event* is a one-frame pulse. It cannot be searched backward, so a
TRIGGER cannot be driven by "the last time `audio.onset` fired". Instead, each TRIGGER source reads
the *schedule* the event comes from: onsets and beats from the analysis track, and music events from
`musical_events`. Routing an event onto a parameter (for example `audio.onset →
particles/sparks/burst`) is still fine for live use. It is simply not scrub-exact, and that is
stated.

## 3. Per-effect modulation table

*Beat leaf* is the row the panel's "Beat response" slider drives (`EffectSchema::beatLeaf`). *Default
routes* are what a freshly added instance carries. Every type must declare at least one;
`checkRegistry` enforces this.

| Effect | Beat leaf | Default route(s) | Other recommended targets |
|---|---|---|---|
| Space Warp | `strength` | `owner.speed → strength` 0.8 | `swirl ← audio.bass`, `chroma ← owner.acceleration` |
| Gravitational Lens | `einsteinRadius` | `audio.bass → einsteinRadius` 0.3 | `photonRing ← beat.pulse` |
| Heat Shimmer | `strength` | `audio.rms → strength` 0.4 | `riseSpeed ← owner.speed` |
| Shockwave | `strength` | trigger `music.drop`; `audio.rms → strength` 0.3 | `maxRadius ← owner.speed` |
| Ripple | `amplitude` | trigger `beat` (every 1) | `amplitude ← audio.treble` |
| Bubble | `wobbleAmount` | `audio.mid → wobbleAmount` 0.5 | `iridescence ← beat.pulse` |
| Time-Warp Distortion | `delayFrames` | `owner.timeRate → delayFrames` −1.0 (slower = more lag) | `swirl ← beat.pulse` |
| Portal | `rimEmission` | `audio.bass → rimEmission` 0.8 | `open ← timeline`, `swirl ← beat.pulse` |
| Reality Tear | `open` | trigger `music.drop` | `flicker ← audio.onsetStrength` |
| Radial Distortion | `amount` | `beat.pulse → amount` 0.4 | `zoomBlur ← camera.speed` |
| Glow | `gain` | `beat.pulse → gain` 1.0 | `gain ← audio.bass`, `bloomBoost ← owner.cameraDistance` |
| Pulse | `depth` | none needed (its own waveform); `beat.phase` when beat-locked | `rate ← owner.speed` |
| Flicker | `amount` | `audio.treble → amount` 0.3 | `dropoutRate ← music.break` |
| Bloom Source | `weight` | `beat.pulse → weight` 0.5 | – |
| Light Beam | `intensity` | `audio.bass → intensity` 0.6 | `dust ← audio.rms` |
| Volumetric Beam | `intensity` | `beat.pulse → intensity` 0.5 | `hazeDensity ← audio.rms` |
| God Rays | `intensity` | `audio.rms → hazeDensity` 0.3 | – |
| Aura | `thickness` | `audio.bass → thickness` 0.6 | `riseSpeed ← owner.speed` |
| Halo | `intensity` | `audio.treble → intensity` 0.4 | `radius ← owner.cameraDistance` |
| Trail (and Light Trail) | `widthStart` | `owner.speed → opacity` 1.0 | `emission ← audio.treble` |
| Afterimage | `copies` | `owner.speed → fade` 1.0 | `interval ← beat` |
| Motion Smear | `amount` | (implicit on velocity) `beat.pulse → amount` 0.3 | – |
| Velocity Distortion | `strength` | `owner.speed → strength` 1.0 | `chroma ← owner.acceleration` |
| Wind Response / Sway | `strength` | `audio.rms → strength` 0.3 | – |
| Orbit | `radius` | `audio.bass → radius` 0.2 | angle source `beat.phase` |
| Spiral | `r1` | `audio.rms → r1` 0.2 | – |
| Float | `height` | `audio.bass → height` 0.3 | `sync ← beat.phase` |
| Shake | `trauma` | trigger `music.impact`; `audio.bass → trauma` 0.4 | `trauma ← owner.acceleration` |
| Bounce | `height` | trigger `beat` | `height ← audio.bass` |
| Lightning | `coreIntensity` | trigger `music.drop` or `impact`; `audio.rms → randomRate` | `jaggedness ← audio.treble` |
| Arc | `intensity` | `audio.bass → intensity` 0.6 | `rate ← audio.treble` |
| Electric Field | `crackle` | `audio.treble → crackle` 0.6 | `arcRate ← audio.onsetStrength` |
| Plasma | `coreIntensity` | `audio.rms → coreIntensity` 0.6 | `radius ← audio.bass`, `turbulence ← beat.pulse` |
| Energy Shield | `rimIntensity` | hits trigger `audio.onset`; `owner.speed → rimIntensity` 0.3 | `idleReveal ← audio.rms` |
| Force Field | `idle` | `audio.rms → idle` 0.4 | `scrollSpeed ← beat.pulse` |
| Charge-Up | `particleRate` | trigger start `music.build`, release `music.drop` | publishes `fx.<id>.charge` and `.release` |
| Discharge | `flashIntensity` | trigger `fx.<charge>.release` or `music.drop` | – |
| Bioluminescence | `intensity` | `audio.mid → intensity` 0.4 | flare trigger `Proximity`; `breatheRate ← owner.speed` |
| Pulsing Veins | `intensity` | `beat.pulse → intensity` 0.8 | `pulseSpeed ← audio.bass` |
| Growth | `progress` | timeline (no audio default); optional `audio.rms → progress` | – |
| Breathing | `amplitude` | `audio.rms → amplitude` 0.3 | phase `beat.phase` |
| Tendrils | `waveAmplitude` | `audio.bass → waveAmplitude` 0.5 | `reach ← beat.pulse` |
| Organic Pulsation | `amplitude` | `audio.bass → amplitude` 0.5 | interval ← beat |
| Particle presets (all) | `spawnRate` | per preset (Fireflies: `beat.phase → pulseSync`; Embers: `audio.onset → burst`; Leaves: `audio.rms → wind`) | – |
| Stars | `twinkle` | `audio.treble → twinkle` 0.3 | – |
| Fresnel | `scale` | `audio.treble → scale` 0.3 | – |
| Rim Light | `intensity` | `beat.pulse → intensity` 0.5 | – |
| Dissolve | `progress` | timeline; optional trigger | – |
| Hologram | `glitchAmount` | `audio.onsetStrength → glitchAmount` 0.5 | `flicker ← beat.pulse` |
| Scanlines | `strength` | `beat.pulse → rollStrength` 0.3 | – |
| Chromatic Aberration | `amount` | `beat.pulse → amount` 0.5 | – |
| Pixelation | `blockSize` | `beat.pulse → blockSize` 0.5 | – |
| Dithering | `levels` | none (look); optional `music.break → levels` | – |
| Toon Edges | `width` | `beat.pulse → width` 0.3 | – |
| Color Cycling | `speed` | phase `beat.phase` | `speed ← audio.spectralCentroid` |
| Echo / Smear / Ghosting | `strength` | `audio.rms → strength` 0.4 | Echo: `spacing ← beat` |
| Freeze-Frame | `hold` | trigger `music.drop` | – |
| Time Dilation | `rate` | none (authored curve); optional `beat.phase` | publishes `entity.<n>.timeRate` |
| Delayed Motion | `delay` | `beat → stepRate` (Stepped) | – |
| Reverse | `speed` | trigger `music.break` | – |

**A rule the table follows:** no default route targets a leaf that changes a *phase* (period, rate,
spacing in time) from a continuous source. Such a route produces phase jumps, because every phase
here is `t·rate`, not an accumulated integral; an accumulated phase would not scrub. Phase-like
musical sync uses `beat.phase` as the phase itself. This is documented under Pulse 2.8 and Orbit
2.7.
