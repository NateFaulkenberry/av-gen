# Rainbow World 14: Feline Footwear, the stems analysed

Brief 13, Phase 2. Branch `art/rainbow-world-2`.

- **Script:** `scripts/stem_analysis.py`, numpy and matplotlib only, since the environment has no scipy or librosa.
  - It writes `analysis.json`, the per-bar tables and the plots.
  - The plots committed here are in `stems/` (JPEG, reduced).
  - Full-size plots and the raw event lists stay in `build/rw2/stems/`.
- **The engine's own view:** `avgen --sonic-trace` now writes `stem.*`, `beat.*` and `env.*` columns (ADR-1571).
  - Run it on `build/rw2/trace/feline-stems.json` (the mix, the three stems, drums with `hits`). Events are traced
    through envelope sources.
  - `build/rw2/trace/events.py` compares those events with the grid.

**How the music was "listened to".** I cannot hear audio. Everything below comes from:
- the waveforms, spectrograms and band envelopes;
- a 16th-note transcription on the measured grid;
- autocorrelation pitch tracking of the bass;
- the engine's own detectors.

Treat the musical descriptions as close readings of those measurements, not as listening notes. Where a reading is an
inference, the text says so.

## 1. Files and validation

| | Drums | Bass | Synth | Mix |
|---|---|---|---|---|
| File | `feline-footwear (Drums).wav` | `(Bass).wav` | `(Synth).wav` | `assets/audio/feline-footwear.wav` |
| Format | PCM 24-bit, 48 kHz, stereo | same | same | same |
| Length | 210.500 s (10,104,000 frames) | same | same | 210.5004 s (+19 frames) |
| Peak / RMS dBFS | -0.22 / -15.2 | -1.55 / -15.3 | -2.13 / -17.7 | -0.10 / -11.1 |
| Crest | 15.0 dB | 13.8 dB | 15.5 dB | 11.0 dB |
| First / last sound (> -60 dBFS) | 0.24 / 182.9 s | 8.19 / 193.5 s | 20.11 / 209.8 s | 0.24 / 209.8 s |
| L/R correlation, side/mid | 0.95, 0.16 | 0.95, 0.16 | 0.83, 0.31 | 0.92, 0.20 |

**Alignment, measured:**
- The three stems sum to the mix with a residual of **-40.4 dB** (gain 1.0007).
- The best lag of the sum against the mix is **0 samples**.
- The stems are sample-aligned with no export padding, so every stem's `offset` is 0.
- The 19 extra frames at the end of the mix are silence.

**Gain:** the drums and bass are about equally loud; the synth is 2.4 dB quieter and has a much narrower dynamic
range (rms p10-p90 -29.9 to -14.4 dB, against the bass's -34 to -10). Raw amplitudes are not compared across stems
anywhere in the mapping: every stem is read through `stem.<key>.level` (ADR-1510's law, against the stem's own
reference) or its own bands.

**Stereo:** the synth is the only wide stem (side/mid 0.31; `stem.synth.width` p50 0.32). Left/right differences
are not used: the drums and bass are near-mono.

## 2. Tempo and grid

**121.000 BPM**, beat 0.4959 s, bar 1.983 s, first downbeat **0.2425 s**.

- The kicks sit on the grid: phase coherence 0.9986, deviation p50 2.7 ms and p95 6.7 ms, every kick within 30 ms.
- **No tempo change.** A constant grid is correct for this track, not imposed on it.
- **Phrases are 4 bars**, starting on bars 1, 5, 9 and so on. This is the bass progression's length (§4).
  - The engine's `beat.phrasePulse` lands on exactly these bars: 0.28, 8.22, 16.15 ... 47.88 s.
- **The engine's beat clock** (offline `trackBeatsOffline` on the mix):
  - 120.81 BPM; its pulses are 20-40 ms after the kicks, which includes one 60 fps trace frame;
  - downbeats on bar 1 (`beat.count % 4 == 0` on the kick at steps 0-1);
  - so `beat.phase`, `beat.bar` and `beat.phrase` can be used as the music's clock.

Bar *n* starts at `0.2425 + (n - 1) x 1.98347` s.

## 3. What each stem is (from the measurements)

### Drums (0.24-182.9 s)

The 16th-step transcription (`stems/drum_layers.csv`) gives a four-on-the-floor house kit:

| Layer | Steps in the bar (0-15) | Where | Evidence |
|---|---|---|---|
| Kick | 0, 4, 8, 12 (every beat) | bars 1-92, no gaps except fills | sub-band rise 25-70 dB on the beat; 95% of the stem's energy is below 150 Hz |
| Offbeat open hat | 2, 6, 10, 14 | bars 2-60, then thinning, gone from bar 61 | high-band rise 30-38 dB on the "and" |
| Ticks or shaker | 3, 7, 11, 15 ("a") | bars 3-92 | mid-band rise about 35 dB, no sub |
| Clap on the backbeat | 4 and 12 (beats 2 and 4, layered on the kick) | bars 15-92, missing in bars 52 and 56 | 2-6 kHz louder on beats 2 and 4 than on 1 and 3 by 4-10 dB |
| 16th hats | 1, 5, 9, 13 ("e") | bars 58-92 | high-band rise on the "e" |

- **Fills and breaks:** bars 52 (101.4 s) and 56 (109.3 s) drop a kick and the clap, the turnarounds into section
  B.
- **Section B's change, bar 57 on (111.3 s):** the offbeat hat gives way to 16th hats, so the top end goes from
  "and"s to a running 16th line. That is the drums' biggest change after the clap enters.
- **The end:** the drums stop dead at bar 93 (182.7 s).

### Bass (8.19-193.5 s)

A short, plucky sub bass (centroid 73 Hz; 75% of its energy below 60 Hz) with a syncopated pattern:
- **Bars 5-24:** `x..x..x..xx...x.`;
- **from bar 25:** `x..x..x...x..x..` (3+3+4+3+3: the tresillo feel), notes decaying within about 300 ms.

Pitch, tracked per 8th note by autocorrelation:

| Bars | Time | Pitch | Reading |
|---|---|---|---|
| 5-20 | 8.2-39.9 s | **F1 pedal** (43.7 Hz) | the intro: one note, the groove is all rhythm |
| 21-83 | 39.9-166.8 s | **F - Ab - Bb - F**, one chord a bar, a 4-bar cycle starting on bars 1 + 4k | with walk-ups (G1 into Ab, C2 back into F) on the last 8th of a bar. This progression is the song's harmony; F minor |
| 84-98 | 164.9-193.5 s | **F1 pedal** again | the progression stops: tension before the end |

Glides and fills (bass onsets on extra 16ths, octave jumps to F2/D#2/G#2/A#2 at the ends of bars 56, 60, 68, 76, 80)
mark the last bar of several phrases.

### Synth (20.1-209.8 s)

Two characters, divided at bar 25.

- **Bars 11-24 (20.1-47.8 s), the intro calls:** short gestures whose pitch falls (the spectrogram shows
  descending glides from about 1.5 kHz). They come at 20.1, 28.0, 31-32, 35-36, 39.6 and 44-45 s, about every 2-4 s,
  separated by silence. Their level swings from -45 to -25 dB. These are the most recognisable synth events in the
  track.
- **Bar 25 on (47.85 s), the drop:** a sustained, chordal, harmonically rich texture (harmonics to 8 kHz) at a steady
  -18 to -16 dB.
  - It is pumped by the kick: its envelope dips at every beat (sidechain). The followed level therefore carries a
    kick ripple, and synth routes must smooth over at least a beat.
  - A low layer joins it below 300 Hz from about 72 s (bar 37), thickening it.
- **Its brightness carries the arrangement** (median centroid per bar, Hz):

  | Bars | Time | Centroid | Reading |
  |---|---|---|---|
  | 25-60 | 47.8-119 s | 550-870 | |
  | 61-76 | 119-151 s | about 720 | |
  | 77-86 | 151-170 s | rising to **1,030** (bar 82, 160.9 s) | the brightest stretch |
  | 88-92 | 172.8-182.7 s | 460-560 | darkening as the pedal returns |
  | 93-97 | 182.7-192.6 s | | a **riser**: a bright sweep 1-10 kHz, 183-190 s, exactly where the drums stop |
  | 99-106 | 194.6-210.5 s | | the synth alone, fading from about 200 s |

The synth stem therefore holds at least three perceptually different elements: the falling calls, the pumped chord
bed, and the riser. The engine's analysis cannot separate elements inside one stem. They are separated in **time**
(they do not overlap), so one set of features (level, onset, centroid, treble) serves all three, each meaning
something different in its section.

## 4. The event map

Bars and times from the grid. The "Visual intent" column is the proposal of `16-effect-mapping.md`.

| Bars | Time (s) | What happens musically | Strength | Visual intent |
|---|---|---|---|---|
| 1-4 | 0.24-8.18 | Drums alone: kick, offbeat hats; ticks from bar 3 | low | Kick rings and hat glints only, on a calm world. The ride's first stretch. |
| 5 | 8.18 | **Bass enters** on an F pedal | medium | The ground starts to swell with the bass: the land comes alive. |
| 5-10 | 8.2-20.1 | Kick + hats + bass pedal | low-medium | Rings, glints, swell. |
| 11 | 20.1 | **First synth call** (falling) | medium | A falling band of light down every arch in view. |
| 11-14 | 20.1-28.0 | Calls about every 4 s, drums, bass | medium | Each call: a falling arch band and a cloud glow. |
| 15 | 28.0 | **Clap enters** on beats 2 and 4 | medium | Clap fronts race up the arches on the backbeat. |
| 15-20 | 28.0-39.9 | Full drums, bass pedal, calls more frequent (31, 35, 39.6 s) | medium-high | Rising density. |
| 21 | 39.9 | **Bass leaves the pedal**: F-Ab-Bb-F begins | medium | Swell deepens. |
| 21-24 | 39.9-47.8 | The build: progression, calls at 44-45 s | high | Phrase-level lift: haze and bloom rise toward the drop. |
| **25** | **47.85** | **The drop**: the synth chord bed enters at full level, new bass pattern; phrase boundary | **peak** | **The world-scale event**: every rainbow ignites, a great ring races out, the sky warms. Then a decay. |
| 25-51 | 47.9-101.4 | Groove A: four-on-the-floor, clap, offbeat hats, ticks, F-Ab-Bb-F, pumped synth bed (low layer from bar 37) | high, steady | Continuous: kick rings, clap fronts, glints, swell, flowing arch bands and cloud light at the synth level. Phrase boundaries (every 4 bars): a phrase flare. |
| 52 | 101.4 | Fill: kick and clap drop out of the bar | dip | The rings skip; a brief hush. |
| 53-55 | 103.4-109.3 | Groove | high | |
| 56 | 109.3 | Fill again; bass octave fill | dip | |
| 57-60 | 111.3-119.3 | 16th hats come in, offbeat hats go | high | The glint field gets denser and finer. |
| 61 | 119.25 | Section B proper (phrase boundary) | high | A phrase flare. |
| 61-83 | 119.3-166.8 | Groove B: 16th hats, clap, progression; the synth brightens from 151 s to its peak at 160.9 s | high, rising | The arch bands brighten and the sky colour rises with the synth's centroid. |
| 84 | 164.9 | **Bass back to the F pedal**; the progression stops | medium | The swell steadies. |
| 84-92 | 164.9-182.7 | Groove over a pedal; the synth darkens (172-182 s) | medium-high | The world dims a little: the synth light falls. |
| **93** | **182.7** | **Drums stop dead**; the riser starts | **peak (inverse)** | Rings and glints stop; a long rising light (the riser) through haze and arches: anticipation. |
| 93-98 | 182.7-194.6 | Bass pedal + riser (183-190 s), the bass stops at 193.5 s | rising | The riser lifts the light toward a release. |
| 99-106 | 194.6-210.5 | The synth alone, fading from about 200 s | falling | The world's light settles, with the arches the last thing glowing. |

**Strongest moments:**
- **47.85 s:** the drop. The biggest payoff, and the only world-scale ignition.
- **182.7 s:** the drums stop and the riser starts. An inverse payoff: the beat-driven systems fall silent, and the
  synth-driven light takes the frame.
- Phrase boundaries every 7.93 s inside the grooves: medium accents.
- 111.3 / 119.25 s: the hat change into section B.

**Quiet stretches that must stay alive:**
- 0-8 s (drums alone);
- the gaps between the intro calls;
- 194-210 s (the synth alone).

The bass swell, the slow cloud drift and the arch flow keep moving through them.

## 5. Features per stem, and what the engine gives

From `--sonic-trace` at 60 fps, with the grid for reference. A 60 fps trace adds up to one frame of delay to every
event.

| Signal | Result on this track | Use |
|---|---|---|
| `stem.drums.onset` (log flux) | 1,120 events, on every layer's step, `|dev|` p50 19 ms | **Reliable.** Hats and ticks as well as kicks, so it needs a gate to mean "kick". |
| `stem.drums.kick` (hits tier, ADR-1060) | 406 events; a third land one 16th (124 ms) late (steps 1, 5, 9, 13); `|dev|` p50 56 ms | **Too late for an impact** on this kit. Not used. |
| `stem.drums.snare` | 235 events, mostly on the clap (steps 4 and 12) but also late | Not used for timing. |
| `stem.drums.hat` | 545 events, on the "and"s and later the "e"s | Usable, but `treble` is better (below). |
| `stem.drums.bass` / `.highMid` / `.treble` at an onset | kick steps: bass 0.34; clap steps: highMid 0.87-0.96; hat steps: treble 0.54-0.81, bass 0.03 | The bands **classify** the onsets: they are the gates. |
| `stem.drums.level` | p10 / p50 / p90 0.39 / 0.73 / 0.96 | Overall drum drive. 0 after 182.7 s. |
| `stem.bass.onset` | 465 events on the bass pattern's steps (0, 3, 6, 10, 13), `|dev|` p50 33 ms | **Reliable**: the bass notes. |
| `stem.bass.level` | 0.43 / 0.68 / 0.84 | Bass body, the swell's depth. |
| `stem.synth.level` | 0.47 / 0.59 / 0.71 (narrow: a pad) | The arch light and cloud light. Pumped by the kick: smooth at least 400 ms. |
| `stem.synth.onset` | 296 events: the calls, chord changes | Arch "call" bands in the intro; sparse accents later. |
| `stem.synth.centroid` | 0.54 / 0.59 / 0.68 | The arrangement's brightness arc: 151-165 s rise, 172-182 s fall, the riser. |
| `beat.phase`, `beat.bar`, `beat.phrasePulse` | 120.8 BPM, downbeats correct, phrase pulses on bars 1 + 4k | **The clock** the impacts travel on (§6). |
| `response.kickEnv`, `snareEnv`, `hatEnv`, `bass`, `sustain`, `level`, `levelSlow` | mix only | The live fallbacks (ADR-1570). |

## 6. Recommendations that follow

1. **Impacts ride the beat clock, gated by the stem's energy.**
   - The kick is on every beat, and the engine's beat clock is locked to it. So a kick ring's radius is a function of
     `beat.phase`: it starts on the beat and is gone before the next.
   - Its brightness is the drum stem's low band, with fast attack and short decay, so a missing kick (bars 52 and
     56, after 182.7 s) draws no ring.
   - The clap is the same on beats 2 and 4, gated by `stem.drums.highMid`.
   - This avoids both the hits tier's lateness and a hat retriggering a kick's ring. It is exact offline, and live it
     is the live tracker's phase with the same gate.
2. **The bass is a body, not a trigger.** Its notes are short plucks on a syncopated pattern, so each note gives the
   land a weighty push (`stem.bass.level` with fast attack and 400-600 ms release). The pattern's syncopation against
   the kick gives the land a groove different from the rings.
3. **The synth is light and colour over long times:**
   - its level drives the arches' flow and the clouds' light, smoothed over a beat or more;
   - its centroid drives the warmth of the sky and haze, smoothed over a bar;
   - its onsets mark the intro calls.
4. **Phrase pulses mark the 4-bar phrases.** The drop at 47.85 s is a phrase pulse plus the synth's jump against
   its adaptive reference (ADR-1510: a loud entry reads at the top for a few seconds). That combination is unique in
   the track, and it is what the world-scale ignition keys on.
5. **Not used:**
   - stereo width;
   - the bass pitch (not on the bus: the engine has no stem pitch);
   - the hits tier's kick and snare timing.

## 7. Limitations found

- **The hits tier is late on this kit:** a third of the kicks land one 16th late. That is ADR-1060's causal
  classifier on an isolated drum stem.
  - Worked around with the beat clock and band gates.
  - Not changed here: the stem analysis belongs to the stems work, and another agent owns the beat clock and stem
    detectors.
- **No pitch per stem.** The F-Ab-Bb-F progression, the clearest harmonic structure, is not available to routes.
  - `beat.phrase` (position in the 4-bar phrase) stands in for it in the grooves, where one chord lasts one bar.
  - It is not true harmony tracking, and it is wrong in the pedal sections.
- **One analysis per stem:** the synth's calls, bed and riser cannot be told apart by a feature, only by when they
  happen.
- **The beat clock is the mix's** (stems do not feed it): about 20-40 ms behind the kicks offline. That is within a
  frame at 30 fps.
