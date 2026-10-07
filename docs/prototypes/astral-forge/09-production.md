# The Astral Forge: production (LIVE and OFFLINE)

The owner unshelved the Astral Forge on 2026-10-07 and asked for a **production scene for live and offline**,
built with the new custom-renderer seam.

**The look target** is the prototype's TEST 06 clips from iterations 1 and 2 (`37-astral-forge/clips/` and
`iter2/clips/t06-audio-trench.mp4`), with slightly closer shots and longer face poses.

**The other requirements:**
- tuned for MIDI and audio at live and offline tiers;
- no Critic runs;
- a final render of the production version on *Trench*.

**Where things are:**
- branch `prod/astral-forge`;
- ADR-1221 (the environment) and ADR-1222 (tiers and MIDI);
- media in `~/Desktop/av-gen-review/41-astral-forge-production/`.

## 1. How it is built

**The seam is ADR-1200's Environment interface.** It is cherry-picked from proto/bioluminescent
`a8599186` as `abdcaef9`, and the header is byte-identical.

The Astral Forge is the second Environment: a scene block, `"astral"`, owned by `rendering::AstralRenderer`. It draws
into the scene's HDR and depth after the lit pass. AV Gen's bloom, tonemap, outputs, live ladder, audio, signal bus,
routes, MIDI, scene states and timeline are all shared.

**Merging with the Bioluminescent branch.** The one conflicting line will be
`environments_ = {ecosystem_.get(), astral_.get()}`.

**The prototype moved into the engine; it was not rewritten:**

| Part | Engine location | What it is |
|---|---|---|
| Song analysis | `src/scene/astral_song.*` | built from AV Gen's own analysed track |
| Conductor | `src/scene/astral_conductor.hpp` | |
| Shaders | `shaders/astral/*.wgsl` | |
| Scene block, parameters, live conductor | `src/scene/astral_forge.*` | |
| Renderer | `src/rendering/astral_renderer.*` | |

The prototype still builds from the same files, behind `-DAVGEN_ASTRAL_FORGE_PROTOTYPE`, and stays as the reference.

### Two conductors, one vocabulary

**SONG.** Used when the frame's audio is an analysed track (offline, or the editor playing a file).
- Sections choose the gods.
- 16-beat phrases build and hold, and a strong kick on a phrase downbeat collapses the god.
- Snares flash the face.
- It is a pure function of the song second.

**LIVE.** Used with live input.
- **Inputs.** It reads the live analysis's spectrogram and its beat, kick, snare and hat onsets.
- **Phrases.** It opens a phrase every sixteen beats it hears and collapses on a phrase-opening kick.
- **Gods.** Virtual sections of eight phrases cycle them.
- **Silence.** Through silence or a beat-tracker dropout it keeps time at the last tempo it heard, so the god never
  stalls.

**The camera.** Either conductor can place the scene's camera (`astral/camera`). A new shot is an ADR-912 cut.

### The camera and the look

**Camera.** Production uses iteration 2's camera vocabulary, the one the owner praised: the god centred, one
behaviour per phrase.
- **Build phrases:** OBSERVER, PROFILE, LOW.
- **Held phrases:** DESCENT, ORBIT, PORTRAIT.
- **MICRO:** an eye close-up, only in every eighth held phrase.
- **Withheld sections:** HOVER on the eyes.
- **Section openings:** a REVEAL.
- **Phrases opened by a collapse:** COLLISION.

The owner's changes, plus the coordinator's review of the first comparison:
- **closer:** `zoom` 1.1, and the reveal at half its iteration-2 amplitude;
- **longer face poses:** a build peaks by 60% of its phrase (about 4 s held), and a held phrase stays formed for
  about 10 s;
- **frontal at the peak:** PROFILE turns to about 18° as the face forms, ORBIT is a ±28° frontal arc, and the held
  face's dimensional folds are damped to 0.35;
- **readable faces:** v2's legibility at 0.55, feature light at 0.25 (raking key, rim and a restrained eye glow), and
  loose dust drawn into the formed face so the void is black at a peak.

**Material** is iteration 1/2's: silver engraved metal with temper, in a black void. Palette, atmosphere and god rays
are off; they are parameters.

**Side-by-side sheets:** `dev/reference-vs-production-first.jpg` and `dev/reference-vs-production-2.jpg`.

## 2. Parameters and the MIDI map

Every control is a real parameter at `astral/<leaf>`, so routes, MIDI, macros, scene states, the timeline and the
editor reach it.

| Parameter | Range | Default (scene) | What it does |
|---|---|---|---|
| `summon` | 0..1 | 0 | coherence floor: 1 summons the god fully formed |
| `hold` | 0..1 | 0 | holds the formed face |
| `collapse` | 0..1 | 0 | trigger: rising past 0.5 opens a collapsed phrase at once |
| `god` | -1..6 | -1 | -1 the score; 0 mask, 1 seraph, 2 abyss, 3 chimera, 4 machine, 5 choir, 6 horns |
| `intensity` | 0..2 | 1 | matter energy (flow, shimmer, flash, blast) |
| `zoom` | 0.25..4 | 1.1 | camera distance divisor |
| `cameraStyle` | 0..1 | 0 | 0 iteration 2's vocabulary; 1 v2's shot sizes |
| `camera` | 0..1 | 1 | the conductor drives the camera (0: the scene's camera) |
| `legibility` | 0..1 | 0.55 | a formed face reads by form |
| `light` | 0..1 | 0.25 | raking key, rim, eye glow |
| `dust` | 0..1 | 1 | loose dust absorbed into a formed face |
| `folds` | 0..1 | 0.35 | the held face's dimensional folds |
| `palette`, `atmosphere`, `godRays` | 0..1 | 0 | v2's per-god colour, void glow, shafts |
| `exposure` | 0..4 | 1 | |
| `density` | 0.05..1 | 1 | fraction of the particles simulated |

**The MIDI map** (`examples/astral-forge/astral-forge-live.json`, any device and channel):

| Control | Parameter | Mapping |
|---|---|---|
| CC 1 | summon | 0..1 |
| CC 2 | hold | 0..1 |
| CC 3 | intensity | 0..2 |
| CC 4 | zoom | 0.6..1.8 |
| CC 5 | palette | 0..1 |
| CC 6 | light | 0..1 |
| CC 7 | atmosphere | 0..1 |
| CC 8 | godRays | 0..1 |
| CC 9 | god | -1..6 |
| CC 10 | exposure | 0.3..1.6 |
| CC 11 | camera (conductor on/off) | 0..1 |
| CC 12 | density | 0.1..1 |
| CC 13 | legibility | 0..1 |
| Note 36 (pad) | collapse | velocity; fires on press |
| Notes 37..43 (pads) | choose a god: 0 mask, 1 seraph, 2 abyss, 3 chimera, 4 machine, 5 choir, 6 horns | latched |
| Note 44 (pad) | back to the score's god | latched |

**End-to-end MIDI test.**
- **Setup.** A virtual MIDI port (`live/midi_send.py`) played the map into AV Gen (`--midi AstralTest`, live mode,
  with Trench as live input).
- **What happened.** The god pad changed the god in 10 ms, CC 1 summoned the god fully formed, the collapse pad struck
  6 ms after the note, and the score pad returned the god to the score.
- **Logs.** `live/midi-test-sent.json` and `live/midi-test-received.log`.
- **Summon outranks collapse.** A collapse while summon is up flashes and restarts the phrase, but the god stays
  formed.

## 3. Live and offline tiers

`QualitySettings::astralTier` is ADR-1222. It changes the sampling only, never the composition.

| Tier | Used for | Particles | God-ray taps | Shards | Seek |
|---|---|---|---|---|---|
| 0 | offline (full-resolution march) | 100% | 20 | yes | exact: re-simulated from the song's start |
| 1 | live High/Ultra | 100% | 20 | yes | pre-roll |
| 2 | ladder Medium | 70% | 12 | yes | pre-roll |
| 3 | ladder Low | 50% | 8 | no | pre-roll |
| 4 | ladder Emergency | 33% | 0 | no | pre-roll |

The ladder's render scale lowers the march and the shading together. The renderer draws at the HDR target's size.

**Determinism.**
- **Fixed rate.** The simulation steps on an integral 60 Hz grid whatever the frame rate. Each step is conducted at
  its own second and splats the density.
- **Offline seek is exact.** At the offline tier a `--range` or seek lands on exactly the frame play produces (GPU
  test: seek against play, and 30 fps play against 60 fps play, byte-identical).
- **Live seek is approximate.** Live tiers pre-roll 6 s (ADR-360's contract).

### Measured (M2 Max, 1080p output)

**LIVE with music** (`--live-profile --mode live`, Trench into the BlackHole loopback as live input, 45 s, auto,
resolution_first):

| Quality | Settles at | Frame interval | GPU median / P95 | Deadline misses |
|---|---|---|---|---|
| auto | Emergency (scale 0.5, 33%) | median 16.65 ms | 12.7 / 15.8 ms | 0.3% (7 of 2689): 60 fps holds |
| ultra | Ultra (scale 1.0, 100%) | median 21.7 ms | 20.9 / 45.6 ms | 41.7%: about 45 fps |

**Held formed face, headless** (the Seraph held in a portrait, GPU median / P95):

| Quality | Scale, particles | GPU median / P95 |
|---|---|---|
| Ultra | 1.0, 100% | 20.4 / 22.2 ms |
| High | 0.85, 100% | 18.2 / 20.1 ms |
| Medium | 0.71, 70% | 14.7 / 15.7 ms |
| Low | 0.5, 50% | 11.3 / 13.2 ms |

**The Machine God at Ultra** costs 39.1 ms (Medium 26.7 ms). Its anatomy is three peeling shells of the face plate,
evaluated in the surface's bisection and normal.

**Where an Ultra frame goes:**
- surface shading 8.3 ms (Seraph portrait) to 21.6 ms (Machine God);
- particle step 5.7 ms;
- flakes 2.3 ms;
- density 1.6 ms;
- march 1.2 ms;
- cache 0.3 ms.

**The answer to "60 fps at the face":**
- **Which tier holds it.** At 1080p on this machine, 60 holds from Medium down on most gods, and at Low or Emergency
  on the Machine God. The auto ladder finds it: 0.3% misses over 45 s of Trench.
- **What it costs visually.** Half internal resolution (softer engraving lines, a little aliasing on the grooves)
  and a third of the particles (sparser matter while forming, a thinner dust field). The faces, the metal, the
  conductor and the camera are unchanged (`live/live-auto-emergency-last-frame.png`).
- **Ultra is the offline picture.** Live it runs at about 45 fps.

**Live capture:** `live/astral-forge-live-capture.mp4`, 55 s with music, from the live conductor. It was re-rendered
by `--live-capture`, so it runs at about 10 fps.

**OFFLINE** (the final render, 1080p30, the offline tier: every particle, the full-resolution march): 6,228 frames in
235 s, which is 26.5 fps or about 38 ms a frame for two simulation steps and the frame. 0 GPU errors.

## 4. The final render

The full *Trench* (207.6 s, 1080p30, with sound) through the production path:
`avgen --project examples/astral-forge/astral-forge-trench.json --render astral-forge-production-trench.mp4` (the
offline tier).

**Output:** `astral-forge-production-trench.mp4`, 207.6 s, h264 + AAC, 350 MB. It renders in 3 min 55 s.

**Contact sheets:** `contact-sheet-every-5s.jpg` and its greyscale twin.
- Faces read through every section: the Seraph with wings, the Machine God, the break where only the eyes form, the
  Chimera, and the Choir merging into one face.
- Collapses are dust sprays.
- The ending pulls back and fades.

## 5. What is left

- **The Machine God** costs about twice what the other gods do at the face, because of its three peeling shells
  (section 3). A live performance with long Machine sections runs at Low or Emergency.
- **No shadow casting.** The god casts nothing into the scene's shadow maps (ADR-1200 has no shadow hook). A HYBRID
  scene that needs it would add one, on the model of `SdfRenderer::drawShadow`.
- **The live conductor** knows no song structure: virtual sections of eight phrases stand in for sections, and the
  god's choice cycles. A performer's god pads and `astral/god` override it.
- **Live capture** (`--live-capture`) re-renders the frame, so the capture runs at about 10 fps. The live profile is
  the measurement.

