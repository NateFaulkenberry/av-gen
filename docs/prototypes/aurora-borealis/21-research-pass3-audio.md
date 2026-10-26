# Aurora Borealis 21: Sabertooth audio, synchronization and per-instrument events (Research Agent A, pass 3)

Brief: `20-brief-sabertooth.md` (Subagent A) and `20b-brief-river-light.md`. Branch `scene/aurora-borealis`. This is
research only: no production code changed. Agent B's `22-research-pass3-scene.md` did not exist when this was written,
so the scene-side targets below have descriptive names. Bind them in one pass once B's names exist.

**Revision 2 (2026-10-24, 19:35): the re-bounced set.** The first version of this report found that seven stems ran 3%
slower than the mix. The owner then re-bounced Cloud Diving, Kick, Moog, Snare, Toms and tone block (19:25-19:30) and
BASS (19:31), at 83 BPM with 3% Varispeed, the same as the mix. Everything below is re-measured on those files. §10
keeps the first version's finding as history. The 49,440 Hz relabel copies it proposed were never put in the
repository, and the scratch copies are deleted.

- **Binary:** `build/release/src/avgen` in this worktree. Every run here is CPU-only (`--sonic-trace`, no GPU), so
  `tools/gpu-lock.sh` was not needed.
- **Evidence:** `~/Desktop/av-gen-review/aurora-borealis/pass3-research-a/`:
  - **Probe generators:** `make_probe.py` and `make_probe2.py`. Their projects are in `proj/`.
  - **Analysis scripts:** `wavio.py` (a numpy WAV reader), `tr.py` (trace and truth helpers) and the per-question
    scripts listed in §9.
  - **Outputs:** `*-out.txt` beside the scripts, and the traces in `trace/`.
  - **Event lists:** `events-<instrument>.txt`.
  - **Plots:** `01-overview-rebounce.png` through `09-event-raster.png`.
  - **Checksums:** `sha256-rebounce.txt` for the ten files.
  - **The first version:** its outputs, plots and traces are kept in `v1/`.
- **Conditions:** the Mac was on battery, not AC (`pmset`: "Battery Power"). No number here depends on it: they are
  offline traces and audio analysis, with no GPU or live timing.

## 0. Summary

1. **The re-bounced set is in sync.**
   - **All ten files** are 48 kHz 24-bit stereo, 3,368,815 frames (70.1836 s), 25 bars at 85.49 BPM.
   - **The lag is constant and the drift is zero.** Cross-correlated against the mix at sample level in ten 7 s
     windows, every stem with something to lock to sits at **0 samples** in every window (Kick ±1). BASS sits at +1 to
     +3 samples (≤ 0.06 ms).
   - **Cloud Diving is a pad,** with no sample-level lock. Its envelope correlates best within 20 ms of 0 (r 0.49
     either way).
   - **Through the engine:** kick drift is 0 (-4.5 ms in the first half, -4.0 in the second, against the mix's own
     transient). The Kick-fed beat clock reads 85.51 BPM, with pulses +4.7 / +4.9 ms after the grid in the two halves.
2. **⚠ `Moog.wav` is the bass part. The Moog part is missing.**
   - **The evidence:** the new `Moog.wav` (19:29:58) and `BASS.wav` (19:31:49) carry the same audio, differing by at
     most 0.0006. Peak, RMS, centroid, onsets and side/mid all match.
   - **What they contain:** both rise +29 dB at the old bass note onsets, and only +14 dB at the old Moog notes. The
     overview plot shows the bass riff in both rows.
   - **The old Moog file is gone:** the Desktop original was replaced, so my copy was too. Only the v1 analysis of it
     survives (`v1/`).
   - **What it blocks:** every left-mountain mapping, until the owner re-bounces the Moog track (§8 Q1). It also makes
     the stem-set check fit a duplicate (see 3).
3. **The engine's stem-set check (ADR-1632) is now meaningful.**
   - **The residual is 7.2 dB below the mix** (was 0.0 dB), and Kick, Snare, Toms and BASS pass with no warning.
   - **The remaining warnings are expected:** Crash 0.05, HH 0.02, Cloud Diving 0.07 and tone block 0.14 "do not
     match". They are quiet, processed or pad parts, and my windowed cross-correlation shows them at lag 0.
   - **The duplicated Moog reads "8.2 ms later (0.35; -0.99 at its offset)".** That is an artefact of fitting two
     identical stems. Without it, the check reads the same 7.2 dB with no Moog warning.
4. **Read every stem by key, `stem.<key>.onset`, with no classifier** (unchanged from v1, re-measured).
   - On Kick, the kick class is **+33 ms late** (p90 +42).
   - On Snare, it adds 49 hats and 7 kicks.
   - On Toms, it adds 21 hats and 9 kicks.
   - **Proposed roles:** `drums` for Kick only, `synth` for Cloud Diving, `bass` for BASS, `other` for the rest.
   - **No limit on stem count:** nine stems analyse in 225 ms with no drums role, 767 ms with one and 905 ms with five.
5. **Events on the re-bounced files** (mix clock, 240 fps trace; full detail in §3):

   | Instrument | Events | Detector | Quality | Change since v1 |
   |---|---|---|---|---|
   | **Crash** | **12** | onset with a refractory of 100 ms or more | 12/12, 0 false | unchanged (same file) |
   | Kick | 121 | onset | 121/121 | adds the downbeat at t = 0, published at 0.033 s |
   | Snare | 48 | onset with a 100 ms refractory | 48/48 | **the raw onset double-fires on 40 of 48** (was 25) |
   | Toms | 28 note starts | onset with a 100 ms refractory | | the raw onset now fires 38 (10 doubles); starts within 9 ms of v1 |
   | Tone block | 176 accents | onset | | identical to v1 |
   | Cloud Diving | 15 swells | rms envelope | | **2x louder (use gain 4); a reverb tail instead of a hard cut; 22 onsets now fire at swell starts** |
   | Moog | — | — | — | **not measurable: no Moog audio** |

6. **Kick and snare still always land together:** 47 of 48 snares fall within 20 ms of a kick, and all 12 crashes do.
7. **Timing, offline:**
   - **Onsets publish 1-9 ms early.** The analysis frame is time-stamped at its window centre.
   - **At 30 fps,** a transient shows on a frame -11 to +29 ms from the true onset (median +5 to +14 ms).
   - **What adds lag:** stacked smoothing, the classifier (+33 to +42 ms) and `macro.*` (+1 frame). Nothing else on the
     proposed paths does.
   - **Live,** with separate channels, the onset path is 58 ms p50 / 82 ms p95 to light (PROGRESS). The class paths
     are 87-111 ms.

## 1. Inventory and synchronization

### 1.1 The ten files (`inventory2-out.txt`, `sha256-rebounce.txt`)

All are 48 kHz, 24-bit PCM, stereo, 3,368,815 frames (70.1836 s). The bext origin is Logic Pro, the TimeReference
is 178,213,012 on all ten, and every LIST chunk reads "Tempo: 83.0". The playing speed is 85.49 BPM (83 × 1.03, the
Varispeed). The casing matches the brief, and nothing is missing by name.

| File | Bounced | Peak / RMS dBFS | First / last sound (> -60 dBFS) | Side/mid | Note |
|---|---|---|---|---|---|
| `Sabertooth.wav` (mix) | 18:21:05 | -0.30 / -11.70 | 0.00 / 70.18 (cut, not faded) | -12.1 dB | unchanged |
| `Crash.wav` | 18:27:03 | -4.38 / -31.59 | 2.81 / 62.16 | -1.4 | unchanged |
| `HH.wav` | 18:25:02 | -4.47 / -33.16 | 2.81 / 70.09 | -9.9 | unchanged |
| `Kick.wav` | 19:27:44 | -0.39 / **-15.58** | 0.00 / 69.63 | -78.7 | 7.6 dB louder than the old bounce |
| `Snare.wav` | 19:28:25 | -1.28 / -24.04 | 3.51 / 69.98 | -69.0 | |
| `Toms.wav` | 19:28:50 | -3.51 / -29.30 | 1.05 / 70.18 | -5.5 | |
| `tone block.wav` | 19:25:24 | -1.49 / -28.60 | 2.65 / 69.44 | -6.9 | now stereo |
| `Cloud Diving.wav` | 19:30:19 | -0.63 / **-18.41** | 2.81 / 69.70 | -2.6 | 5.8 dB louder; reverb tails |
| `BASS.wav` | 19:31:49 | -0.40 / -13.35 | 2.29 / 70.18 | -37.5 | |
| **`Moog.wav`** | 19:29:58 | -0.40 / -13.35 | 2.29 / 70.18 | -37.5 | **the same audio as BASS (§0.2)** |

- **Tempo** (onset autocorrelation over 8 beats, `tempo2-out.txt`): the mix, Kick, Snare, HH and tone block are
  701.9 ms per beat (85.49 BPM), BASS 701.8 and Toms 702.1. Each comes to 25.00 bars.
- **Pitch against the mix** (`pitch2-out.txt`): Cloud Diving is +0.000 semitones (it was +0.509 before the re-bounce).

### 1.2 Alignment

**Windowed sample-level cross-correlation** (`winlag2.py` → `winlag2-out.txt`: no resampling, ±24,000 samples searched,
ten 7 s windows):

| Stem | Lag in every window, samples (correlation) |
|---|---|
| Kick | **0 or ±1** (0.51-0.61) |
| Snare | **0** (0.12-0.16) |
| Toms | **0** in every window where it plays (0.05-0.12) |
| tone block | **0** (0.03-0.08) |
| HH | **0** (0.02-0.03) |
| Crash | **0** in each window with a hit (0.05-0.08) |
| BASS (= `Moog.wav`) | **+3, +3, +3, +2, +2, +2, +1, +2, +2, +2** (0.60-0.75): ≤ 0.06 ms, 2 samples over the song |
| Cloud Diving | no stable sample lag (a pad, 0.03-0.08). On its 300-3,000 Hz log envelope it reads +20 ms, r 0.492, and 0.492 at 0 (`cloudlag2-out.txt`): flat, aligned within the envelope's 10 ms resolution |

- **Event level** (`kickevt2-out.txt`, the clean stem's transients against the mix's own transient in the band):
  - kick -4.2 ms median (-4.5 first half, -4.0 second half);
  - snare -0.7 ms.
- **Through the engine** (probe `probe2`, `"feeds": {"beat": "stem.kick"}`):
  - "the beat clock follows stem 'kick': 85.51 bpm";
  - pulses +4.8 ms after the 85.49 grid (p10/p90 +3.1/+6.5), +4.7 in the first half and +4.9 in the second;
  - the nearest kick lands 5.0 ms before each pulse.
- **The mix's own beat grid still locks at 114.44 BPM** (4/3 of the tempo): 106 of its 134 pulses are more than 50 ms
  from a kick. Feed the beat from the Kick stem.

### 1.3 The engine's stem-set check (`trace/asis-roles.log`, `trace/nomoog.log`)

```
stems: 9 of 9 sum to the mix with a residual 7.2 dB below it
stem 'Crash' ... does not match the mix within +-500 ms (correlation 0.05)
stem 'HH' ... (0.02)   stem 'Cloud Diving' ... (0.07)   stem 'Tone Block' ... (0.14)
stem 'Moog' (Moog.wav): it lines up with the mix 8.2 ms later ... (correlation 0.35 there, -0.99 at its offset)
```

- **Kick, Snare, Toms and BASS** give no warning.
- **Without the duplicated Moog** (probe `nomoog`): the same 7.2 dB residual, and only the four expected warnings.
- **The four "does not match" warnings are uninformative here.** Each of those stems sits at lag 0 in every window.
  - The check normalises each stem against what the other stems leave of the mix.
  - At a 7.2 dB residual (Feline's was 42.5), the mix's processing leaves too much for quiet or pad stems to stand
    out.
  - Expect these four standing warnings in any Sabertooth project.
- **Why the -0.99:** two identical stems make the least-squares fit put opposite gains on them. Once a real Moog
  bounce replaces the file, this warning should go.

## 2. Loading through the existing workflow

### 2.1 Where the files are

- **The copy:** the ten files are byte-for-byte copies of `~/Desktop/sabertooth/`, re-copied after the re-bounce and
  checked with `cmp` and SHA-256 (10/10 OK). They are in the owner's main checkout:
  `/Users/natefaulkenberry/Documents/GitHub/av-gen/assets/audio/stems/sabertooth/<exact name>`.
- **Ignored:** `.gitignore` covers `assets/audio/stems/*.wav`, but not a subfolder. So `/assets/audio/stems/sabertooth/`
  is in the shared `.git/info/exclude`, with a comment; it applies to every worktree and commits nothing.
  - `git check-ignore` names that rule, and `git status` is clean in both checkouts.
- **This worktree links to the copies** with per-file symlinks at `assets/audio/stems/sabertooth/*.wav` (all ten
  resolve). `tools/link-worktree-assets.sh` will link them in any new worktree.
- **The 49,440 Hz relabel copies from v1 were never created in the repository,** and the scratch copies are deleted.

### 2.2 Proposed project setup (a Sabertooth twin beside the Feline projects)

Paths are relative to `examples/aurora-borealis/`, following `music.py`'s `apply()`. Every file is used as delivered.

```json
"assets": {
  "audio": "../../assets/audio/stems/sabertooth/Sabertooth.wav",
  "stems": [
    {"key": "kick",      "label": "Kick",         "role": "drums", "file": "../../assets/audio/stems/sabertooth/Kick.wav"},
    {"key": "snare",     "label": "Snare",        "role": "other", "file": "../../assets/audio/stems/sabertooth/Snare.wav"},
    {"key": "toms",      "label": "Toms",         "role": "other", "file": "../../assets/audio/stems/sabertooth/Toms.wav"},
    {"key": "crash",     "label": "Crash",        "role": "other", "file": "../../assets/audio/stems/sabertooth/Crash.wav"},
    {"key": "hh",        "label": "HH",           "role": "other", "file": "../../assets/audio/stems/sabertooth/HH.wav"},
    {"key": "bass",      "label": "Bass",         "role": "bass",  "file": "../../assets/audio/stems/sabertooth/BASS.wav"},
    {"key": "cloud",     "label": "Cloud Diving", "role": "synth", "file": "../../assets/audio/stems/sabertooth/Cloud Diving.wav"},
    {"key": "moog",      "label": "Moog",         "role": "other", "file": "../../assets/audio/stems/sabertooth/Moog.wav"},
    {"key": "toneblock", "label": "Tone Block",   "role": "other", "file": "../../assets/audio/stems/sabertooth/tone block.wav"}
  ]
},
"feeds": {"beat": "stem.kick"}
```

**Why this setup:**
- **Routes read `stem.<key>.*`, not `role.*`.**
  - Five roles cannot tell ten instruments apart: `role.other` is just the first `other` stem.
  - Live channels keyed the same way publish the same names (ADR-1521).
- **Kick has the drums role,** so `role.drums.kick` exists and the existing drum signals still resolve. The proposed
  signals never use the class.
- **`feeds.beat` comes from the Kick stem,** because the mix's own grid locks at the wrong tempo (§1.2).
- **Keep the scene's existing `sonic` block** for the mix-only fallback.
- **Until the Moog is re-bounced,** leave the `moog` entry in. It loads, and its signals are wired, but it plays the
  bass. Or drop it, to silence the -0.99 warning.

### 2.3 What a non-drums stem publishes

`stem.<key>.`:
- `active`, `rms`, `peak`, `level`;
- `onset` (an event), `onsetStrength`;
- the bands `bass`, `lowMid`, `mid`, `highMid`, `treble`, plus `centroid` and `width`;
- the onset register `onsetLow`, `onsetMid`, `onsetHigh`.

It never publishes kick, snare or hat (ADR-1630).

**The onset's own rules** (`StemOnsetConfig`; not settable per project):
- log-spectral flux below 16 kHz;
- an adaptive threshold: the median of the last 11 hops × 1.5, + 0.002;
- a rising edge;
- a fixed 60 ms minimum interval. Snare, toms and crash decays re-fire at that interval, so every transient signal
  needs its own refractory in the chain.

### 2.4 The classifier on single-instrument stems (probe `relabel-drums`, which is now every file as delivered with five drums stems; `classes-out.txt`)

| Stem | Onsets | kick class | snare class | hat class |
|---|---|---|---|---|
| Kick | 121 | 120, **+33 ms** after the onset (p90 +42) | 0 | 0 |
| Snare | 88 | 7 (wrong) | 47, **+42 ms** | 49 (wrong) |
| Toms | 38 | 9 (wrong) | 0 | 21 (wrong) |
| Crash, HH (unchanged files; v1) | 13 / 375 | 0 / 0 | 10 / 61 (wrong) | 25 / 396 |

**Verdict:** unnecessary on every stem, and late and wrong on most of them.

## 3. Per-instrument events, on the re-bounced audio

**Method:**
- `--sonic-trace` at 240 fps, so each 10.67 ms hop gets its own frame.
- Events were read through `signal.*` envelopes (attack 0, a fall of 1,000 a second).
- The truth is a sample-level rise detector on the audio (`tr.py`, `events_all.py`), checked against the plots.
- All times are on `Sabertooth.wav`'s clock. Lists: `events-*.txt`.

### 3.1 Crash: 12 hits (the same file as v1; `02-crash-hits.png`, `analyse2-out.txt`)

| # | s | Bar.beat (85.49 BPM, from t = 0) | Peak | Context |
|---|---|---|---|---|
| 1 | **2.808** | 2.1 | 0.26 | the full band enters |
| 2 | **23.158** | 9.2 | 0.42 | phrase start, after silence |
| 3 | **24.567** | 9.4 | 0.18 | re-hit 1.41 s later, on the decay |
| 4 | **25.262** | 10.1 | 0.28 | re-hit 0.70 s later |
| 5 | **35.788** | 13.3 | 0.41 | phrase start |
| 6 | **36.504** | 14.0 | 0.18 | re-hit 0.72 s later |
| 7 | **45.625** | 17.1 | 0.17 | phrase start (quiet) |
| 8 | **47.021** | 17.3 | 0.42 | re-hit 1.40 s later |
| 9 | **47.725** | 18.0 | 0.26 | re-hit 0.70 s later |
| 10 | **56.846** | 21.1 | 0.42 | phrase start |
| 11 | **58.254** | 21.3 | 0.18 | re-hit 1.41 s later |
| 12 | **58.958** | 22.0 | 0.28 | re-hit 0.70 s later |

- **The raw onset fires 13:** one double at 25.325, 63 ms after a hit.
- **With a refractory of 100, 200, 300 or 500 ms** it fires exactly these 12, with 0 false fires and a median offset
  of +0.5 ms against the audio.
- **Recommended refractory: 300 ms.** The shortest real gap is 0.70 s.
- **Do not gate by `onsetStrength` ≥ 4:** that loses 3 of the 12.
- **Strength:** `stem.crash.peak` on the onset frame is 86% of the hit's 50 ms peak. Phrase starts reach 0.41-0.42,
  re-hits 0.17-0.28.

### 3.2 Kick and Snare (`events_all-out.txt`, `size-out.txt`)

- **Kick:** 121 hits (98 on the beat, 20 on the "a" 16th before a beat, and the rest on the "e").
  - **The onset:** 121/121, 0 doubles. The median offset is -9.1 ms against the stem's -40 dB crossing, and -4.2 ms
    against the mix's own transient (§1.2).
  - **The downbeat at t = 0** is published at 0.033 s, because the detector has no history on the file's first hop.
    The old bounce missed it altogether.
  - **Peak:** 0.92-0.96, all the same velocity.
- **Snare:** 48 hits, all on beats 2 and 4.
  - **The raw onset fires 88: 40 doubles** at 62-75 ms (the re-bounced snare rings longer).
  - **With a refractory of 100 or 150 ms** it fires 48/48, at -5.6 ms.
  - **Peak:** 0.76-0.84, a spread of only 1.1x (heavily compressed). A size depth adds little.
- **Coincidence:** 47 of 48 snares fall within 20 ms of a kick (`coincide-out.txt`).

### 3.3 Toms: six fills of buzz-roll notes (`03-toms.png`)

- **The 28 note starts** (`stem.toms.onset` with a 100 ms refractory; the raw onset fires 38, the extra 10 being
  doubles under 100 ms). Every start is within 9 ms of v1:

  | Fill | Note starts (s) |
  |---|---|
  | 1 | 1.05, 1.23, 1.57, 1.92, 2.45, 2.63 |
  | 2 | 24.03, 24.39, 24.56, 24.91 |
  | 3 | 35.25, 35.61, 35.79, 36.14, 36.31 |
  | 4 | 41.75, 41.93 |
  | 5 | 45.97, 46.15, 46.50, 46.84, 47.36, 47.54 |
  | 6 | 68.95, 69.30, 69.48, 69.82, 70.01 |

- **Peak:** 0.15-0.41, a spread of 1.9x, carried on the onset frame. Each fill's last one or two notes are the loudest
  (0.37-0.41). The first fill climbs from 0.15 to 0.39.
- **The rms-rise gate keeps only 10** of the 28, so it is not wanted.
- **For a body glow,** use `stem.toms.level`.

### 3.4 Tone block: 176 accents (`05-toneblock.png`)

- **The same 176 accents as v1** (all within 30 ms). The pattern is syncopated 16ths, never on the beat, about 7 a
  bar.
- **The onset catches every accent and none of the 258 ghost notes,** at -6.0 ms.
- **Peak:** 0.34-0.55, a 1.6x spread (2.2x before the re-bounce).
- **The rests:** silent before 2.65 s, at 23.6-25.4 s and at 45.0-48.0 s. These are where the tom fills are, so the
  two right-side instruments trade bars.
- **They rarely coincide:** 16 of 176 tone-block accents and 9 of 28 tom notes fall on a kick.

### 3.5 Moog: not measurable on this set

`Moog.wav` contains the bass part (§0.2; `04-moog-file-is-bass.png`). On it, rise A fires 114 times and the onset
222 times: the bass riff.

**From the old Moog bounce** (v1, 83 BPM; `v1/`), the articulation is known:
- about 24 played notes, each a plucked attack with 8th-note echo repeats (0.35 s, about -5 dB each);
- legato steps from about 51 s;
- the raw onset is unusable there, at 321 fires.

**The detector for the real Moog:**
- **`stem.moog.rms` trend 0.25 s / 15 ms, floor 0.003, ×1.6, latched 150 ms ("rise A").** On v1 it caught all 24 notes
  plus 2 extras (14.68 and 35.13 s, both on echo steps right after a note).
- **×2.2 ("rise C")** was fully precise, but missed 2 legato notes.
- **Decision: the catch-all ×1.6.** The extras are an echo re-lighting the same glint, not a visibly wrong event.
- **Re-check it on the re-bounced Moog:** the new bounces run 6-12 dB hotter, but the trend ratio is level-free.

**The sustain:** `stem.moog.level`, 20 ms attack / 600 ms decay.

### 3.6 Cloud Diving: 15 swells (`08-cloud-diving-envelopes.png`, `cloud-out.txt`)

- **The re-bounce changed this stem the most.**
  - **Level:** RMS p50 0.11, p95 0.19 and max 0.24, about 2x louder. So **use gain 4** (gain 8 or 10 would clip the
    plateaus).
  - **Shape:** each swell now ends in a reverb tail of about 2 s instead of a hard cut.
  - **Onsets:** `stem.cloud.onset` now fires 22 times, at the swell starts. Pass 1 found that onset events are wrong
    for swells, and they are not used.
- **The swells** (start - end at rms × 4 > 0.03; peak):

  | Start - end (s) | Peak | Start - end (s) | Peak |
  |---|---|---|---|
  | 2.87-6.20 | 0.88 | 36.57-40.03 | 0.98 |
  | 8.50-11.93 | 0.71 | 42.13-45.63 | 0.88 |
  | 14.10-17.50 | 0.82 | 47.77-53.93 | 0.90 |
  | 19.70-23.27 | 0.92 | 54.10-55.93 | 0.71 |
  | 25.33-28.63 | 0.97 | 58.97-61.03 | 0.79 |
  | 30.90-34.33 | 0.85 | stabs 63.2, 64.1, 65.3, 67.7 | 0.34-0.69 |

  The rise to 90% takes 0.8-2.6 s.
- **Smoothing** (per-frame change at 30 fps inside the swells, after their first second; lag to 50% of each swell's
  peak):

  | `stem.cloud.rms` × 4, attack / decay ms | Jitter p50 / p99 | Lag | Level left 0.5 s after the end |
  |---|---|---|---|
  | raw | 0.033 / 0.215 | 0 | 0.01 |
  | 80 / 400 | 0.011 / 0.056 | 33 ms | 0.09 |
  | 150 / 600 | 0.008 / 0.036 | 33 ms | 0.19 |
  | **300 / 1200** | **0.005 / 0.022** | **100 ms** | **0.43** |
  | 600 / 2000 | 0.004 / 0.016 | 133 ms | 0.60 |
  | **1500 / 4000** | **0.003 / 0.009** | (slow by design) | 0.78 |

- **Recommendation:**
  - **Intensity, width and folds: 300 / 1200.**
  - **Hue: 1500 / 4000.** Its p99 is 0.009, inside pass 2's palette bar (step p99 < 0.02).
  - The same choices as v1. Only the gain changed.

### 3.7 Bass and HH, for the later decision (`events_all-out.txt`, `v1/hh-bass-out.txt`)

- **HH** (unchanged file):
  - 374 closed hats, steady 16ths at 5.3 a second. Per hit, this is a flash stream: do not use it.
  - 29 open hats, once a bar at 1.75 beats in. They are the one legible pattern, a candidate for a once-a-bar
    micro-glint, with a sustained-energy detector I have not built.
- **Bass** (re-bounced):
  - 312 note rises (4.45 a second, 16th riffs in 2-bar phrases), now with a reverb tail.
  - The onset with a 100 ms refractory catches 210 of them (precise 0.98), at -8.6 ms.
  - It rests at the phrase ends.
  - **The candidate:** a slow `stem.bass.rms` envelope (150/800 ms) for river flow energy, at gain about 3 (rms p95
    about 0.25).

## 4. Timing

### 4.1 Granularity and where an onset lands

- **The analysis hop:** 512 samples at 48 kHz (10.67 ms), with a 2,048-sample window (42.7 ms), time-stamped at the
  window centre.
- **Publishing (ADR-1520):** each frame takes the newest analysis frame at or before the playhead, and ORs the events
  since the last frame.
- **The onset leads the stem's true transient by:**

  | Kick | Snare | Crash | HH | Tone block | Bass |
  |---|---|---|---|---|---|
  | -9.1 ms (-4.2 against the mix's transient) | -5.6 | +0.5 | -1.3 | -6.0 | -8.6 |

- **At 30 fps** (`frame30-out.txt`), the frame that shows the event minus the true transient is:

  | | p10 | p50 | p90 | max |
  |---|---|---|---|---|
  | Kick | -8.6 | +5.4 | +20.0 | +26.0 ms |
  | Snare | -6.2 | +8.3 | +22.4 | +27.4 ms |
  | Crash | +5.1 | +10.4 | +24.8 | +29.3 ms |
  | HH | -0.6 | +14.0 | +26.7 | +33.4 ms |

  The kick's minimum of -28 ms is the t = 0 hit. This window is the frame quantisation less the window lead. A 30 fps
  offline render cannot do better.
- **Over the full 70 s nothing drifts:**
  - every windowed lag is 0 (BASS +1 to +3 samples);
  - kick event offsets are -4.5 / -4.0 ms in the two halves;
  - beat pulses are +4.7 / +4.9 ms.

### 4.2 What delays a visual attack (the proposed paths avoid all of it)

| Stage | Cost | Use |
|---|---|---|
| `stem.<key>.onset` → `signal.*` with attack 0 | **same frame** (ADR-1602) | every transient |
| envelope refractory / re-arm (ADR-1574) | none (it blocks re-triggers only) | every transient |
| rms trend detector (Moog, rise A) | v1: -4.8 ms against the note's true attack | Moog notes |
| the classifier | **+33 to +42 ms** offline, +30 to +44 ms live | none |
| attack > 0 on an event | + the attack, to full value | only designed swells |
| `macro.*` | +1 frame | none |
| the beat grid (Kick-fed) | +4.8 ms after the grid; the kick is 5 ms earlier | counting and indexing only |

### 4.3 Live, when the stems become separate channels

- **The accent paths:** each channel keyed as in §2.2 publishes the same `stem.<key>.*` (ADR-1521). So each accent
  rides the onset path: **58.2 / 81.6 ms p50 / p95 to light** on this scene (PROGRESS). The class paths would be 87-111
  ms.
- **Ten sources** cost about 0.26 ms per 10.7 ms hop on the analysis thread (about 2.5% of a core).
- **The device:** it needs 10 or more input channels (or BlackHole 16ch) on one clock.
- **The live beat tracker** is no faster than the onset path (pass 2), so the grid is for counting only.
- **Offline and live differ by a constant:** offline the light shows -10 to +30 ms from the sound; live it is 58 ms
  p50.

### 4.4 Rules for the bindings

- **Every transient:**
  - `stem.<key>.onset` (or the Moog rise) → a latching envelope with **attack 0**, its own refractory and a designed
    decay;
  - size from `peak`, held from the onset frame;
  - no route chain.
- **Smoothing only where designed:** the Cloud Diving intensity, the hue and the Moog sustain.
- **No classes, no `macro.*`, and no `.level` as an accent depth.**

## 5. Proposed mapping

### 5.1 The table

**Notes on the table:**
- Times are in ms. *Release* is the envelope's fall to 0. *Refr* is the latch's refractory.
- Signal names are proposals in the existing `signal.*` style. Targets are descriptive, for B to bind.
- Every input carries its stem's isolation depth (a control channel resting at 1, ADR-1612).
- **Size depths are re-scaled to the re-bounced peaks.**

| File | Key / role | Signal (definition) | Visual role (brief) | Attack | Release | Refr | Cap / bound | Measured |
|---|---|---|---|---|---|---|---|---|
| `Cloud Diving.wav` | `cloud` / synth | `cloudSwell` = `stem.cloud.rms` {**gain 4**, clamp, attack 300, decay 1200} | aurora intensity, curtain width / height, fold amplitude, inner detail, persistence | 300 | 1200 | none | adds to a non-zero baseline; within the pass-2 flash budget | 15 swells, peaks 0.71-0.98; p99 step 0.022 |
| | | `cloudHue` = `stem.cloud.rms` {gain 4, clamp, attack 1500, decay 4000} | dominant hue / palette blend | 1500 | 4000 | none | a blend, never a jump | p99 step 0.009 |
| `Crash.wav` | `crash` / other | `crashStar` = `stem.crash.onset` {linearfall, attack 0, **refr 300**, rearm 0.05}; size `stem.crash.peak` {attack 0, decay 300} | shooting star + environment flash | 0 | the star's own timeline (≈ 0.5-0.7 s); wash ≈ 400 | 300 | **2 stars alive at most**; a new crash takes the oldest slot | **12 hits** (§3.1), 0 false |
| `Moog.wav` (**needs the real Moog bounce**) | `moog` / other | `moogNote` = `stem.moog.rms` {trend 0.25 s / 15 ms, floor 0.003, binary **1.6**} → {linearfall, attack 0, refr 150} | LEFT mountains: a glint burst on a deterministic facet subset per note | 0 | 300-500 | 150 | one burst per note | v1: 24/24 notes + 2 echo extras, -4.8 ms |
| | | `moogGlow` = `stem.moog.level` {attack 20, decay 600} | LEFT: a gentle sustain glow while the note rings | 20 | 600 | none | ≤ 0.3 of the glint peak | |
| `Kick.wav` | `kick` / **drums** | `kickBeam` = `stem.kick.onset` {linearfall, attack 0, **refr 100**} | river beam launch (hot colour) + ripple | 0 | the beam's travel (≈ one beat, 0.70 s) | 100 | pool of 2-3 (16th pickups 0.18 s before a beat); summed glow clamped | 121/121 |
| `Snare.wav` | `snare` / other | `snareBeam` = `stem.snare.onset` {linearfall, attack 0, **refr 100**} | river beam launch (second colour, shorter and sharper) + ripple | 0 | shorter (≈ 0.4-0.5 s) | 100 (**required**: 40 doubles otherwise) | pool 1-2; launches with a kick on 47 of 48 | 48/48 |
| `Toms.wav` | `toms` / other | `tomHit` = `stem.toms.onset` {linearfall, attack 0, **refr 100**}, depth `stem.toms.peak` {attack 0, decay 300} remapped 0.15-0.41 → 0.4-1.3 | RIGHT: substantial accents; a fill's last notes the biggest | 0 | 400-600 | 100 | one rotating subset per note | 28 notes, 6 fills |
| | | `tomBody` = `stem.toms.level` {attack 30, decay 500} | RIGHT: a broader glow through a fill | 30 | 500 | none | | |
| `tone block.wav` | `toneblock` / other | `blockTick` = `stem.toneblock.onset` {linearfall, attack 0, refr 80}, depth `stem.toneblock.peak` {attack 0, decay 200} remapped 0.34-0.55 → 0.5-1.0 | RIGHT: small sharp glints stepping through a facet sequence | 0 | 120-200 | 80 | small area | 176 accents, 0 ghosts |
| `BASS.wav` | `bass` / bass | (reserved) `bassFlow` = `stem.bass.rms` {gain 3, clamp, attack 150, decay 800} | optional: river flow energy | 150 | 800 | | not the aurora's colour | 312 notes; onset 210 |
| `HH.wav` | `hh` / other | (reserved) open-hat glint (not built) | optional: once-a-bar micro-glint | 0 | 150 | 500 | small area | 29 open hats |
| `Sabertooth.wav` | (mix) | plays; feeds the existing `sonic` / `response` fallbacks; `beat.*` from `feeds.beat = stem.kick` | `beat.count` for subset rotation | | | | | 85.51 BPM, +4.8 ms |

### 5.2 The mix-only fallback (v1 `mixonly-out.txt`; the mix is unchanged)

| Mix signal | Against the stems |
|---|---|
| `response.kick` | 0 fires: a mix has no kick (ADR-1620) |
| `response.snare` | recall 0.31 |
| `response.hat` | 0.92 / 0.87 against HH |
| crash | nothing finds the crashes (`response.onset` precision 0.04) |
| beat grid | 114 BPM, wrong |

**Proposal:**
- Make every transient signal stem-only, with no mix input, so a mix-only run fires no stars, beams or glints.
- The aurora keeps the existing mix context.
- A mix stand-in would fire on the wrong instrument.

### 5.3 Overlap rules this music needs

- **Kick and snare launch together on beats 2 and 4.** Give the snare a distinct launch (speed, position or lateral
  offset) inside the summed clamp.
- **Kick density:** 1.72 a second, with pickups 0.18 s before a beat.
  - A beam that lives about a beat keeps 1-2 alive.
  - Retire pass 2's ring-of-two with a 1.8 s life: it would drop most kicks.
- **Crash:** groups of three (+1.41 s, +0.70 s). Two star slots suffice.
- **Right side:** toms and tone block trade bars. Moog on the left overlaps both.

## 6. Cost

- **Load:** 9 stems in 0.77 s with Kick as drums (0.23 s with none). The timbre pass is 1.1 s, and the stem-set
  check 80-93 ms.
- **Memory:** about 1.2 MB of probe plus 1.7 MB of spectrum rows per 70 s stem.
- **Per frame:** about 12 chains, ns each.
- **Seek:** the replay from 0 on a 70 s song is about 4 times cheaper than Feline's.

## 7. Risks

1. **The Moog part is missing** (§0.2). The left-mountain mappings cannot be tuned until it arrives.
2. **Four standing "does not match" warnings** (Crash, HH, Cloud Diving, tone block) are uninformative. Alignment is
   proven by the windowed lags. A scene test should pin `stem.kick.onset` against the 85.49 BPM grid (≤ 10 ms, no
   slope).
3. **Re-bounces change levels.** These were 6-12 dB hotter, and the snare rings longer (40 doubles without a
   refractory).
   - Every gain and size range in §5.1 is for these files.
   - The trend and onset detectors are level-free; gains and size remaps are not.
4. **The 30 fps window:** offline the light lands -10 to +30 ms from the sound.
5. **The first downbeat** (t = 0) is published at 0.033 s.
6. **Battery:** nothing here was GPU-timed; B's and the implementer's GPU numbers need AC and the 60 Hz LG.

## 8. Open questions for the coordinator / owner

1. **Re-bounce `Moog.wav`.** The 19:29:58 file contains the bass part, the same audio as `BASS.wav`.
2. **HH open-hat micro-glint:** worth building once the primary mappings work?

The Moog detector question from v1 is decided: the catch-all ×1.6, to re-check on the real bounce.

## 9. Reproduce

```sh
R=~/Desktop/av-gen-review/aurora-borealis/pass3-research-a; BIN=<worktree>/build/release/src/avgen; cd $R
python3 inventory.py; python3 tempo.py; python3 pitch.py; python3 overview.py        # §1.1
python3 winlag2.py; python3 kickevt.py                                               # §1.2
python3 make_probe.py <scratch> proj; python3 make_probe2.py proj/probe2.json        # every file as delivered
for p in asis-roles asis-other relabel-drums; do                                     # (relabel-drums = five drums roles)
  $BIN --project proj/$p.json --sonic-trace trace/$p-240.csv --fps 240 > trace/$p.log 2>&1; done
$BIN --project proj/probe2.json --sonic-trace trace/probe2-240.csv --fps 240 > trace/probe2.log 2>&1
$BIN --project proj/probe2.json --sonic-trace trace/probe2-30.csv --fps 30
python3 events_all.py; python3 crash.py; python3 analyse2.py; python3 detail.py
python3 classes.py; python3 beat.py; python3 cloud.py; python3 raster.py
python3 crashplot.py; python3 detailplot.py
```

`size-out.txt`, `coincide-out.txt`, `frame30-out.txt`, `cloudlag2-out.txt` and `nomoog.log` came from inline snippets
of the same `tr.py` helpers; each output states what it measured.

## 10. History: the first delivery (v1, before the re-bounce)

- **Two speeds:** the first delivery had the mix, Crash and HH at 85.49 BPM (70.18 s) and the seven batch stems at
  83.00 BPM (72.29 s).
- **What showed it:**
  - the same 25 bars, a ratio of exactly 1.03;
  - the mix +0.51 semitones above the stems (Varispeed);
  - the engine's kick drifting +30.0 ms/s (+1.96 s by 65 s);
  - the stem-set check reading "does not match" on all nine, at 0.0 dB.
- **The cause:** the seven stems were bounced without the mix's Varispeed.
- **The interim proposal,** a 49,440 Hz header relabel, measured 0 drift but was overtaken by the owner's re-bounce.
- **The data:** v1's outputs, plots and traces are in `v1/`.
