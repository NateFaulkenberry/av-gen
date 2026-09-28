## Modulation audit for the GV3 revision (read-only)

(Written by the coordinator from the audit agent's hand-back, because the agent's own file write was cut short. Paths are relative to the av-gen-gv3 worktree at the time of the audit. The agent's one scratch file is `chain_sim.py` in this directory's parent, which simulates the route chain against GV2's event routes at 60 fps.)

The engine can already do a hierarchical, staggered response, but four gaps stop a Director from producing one reliably. GV3 used little of what exists: every hero cap and all the small mushrooms are driven through shared material parameters, so any route on them moves everything in lockstep.

Nothing was rendered for this audit. Every "reaches the pixels" claim below is a code path; where a GV3 render has already confirmed one, it says so.

### 1. Audio analysis

- **What the analyser produces** (`src/analysis/analyzer.cpp`):
  - An STFT of 2048 samples with a hop of 512 (about 94 frames/s).
  - rms, peak, and five bands: bass, lowMid, mid, highMid, treble (lines 25-30).
  - Spectral centroid, spectral flux, onset strength, and one broadband onset.
- **No smoothing, and the bands are auto-gained.** Each band is divided by its own running peak, which decays over 4 s (`analyzer.hpp:32-34`, `analyzer.cpp:198-212`). A section 4.6 dB quieter reads full-scale again within about 2 s. The raw band levels never reach the signal bus, and there are no per-band onsets.
- **Signals on the bus** (* marks a one-frame event):
  - `audio.{rms, peak, bass, lowMid, mid, highMid, treble, spectralCentroid, spectralFlux, onsetStrength, onset*, tempo, tempoConfidence, beat*, beatPhase, beatCount}`
  - `beat.{phase, pulse*, count, bpm, bar, phrase, phraseCount, phrasePulse*, section, sectionCount}` and `time.*` (`engine.cpp:59-71`)
  - `music.{beat, downbeat, bar, phrase, section, energyRise, energyDrop, build, break, drop, impact}*` (`musical_events.cpp:10-22`), smoothed over 0.45 s and 4 s
  - `lfo.*`, `env.*`, `noise.*`, `random.*`, `timeline.*`, `control.*`, `macro.*`, `state.*`, `field.<n>.*`, `entity.<n>.*`, `character.<n>.*`
- **Why bars start on beat 4.** The first tracked beat is counted as 1, not 0 (`analysis_track.cpp:60`). `beat.bar` is `(count % 4 + phase) / 4` (`engine.cpp:4253`), and a downbeat is `count % 4 == 0` (`music_runtime.hpp:118-121`).
  - Nothing estimates where the real downbeat is, and there is no offset setting.
  - The same off-by-one reaches LFO beat-sync (`source.cpp:209`), the timeline's beats time base (`engine.cpp:4375`) and the shaders' bar input.
  - The effect trigger's Beat source counts from 0 (`effect_trigger.cpp:227-240`), so it and the bus disagree by one beat.
  - `phraseBars` is 4, but this track's phrases are 8 bars.
- **Sections never reach the bus.** Neither the detected `songPlan` nor the authored `sequence.sectionTimeline` (13 sections, each with an energy value) is published. `beat.section` and `music.section` are just 16-bar counters.
- **Caveats (inferred, not measured):**
  - Offline, `audio.beat` loses about a third of beats at 60 fps: only onsets are merged across a render frame's batch of analysis frames (`engine.cpp:4186-4205`). Use `beat.pulse` or `music.beat` instead.
  - Material programs read raw, unsmoothed band values (`scene_renderer.cpp:2932-2941`).

### 2. The route chain

- **Stages, in order:** gain, offset, curve, clamp, threshold, attack/decay, envelope, remap. The result is then multiplied by amount, spatial gain and master gain (`processor.cpp:64-140`, `modulation.cpp:129`).
- **Ops** apply in the order Replace, Multiply, Add, Min, Max, per component or all components. Timeline tracks apply before routes.
- **Why event routes get swallowed.** Smoothing runs before the envelope, so a one-frame event only reaches `1 - e^(-16.7ms/attack)` of its amount: 81% at 10 ms attack, 24% at 60 ms, 1% at 1.4 s.
- **Master and spatial gain scale the output, not the depth.** Below 1 they pull a Multiply route's target towards 0 rather than reducing the modulation.
- **What is missing:**
  - A per-route delay or phase offset.
  - A way to scale a route's depth by another signal: `amount` is not a parameter, so section-aware depth on an Add route is impossible.
  - Per-target stagger or randomisation.
  - Spatial scaling outside entity reactions (spatial gain is set only for entity-reaction routes, `entity.cpp:3304-3310`).
  - Envelope sources fire only on bus events (`source.cpp:384`), but timeline sources publish continuous values (`source.cpp:588`), so GV3's scored pulses cannot trigger an envelope.
- **Workarounds that exist today:**
  - Copies of a timeline source with different `sources/<n>/offset` values.
  - The `phase` rows on LFO, Pulse, Breathing and ColorCycling.
  - Trigger activation plus the per-effect `fx/<id>/delay` row.

### 3. The entity effects library

- **Size:** 48 kinds (enum 0-47).
- **Ownership works on procedural nodes.** Entity-targeted kinds attach to any node by name, including GV3's procedural hero parts. The per-object effect lanes multiply after the material program (`pbr_shade.wgsl:745`); GV3 already confirmed this on `elder-2-gills` (finding F23).
- **But each lane effect covers only its own node's draws** (`composition.cpp:4024-4082`). Each hero is four unparented nodes. All ten caps share `glowmere2Cap` and nine heroes share `glowmere2TissueCool`. So a per-hero light handle needs one effect on `<hero>-under` and one on `<hero>-gills`.
- **Parameters:** every row is `fx/<id>/<leaf>`, plus the timing rows `delay`, `lifetime`, `fadeIn`, `fadeOut`, `windowStart`, `windowSeconds` and `repeat` (`effect_registry.cpp:488-507`).
  - Trigger settings are stored in the file, not as parameters.
  - A triggered lane effect honours its own `delay` (`entity_fx.cpp:62`) and relaxes back to neutral gain (`:425`).
  - Only the latest trigger is live, so a delay must be shorter than the trigger period.

| Kind | What it visibly does | Default route |
|---|---|---|
| Glow | Brightness multiplier on all emission, added self-glow, rim, and a spill light that lights the surroundings (16-light pool) | `beat.pulse` → gain |
| Pulse | Whole-object swell, or a band travelling along an axis; Heartbeat waveform; `phase` row | `beat.pulse` → peak |
| Bioluminescence | Spots, stripes or cells, each breathing on its own phase, plus a periodic wave up the body | `audio.mid` → intensity |
| Pulsing Veins | Light pulses travelling along a vein network | `audio.bass` |
| Breathing, Organic Pulsation | Surface swell or a travelling bulge, applied in every pass including shadows | rms / bass |
| Color Cycling | Hue rotation | `audio.treble` → speed (defect, below) |
| Ground Pulse | A radial front through ground and foliage that also brightens existing emission, the small mushrooms included (`wave_effects.wgsl:64-68,176`); at most 8 live | `beat.pulse` |
| Rim Light, Fresnel, Bloom Source, Halo | Rims, bloom share, glare or ring | treble / beat |
| Float, Bounce, Shake | Visual-only motion that children follow; at most 64 owners | — |

- **The library's defaults are the visualiser:**
  - Default routes are installed only by the Add-Effect gesture (`engine.cpp:770-806`), as Add routes with attack and decay only.
  - 15 kinds default to `beat.pulse`.
- **ColorCycling's default route is a defect.** Hue is `renderTime × speed` (`pbr_shade.wgsl:268`), so routing `speed` makes the hue jump in proportion to elapsed time. At 180 s, a 0.01 change in treble moves the hue about 0.7 of a turn; expected to read as noise (not rendered).
  - The same trap applies to the aurora's flow and drift speeds (`atmospherics.cpp:697,732`), Pulse `rate`, particle `pulseRate` and LFO `rate`. Key a phase instead of ramping a speed.
- **Scale and motion:** `nodes/<n>/scale|position|rotation` are live on procedural nodes (`composition.cpp:5651-5662`).
- **GV3's hero `reactionProfile: "organism"` does nothing.** Profiles are installed only by the world builder (`world_builder.cpp:116-150,336`). If they were installed, the emission reaction targets `emissiveBoost` (dead on these nodes) and the scale reaction would scale the cap alone.

### 4. The source film (GV2 multicam)

It had 40 routes and 18 effects: the aurora, the camera travel beam, and 16 Ground Pulses that fire only while the cut is on their hero.

- **Dead routes:**
  - Three `emissiveBoost` routes on the elder (downbeat and drop onto the gills, section onto the cap). The boost is written only to mesh entities (`composition.cpp:7758`) and these parts are lit by a material program anyway (ADR-179).
  - Seven swallowed event routes. Share of their amount that reaches the target: phrase → spore turbulence 1.2%, section 0.8%, build → wind 0.7% and spawn 0.6%, break → wind 1.1% and spawn 1.2%, drop → volume scattering 6.4%.
  - downbeat → elder practical light is half-dead: it reaches 24% of its amount, and lands on beat 4.
  - The 16 hero pulses are dormant without a Song-mode cut, which GV3 no longer has.
- **Visualiser routes:**
  - 18 `beat.pulse` routes: +6.5 on a ring intensity of 5, plus the travel beam and the aurora edge.
  - bass → water glow +1.3, which pumps with the sidechained bass.
  - `music.beat` → all mushroom tissue.
- **Ideas worth keeping:**
  - Hero rings as spatial propagation.
  - The elder's practical light answering the downbeat.
  - A spore burst and thicker air on the drop.
  - Build and break moving the wind and spores, done as timeline keys.
  - Treble on water sparkle and river motes.

### 5. Hooks at each level

- **Micro (individual objects):**
  - Hero lane effects per part (section 3).
  - `nodes/<part>/scale`.
  - `particles/<hero>-spores/{spawnRate, emissive, burst}`.
  - `procedural/<part>/emissiveFieldAmount`: a spatial field multiplier applied after the program (`procedural.wgsl:522`, then `pbr_shade.wgsl:773`). Not verified on program-lit parts.
  - The small mushrooms (fungi, shelf-fungi, beacons) can only be reached through the one shared material `material/glowmereTissue/*`: its `emissionIntensity`, its op constants such as `op/10/constant/constant` (the emission colour), or a GV3-only fork of the program using per-instance-random and world-position ops.
- **Meso (clusters and regions):**
  - Ground Pulse rings.
  - Glow spill lights.
  - `lightrig/GlowmereValley/elder-practical/intensity`.
  - `nodes/valley/water/{glow, ripple, swell, sparkle, foam}`.
  - Firefly `pulseRate`, `pulseDepth` and `pulseSync` (0 = each blinks on its own phase, 1 = all together; `particles.hpp:233-242`).
  - `field/<n>/*` travelling-wave fields.
  - The light the mushrooms throw onto their surroundings is fixed when the scene is built (`composition.cpp:8547-8610`) and cannot be modulated.
- **Macro (world):**
  - The existing arc (exposure, grade, fog, valley light, aurora).
  - `scene/volume{Density, Scattering, Emission}`.
  - `scene/windSpeed` and `wind/*`, which are live.
  - `fx/aurora/*`.
  - The light rig's ambient and key intensity.
  - `env/sky/*`.
  - Bloom.

### 6. Deficiencies that block "a world, not a visualiser"

1. No bar alignment and no downbeat estimate.
2. Auto-gained bands, so the audio signals carry no section energy.
3. No section signal on the bus, and no way to vary a route's depth.
4. No per-target delay or phase for audio-driven routes.
5. The scattered mushrooms can only be addressed as one whole material.
6. The silent no-op family keeps growing: swallowed attacks, `emissiveBoost` on procedural or program-lit nodes, routed speeds, beat-default routes, inert reaction profiles.
7. Onsets are broadband only, so kicks have to be hand-scored (GV3 scored 475).

### 7. Minimum engine additions

- **A. Bar offset.** A `control.barOffset` (in beats) applied everywhere the beat count becomes bars: `engine.cpp:4253-4262, 4375, 5267`, `music_runtime.hpp:118-121`, `source.cpp:209`, `scene_renderer.cpp:2939`.
  - Tests in `test_music_runtime.cpp`: `music.downbeat` lands on accented clicks and shifts with the offset.
  - Tests in `test_trigger.cpp`: the trigger's every-4 beats land on the same instants.
- **B. A route audit at `Modulator::bind`** (`modulation.cpp:50-99`), plus a JSON dump for the evaluator's "configuration" tier (brief §16). It should flag:
  - event routes whose attack lets through less than 50% of the amount;
  - `emissiveBoost` or `emissiveIntensity` on program-owned surfaces;
  - speed rows of oscillators whose phase is time × speed.
  - Also land the brief's `emissiveBoost` fix as a multiplier applied after the program, replacing `composition.cpp:7755-7759`.
  - Tests: `test_modulation.cpp`, plus a GPU difference-image test on a program-lit procedural node.
- **C. Section awareness.**
  - Publish `section.{index, progress, energy}` and a `section.change` event from `sequence.sectionTimeline` (`seq/sequence.hpp:642`), declared in `declareFrameSignals` so seek replay sees them.
  - Add `ModRoute::depthSource`, a signal that scales the route's deviation from its op's neutral value (`modulation.hpp/.cpp`, `serialization.cpp`).
  - Tests in `test_modulation.cpp` and `test_section_timeline.cpp`.
- **D. Per-route delay.** `ProcessorChain::delayMs` as the first stage, with a time-stamped ring buffer; events stay events.
  - Tests in `test_processor.cpp`: a 100 ms delay lands at frame 6 at 60 fps and frame 3 at 30 fps.
- **Optional:** long-term (non-auto-gained) band levels, band-limited onsets, and an event mode for timeline sources.

A-D are what a Director needs to produce a plan like the one below on its own. The GV3 plan itself needs none of them.

### 8. Hierarchical plan for GV3 in existing parameters

Each element gets its own musical owner and its own clock. Depths multiply onto the arc-keyed base values, so the sections gate them. Confirm every item with a before/after difference image (the lesson of F23).

**Macro**
- Key `scene/windSpeed` and `wind/gustAmount`: +25% in the lift and arrival, -50% in the break, +40% in the drop.
- Key `scene/volumeScattering` +0.08 over the drop's first two bars.
- Route `audio.mid` → `fx/aurora/intensity`, Multiply, remap 0.9-1.15, attack 400 ms, decay 1600 ms, so the aurora answers the lead.

**Meso**
- **Elder's practical light:** a copy of the kick source, `kick-late`, with offset -0.05 s, routed to the elder-practical intensity with Add 2.5 and decay 320 ms. The ground under the elder echoes the heartbeat 50 ms late.
- **Phrase rings:** a Ground Pulse owned by `elder-2-cap` with the trigger `{"source":"repeat","period":14.769,"phase":74.306}` (every 8 bars from bar 41), emissive response 1.4. Key its `intensity` to 0 in the suspension and break. The mushrooms flare as each ring passes.
- **Crash ring:** a second Ground Pulse on the `drop` marker, speed 30, range 150.
- **Mushroom colour by section:** key `op/10/constant/constant`: cyan-green, then blue in the suspension and break, then gold-tinged in the drop.
- **Mushroom breath:** a `breath-late` LFO one beat behind the existing breath, Multiply 0.94-1.08 on the mushroom material. Keep the kick-gap dips.
- **Water:** `audio.treble` → `water/sparkle`, Multiply 0.8-1.5; `lfo.breath` → `water/swell`, Add 0.015.
- **Fireflies:** `pulseRate` 1.0833 Hz, `pulseSync` keyed 0.05, then 0.4, then 0.9 in the drop, so the chorus synchronises only at the climax.
- **Spores:** the crash source → `particles/spores/burst`.

**Micro (heroes)**
- **Elder:** keep the kick heartbeat. In the riser, add a travelling-band Pulse on `elder-2-under` with rate 0 and its `phase` keyed as a sawtooth, climbing 1, 2 and then 4 times per bar across bars 93-96.
- **Lantern (claps):** a scored `timeline.clap` → Glow `gain` on `lantern-under` and `lantern-gills`, Add 0.8, decay 180 ms. A scored clap is safer than the Beat trigger, whose every-other-beat parity breaks if the tracker slips a beat.
- **Bloom (riff):** Bioluminescence on `bloom-cap`, with `audio.mid` → its intensity.
- **Umbra and veil (bass glide):** a Pulse at 0.2708 Hz, depth 0.2, with `phase` +0.1 and +0.2 respectively, so the breath travels outward.
- **Spire (shimmer):** Pulsing Veins on `spire-cap`, with treble → intensity, enabled only in bars 41-80 and 97-120.
- **Cairn, ridge, scree, ember:** stay still.
- **The crash:** a Glow on each hero's `-under`, triggered by the `drop` marker (gain 2.5, lifetime 1.6 s), with `delay` = distance ÷ 120 m/s. That gives bloom 0.69 s, lantern 0.72, umbra 1.05, spire 1.46, ember 1.77, ridge 2.35: the valley relights outward from the elder.
