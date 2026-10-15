# Every live scene reads the Response controls

**Request** (the owner, 2026-10-14): "can we make all live scenes read response signals".

**Answer: yes.** Digital Mosh Live, THE RIFT LIVE, Sonic Live and Astral Forge Live now follow the Live panel's
Response group (Sensitivity, Transients, Sustain, Attack, Release: the parameters `sonic/response/*`, ADR-1062), as the
Sonic Abstract and Sonic VFX scenes already did. At the neutral settings (0.5 / 0.5 / 0.5 / 1x / 1x) each looks as it
did on Trench.

Decision record: [ADR-1361](../decisions/ADR-1361-every-live-scene-reads-the-response-controls.md). Review media:
`~/Desktop/av-gen-review/51-live-response/` (one folder per scene, plus `data/` with the traces and the scripts).
Branch `live/response-everywhere`, from main `b7d43921`.

## 1. What the controls reach now

| Control | What it moves | Neutral |
|---|---|---|
| Sensitivity | the hit floor and curve, and the level floor (+-18 dB) and curve | 0.5 |
| Transients | the hits only: `response.kick/snare/hat/onset/note`, and every onset the audio history hands out | 0.5 |
| Sustain | the levels only: `response.bass/level/levelSlow/sustain`, and the spectrogram the fields read | 0.5 |
| Attack, Release | multiply every follower's attack and release; Release also lengthens an onset field's decay and THE ASTRAL FORGE's hit envelopes | 1x |

There are two paths into a scene, and both now go through the controls:

1. **Routes and interpret mappings** read the bus. The live generators re-source them from `audio.*` / `sonic.*` to
   `response.*` (sections 2-4).
2. **The audio history** (ADR-1116) is read by spectrum and onset fields, by the GPU ring, and by THE ASTRAL FORGE's
   conductors. The engine sets an `AudioResponse` on it every frame (`Engine::updateAudioHistory`), and every
   reader hears it through that response:
   - an onset under the hit floor is not heard, and the next older onset takes its place among a field's newest
     eight;
   - the remaining onsets are curved;
   - a spectrogram bin is curved by the level sensitivity;
   - an onset field's decay is divided by Release.

   The neutral setting returns the stored bytes unchanged (`tests/unit/test_audio_response.cpp` checks every row,
   bin and onset). So projects without a `sonic` block, and projects that leave the controls at neutral, render
   unchanged. The Sonic VFX and Abstract scenes that set non-neutral defaults use no spectrum or onset fields, so
   they do not move either.

**Levels.** The `response.*` levels are fixed dB and never auto-gained. The `audio.*` bands are auto-gained against a
running maximum. So the generators do not swap one for the other where an auto-gained band carries the picture.
Instead they keep the band's shape, which is what the music is doing, and multiply it by the response level it is
heard at:

```text
heard band = band x response level x (1 / mean response level on Trench)
```

This is an interpret mapping. On a mastered mix at neutral it equals the band. On a quiet input it reads quieter,
and Sensitivity lifts it.

**Level range.** The three route-based projects set `sonic.response` to the range the Sonic Abstract scenes use: a
floor of -44 dB and a range of 42 dB. A mastered mix reads about 0.8, and a take 15 dB down reads about 0.45. With the
default range (-60 dB, 48 dB), Trench pins every level at 0.95-1.0, which leaves no room to move.

## 2. Digital Mosh Live

Generator: `examples/digital-mosh/build.py` (`LIVE_HEARD`, `LIVE_LISTEN`, `respond`). The change is live only.
`digital-mosh.json` and `digital-mosh-trench.json` keep their `audio.*` routes, so their renders do not move.

| Old signal | New signal | Amount | Route targets |
|---|---|---|---|
| `audio.onsetLow` | `response.kick` | x0.77 | fracture light, block effector, split |
| `audio.onsetMid` | `response.snare` | x0.78 | infect, mosh, tear |
| `audio.onsetHigh` | `response.hat` | x0.91 | ground and block emission |
| `audio.bassLevel` | `response.bass` | x1.04 | melt, bark |
| `audio.bass` | `visual.hBass` = audio.bass x response.bass x 1.22 | x1 | the limbs' lift |
| `audio.treble` | `visual.hTreble` = audio.treble x response.level x 1.2 | x1 | the motes |
| `audio.trebleLevel` | `visual.hTrebleLevel` = trebleLevel x response.level x 1.2 | x1 | the glitch amount |
| `visual.drive` (arc) | `visual.busy` x `visual.presence` | x1 | `visual.busy` is the old drive; presence = clamp(2.5 x response.level - 0.625): 1 from a level of 0.65 |

How the amounts were set:

- **Hits.** Each amount is the ratio of the old onset's mean strength to the new hit's on Trench (0.506 / 0.659 for
  the kick), so a single hit lands as hard as before. The response kick fires 389 times on Trench, where
  `audio.onsetLow` fired 155. The new detector (ADR-1067) hears the kicks the band onset missed, so the fracture
  answers more of the kicks.
- **Levels.** Each gain is 1 / the response level's mean on Trench at neutral, so the means are the old ones.

The scene's own SENSITIVITY macro (CC 1) stays. It trims how far the arc follows; the Response Sensitivity decides
what is heard at all.

**Why the arc gets a knee.** The arc integrates its drive into the stage ladder, so it is the one place where a
small, steady difference adds up. A first cut multiplied the drive by the plain response level (x 1.2). On a
mastered mix that level swings by a few per cent where the old drive did not, and Trench's stages moved by up to
16 s. Multiplying by `presence` instead fixes this. Presence is 1 whenever the music reads at least 0.65 (a mastered
mix at neutral), falls to 0.5 at 0.45, and reaches 0 at 0.25. So a full mix drives the arc exactly as before, and a
quiet input or a low Sensitivity calms it.

**Neutral, on Trench** (`--sonic-trace`; the CSVs are in `data/`):

| | old | new |
|---|---|---|
| `macros/energy` mean | 0.744 | 0.741 (r = 0.999, mean abs diff 0.004) |
| limbs' lift source mean | 0.529 (audio.bass) | 0.525 (hBass, r = 0.988) |
| motes source mean | 0.453 (audio.treble) | 0.453 (hTreble, r = 0.992) |
| melt source mean | 0.805 (bassLevel) | 0.802 (response.bass x 1.04) |
| stages (s) | Uncanny 21, Infection 37, Respite 63, Uncanny 79, Infection 101, Corruption 118, Nightmare 137, Collapse 168, Recovery 198 | 22, 37, 63, 79, 99, 115, 133, 164, 193 |
| stages on Feline Footwear | | identical, to the second |

**Sliders, on Trench** (means over the song):

| | `macros/energy` | presence | hBass | hTreble | kick strength | kicks |
|---|---|---|---|---|---|---|
| Sensitivity 0.2 | 0.420 | 0.548 | 0.291 | 0.254 | 0.566 | 388 |
| neutral | 0.741 | 0.986 | 0.525 | 0.453 | 0.659 | 389 |
| Sensitivity 0.8 | 0.744 | 0.994 | 0.624 | 0.531 | 0.739 | 389 |
| Transients 0.2 | 0.741 | | 0.525 | 0.453 | 0.566 | 388 |
| Transients 0.8 | 0.741 | | 0.525 | 0.453 | 0.739 | 389 |
| Trench -15 dB, neutral | 0.219 (old project: 0.381) | 0.571 | 0.298 | 0.260 | 0.659 | 385 |
| Trench -15 dB, Sensitivity 1 | 0.381: the old project's quiet-take arc, stage for stage | 0.993 | 0.591 | 0.507 | 0.783 | 385 |

On a loud mix, Sensitivity above neutral does not push the arc further, because presence is already 1 there. It
brightens the levels and hits the arc reads beside it. On a quiet input, Sensitivity is what brings the arc back.

**Renders** (`~/Desktop/av-gen-review/51-live-response/digital-mosh/`; 1080p stills; the clips are 960x540; the
"before" project is `b7d43921`'s, rendered with this branch's binary, which is the identity at neutral):

- `neutral-t030/045/070/090.png` show the same stage and framing at the same second: the Uncanny, the Infection with
  the eye and the stain, the Respite and the Uncanny again. The flying shots differ by a few metres of path, because
  the camera integrates the energy. At 30 s the new kick lights the stain's fracture on a frame where the old band
  onset did not fire.
- `clip-before-38-48s.mp4` and `clip-after-38-48s.mp4`: the Infection, 10 s.
- `sensitivity-t045.png` (0.2 / 0.5 / 0.8) and `sensitivity-t120.png` (0.2 / 0.8).
- `quiet-t045.png` (Trench -15 dB). The old project, the new project at neutral, and the new project at
  Sensitivity 1. The last matches the old project's frame.
- `signals-trench.png`.

**A seek finding.** A first version computed the drive from its own source's outputs, a frame late. Traces and
`Engine::seekSeconds` agreed with play, but `avgen --render --range 45:45.03` landed in the Dream while play was in
the Infection. Moving the heard signals to a first `heard` source, as THE RIFT and Sonic Live do, fixed the render.
The four "before" stills reproduce byte for byte across two runs. `test_live_response.cpp` also seeks the arc on
Trench.



## 3. THE RIFT LIVE

Generator: `examples/bioluminescent/build.py` (`LIVE_HEARD`, `respond`). The change is live only, and `rift.json` is
untouched. The ADR-1344 live LOD and cull settings are unchanged; the generator still writes them, and the existing
test still holds them.

| Old signal | New signal | Route targets |
|---|---|---|
| `audio.energy` | `visual.hEnergy` = audio.energy x response.level x 1.2 | `macros/energy` (the arc, the Aftermath trigger) |
| `audio.bass` | `visual.hBass` = audio.bass x response.bass x 1.22 | `macros/bassSlow` (Breath / Awake / Drop), the ignition rate |
| `audio.trebleLevel` | `visual.hTreble` = trebleLevel x response.level x 1.19 | spores |
| `audio.mid` | `visual.hMid` = audio.mid x response.level x 1.19 | the siphonophore chains |
| `audio.onsetMid` | `response.snare` (amount x0.78) | the swarm burst |
| kick front (onset field, `low`) | the heard audio history | Transients decides which kicks launch a front, Release lengthens it |
| highs (spectrum field) | the heard audio history | Sustain and Sensitivity curve the highs |

**Neutral, on Trench:**

| | old | new |
|---|---|---|
| `macros/energy` mean | 0.663 | 0.666 (r = 0.924) |
| `macros/bassSlow` mean | 0.608 | 0.610 (r = 0.986) |
| stage sequence | Dark 0, Stirring 12, Awake 37, Breath 47, Awake 53, Awake 2 69, Awake 85, Breath 88, Build 94, Drop 96, Body 116, Body 2 132 ... Aftermath 205 | **identical, to the second** |

**Sliders, on Trench** (song means):

| | `macros/energy` | `macros/bassSlow` | stages reached |
|---|---|---|---|
| Sensitivity 0.2 | 0.377 | 0.346 | Build at 52 s, Drop 53, Body 74, Aftermath at 78 (the canyon gives up) |
| neutral | 0.666 | 0.610 | the full arc |
| Sensitivity 0.8 | 0.790 | 0.705 | the full arc, brighter and more ignition |
| Trench -15 dB, neutral | 0.384 (old project: 0.663) | 0.353 | as at Sensitivity 0.2 |
| Trench -15 dB, Sensitivity 1 | 0.750 | 0.675 | the full arc again |

The old project could not tell the quiet take from the loud one, because its listeners were auto-gained. Now a
quiet input wakes the canyon less, and Sensitivity is the knob that brings it back.

**Renders** (`~/Desktop/av-gen-review/51-live-response/rift/`):

- `neutral-t030/090/100/150.png` show the same stages and light at the same seconds. The camera is a metre or two
  apart along its path, which integrates the energy.
- `clip-before-94-104s.mp4` and `clip-after-94-104s.mp4`: the Build into the Drop.
- `sensitivity-t100.png` and `sensitivity-t150.png` (0.2 / 0.5 / 0.8). At 0.2 the canyon is back in its early
  stages, dark with a few lanterns. At 0.8 the crinoid canopy and the fronts are fuller.
- `quiet-t100.png` (Trench -15 dB). The old project lit the drop as if the take were loud. The new one at neutral is
  still in the Awake. At Sensitivity 1 it is in the drop again.



## 4. Sonic Live

Generator: `tools/sonic_live_project.py` (`HEARD`, `respond`). Sonic Live is played on a keyboard and a synth. Its
character stays the Sonic Character's: the organic, crystalline and chaotic families, brightness, roughness, grit and
register. Only its dynamics move to the response layer.

| Old signal | New signal | Read by |
|---|---|---|
| `sonic.energy` | `visual.loud` = response.level x 0.89 | the energy, radiance, grain and grit mappings |
| `sonic.energy.slow` | `visual.loudSlow` = response.levelSlow x 0.94 | silence (the waiting world), mass (the heavy world) |
| `sonic.transient` | `visual.strike` = clamp(4 x response.onset - 3) | the chaotic world's shards, sparks, shock and top light (8 routes; their gates unchanged) |
| `notes.noteOn` | `response.note` | 30 routes: note gestures (velocity through the Transients curve; equal at neutral) |

`response.levelSlow` is new: the level over about 3 s, with Attack and Release multiplied. It is declared after every
older id. It does the job `sonic.energy.slow` did: it decides when a rest becomes the waiting world.

`heard` is a new first interpret source, so the garden reads `loud` and `loudSlow` on the same frame. The generator now also writes the project's `live.qualityStrategy` (`effects_first`). The committed project
carried it as a hand edit, so a regeneration would have dropped it, and the live controller would have chosen
"balanced".

**Neutral, on Trench:**

| | old | new |
|---|---|---|
| `visual.energy` | 0.739 | 0.739 (r = 0.955) |
| `visual.radiance` / `grit` / `grain` | 0.705 / 0.822 / 0.394 | 0.705 / 0.821 / 0.393 |
| `visual.mass` | 0.318 | 0.318 (r = 0.999) |
| the families (organic, crystalline, chaotic) | | identical (r = 1.0) |
| strikes | 31 `sonic.transient` events, 26 of them in the sparse intro | 89 strikes (the strongest eighth of the onsets), spread through the song |

**Sliders:**

| | `visual.energy` | `visual.mass` | strikes |
|---|---|---|---|
| Sensitivity 0.2 | 0.415 | 0.183 | |
| neutral | 0.739 | 0.318 | 89 |
| Sensitivity 0.8 | 0.879 | 0.373 | |
| Transients 0.2 / 0.8 | (unchanged) | (unchanged) | 39 / 192 |
| Trench -15 dB, neutral | 0.423 (old project: 0.481) | 0.188 | |
| Trench -15 dB, Sensitivity 1 | 0.834 | 0.354 | |

`sonic.energy` was already a fixed-dB loudness, so the quiet take is close to the old project's (0.42 against 0.48).
What is new is that Sensitivity can lift it. At Sensitivity 0 the waiting world holds while music plays (tested).

**Renders** (`~/Desktop/av-gen-review/51-live-response/sonic-live/`): `neutral-t020/100/150.png` (the chaotic world on Trench, matching), `clip-before-94-104s.mp4`
and `clip-after-94-104s.mp4`, `sensitivity-t100.png` and `quiet-t100.png`. The garden's loudness channels drive
emission and the ground's veins, so the slider differences in a still are real but modest: the ground glow and the
hero's light. The strikes are easier to see in the clips. The character, which is the world itself, does not move,
by design.



## 5. Astral Forge Live

Astral Forge Live has no routes. Its conductor reads the audio history, so it follows the controls through the heard
history. The project files did not change, apart from a note.

- **Song mode.** This is an analysed track: an offline render, or the editor playing a file. The conductor conducts
  `conditionSong(song, response)`:
  - the spectrogram and the rms go through the level sensitivity;
  - the band envelopes and the centroid are folded again, with Attack and Release multiplied (`foldSongEnvelopes`,
    which `buildSong` now shares);
  - the kicks, snares and hats go through the hit sensitivity, and a hit under the floor is dropped;
  - the hit envelopes' decay is lengthened by Release;
  - the score is rebuilt.

  The heard song is rebuilt when a control changes, about 10 ms on Trench. At neutral it **is** the song: the
  composition hands over the same object (`tests/integration/test_live_response.cpp`). So
  `astral-forge-trench.json`, which has no `sonic` block, renders unchanged.
- **Live mode.** The live conductor reads the heard rows and hears each onset through the hit sensitivity before the
  collapse's 0.5 threshold and the blast. Attack and Release multiply its 12 ms and 220 ms band envelopes.

**On Trench** (the heard-song log line):

| | kicks heard | kick-opened phrases (collapses) |
|---|---|---|
| neutral | 155 of 155 (the same song object) | 7 of 7 |
| Transients 0.3 or more | 155 of 155, curved | 7 of 7 |
| Transients 0.15 | 72 of 155 | 7 of 7 |
| Sensitivity 0.2 | 78 of 155 | 7 of 7 |

The collapses are carried by Trench's strongest kicks, so they survive any setting above 0. What Transients changes
is how hard the god breathes and blasts on every other kick. Sensitivity and Sustain change how much the spectrum
lifts its flow, shimmer and temper.

**Renders** (`~/Desktop/av-gen-review/51-live-response/astral-forge/`):

- `neutral-t060.png` and `neutral-t120.png`: the old and new projects at neutral are **byte-identical**
  (`cmp`, 0 differing pixels).
- `transients-t120.png` and `sensitivity-t120.png` (0.15 or 0.2 / neutral / 0.85 or 0.8). About 46% of pixels
  differ: the god's breath, flow and blast. The god and its phase do not change, because the score's collapses are
  the same.
- `clips-transients-0.15-top-0.85-bottom.png` and the two clips (110-120 s) show the god's surface and the dust
  through a collapse at the two settings.



## 6. Frame time

Measured with `tools/live_scene_profile.py` under `tools/gpu-lock.sh`, with the setup of
`docs/prototypes/bioluminescent/live-30fps-report.md`:

- 1920x1080 projection window (`--mode live`), a 30 fps target, auto quality, 8 s warm-up, 20 s measured.
- Each project is a scratch copy with Trench as its audio and `sonic.live` removed, so the file drives it. Before is
  `b7d43921`'s project, after is this branch's. Both run with this branch's binary, whose engine change is the
  identity at neutral.
- Runs are interleaved. The raw reports and JSON are in `data/perf/`.

| Scene, start | Level | Frame median / P95 (ms) | GPU median (ms) | CPU work (ms) | Deadline misses |
|---|---|---|---|---|---|
| THE RIFT LIVE 96 s, before (2 runs) | Low | 33.29-33.31 / 35.08-35.19 | 25.30-25.62 | 6.31-6.86 | 0 |
| THE RIFT LIVE 96 s, after (2 runs) | Low | 33.29-33.30 / 34.93-35.03 | 25.10-25.17 | 6.19-6.59 | 0 |
| THE RIFT LIVE 150 s, before / after | Low | 33.30 / 33.31; P95 35.09 / 35.10 | 24.90 / 25.10 | 6.35 / 6.22 | 0 / 0 |
| THE RIFT, headless pinned High, 96 s, before / after (2 runs each) | High | | 28.11-28.44 / 27.98-28.11 | 5.37-5.48 / 5.41-5.42 | |
| Digital Mosh Live 60 s, before / after (2nd pair) | Emergency | 33.36 / 33.29; P95 39.09 / 34.92 | 28.18 / 26.15 | 6.09 / 5.83 | 19 / 0 |
| Sonic Live 100 s, before / after (2 runs each) | High | 33.28-33.30 / 33.29-33.31; P95 35.0-35.1 both | 26.74-26.80 / 26.74 | 2.96-3.06 / 2.95-3.02 | 0 |

- **THE RIFT LIVE still holds 30 fps** at 1080p, at the same level as before (Low), with no deadline misses. The
  interpret source costs nothing measurable. The history's conditioning costs nothing at neutral, where it returns
  the stored value.
- **Digital Mosh Live.** The two pairs differ by run-to-run noise (first pair: 24.71 / 24.31 ms GPU, both without
  misses).
- **Sonic Live**, first pair: the regenerated project had lost its `live.qualityStrategy` (see section 4), so the
  controller chose "balanced". The generator now writes the block, and the second pair above is identical.
- **Astral Forge Live** was not measured. With a file it plays in song mode, and the live profile then spends 3.3 s a
  frame in `astral.sim`, before and after alike (3311 / 3312 ms). The renderer conducts its sub-steps from the song
  and falls behind a slow frame. This is a pre-existing property of playing the file live, not of this change. The
  scene's real live path (an input device, the live conductor) was not measurable headless. Its added per-frame work
  is one comparison of the controls, and the heard song is rebuilt (about 10 ms) only when a slider moves.


## 7. Tests and suites

- `tests/unit/test_audio_response.cpp` ([adr1361]) has five cases:
  - neutral is the identity, bit for bit, on rows, bins, onsets and the decay scale, and the revision does not move;
  - Transients drop and curve onsets, older onsets fill in, and Sensitivity 0 hears nothing;
  - Sustain curves the spectrogram monotonically, and Attack and Release neither refill a ring nor change a row,
    while Release slows an onset field;
  - `conditionSong`: neutral is the song, Transients drop kicks, and Release slows the envelopes;
  - the live conductor's blast follows Transients.
- `tests/integration/test_live_response.cpp` ([adr1361]) plays a synthetic groove into each project:
  - **Digital Mosh Live:** the arc rises with Sensitivity; a take 16 dB down is heard quieter and Sensitivity lifts
    it; the fracture rises with Transients.
  - **Sonic Live:** loudness follows Sensitivity and Sustain but not Transients; Sensitivity 0 holds the waiting
    world; strikes follow Transients.
  - **Astral Forge Live:** neutral conducts the same song object; Transients drop kicks; Sensitivity lifts the
    spectrum; Release reaches the hit envelopes.
- `tests/integration/test_rift.cpp`: the arc's two listeners rise with Sensitivity, and the fields' history carries
  the controls.

**Suites** (at `f7f7cc84`; the later commits are documentation):

- **CPU**, `tools/ci/run-suite.sh cpu-assets`: **PASS, exit 0**. 4240 passed, 0 failed, 19 skipped, and 1 expected
  failure (the known `[!shouldfail]` case); 4260 of 4260 ran in 16 min 44 s.
- **GPU**, `tools/ci/run-suite.sh gpu --bin build/release/tests`: **PASS, exit 0**. 657 passed, 0 failed, 1 skipped;
  658 of 658 ran in 33 min 2 s.
- `python3 tools/check_project_integrity.py`: exit 0.

The suite results are in `~/Desktop/av-gen-review/51-live-response/data/suites/`.
