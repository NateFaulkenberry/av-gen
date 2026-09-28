## GV3 audit: small mushrooms, spatial modulation, wind and macro hooks

(Written by the coordinator from the audit agent's hand-back, because the agent's own file write was cut short. Paths are relative to the av-gen-gv3 worktree at the time of the audit, which is main 83a12334 plus the first-pass fixes. The replica scripts `hue_sim.py` and `wind_sim.py` are in this directory's parent.)

### Headline
- **The small mushrooms are three scatter layers on the `valley` terrain, not nodes.** They are `fungi` (Mushroom_Common, 0.28 m, up to 6,000 instances), `shelf-fungi` (Laetiporus, 0.55 m, up to 900) and `beacons` (1.5 m, up to 400). They are the only users of the `glowmereTissue` program, so `material/glowmereTissue/*` is their only runtime control. (Inference: the `flowers` layer, 0.55 m with a purple emissive, may be part of what the owner calls "small coloured mushrooms".)
- **A wave travelling through them on a musical event can be authored today with no engine work.**
- **Wind is enabled and does reach the vertex stage, in both films.** It looks absent because the motion is slow and a few centimetres, and because GV3 removed GV2's two wind routes.
- **GV3 has several silent no-ops**, listed in §4.

### 1. The mushrooms
- **How they are built.** Each layer becomes a procedural object named `valley_<layer>` (composition.cpp:6394-6454, scatter_anchors.cpp:12-17). Hue variation is forced into the perceptual (OKLab) mode (:6454).
- **Who owns the emission.** The program does (ADR-179). glowmereTissue computes `instanceEmissive × (0.08, 0.95, 0.53) × mask × ramp × 2.0`, and pbr_shade.wgsl:685 replaces the material's emission with that.
  - The layers' authored purple `emissiveColor` and `emissiveIntensity` (6.888 / 4.428 / 6.15) therefore never reach the pixels.
  - They only feed the ecology-light power (composition.cpp:6480-6486) and the base colour the hue offsets are computed against.
  - ADR-179's ownership check covers entities, not scatter, so this goes unflagged.
- **Per-instance variation.** Two kinds exist:
  - Baked once on the CPU (ADR-054, procedural.cpp:2480-2503): `hueRandom`, `hueField` (a regional hue field), `emissiveRandom`, sparsity.
  - Live per vertex (ADR-057 living chroma, procedural.wgsl:386-393, chroma.wgsl:15-20): fungi drift ±0.07 turns of hue over a 20 s period.
- **Inference, with code evidence: the per-instance multiplier `m` is applied twice.** The program reads `instanceEmissive` (procedural.wgsl:531), and `fs_proc` multiplies by the same `m` again after the program (:519, pbr_shade.wgsl:773).
  - Brightness spread becomes m²: 0.12–2.7× instead of 0.35–1.65×.
  - Hue offsets computed against purple land on the program's cyan-green. The replica shows the displayed hue going 145° → 166° → almost grey (chroma 0.02 at +0.08 turns) → 20° pink, not a rotation around 299°.
  - So retuning `hueField` or `chromaDrift` will not behave predictably.
- **What is routable.** Only `material/glowmereTissue/emissionIntensity` plus every op's `value`, `constant..constant4` and `enabled` (material_params.cpp:32-91, applied each frame at composition.cpp:8103-8110). Routes run after timeline tracks, so they stack on the arcs (engine.cpp:5151-5162).
- **What is not routable, per layer:**
  - The terrain registers only LOD, cull, distance and water parameters (composition.cpp:5291-5331).
  - Scatter objects have no `proceduralIndex` (:3705-3716), so the entity lane effects (Pulse, Glow, ColorCycling, Bioluminescence) cannot attach to them.
  - `nodes/valley/emissiveBoost` writes entities only (:7757-7758).
  - `hueField` and `chromaDrift` are not parameters, and scatter JSON has no `emissiveField` key.
- **What GV3 does with them now:** a slow arc from 0.5 to 4.2 on `emissionIntensity`, plus a `timeline.gap` dip to ×0.45. GV2's `music.beat` and `music.impact` routes on the same parameter were removed.

### 2. Local waves, groups and chromatic shifting
- **GroundPulse world effect (ADR-207/702).** It adds a ring of light to every shaded surface (wave_effects.wgsl:73-183).
  - Scatter takes `response/foliage`, and existing glow is amplified by `response/emissive` (:176-177), so mushrooms flare as the ring crosses them.
  - It can fire on the music with `activation: trigger` (effect_timing.hpp:46, 71-96): every Nth beat with an offset, onset threshold, `musicEvent` (drop, impact, build, downbeat), `timelineMarker` (GV3 has 13 section markers, including `riser` and `drop`), or a repeat.
  - Trigger times are derived from the offline analysis track (effect_trigger.cpp:171-188), so it is a pure function of time and scrub-safe. All its numbers are `fx/<id>/…` parameters (wave_rows.hpp:40-130).
  - Limits: at most 8 live at once (wave_effect.hpp:59), a single origin, it cannot be scoped to a layer, and the amplified light is not hue-shifted.
- **Material program.** Inputs include `worldPosition`, `instanceRandom`, `time`, `audio` (rms/bass/mid/treble), `audioBands` and `beatPhase` (material_program.hpp:82-92).
  - These are filled from the analysis frame (pbr_shade.wgsl:655-676, scene_renderer.cpp:2934-2940), which offline renders do pass (render_job.cpp:671-672).
  - Useful ops: `Palette` (a cosine, so periodic), `HueShift` (its angle is a routable `value`), and `Field`.
  - glowmereTissue uses 14 of 48 ops. The GPU table holds 8 programs and GV3 already uses all 8 (material_programs.hpp:36). A ninth is silently not uploaded, so edit glowmereTissue in place and append ops so existing parameter paths stay stable.
- **Scene field of kind `wave`.** Its value is `s = dist − waveOrigin − waveSpeed·t` with a pulse shape (fields.wgsl:347-365). All its parameters are registered, including `waveOrigin`, `position`, `strength` and `falloff/outer` (field_params.cpp:85-125).
  - The same field can also shape the mist via `volumeDensityField`/`volumeColorField` (volume.wgsl:619-621, 1086-1087), and hero parts via `emissiveField`, which applies after the program (procedural.wgsl:520-523).
- **ADR-097 spatial gain** works per entity route, not per instance, so it does not help scatter.
- **Answer: yes, a route or event can send a wave through the mushrooms.** Three ways:
  1. A GroundPulse on a trigger.
  2. A field ring moved by timeline step keys (`waveOrigin = −v·t₀` at each event), which is pure in time.
  3. A `LinearFall` envelope with a reversed remap on `waveOrigin`. This is stateful, so seeking gives a different result from playing.
- **What is missing:** scoping a wave to one layer, field time measured from the last event, and hue shift on the wave's amplification.

### 3. Wind
- **Settings.** Identical in GV2 and GV3: speed 0.74, gusts 0.52 × 38 m at 4 m/s, turbulence 0.22.
- **Code path, all live each frame:**
  - parameters `scene/windSpeed`, `scene/windDirection`, `scene/wind/*` (composition.cpp:4514-4553);
  - copied into the environment each frame (:8332-8346) and packed into the frame block (scene_renderer.cpp:2911);
  - per-species gains recomputed per draw (procedural_renderer.cpp:1477, 1766-1771);
  - vertex bend (procedural.wgsl:448-472).
- **What responds:** all 16 vegetation layers (not boulders or pebbles), the lilies, petals and foreground leaves, and all 44 hero parts.
  - The simulated-plant tier (ADR-056) is off everywhere.
  - Among particles only tree-fireflies respond (`windInfluence` 0.25); spores and motes are at 0.
  - Fog, volume and water never sample the wind.
- **Why it looks absent** (replica of wind.cpp:94-128):
  - The gust period is 9.5 s. Grass, ferns and flowers swing 3–6 cm, with about 1 cm of flutter.
  - Trees resonate at 0.07–0.09 Hz (mass 40–60 against stiffness 9–20).
  - Most of the displacement is a static 7–20 cm lean, which reads as shape rather than motion. Fungi move about 2 mm, which is correct for them.
  - GV3 also dropped GV2's `music.build → +0.18` and `music.break → −0.2` routes on `scene/windSpeed`, and its art direction put trees under "never react" (02-art-direction §2.6).
- **Music modulation.** Bend scales linearly with `windSpeed` (wind.cpp:77).
  - Safe to route: `windSpeed`, `gustAmount`, `turbulence`, and `windDirection` if changed slowly.
  - Never route `gustSpeed`, `gustScale`, `turbulenceSpeed`, `turbulenceScale`, `regionDrift` or `flutterScale`. Their phases are time × rate (wind.cpp:63), so any change makes the field jump. `scene/volumeNoiseSpeed` has the same hazard (volume.wgsl:18).

### 4. Macro hooks and dead targets
- **Registered and routable:**
  - `lightrig/GlowmereValley/{keyIntensity, ambient*, moon/*, skyfill/*, elder-practical/*}`
  - `env/{intensity, sky/*, skyBloom}`
  - `scene/{brightness, keyLight, styled*Ambient, fog*, volumeDensity|Scattering|Emission|Noise|LocalLights}`
  - `fx/aurora/*` (48 parameters). The aurora is already audio-reactive through its `audioBass`/`audioHigh`/… weights, which read the frame block (atmosphere_fx.wgsl:392-396). `audioBeat` is 0 in GV3.
  - `post/{bloom, grade, look/atmospheric, halation, tonemap/chroma-retention}` and `camera/exposure/compensation`.
- **Not routable:** `ecologyLight` (1.4 in GV3, composition.cpp:8607). The light the mushrooms cast on the ground never pulses with them.
- **Dead in GV3, with no warning unless noted:**
  - The arc on `material/paintedGround2/emissionIntensity`: that program has no emission output (material.wgsl:740-741).
  - The arcs on `material/glowmereFirefliesCrown/emissionIntensity` and `…Scaled/emissionIntensity`: their glow lives in a layer, and layer intensity is not registered (material_params.cpp:80-90).
  - The terrain's `groundGlow*` and `groundMottle`, because the authored program overrides them (a warning is logged at composition.cpp:5879-5887).
  - The scene's 12 effects (11 hero ground pulses and the travel beam) are replaced by the project's two-effect list (engine.cpp:2428-2447, and `setEffects` replaces at :867-890).
  - `heroFocus` and `cameraTravel` activations also need shot spans, which GV3 no longer carries (engine.cpp:2606-2635, :4839; ADR-582). Inference: they are empty in a headless render.

### 5. Recommendations using existing parameters
**Micro**
- Append ops to glowmereTissue:
  - a responder mask (`threshold` on `instanceRandom.y`, about 25% of mushrooms respond);
  - a bass group and a treble group chosen by a second random lane;
  - per-group depth constants, each driven by a smoothed route (`audio.bass` 30/400 ms, `audio.treble` 15/200 ms) into `op/N/constant/constant`;
  - staggered timing: `Palette` of `time`×130/60 + `instanceRandom.x`, sharpened with `Power`. For grouped responses, use region `Noise(worldProject)` instead of `instanceRandom`.
  - Inference: the 130 BPM grid starting at 0.48 s comes from the marker times.
- Keep the depth within ±30%.
- Append a `HueShift` on the emission and key its `value` per section for section colour evolution.
- Particles: key `pulseSync` per section (independent blinking, then in unison at the drop), and set `windInfluence` to 0.2–0.4 on spores and motes.

**Meso**
- Put 4–6 world-owned GroundPulses at fungi-dense marsh and bank patches:
  - trigger on beat, every 4th, with offsets 0–3 (one patch per beat, round robin);
  - speed 8–14 m/s, range 30–60 m, `intensity` about 0.2, `response/emissive` 1.5–2.5;
  - route smoothed `audio.rms` into `fx/<id>/response/emissive`;
  - add one valley-wide ring on `musicEvent: drop` (speed 40, range 250);
  - author them in the **project's** `effects` list (tools/gv3/look.py), not the scene.
- For a ring that touches only the mushrooms: a radial `wave` field sampled by an appended `field` op, with `waveOrigin` keyed at each event. Point `volumeColorField` at the same field for a glowing mist crest.

**Macro**
- Wind retune: `gustSpeed` about 8, `gustScale` about 26, `gustAmount` 0.9, `turbulence` 0.45; tree mass 40 → 4–6; grass and fern `tipAmplitude` 0.2–0.25.
- Restore the build/break routes on `scene/windSpeed` and add `gustAmount` routes.
- Fix the three dead arcs. Add section arcs on `scene/volumeLocalLights` and `volumeScattering`, and a small aurora `audioBeat` after the riser.
- Decorrelate the layers: different attack/decay per layer, and phase-offset LFO sources (source.hpp:64-99).

### 6. Smallest engine additions
1. **Scatter `emissiveField` and `emissiveFieldAmount`, plus a registered parameter.** Files: ecology.hpp/.cpp, and composition.cpp around :6447 and :5291. The GPU path already exists (procedural_renderer.cpp:1790-1793), and it applies after the program, so it respects ADR-179.
2. **Per-layer lanes applied after the program: `emissionGain` and `hueOffset`, with registered parameters.** Files: procedural.hpp, procedural_renderer.cpp, procedural.wgsl `fs_proc`, and composition.cpp. Add a test that a route changes pixels.
3. **Fields timed from the last event.** Add a `trigger` to `FieldSpec` that reuses the effects' Trigger and TriggerClock, so field time counts from the most recent event. Files: spatial/field.*, rendering/field_uniforms.cpp, scene/field_params.cpp.
4. **A `scene/ecologyLight` parameter.** Files: composition.cpp :4514 and :8607.
5. **Register a material program layer's `emissionIntensity`.** File: material_params.cpp. This revives the fireflies arcs.
6. **Correctness fix:** add a `materialEmission` program input and apply `m` only once, so hue offsets actually rotate the displayed colour. Files: material_program.*, material.wgsl, pbr_shade.wgsl, procedural.wgsl.
7. **Optional: a hue lane on the wave.** Medium effort, because the GPU wave record is already full at 144 bytes and the frame-block size is fixed by a static assert.
