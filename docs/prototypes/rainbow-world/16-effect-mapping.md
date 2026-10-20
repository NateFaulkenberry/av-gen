# Rainbow World 16: the effect mapping

Brief 13, Phases 4-5 and 7. It rests on the stem analysis (`14-stem-analysis.md`) and the research
(`15-research.md`).

- **The code:** `examples/rainbow-world/blender/music.py` (the routes), `blender/look.py` (the ops each effect adds to
  a program) and `build_world.py` (the deformers, the arches as programs, the two projects).
- **Numbers:** every number here is the generator's. If this document and the generator disagree, the generator is
  right and this document is stale.

## 1. The architecture: three layers, no new pipeline

```
stem.<key>.* (a stem file offline, a mapped input channel live)      response.*, beat.* (the main mix)
        |  optional routes (ADR-1570)                                        |  fallback routes: depth 0 while the
        |                                                                    |  stem is active, full depth otherwise
        v                                                                    v
   musical controls: macro knobs `swell`, `synth` (their own attack/release)  +  direct routes for the impacts
        v
   targets: material-program constants (ring, glints, arch fronts, arch flow, cloud light),
            world-space sine deformers (the swell), light intensity
```

- **Reused, not added:**
  - the analysis and the `stem.*` vocabulary (ADR-1520/1521);
  - route chains (attack/decay in time, not frames; envelopes; curves; clamps);
  - macro sources;
  - material programs, whose constants are route targets that need no recompile (ADR-1493);
  - procedural deformers;
  - the beat clock.
- **Added to the engine:**
  - ADR-1570 (optional routes, depth defaults), so one project serves stems and a stereo input;
  - ADR-1571 (traces), to measure.
- **No entity scans audio and no shader holds a threshold.** Every threshold, gain and time constant is on a route in
  the project, where the Modulation panel shows it.

## 2. Drums: impact, rhythm, propagation

### 2.1 The kick ring

- **Trigger and timing:**
  - Every beat; the drums are four-on-the-floor.
  - Its radius follows `beat.phase`: radius = 18 m + 70 m x phase. The front leaves 18 m ahead on the beat at about
    140 m/s and has faded by the next one. Starting nearer read as a pale wash over the bottom of the fisheye frame.
  - Its brightness is `stem.drums.bass`: the kick's body, attack 4 ms, decay 160 ms, conditioned so a hat or tick
    (which has no low end) reads 0. A beat with no kick draws nothing: the fills at 101.4 and 109.3 s, and after the
    drums stop at 182.7 s.
- **Why not `stem.drums.kick` or `stem.drums.onset`:**
  - The hits tier is up to a 16th late on this kit (14 §5).
  - The onset fires on hats as well, and would restart the ring every 8th.
  - The beat clock is musical and steady; the stem's energy says whether the hit is really there.
- **Where it is:**
  - A thin shell of light on every surface at that distance from the eye: a sphere about the camera
    (`cameraDistance`).
  - Because the world's gravity turns, a ground-plane ring (ADR-207's world effects) would be wrong on most faces. A
    camera-distance ring lies on whatever face it crosses, follows the folds, and climbs the crossings' cliffs.
  - Programs: `rw_ground`, `rw_ground_lip`, `rw_grass`, `rw_rock`.
  - Shape: a sharp 1 m leading edge and a 4 m trailing tail: a line of light, not a band.
- **Colour:** warm white-gold (`#FFE6A6`). It is light, not a hue of its own. It fades to 12% over the beat.
- **Maximum:** gain 1.1 at a full kick (1.5 on a phrase's first beat). It covers about 5-12% of the frame at once, never the frame, and never more
  than one ring at a time.
- **Cost:** 8 ops in three programs.
- **Live:** `response.kickEnv` (the mix's kick envelope) with the live beat clock. Offline: the stem.

### 2.2 The clap fronts on the arches

- **Trigger:** beats 2 and 4, through `stem.drums.highMid` (the clap is 2-6 kHz), conditioned so beats 1 and 3, which
  have no clap, read 0.
- **Where:** on every arch, two fronts start just above the feet (where the bow is fully opaque) and meet at the crown in
  one beat (`beat.phase` moves both edges). They brighten the bow's own spectrum, so they are never another colour.
- **Time:** attack 4 ms, decay 280 ms.
- **Maximum:** gain 10 on top of the arch's 2 (primary). At 5 the front was lost in the bow's own saturation.
- **Interaction:** the backbeat makes the ring and the fronts alternate. Beats 1 and 3 are a ring on the land;
  beats 2 and 4 are a ring plus a flash running up the bows.
- **Live:** `response.snareEnv`.

### 2.3 Hat glints

- **Trigger:** `stem.drums.treble`, conditioned (`x 1.6 - 0.35`), attack 3 ms, decay 110 ms. That covers the offbeat
  open hats in groove A and the 16th hats from bar 57.
- **Where:** the tips of a few grass tufts flash.
  - The tufts are chosen by instance random against a threshold the hats lower, so more tufts flash on a louder hat.
  - It is the same tufts each time, but at 30 m/s the field in view is always new.
  - Its density follows the hats' density, so section B's 16th hats make the field finer and denser by themselves.
- **Colour:** pale warm white.
- **Live:** `response.hatEnv`.

## 3. Bass: the physical world

### 3.1 The swell

- **Musical control:** `macro.swell`, the bass's body.
  - `stem.bass.level` conditioned (`x 1.6 - 0.55`, so the stem's breathing floor reads near 0), attack 60 ms, decay
    500 ms;
  - plus 0.35 on every `stem.bass.onset` (each note), falling at 3 per second.
  - The bass plays short syncopated plucks against a four-on-the-floor kick, so the swell moves on the bass's own
    rhythm (`x..x..x...x..x..`), not the kick's.
- **Where:** one world-space sine deformer on the band and on every grass, flower, rock and tree node.
  - Wavelength 105 m, oblique to the knot, travelling at about the ride's own speed.
  - Amplitude at a full swell: 1.9 m. A second wave cost 1.1 ms more (17 section 4) for a shape the camera's motion
    hides.
  - The ground and what stands on it move together, because the displacement is a function of the world position.
    It is not a scale of the world: every place moves by a different amount at a different time.
- **Also:** the cloud sea's bounce light (`lights/cloudsea`) rises with the swell, the Stage 7 route moved onto the
  stem.
- **Live:** `response.bass`.
- **Cost:** two deformers on every scattered vertex. Measured in 18 (first prototype).

## 4. Synth: atmosphere, colour, spectacle

### 4.1 The musical control

`macro.synth` is:
- `stem.synth.level` conditioned (`x 2.2 - 0.95`: the drop's bed at 0.47-0.71 reads 0.1-0.6), attack 250 ms, decay
  1.2 s, so the kick's sidechain pump does not ripple the light;
- plus 0.3 per `stem.synth.onset`, falling at 1.4 per second: the intro's falling calls and the chord changes.

### 4.2 The arches as instruments

The arches are now procedural nodes drawn by `rw_rainbow` and `rw_rainbow2`, which rebuild the Stage 2-8 bow from
its UVs (`look.rainbow_program`). On top of that:
- **flow:** bright bands drifting along every arch from foot to foot. Their depth is `macro.synth`; they are always
  there, faintly, and full when the synth is.
- **brightness:** `macro.synth` lifts the arch by up to 1.2.
- **CC 1** still multiplies the arches' glow (`nodes/rainbows_*/emissiveBoost`).

### 4.3 The clouds

`macro.synth` lifts the painted clouds' light by up to 30% (`rw_cloud`'s `cloud_glow`). The synth's light reaches the
sky, not only the arches.

**Live:** `response.sustain` drives the synth control.

## 4.4 The meadow wash (synth, phrase, drop)

Slow bands of the bow's own colours (blue, green, orange, red-orange: a cyclic OKLab ramp) drift across the meadow and
the grass in world space, one band per 160 m, along world X: the arches' light reaching the land.

- **Gain:** `macro.synth` x 0.12, plus `macro.lift` x 0.1, plus a phrase flare of 0.3 that falls over 2 s. At three
  times that the meadow read as painted stripes. The flare's
  depth is the synth, so phrases before the drop flare less.
- **At rest it is 0:** with no music the meadow is as built.
- **Why it exists:** the drop at 47.85 s comes as the ride leaves an arch hold (41.6-50.4 s) and the camera faces
  open meadow. A world-scale moment cannot depend on an arch being in view.

## 4.5 Trees and rocks

The kick ring is also on the stones (`rw_rock`). It was tried on the canopies and removed: the leaf cards are masked,
and a masked draw runs its program in the depth prepass as well, which cost 3.6 ms at the 200 s hold. The trees take
the ring from the meadow and grass round them. They heave
with the swell, because they carry the same deformer as the ground under them.

## 5. The hierarchy (brief section 7.3)

| Tier | What | How often | Share of the frame lit |
|---|---|---|---|
| Background | the swell, the arch flow, the cloud light | always, following the bass and synth | the whole world, gently |
| Rhythm | kick ring; glints | each beat; each hat | under 15%; specks |
| Accent | the clap fronts | beats 2 and 4 | the arches only |
| Phrase | the arches flare, the ring brightens, the meadow wash flares; all scaled by the synth | every 4 bars (7.9 s) | the arches and soft bands on the land |
| World-scale | the drop at 47.85 s (phrase pulse + the synth's entry read against its adaptive reference); the riser at 183-190 s (`lift`: sun glow, bloom, arches, wash) | twice in the track | the whole world, colour not white |

No full-frame flash anywhere. The Stage 7 kick-to-bloom route is removed. The sky's exposure never moves on a hit.

## 6. Determinism and live

- **Every control is a route chain or a stateless function of the transport:** `beat.phase`, the material `time`
  input, the deformers' time.
  - Route chains are replayed on a seek.
  - Stems are published on the replay bus (ADR-1520).
  - So a seek lands where a play does.
- **Live:**
  - The same routes run on the mix's `response.*` and the live beat tracker.
  - A mapped input channel keyed `drums`, `bass` or `synth` turns the stem routes on, and the fallbacks stand aside
    (ADR-1570).
  - No route assumes knowledge of the future: the ring is phase-locked to the tracker's beat, as a live tracker
    predicts it.

## 7. The effect timeline (Feline Footwear)

| Time (s) | Bars | Stems | Feature | Effect | Intensity, duration | Scope | With |
|---|---|---|---|---|---|---|---|
| 0.24-8.2 | 1-4 | drums | kick body on the beat; treble | kick rings, glints | ring 1.1, one beat; glints 110 ms | land, trees, rocks; grass | a calm world: no swell, no synth light |
| 8.2 | 5 | bass | level, onsets | the swell starts | up to 1.9 m, 60 ms / 0.5 s | the whole land | rings ride on the heaving ground |
| 20.1, 28, 31-32, 35-36, 39.6, 44-45 | 11-24 | synth | onset (the falling calls), level | each call: the arches, clouds and wash swell, falling over 0.7 s | synth macro +0.3 | arches, clouds, meadow | |
| 28.0 on | 15 | drums | 2-6 kHz on beats 2 and 4 | clap fronts up every arch | gain up to 5, 280 ms | arches | alternates with beats 1 and 3: ring alone |
| 39.9 | 21 | bass | the progression starts | the swell's pattern changes with the bass's | | land | |
| **47.85** | **25** | **all** | **phrase pulse + synth level jumps against its reference** | **arches +2 (falling over 1.7 s) and the synth's +1.2; wash flare 0.9; ring +1** | **the track's largest** | **arches, meadow, clouds, ring** | the bass pattern changes; the camera leaves the arch hold into open meadow, so the meadow wash carries it |
| every 7.93 | 1 + 4k | all | phrase pulse | phrase flare | x synth | arches, meadow | |
| 101.4, 109.3 | 52, 56 | drums | fills: no kick, no clap | the ring and fronts skip | | | a hush the swell carries over |
| 111.3 on | 57 | drums | 16th hats | glints denser and finer | | grass | |
| 151-165 | 77-84 | synth | centroid rising to its peak (160.9 s) | `lift`: sun glow, bloom, arches, wash rise slowly | lift 0-1, 1.5 s / 3 s | sky and world | |
| 172-182 | 88-92 | synth | centroid falls | the lift falls back | | | the bass returns to the pedal |
| **182.7** | **93** | drums stop, synth riser | no drum energy; centroid and treble rise | **rings, fronts and glints stop**; the lift rises through the riser | | | the swell continues on the bass pedal until 193.5 s |
| 194.6-210.5 | 99-106 | synth alone | level, falling from 200 s | the arches' flow, clouds and wash fade with the synth | | | the last thing glowing is the arches |

## 8. Further ideas (not built)

- **Phrase pulses** (`beat.phrasePulse`, bars 1 + 4k): a stronger, wider ring in the arches' colours, and an arch
  flare.
- **The drop** (47.85 s): the synth's jump against its adaptive reference, plus the phrase pulse. All arches ignite,
  then decay over two bars.
- **The riser** (183-190 s, the drums gone): the synth's centroid lifts the haze and the arches' light toward the
  release.
- **Vegetation sway** on the trees from the swell, if the wind body reaches procedural point nodes.
