# ADR-1361: Every live scene reads the Response controls

- Status: Accepted (2026-10-15), the owner's request
- Extends: ADR-1062 (the response model and the performer's Sensitivity / Transients / Sustain / Attack / Release),
  ADR-1116 (the audio history the spectrum and onset fields read), ADR-1221/1222 (THE ASTRAL FORGE's conductors).
- Code: `src/spatial/audio_history.{hpp,cpp}` (`AudioResponse`, `AudioHistory::setResponse`, `onsetDecayScale`),
  `src/spatial/field.cpp` and `shaders/fields.wgsl` (an onset field's decay over Release), `src/rendering/field_uniforms.cpp`
  (`audio.timing.w`), `src/app/engine.cpp` (`updateAudioHistory`), `src/scene/astral_song.{hpp,cpp}` (`conditionSong`,
  `foldSongEnvelopes`, `SongAnalysis::release`), `src/scene/astral_forge.cpp` (the live conductor),
  `src/scene/composition.cpp` (the heard song), `src/sonic/response.{hpp,cpp}` (`response.levelSlow`); the generators
  `examples/digital-mosh/build.py`, `examples/bioluminescent/build.py`, `tools/sonic_live_project.py`.
- Tests: `tests/unit/test_audio_response.cpp`, `tests/integration/test_live_response.cpp`,
  `tests/integration/test_rift.cpp` ("THE RIFT LIVE: the arc, the stage machine's listeners and the fields hear the
  Response").
- Evidence: `docs/development/live-response-everywhere.md`.

## Context

The Live panel's Response group edits `sonic/response/*`, and only the Sonic Abstract and Sonic VFX scenes read the
`response.*` signals those controls shape. The other four live scenes ignored the sliders: Digital Mosh Live and THE
RIFT LIVE routed `audio.*` (auto-gained bands, the analyser's band onsets), Sonic Live routed the Sonic Character's
`sonic.energy` and `sonic.transient`, and THE ASTRAL FORGE's conductor read the audio history directly, with a fixed
0.5 kick threshold. THE RIFT's kick front and highs, and Digital Mosh's spectrum fields, also read the history. The
owner: "can we make all live scenes read response signals".

## Decision

**The audio history is heard through the controls.** `AudioHistory` carries an `AudioResponse` (the hit and level
sensitivities the controls give, and Attack and Release), which the engine sets every frame from `sonic/response/*`.
Every reader then hears the same thing the routes hear:

- an onset below the hit sensitivity's floor is not heard (an older one takes its place in a field's newest eight),
  the rest are curved -- the response model's chain (`sonic/response.cpp`), so Transients decides how many kicks a
  kick front launches and how hard;
- a spectrogram bin is curved by the level sensitivity and fades out over its first tenth, so Sustain and Sensitivity
  move the spectrum fields and the GPU ring (a change bumps the history's revision and the ring refills);
- an onset field's decay rate is divided by Release (`audio.timing.w` on the GPU, the same on the CPU).

**The neutral controls are the identity, bit for bit.** `value`, `lastOnsets` and the decay scale return what they
always did, so a project that never moves the sliders -- and every project without a `sonic` block, whose controls
stay neutral -- renders exactly as before. Attack and Release do not change what a row means and do not refill a ring.

**THE ASTRAL FORGE's conductors.** Song mode conducts `conditionSong(song, response)`: the spectrogram and the rms
through the level sensitivity, the band envelopes and centroid folded again with the attack and release multiplied
(`foldSongEnvelopes`, which `buildSong` now uses too), the kicks, snares and hats through the hit sensitivity (one
under the floor is dropped, the rest curved against the collapse's 0.5 and the blast), and the hit envelopes' decay
over Release; the score is rebuilt from it. It is rebuilt when the controls change, and at neutral it is the song
itself (the same object), so an offline render of `astral-forge-trench.json` is unchanged. The live conductor reads
the heard rows, hears each onset through the hit sensitivity, and multiplies its envelopes' times.

**The routes, re-sourced in the generators (live projects only).**

- *Hits* are the response model's: `audio.onsetLow/Mid/High` become `response.kick/snare/hat`, their amounts scaled by
  the ratio of the mean strengths on Trench so a hit lands as hard as before.
- *Levels* are fixed-dB and never auto-gained. A steady level becomes the response level (`audio.bassLevel` ->
  `response.bass`, which matches it within 4 % on Trench). An auto-gained band, or the analyser's energy, keeps its
  shape -- what the music is doing, which the Response does not govern -- and is multiplied by the response level it
  is heard at, in an interpret mapping (`visual.hBass` = `audio.bass` x `response.bass` x 1.22, the gain the inverse
  of the level's mean on Trench). A quiet input now reads quiet, as the Sonic Abstract scenes do; Sensitivity lifts it.
- *Digital Mosh's arc* integrates its drive into the stage ladder, where a small steady difference adds up, so its
  drive is multiplied by a knee instead (`presence`: 1 from a response level of 0.65, which a mastered mix holds at
  neutral): the old arc on a full mix, a calmer one on a quiet input until Sensitivity is raised.
- *Sonic Live* keeps its character (the families, brightness, roughness) and takes its dynamics from the response:
  `sonic.energy`/`.slow` -> `response.level`/`response.levelSlow` (scaled to the old means), `sonic.transient` ->
  `visual.strike` (response.onset above 0.75, stretched), `notes.noteOn` -> `response.note`.
- The level range of the three projects is the Sonic Abstract scenes' (`sonic.response` floor -44 dB, range 42 dB):
  a mastered mix reads about 0.8, a take 15 dB down about 0.45.

**`response.levelSlow`** (new, declared after every older id): the level over about 3 s, Attack and Release
multiplied -- the tier that decides when a rest becomes the Sonic garden's waiting world.

The offline projects (`digital-mosh.json`, `digital-mosh-trench.json`, `rift.json`) keep their `audio.*` routes: a
file is not played, and their renders do not move.

## Consequences

- Every live scene answers the panel in the expected direction: Sensitivity and Sustain the levels and the arc,
  Transients the hits, Attack and Release the envelopes. At the neutral controls each looks as it did on Trench
  (measured: Digital Mosh's arc energy r = 0.999 with its stages within 2-5 s, identical on Feline Footwear; THE
  RIFT's stage sequence identical; the Sonic garden's dynamics channels within 1 %).
- A live input well below a mastered mix now reads quieter at neutral where the auto-gained bands hid it: THE RIFT
  stays in its early stages, Digital Mosh's arc is calmer. That is the response model's contract ("a quiet passage
  looks quiet"); the performer raises Sensitivity, which restores the full arc for a take 15 dB down.
- Digital Mosh and THE RIFT keep their own SENSITIVITY macro (CC 1): it trims how far the arc follows; the Response
  Sensitivity decides what is heard at all.
- Sonic Live strikes on the strongest onsets (89 on Trench where `sonic.transient` fired 31, 26 of them in the
  intro), so a dense mix strikes through, not only its sparse intro.
