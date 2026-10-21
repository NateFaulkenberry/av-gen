# Rainbow World 16: the effect mapping

Brief 13, Phases 4-5 and 7. It rests on the stem analysis (`14-stem-analysis.md`) and the research
(`15-research.md`).

- **The code:** `examples/rainbow-world/blender/music.py` (the routes), `blender/look.py` (the ops each effect adds to
  a program) and `build_world.py` (the arches as programs, the looks, the flight, the two projects).
- **Revised for gate 4** (2026-10-20), after the owner's colour brief (`19-brief-color-vfx.md`): colour over geometry.
  The swell's deformers and the per-beat world ring are gone; the kick is local, the world scan is the drop's moment,
  and the looks carry the colour.
- **Numbers:** every number here is the generator's. If this document and the generator disagree, the generator is
  right and this document is stale.

## 1. The architecture: three layers, no new pipeline

```
stem.<key>.* (a stem file offline, a mapped input channel live)      response.*, beat.* (the main mix)
        |  optional routes (ADR-1570)                                        |  fallback routes: depth 0 while the
        |                                                                    |  stem is active, full depth otherwise
        v                                                                    v
   musical controls: macro knobs `swell`, `synth`, `lift`, `scan` (their own attack/release)  +  direct routes
        v
   targets: material-program constants (kick tips, glints, shimmer, arch flow and brightness, the scan front,
            cloud light), the palette's saturation, light intensity, sun glow, bloom
```

- **Reused, not added:**
  - the analysis and the `stem.*` vocabulary (ADR-1520/1521);
  - route chains (attack/decay in time, not frames; envelopes; curves; clamps);
  - macro sources;
  - material programs, whose constants are route targets that need no recompile (ADR-1493);
  - the palette (ADR-1043/1420) for the looks;
  - the beat clock, and the drums stem as its feed (`"feeds": {"beat": "stem.drums"}`, ADR-1581).
- **Added to the engine:**
  - ADR-1570 (optional routes, depth defaults), so one project serves stems and a stereo input;
  - ADR-1571 (traces), to measure;
  - ADR-1572 (a glTF node drawn through a material program), for the arches;
  - ADR-1573 (a seek replays the control layer when routes feed a source), so the macros land where a play does.
- **No entity scans audio and no shader holds a threshold.** Every threshold, gain and time constant is on a route in
  the project, where the Modulation panel shows it.

## 2. Drums: impact, rhythm, propagation

### 2.1 The kick, locally

- **History:** gate 2 had a ring of light running out from the camera on every beat. The owner: no world sweep on every
  quarter note. The kick is now local.
- **What:** the grass tips within about 32 m of the camera light up on each kick, in the look's own kick colour
  (a warm white in Bright Noon; never a yellow on a green meadow, which read as lime).
- **Signal:** `stem.drums.bass` (the kick's body), x3, clamped, attack 4 ms, decay 140 ms. A beat with no kick draws
  nothing (the fills at 101.4 and 109.3 s; after 182.7 s).
- **Cost:** a few ops in `rw_grass`.
- **Live:** `response.kickEnv`.

### 2.2 The clap on the arches

- **Trigger:** beats 2 and 4, through `stem.drums.highMid` (the clap is 2-6 kHz), conditioned (`x 2.5 - 1.55`) so
  beats 1 and 3 read 0. Attack 4 ms, decay 280 ms.
- **What:** the bright flow bands along every arch jump (`flow_gain` +2.5): the backbeat travels along the bow, in the
  bow's own spectrum.
- **Live:** `response.snareEnv`.

### 2.3 Hat glints

- **Trigger:** `stem.drums.treble` (`x 1.6 - 0.35`), attack 3 ms, decay 110 ms.
- **What:** the tips of a few grass tufts flash. The hats lower the threshold that picks the tufts, so a louder hat
  lights more, and section B's 16th hats make the field finer by themselves.
- **Live:** `response.hatEnv`.

## 3. Bass: colour energy

- **Musical control:** `macro.swell`, the bass's body: `stem.bass.level` conditioned (`x 1.6 - 0.55`), attack 60 ms,
  decay 500 ms; plus 0.35 per `stem.bass.onset`, falling at 3 per second. It moves on the bass's own syncopated
  rhythm, not the kick's.
- **What it drives:**
  - the palette's own saturation (+0.18 at a full swell): every colour the look binds -- meadow, sky, clouds,
    stones -- deepens with the bass. No shader cost;
  - the cloud sea's bounce light (`lights/cloudsea`, +0.6).
- **Removed:** the world-space sine deformers (1.9 m of heave). The owner's colour brief puts colour over geometry,
  and they cost about 1.1 ms each (17 section 4).
- **Live:** `response.bass`.

## 4. Synth: atmosphere, colour, spectacle

### 4.1 The musical control

`macro.synth` is:
- 0.4 x `stem.synth.level` conditioned (`x 2.2 - 0.95`), attack 250 ms, decay 1.2 s, so the kick's sidechain pump does
  not ripple the light. The level reads against the stem's adaptive reference (ADR-1510), so on its own it put the
  intro's isolated calls at the top and the drop's steady bed in the middle -- backwards for this track.
- 0.7 x `stem.synth.rms` (`x 4.5`, absolute: the bed 0.12-0.18 against the calls' 0.02-0.06), attack 300 ms, decay
  1.2 s. With both, the synth light is 0.3-0.6 through the calls, 0.85 at the drop, 0.6-0.8 through the grooves and
  0.85-0.9 through the riser, falling with the outro (`stems/05-controls.jpg`, emulated by
  `scripts/control_plot.py` from the engine's own stem features);
- plus 0.3 per `stem.synth.onset`, falling at 1.4 per second: the intro's falling calls and the chord changes.

**Live:** `response.sustain`.

### 4.2 The arches as instruments

The arches are glTF bows drawn through `rw_rainbow` (ADR-1572); the textures carry the spectrum, the program the
light.
- **Flow:** bright bands drift along every bow, foot to foot. `macro.synth` x3; always there faintly.
- **Brightness:** `macro.synth` lifts the bow by up to 1.2; the faint secondary bow by `emissiveBoost` 0.8.
- **Phrase flare:** every 4 bars (`beat.phrasePulse`) the bows flare (+4, falling over 1.7 s), its depth the synth
  stem's rms, so the intro's phrases flare less.
- **CC 1** still multiplies the arches' glow.

### 4.3 Clouds, sky and flowers

- **Clouds:** `macro.synth` lifts the painted clouds' light by up to 30% (`cloud_glow`).
- **Sun glow:** `macro.synth` +0.25; `macro.lift` +0.35.
- **Shimmer:** the flower fields shimmer (world-position noise drifting in time), `macro.synth` x1.6 plus the hats.

### 4.4 Lift

`macro.lift` is the synth's brightness over bars (its centroid, conditioned, attack 1.5 s, decay 3 s, depth the
synth's rms so a silent stem's noisy centroid reads 0). It carries the 151-165 s rise and the riser at 183-190 s:
sun glow, bloom (+0.12) and the arches rise together, slowly.

## 5. The world scan: the drop

The drop's showpiece (the owner at gate 2: "rare and dramatic, turning everything rainbow, holding then relaxing").
- **What:** a front runs out from the camera over everything in reach -- meadow, grass, stones, rims (scan ops in
  their programs) and the trees (their canopy ramps crossfade to the spectrum) -- leaving the land rainbow behind it:
  a cyclic OKLab spectrum by distance, its value the surface's own, so the land keeps its shading. The front's edge
  is bright.
- **With it:** the arches +14 and their flow +10, cloud light +0.6, sun glow +1.2, bloom +0.35, saturation +0.25.
- **Shape:** the front runs 970 m in 4 s; the scan holds about six seconds and relaxes over three.
- **When:** only where two conditions meet, both replayable (no stateful route reads a macro):
  - A: the phrase pulse, read 300 ms late, latched six seconds, its depth the synth stem's level near the top of its
    adaptive range (a loud entry after a quiet stretch);
  - B: the synth stem's absolute loudness (rms): the intro's calls are too quiet.
  - Combined by `min` onto `macros/scan`. The trace confirms one firing, 48.1-52 s: the drop.
- **Live:** the scan needs the synth stem; on a stereo input it stays off (the fallback depth is 0).

## 6. The looks

Eight palette states (CC 9 selects, CC 10 locks; unlocked, the look advances on phrase boundaries or a big change in
the music, holding each at least 60 s). Each is a whole world (gate 3: "too similar"; gate 4 sheets):
- its own sky gradient: zenith, horizon and a haze width (`skyHaze`, a palette scalar on `env/sky/haze`, 0.3-0.75)
  that carries the horizon colour well up the sky;
- its own meadow ramp (ground, rims and grass), shade, clouds and cloud floor, sun and bounce light, haze tint;
- its own tree colour: one OKLCH hue turn after each canopy ramp (`foliageHue`), taking the Great Tree's green to the
  look's foliage hue (Candy coral, Electric teal, Dusk plum, Prism blue, Storm teal-green, Golden warm green, Spring
  blossom); the Blossom Puff stays pink;
- its own kick colour and stone tint.

| Look | Sky | Meadow | Trees |
|---|---|---|---|
| Bright Noon (default) | cobalt to cyan | emerald (the lime fixed: a cooler ramp, a paler sun) | as built |
| Golden Hour | cobalt burning orange | emerald, sunlit warm crests | warm green |
| After the Storm | navy to bright aqua | rain-dark teal | teal-green |
| Lavender Dusk | indigo to rose | blue-lilac | plum |
| Prism | periwinkle to rose | spectral: violet, teal, mint, peach | blue |
| Candy | gold to butter | pink-coral, violet shade | coral-crimson |
| Electric | near-black violet to purple | indigo, cyan crests | teal |
| Spring | clear blue to white | mint and cream | blossom violet-pink |

The palette is replace-after-routes: anything a look binds, a route cannot move. So the scan's tree crossfade lives on
the ramp stops and the look's tree colour on a later op.

## 7. The camera: a plane

`21-camera-research.md` and `20-camera-note.md`. Baked into the spline and its timeline, so seek equals play:
- soars to about 30 m over open meadow and over reachable crowns; dives under the bows and into the rim crossings;
- banks into its turns as a coordinated turn banks (up to 29 degrees, mean 10);
- trades speed for height with real g;
- rolls (one aileron roll, 4 s) only on a phrase start where the land offers it -- under a bow, or out of a climb:
  32.0 s and 79.6 s on Feline Footwear. The drop falls in a rim crossing, whose flip over the rim is the drop's move;
- one lap 190 s.

## 8. The hierarchy (brief section 7.3)

| Tier | What | How often | Share of the frame lit |
|---|---|---|---|
| Background | the look; the arch flow, cloud light, sun glow, shimmer; the bass's chroma | always, following the bass and synth | the whole world, gently |
| Rhythm | kick tips; glints; the clap along the bows | each beat; each hat; beats 2 and 4 | the grass near the camera; specks; the arches |
| Phrase | the arch flare (depth: the synth) | every 4 bars (7.9 s) | the arches |
| Musical moments | the rolls under the bows | twice a lap | the whole frame turns |
| World-scale | the scan at the drop; the lift through the riser | once; once | the whole world, colour not white |

No full-frame flash anywhere. The sky's exposure never moves on a hit.

## 9. Determinism and live

- **Every control is a route chain or a stateless function of the transport.** Route chains are replayed on a seek
  (ADR-901); stems are on the replay bus (ADR-1520); routes feeding a macro make the seek replay the control layer
  (ADR-1573). The scan reads only replayable signals (section 5). The look selector is the documented exception.
- **Live:** the same routes run on the mix's `response.*` and the live beat tracker; a mapped input channel keyed
  `drums`, `bass` or `synth` turns the stem routes on and the fallbacks stand aside (ADR-1570). Without a synth
  channel there is no scan.

## 10. The effect timeline (Feline Footwear)

| Time (s) | Bars | Stems | Effect | With |
|---|---|---|---|---|
| 0.24-8.2 | 1-4 | drums | kick tips, glints | Bright Noon; a calm world |
| 8.2 on | 5 | bass | the bass's chroma, cloud-sea light | |
| 20.1-45 | 11-24 | synth | each call: arches, clouds, sun glow swell and fall | |
| 28.0 on | 15 | drums | the clap along the bows | |
| 32.0 | 17 | -- | a roll under a bow | phrase start |
| **47.85** | **25** | **all** | **the camera flips over a rim; the scan runs out 300 ms later and turns the world rainbow, holding to ~52 s, relaxing to ~55 s; arches +14** | the track's largest |
| every 7.93 | 1 + 4k | all | the arch flare (x synth) | |
| 79.6 | 41 | -- | a roll under a bow | phrase start |
| 101.4, 109.3 | 52, 56 | drums | fills: no kick, no clap | a hush |
| 111.3 on | 57 | drums | 16th hats: glints denser | |
| 151-165 | 77-84 | synth | the lift: sun glow, bloom, arches rise slowly | |
| **182.7** | **93** | drums stop | kick, clap and glints stop; the lift rises through the riser | |
| 190 | -- | -- | the ride's lap ends and begins again (seamless) | |
| 194.6-210.5 | 99-106 | synth alone | arches, clouds and sun glow fade with the synth | the last thing glowing is the arches |
