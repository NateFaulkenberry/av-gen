# Rainbow World 16: the effect mapping

Brief 13, Phases 4-5 and 7. It rests on the stem analysis (`14-stem-analysis.md`) and the research
(`15-research.md`).

- **The code:** `examples/rainbow-world/blender/music.py` (the routes), `blender/look.py` (the ops each effect adds to
  a program) and `build_world.py` (the arches as programs, the looks, the flight, the two projects).
- **Revised for gate 4** (2026-10-20), after the owner's colour brief (`19-brief-color-vfx.md`): colour over geometry.
  The swell's deformers and the per-beat world ring are gone; the kick is local, the world scan is the drop's moment,
  and the looks carry the colour.
- **Revised for gates 5-8** (2026-10-21): the bass swell's new conditioning; the scan four times in the showcase,
  with a latching live trigger, the trees' fireworks and twirl; the sky's breathing clouds, colour splashes and sun
  glare; the cloud pass; the live ground's LOD tiers (22-owner-fixes.md has each verdict).
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

- **Musical control:** `macro.swell`, the bass's body. It is `stem.bass.level` conditioned (`x 2.4 - 1.4`, so only
  the bass's top half counts), attack 40 ms, decay 180 ms, plus 0.5 per `stem.bass.onset`. It swings 0-1 within a
  bar, on the bass's own syncopated rhythm, not the kick's (gate 5).
- **What it drives:**
  - the palette's own saturation (+0.18 at a full swell) and value (+0.07): every colour the look binds (meadow,
    sky, clouds, stones) deepens with the bass. No shader cost;
  - the cloud sea's bounce light (`lights/cloudsea`, +1.2);
  - the clouds' breathing (14% swell) and their light (+0.3) (gate 7, section 4.3).
  - The heave (a world-space sine deformer on the land) is retired. `SWELL` is empty, so no node carries it.
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
- **The clouds breathe (gate 7):** every puff swells about its own centre with the bass body, up to 14% at a full
  swell (`macro.swell` sets the strength of a constant field, `skyBreath`; a scale effector on the cloud nodes reads
  it). The clouds also brighten with it (`cloud_glow` +0.3).
- **Splashes of colour (gate 7):** patches of saturated colour bloom across the clouds and fade within a beat or two.
  - **Triggers:** each clap (the drum stem's 2-6 kHz band) and, bigger, each phrase start (`beat.phrasePulse`).
  - **Placement:** the patches are a ~400 m noise whose sample point travels with `beat.phrase` and `beat.bar`.
  - **Colour:** it turns once round the spectrum a phrase.
  - **Extent:** never more than about a third of the cloud area (28 section 4).
- **The cloud pass (gate 8, the owner: "no hard edges or corners"):** the lobes merge in a smooth union and are
  smoothed far longer, the bases round under the floor, and the masses simplify by screen size, not distance. The
  pale outline that the grazing dissolve drew at every fold is softened (28 section 6).
- **The sun (gate 7):** a soft glare (a forward-scattering halo with faint, uneven streaks) in place of gate 5's 22
  spokes. `macro.synth` (x1.6), `macro.lift` and a scan brighten it, and the look's sun colour tints it.
- **Sun glow:** `macro.synth` +0.25; `macro.lift` +0.35.
- **Shimmer:** the flower fields shimmer (world-position noise drifting in time), `macro.synth` x1.6 plus the hats.

### 4.4 Lift

`macro.lift` is the synth's brightness over bars (its centroid, conditioned, attack 1.5 s, decay 3 s, depth the
synth's rms so a silent stem's noisy centroid reads 0). It carries the 151-165 s rise and the riser at 183-190 s:
sun glow, bloom (+0.12) and the arches rise together, slowly.

## 5. The world scan

The showpiece (the owner at gate 2: "rare and dramatic, turning everything rainbow, holding then relaxing"; gates 5-7).
The design and its measurements are in 26 (the scan), 27 (the fireworks) and 28 (the twirl).

**What it does.** A front runs out from the camera over everything in reach, 10 m to 980 m in 7 s:
- the meadow, grass, stones and rims (scan ops in their programs) and the trees (their canopy ramps crossfade to the
  spectrum);
- it leaves the land rainbow behind it, a cyclic OKLab spectrum by distance with the surface's own value, so the land
  keeps its shading;
- the front's edge is bright.

**The trees and fireworks.** As the front reaches each tree, the tree twirls like a dancer and lights up in its own
colour. Its crown bursts like a firework shell, with an ignition flash and streaked sparks that fly out, slow, arc
down, cool from white-hot and crackle, gone about 1.15 s after the front.
- **The twirl:** anticipation, squash and stretch, a hop, a sweeping lean, overshoot and a damped wobble, in two
  temperaments.
- **How it is driven:** every stage is a function of how far the front has passed the tree (scene fields riding the
  front, effectors on the instances). There are no events, so seek equals play.

**With it:** the arches +14 and their flow +10, cloud light +0.6, sun glow +1.2, bloom +0.35, saturation +0.25. The
scan holds about six seconds and relaxes over three.

**When, in the showcase.** Four baked scans at the song's big synth swells and section peaks: 47.85 s (the drop),
111.3 s, 158.9 s and 190.7 s, as `timeline.scan` and `timeline.scanFront`.

**When, live.** A latching trigger on the swell over the current bed:
- the synth stem's rms, or the mix's sustain response without stems;
- read as a trend (short-term average over long-term, ADR-1575);
- latched for its whole hold and fall, with a 12 s refractory time and a re-arm on a new rise (ADR-1574).

Measured, from the drop to the nearest scan (26 section 4):

| Track, input | Scans | Distance from the drop |
|---|---|---|
| Feline, mix | 4 | 1.2 bars |
| Feline, stems | 4 | 0.2 bars |
| Trench | 6 | 0.1 bars |
| Hyperborea | 6 | 0.8 bars |

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
- soars to about 30 m over open meadow and over reachable crowns (52% of the loop above 15 m, the owner's "more
  soar"); dives under the bows (24 m) and toward the rim crossings;
- banks into its turns as a coordinated turn banks, gently (capped at 30 degrees, sprung over 400 m);
- trades speed for height with real g; surges 1.15x round a rim;
- no barrel rolls (the owner at gate 4: none into the rainbows; the bows stand every ~500 m, so none is earned away
  from one); the rim flips are the land's own, smoothed into long even turns;
- held 3 m clear of every tree and 3.5 m over the meadow along the whole spline;
- one lap 190 s.

## 8. The hierarchy (brief section 7.3)

| Tier | What | How often | Share of the frame lit |
|---|---|---|---|
| Background | the look; the arch flow, cloud light, sun glare, shimmer; the bass's chroma and the clouds' breathing | always, following the bass and synth | the whole world, gently |
| Rhythm | kick tips; glints; the clap along the bows and the cloud splashes; the bows' beat colour turn | each beat; each hat; beats 2 and 4 | the grass near the camera; specks; the arches; patches of cloud |
| Phrase | the arch flare (depth: the synth); a bigger cloud splash | every 4 bars (7.9 s) | the arches; patches of cloud |
| World-scale | the scan with its fireworks and twirling trees; the lift through the riser | four times in the showcase; once | the whole world, colour not white |

No full-frame flash anywhere. The sky's exposure never moves on a hit.

## 9. Determinism and live

- **Every control is a route chain or a stateless function of the transport.** Route chains are replayed on a seek
  (ADR-901); stems are on the replay bus (ADR-1520); routes feeding a macro make the seek replay the control layer
  (ADR-1573). The scan reads only replayable signals (section 5). The look selector is the documented exception.
- **Live:** the same routes run on the mix's `response.*` and the live beat tracker; a mapped input channel keyed
  `drums`, `bass` or `synth` turns the stem routes on and the fallbacks stand aside (ADR-1570). The live scan reads the
  synth stem's trend, or the mix's sustain trend without one (section 5).
- **Live tiers:** the ground's bands keep their far LOD rungs at the frame's material tier (ADR-1576), so live ground
  colour and shadows match a render and do not pop at rung boundaries (gate 8).

## 10. The effect timeline (Feline Footwear)

| Time (s) | Bars | Stems | Effect | With |
|---|---|---|---|---|
| 0.24-8.2 | 1-4 | drums | kick tips, glints | Bright Noon; a calm world |
| 8.2 on | 5 | bass | the bass's chroma, cloud-sea light, the clouds breathe | |
| 20.1-45 | 11-24 | synth | each call: arches, clouds, sun glare swell and fall | |
| 28.0 on | 15 | drums | the clap along the bows and splashes on the clouds | |
| **47.85** | **25** | **all** | **the drop: the camera flips over a rim; the scan runs out and turns the world rainbow, the trees twirl and burst; holds to ~53 s, relaxes to ~56 s; arches +14** | the track's largest |
| every 7.93 | 1 + 4k | all | the arch flare (x synth), a bigger cloud splash | |
| 101.4, 109.3 | 52, 56 | drums | fills: no kick, no clap | a hush |
| **111.3** | **57** | **drums** | **16th hats: glints denser; the second scan** | section B |
| 151-165 | 77-84 | synth | the lift: sun glare, bloom, arches rise slowly | |
| **158.9** | **81** | synth | **the third scan** | inside the rise |
| **182.7** | **93** | drums stop | kick, clap, glints and splashes stop; the lift rises through the riser | |
| 190 | -- | -- | the ride's lap ends and begins again (seamless) | |
| **190.7** | **97** | synth | **the fourth scan** | the riser's peak |
| 194.6-210.5 | 99-106 | synth alone | arches, clouds and sun glare fade with the synth | the last thing glowing is the arches |
