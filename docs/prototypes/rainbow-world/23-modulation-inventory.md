# Rainbow World 23: what reacts to the music, and why it reads as weak

The owner (22-owner-fixes.md): "have agent list current modulations that are built into the scene, describe what
should be happening - im not seeing a lot of audio reactivity yet, so I need to understand where improvements can be
made".

## How this was measured

- **Routes:** every route in `rainbow-world-feline.json` (the showcase, 241 routes) and `rainbow-world.json` (the live
  project, the same set with the main-mix fallbacks), read from the project and from `avgen --audit-routes`. 219 are
  live and 22 are "live-only" hazards: their source is a live signal, which is expected.
- **How often each one fires:** a fresh `avgen --sonic-trace` of the showcase gives the engine's own stem, beat and
  macro values every frame. Direct routes are re-run through their chains (gain, offset, clamp, attack and decay). A
  "fire" is an upward crossing of the threshold given in the table.
- **How visible each one is:** my rating at 1080p in the moving shot, from the renders and from two geometric facts
  about the ride:
  - the camera is more than 10 m up for 60% of the lap;
  - a rainbow arch is within 55 degrees of the view and 900 m away for 76% of the lap.

Feline Footwear is 210.5 s long and has 425 beats and 27 four-bar phrases.

**Something I got wrong:** docs 16 and 17 say the bass "swell" deformer was removed. It was not. The 1.9 m bass heave
is still on every land, grass, flower, rock and tree node. That is 91 routes, and 1.1-2.7 ms per frame (17 section 4).
I corrected this here and will fix those docs.

## The inventory (the showcase; the live project uses the fallback where noted)

Visibility ratings: **H** = obvious in the moving shot; **M** = noticeable when you look for it; **L** = hard to see;
**VL** = practically invisible.

| # | Effect | Source (offline; live fallback) | Target and what it does | Depth, timing, gating | How often over the song | Visibility |
|---|---|---|---|---|---|---|
| 1 | Kick on the grass | drums stem, low band (`stem.drums.bass`); live `response.kickEnv` | the grass tips within about 32 m of the camera light in the look's kick colour | x3, clamped, gain 2.2; attack 4 ms, decay 140 ms | 366 kicks (104/min); above half for 47% of the time | **L**: only the grass near the camera lights, and the camera is above 10 m for 60% of the lap; grass is not drawn beyond 42 m |
| 2 | Hat glints | drums stem, treble; live `response.hatEnv` | a few grass tufts flash white; louder hats light more tufts | x1.6 - 0.35, clamped, gain 3.2; attack 3 ms, decay 110 ms | 451 (129/min) | **VL**: specks a few pixels across |
| 3 | Clap on the arches | drums stem, 2-6 kHz (`highMid`); live `response.snareEnv` | the bright bands flowing along each arch jump (flow +2.5) | x2.5 - 1.55, clamped; decay 280 ms | 195 (56/min, beats 2 and 4) | **L-M**: the arches are in view 76% of the time, but often far away, and the bump sits on bands the synth already holds bright |
| 4 | Bass heave ("swell") | `macro.swell` = bass stem level, plus a push on each bass note; live `response.bass` | a 105 m world-space wave lifts the land and everything on it by up to 1.9 m | attack 60 ms, decay 500 ms | above 0.3 for 85% of the song; swings about 0.4 a second | **L**: 1.9 m does not read from 15-30 m up in a moving shot. Costs 1.1-2.7 ms |
| 5 | Bass colour | `macro.swell` | the palette's saturation +0.18: every colour the look binds deepens | as above | as above | **VL**: about +0.08 of saturation in practice, and slow |
| 6 | Cloud-sea light | `macro.swell` | the warm bounce light from the cloud sea below, +0.6 | as above | as above | **L** |
| 7 | Flower shimmer | `macro.synth` x1.6, plus drums treble x1.2 | flower petals shimmer with drifting noise | the synth is slow; the hats are fast | always on; the hats add 129/min | **L**: flowers are near the ground and small |
| 8 | Synth light on the arches | `macro.synth` = synth stem level (0.4) + rms (0.7) + a push on each synth note; live `response.sustain` | arch flow x3, arch brightness +1.2, the faint second bow +0.8 | attack 250-300 ms, decay 1.2 s | above 0.3 for 84% of the song; swings about 0.2 a second | **M**, but it reads as "the arches are bright", not as reacting: it barely moves |
| 9 | Synth light in the sky | `macro.synth` | painted clouds +30% light, sun glow +0.25 | as above | as above | **L** |
| 10 | Phrase flare | `beat.phrasePulse` (bars 1 + 4k), depth = synth stem loudness | the arches flare (+4, falling over 1.7 s) | the intro's phrases flare less | 27 phrases, one every 7.9 s | **M**: the clearest rhythmic arch effect, when an arch is large in frame |
| 11 | Lift | synth stem brightness (centroid), gated by its loudness | sun glow +0.35, bloom +0.12, arches +1 | attack 1.5 s, decay 3 s | above 0.3 for only 2% of the song (151-165 s and the 183-190 s riser) | **L** |
| 12 | World scan | phrase pulse (read 300 ms late, held 6 s), only when the synth enters loud (level near the top of its range) and is absolutely loud (rms) | a rainbow front runs 970 m out from the camera over the land, rocks and trees; the arches flare +14, clouds +0.6, sun glow +1.2, bloom +0.35, saturation +0.25 | about 6 s hold, 3 s relax | **once**, 48.1-52 s (the drop). A 0.12 flicker at 37-40 s is too weak to see | **H** when it fires. Live without a synth channel it never fires |
| 13 | Looks advance | phrase pulse, plus big changes in the music's energy, brightness and density | the whole palette changes look (sky, meadow, clouds, trees) over 8 s | at least 60 s per look, at most 120 s | 3 changes over the song (Noon to Golden to Storm to Dusk, at about 70, 135 and 190 s) | **H**, but slow: a mood, not a reaction |
| 14 | MIDI controls (CC 1, 2, 5, 6, 7, 9, 10) | knobs | glow, lens, haze, light, colour, look, look lock | 150 ms smoothing | only with a controller | n/a |

The camera and the beat clock are not modulations of the picture. The camera is baked; the beat clock follows the
drums stem (`feeds`).

## What should be happening, and why it reads as weak

The design (16-effect-mapping): drums are local and rhythmic, bass is colour energy, the synth is the light and the
sky, phrases flare the arches, and the drop is a world-scale event. The routes do run and do follow the stems, but on
screen it is weak for five reasons:

1. **The fast effects are on small, near things.** The kick, glints and shimmer live on grass tips and flowers within
   about 30 m. The flight now spends 60% of the lap above 10 m, so they are rarely large in frame. The frame is
   dominated by sky, cloud sea, big meadow surfaces and arches, and nothing on those moves with the beat.
2. **The continuous drivers barely move.** The swell and the synth light are smoothed and sit above 0.3 for 85% of
   the song. Their effect reads as part of the look, not as a response. There is no contrast.
3. **There is no per-beat change on big surfaces.** This is a rule from gate 2 ("no world sweep on every quarter
   note", no full-frame flash). It is right for strobing, but it also left the sky, land colour and clouds
   beat-blind.
4. **The arches react on top of an already bright bow.** The +2.5 clap bump on bands the synth holds at 1.5-2.5 hits
   saturation and bloom, so the change is small.
5. **The one big event happens once.** That was by design (gate 2: "rare and dramatic"); the owner now wants more.

## The top 5 improvements, ranked by visible impact per millisecond

All five are routes and palette scalars on parameters that already exist: no new shader ops. They use the frame time
the swell deformer frees up if it goes (item 5).

1. **A beat-locked colour pulse on the arches** (also on the owner's list).
   - Each bow's emissive boost rides a sharp-attack envelope on the beat, with a hue step on each bar.
   - The arches are in view 76% of the lap and are the brightest thing in the frame, so a pulse there reads at any
     distance.
   - Cost: about 0 ms (routes on node parameters).
2. **More scans: 3-5 per song, at the big synth swells and section peaks**, never on a beat grid (on the owner's
   list).
   - The only effect rated H that is tied to the music.
   - Cost: 0 ms; it already runs every frame.
3. **Let the swell and the synth breathe.**
   - Condition them against the song's own range: lower ceilings, faster release, and a peak-hold-and-fall on
     onsets. They would then swing from low to high within a phrase instead of sitting high.
   - Move what they drive onto big surfaces: the bass on the meadow's brightness (`palette/value`, about ±6% on bass
     notes) and on the cloud-sea light; the synth on the sky (the horizon's reach, `skyHaze`) and the sun glow.
   - Cost: 0 ms.
4. **A kick or snare accent on the clouds.** The painted clouds and the cloud sea cover 30-50% of most frames.
   - A snare-driven glow on the clouds (`cloud_glow`, already a route target) is big and visible, and a backbeat
     rather than a quarter-note sweep.
   - Cost: about 0 ms.
5. **Retire the swell heave** (1.1-2.7 ms for an L-rated effect).
   - Either drop it and spend the time on the above, or keep it only on the land (not the 30k flowers and grass).
   - It is also the owner's earlier direction: colour over geometry.

Lower value: a brighter or wider kick on the grass (it stays local); more glints (it stays specks).

## In the live project

The same set, on the main mix and the live beat clock. Without stems:
- the scan never fires;
- the clap and kick fall back to the mix's snare and kick envelopes;
- the synth light falls back to `response.sustain`.

Items 1, 3 and 4 above would work on a plain stereo input as well. Item 2 needs the synth stem (or a mapped synth
channel).
