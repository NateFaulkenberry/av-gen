# GV3 r7b project-state audit (hypothesis B), for W1

W3, 2026-09-28. Subject: `examples/world/glowmere-valley-3{,.scene}.json` as committed on `qa/clean`
(`ad1a9fa4`), which is r7b (byte-identical scene; the project differs only in the song path). The
comparison is GV2 multicam, the file the generator derives GV3 from.

**Method.** JSON inspection plus the engine's own loader, which is CPU-only:
`avgen --project <p> --audit-routes <out.json> --fps 60`. Nothing here was measured on the GPU and
nothing was removed from GV3. W1 owns every cost claim. Each item says whether it is dead data or live
state that costs something, and what W1 would measure.

## Summary

| # | Finding | Kind | Frame cost? | For W1 |
|---|---|---|---|---|
| 1 | 12 effect values stored twice with different numbers (block vs `fx/` parameter) | duplicated state | no (the parameter wins) | none; a correctness/readability item |
| 2 | the scene's 12 `effects` are all shadowed by the project's 18 | dead when opened as a project | no | none |
| 3 | two hidden 32,768-particle beam pools (`visitor-beam`, `scout-beam`) | hidden, possibly evaluated | **maybe** | A/B the beams with the pools at a small capacity, or check that a `visible:false` node skips simulation |
| 4 | 15 `groundPulse` + 1 `travelBeam` trigger effects, idle most of the film | live, intermittently active | **maybe** | per-effect cost when idle vs firing |
| 5 | ecology pools: `ecologyLightRange 300`, `ecologyPoolReach 6`, `ecologyPoolFaintest 6.5` (GV3-only) | live, GV3-specific | **likely view-dependent** | wide vs close with ecology light range at GV2's default |
| 6 | a second UFO (`scout` + `scout-beam`) and two `field` nodes (`elder-rings`, `drop-ring`) (GV3-only) | live | maybe | include in the wide-shot entity/draw counts |
| 7 | `lod.lodCount` on three procedural nodes: an obsolete key the parser ignores | dead data | no | none |
| 8 | terrain `groundGlow 0.08` does nothing under the authored `paintedGround2` program | dead data | no | none |
| 9 | 6,709 photographed parameters (3,572 `procedural/*`) | load-time size | no per-frame cost expected | none unless load time matters |
| 10 | `directingPlans` (80 KB) records the plans the routes and staging were compiled from | provenance | no (read by the Director panel, not per frame) | none |
| 11 | load-time "dead route" warnings on the 16 marker-triggered effects | false alarm | no | none (known engine item) |

**Not found**, with the evidence:
- **parameters naming no effect / node / entity / material:** `check_project_integrity.py` reports
  nothing for GV3; every `nodes/`, `particles/`, `procedural/` and `entity/` parameter names a scene
  node or entity, and the 8 `material/*` names are exactly the 8 material programs. The load logs 0
  "unknown parameter" lines (GV2 multicam logs 9 distinct ones, GV2 7; the generator drops them).
- **dead routes, tracks or effects:** the audit's own verdict is routes 105/105, tracks 130/130,
  effect-default routes 23/23, effects 18/18 live, 0 hazards.
- **parameters left behind by effects `tools/gv3/look.py` removed:** `look.py` deletes every
  `fx/<id>/*` of a removed effect (line 321) and `REMOVED_EFFECT_TYPES` is empty in r7b; the only
  removed effect is `visitor-hero-pulse`, and no `fx/visitor-hero-pulse/*` parameter remains.
- **stale cameras or shots:** 74 cameras, 73 used by the 73 shots; the 74th is `Main` (id 1), which
  `ensureMainCamera` recreates on every load, so it is not stale. GV2 multicam's three cameras, its
  `cameraAimFollow` (39) and `cameraShotSpans` (45) and its 14 `cameras/ufowatch/*` parameters are gone.
- **orphaned staging:** the project's 5 scenarios (`setpiece/e1..e5`) all have cue markers; the scene's
  `staging` holds only the two actor definitions (`saucer`, `scout`), which the scenarios use. GV2
  multicam's 32 `staging/*` parameters are gone.
- **hidden nodes other than the two beams:** none (`visible:false` appears on those two only).

## Evidence per item

### 1. Effect values stored twice, with different numbers
An effect's `parameters` block and its flat `fx/<id>/<name>` parameters both carry the value. The
project's `parameters` are applied after the effect is built, so the flat value is the one that
renders and the block value is dead. Of 402 block values with a matching `fx/` parameter, 12 disagree
(GV2 multicam: 2 of 424):

| effect | block | `fx/` parameter (wins) |
|---|---|---|
| aurora | `appearance.filaments` 0.95 | `filaments` 1.3 |
| aurora | `audio.beat` 0.35 | `audioBeat` 0.0 |
| aurora | `audio.sensitivity` 1.0 | `audioSensitivity` **0.0** |
| aurora | `audio.spectrumShape` 0.75 | `spectrumShape` 0.0 |
| aurora | `rainbow.brightness` 1.0 | `rainbowBrightness` 0.76 (also in GV2 multicam) |
| aurora | `shape.curtainHeight` 2600 | `curtainHeight` 2300 |
| aurora | `shape.turbulence` 0.45 | `turbulence` 0.68 |
| aurora | `shape.waveAmplitude` 0.30 | `waveAmplitude` 0.38 |
| camera-travel-beam | `appearance.edgeColor` [0.96,0.84,1.0] | `edgeColor` [0.8,0.66,1.0] |
| camera-travel-beam | `appearance.edgeIntensity` 3.0 | `edgeIntensity` 2.0 |
| camera-travel-beam | `appearance.intensity` 1.9 | `intensity` 1.4 |
| horse-light | `gain` 1.0 | `gain` 0.5 |

Note `fx/aurora/audioSensitivity = 0`: the aurora's own audio response is off in what renders, while
the block says 1.0. The earlier "glints 0.5 vs 0.3" duplicate is resolved in r7b (both 1.0). A look
question for the owner, not a cost. (Script: compare each block leaf to `fx/<id>/<group><Leaf>`.)

### 2. The scene's effect list is dead under the project
ADR-702: a project's `effects` array replaces its scene's. The scene carries 12 (`camera-travel-beam`,
`visitor-hero-pulse` and ten cap pulses, GV2 multicam's list unchanged); the project carries 18. The
audit reports 18 effects, the project's. The scene list is read only if the scene is opened on its
own. 14.7 KB; no frame cost.

### 3. Two hidden 32,768-capacity beam particle pools
`visitor-beam` and `scout-beam` are `kind: particles`, `visible: false`, `spawnRate 0`, capacity 32,768
each (the other 13 systems total 21,888). They are the staging actors' `beam` parts, switched on by
the E1-E5 scenarios. `ParticleRenderer` allocates a pool per system (`ensurePool(i, sys.capacity, ...)`)
and skips simulation only when `sys.enabled` is false; I did not establish whether `visible:false`
maps to `enabled:false`. **W1:** count dispatched particle work in a shot with no set piece.

### 4. Sixteen trigger effects
15 `groundPulse` (the hero pulses restored in r7) and the travel beam, `activation: trigger` on
`sequence.markers` (193 markers; 140 pulse rings, 22 beam fires), each with a `timeline.kick ->
fx/<id>/intensity` route. GV2 multicam had the same 16 under `heroFocus`. **W1:** the per-frame cost
of an idle trigger effect vs a firing one, and how many overlap in a wide.

### 5. Ecology pools (GV3 only)
`environment` gains `ecologyLightRange 300`, `ecologyPoolReach 6`, `ecologyPoolFaintest 6.5`,
`fogSky 0.5`, `fogSkyDistance 0` (the r6 "ground pools", ADR-945 ecolight). A light range of 300 m is
the most obviously view-dependent new state: a wide shot sees more pooled ground than a close-up.
**W1:** wide vs close with the range at GV2 multicam's default.

### 6. New bodies (GV3 only)
Nodes 77 -> 81: `scout` (procedural UFO), `scout-beam` (item 3), `elder-rings` and `drop-ring` (`field`).
Entities 19 -> 21 (`scout`, `scout-beam`). Heroes 16 -> 17. 44 existing nodes changed (seated heroes,
the valley's closed ends, water glow and specular: the generator's `world.py` fixes).

### 7. `lodCount`
`river-lilies`, `tarn-lilies`, `river-petals`: `procedural.lod.lodCount 2`. Every load logs
"lod: unknown setting 'lodCount' ignored" three times; so do GV2, GV2 multicam and the stylized scenes.
Inherited from the multicam scene, so a fix belongs there (and GV3 regenerated), not in GV3 alone.

### 8. `groundGlow`
Terrain `valley` has `groundGlow 0.08`; the loader warns that the authored program `paintedGround2`
ignores it ("the glow ... and groundMottle all do nothing"). Also inherited. An open engine item.

### 9. Parameter volume
6,709 parameters (GV2 multicam 6,646): `procedural` 3,572, `nodes` 668, `particles` 614,
`material` 611, `fx` 594, `entity` 442. A save photographs every serialised parameter, so most equal
what the scene would produce. Load time is 3.8 s headless, of which the scene is 2.25 s, audio
0.79 s, timeline 0.77 s, parameters 7 ms.

### 10. Directing plans
`directingPlans`: `reactivity` (73 KB: the proposed routes and sources, with provenance) and `ufo`
(6.5 KB: the set pieces). The compiled results are the project's `routes` and `staging`. Loaded and
kept for the Director panel (`Engine`, `director_panel.cpp`), not evaluated per frame.

### 11. False "dead route" warnings
The load logs 16 "modulation route N (timeline.kick -> fx/<id>/intensity) is dead: ... The sequence
has no marker with this trigger's name", while the audit counts all 105 routes live and the markers
exist. Known (GV3 wrap-up, "the travel beam's false dead warning").
