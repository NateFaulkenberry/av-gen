# Aurora Borealis 21: Sabertooth audio, synchronization and per-instrument events (Research Agent A, pass 3)

Brief: `20-brief-sabertooth.md` (Subagent A) and `20b-brief-river-light.md`. Branch `scene/aurora-borealis` at
5ac8ea63. This is research only: no production code changed. Agent B's `22-research-pass3-scene.md` did not exist when
this was written, so the scene-side targets below have descriptive names. Bind them in one pass once B's names exist.

- **Binary:** `build/release/src/avgen` in this worktree. It was built before the pass-2 tail, which changed only
  generators and docs. Every run here is CPU-only (`--sonic-trace`, no GPU), so `tools/gpu-lock.sh` was not needed.
- **Evidence:** `~/Desktop/av-gen-review/aurora-borealis/pass3-research-a/`:
  - **Probe generators:** `make_probe.py` and `make_probe2.py`. Their projects are in `proj/`.
  - **Analysis scripts:** `wavio.py` is a numpy WAV reader; the rest are listed in §9.
  - **Outputs:** each script writes `*-out.txt`. The traces are in `trace/`.
  - **Event lists:** `events-<instrument>.txt`.
  - **Plots:** nine, `01-overview-mix-time.png` through `09-event-raster.png`.
- **Conditions:** the Mac was on battery, not AC, during this work (`pmset`: "Battery Power", 65%). No number here
  depends on it: they are offline traces and audio analysis, with no GPU or live timing.

## 0. Summary

1. **The stems are not in sync with the mix. They are at a different speed.**
   - **Two speeds:** `Sabertooth.wav`, `Crash.wav` and `HH.wav` run at **85.49 BPM**. The other seven (BASS, Cloud
     Diving, Kick, Moog, Snare, Toms, tone block) run at **83.00 BPM**.
   - **The 2.1 s is tempo, not tail.** Both sets are exactly 25 bars: 70.1836 s against 72.2892 s, a ratio of
     1.030000.
   - **Pitch moves with it:** the mix is +0.51 semitones above the seven stems (Moog and Cloud Diving both measure
     +0.509; 3% is +0.512). So it is Logic's Varispeed (speed *and* pitch +3%), not a tempo change re-rendered at
     the same pitch.
   - **The metadata is wrong.** Every file's LIST chunk says "Tempo: 83.0", including the three that run at 85.49.
     The bext origin is **Logic Pro**, not Ableton.
   - **The drift, loaded as delivered:** a kick stem event lands +0.29 s late at 10 s, +1.04 s at 35 s and
     +1.96 s at 65 s. That is +30.0 ms per second (§1.3).
2. **The engine's stem-set check (ADR-1632) flags all nine stems, but it cannot name the cause.**
   - **What it says:** every stem "does not match the mix within ±500 ms (correlation 0.00-0.05)", and the stems
     "sum to the mix with a residual 0.0 dB below it".
   - **Why it cannot say more:** it searches for a constant lag, and this is a constant *ratio*.
   - **Even resampled, the mix is not a linear sum of the stems.** I resampled the seven stems exactly by 1/1.03.
     The check then aligns Bass at +6.5 ms (correlation 0.70) and Kick at +38 ms (0.36), but the residual stays at
     0.2 dB.
   - **So the sample-level check cannot confirm this set, and event-level timing is the evidence (§1.4):**
     - once mapped, every stem's events land within ±10 ms of the mix's own transients;
     - the lag is constant over the whole 70 s to within ±6 samples.
3. **The fix that changes no sample:** make the seven 83 BPM stems declare a 49,440 Hz rate (48,000 × 1.03).
   - **How it works:** AV Gen analyses every stem at its own rate, and frame times are seconds (ADR-1520). So a stem
     whose header says 49,440 Hz is the varispeeded stem, with no resampling. Only the 4 bytes of the fmt chunk's
     rate fields differ.
   - **Measured with these copies:**
     - Kick: 120/120 onsets against the mix clock, at -4 ms (median), with a drift slope of 0.00 ms/s;
     - the Kick-fed beat clock reads 85.51 BPM;
     - the beat pulses fall +5.8 ms after the 85.49 grid in both halves of the song.
   - **The proper fix is a re-bounce,** which only the owner can do (§8, Q1). Bounce the seven stems with the same
     Varispeed setting as the mix, or bounce the mix, Crash and HH without it.
4. **Load every stem by key and read `stem.<key>.onset` directly. No classifier is needed.**
   - **Each stem is one instrument,** so its onset *is* the event.
   - **The classifier would hurt:**
     - On the Kick stem it fires the kick class 120/120, but **+29 ms late** (three hops).
     - It mislabels everything else: on Snare, 48 snare classes plus 48 hats and 8 kicks; on Crash, 10 snares and
       25 hats; on HH, 396 hats and 61 snares.
   - **Proposed roles:** `drums` for Kick only, so that `role.drums` and the feeds keep working; `synth` for Cloud
     Diving; `bass` for BASS; `other` for the rest.
   - **No stem limit:**
     - nothing in the code caps the stem count (one analysis thread per stem);
     - ten stems analyse in 232 ms with no drums role, 521-551 ms with one and 851 ms with five drums stems;
     - the timbre pass (1.1 s) costs more than all of them.
5. **The detectors, measured against the audio (mix clock, 240 fps trace; full table in §3):**

   | Instrument | Events | Detector | Quality | Cause of the misses or false fires |
   |---|---|---|---|---|
   | Kick | 120 | onset | 120/120, 0 doubles, -4 ms | |
   | Snare | 48 | onset with a 100 ms refractory | 48/48 | the raw onset double-fires on 25 of 48 hits, at 58-62 ms |
   | **Crash** | **12** | onset with a refractory of 100 ms or more | 12/12, 0 false | the raw onset fires 13: one double at 25.32 |
   | Toms | 28 note starts in 6 fills | onset | the note starts | buzz-roll notes, not single hits |
   | Tone block | 176 accents | onset | every accent ≥ -5 dB, none of the 249 ghost notes | |
   | **Moog** | 24 played notes | an rms *rise* detector, 26 fires, 24 true | | the raw onset fires 321 times, on every 8th-note echo and timbre wobble |
   | Cloud Diving | 15 swells | 0 onsets (correct) | | |
   | HH | 374 sixteenths, 29 open hats (once a bar) | | | |
   | Bass | 217 of 314 notes | | | |

6. **Kick and snare always land together.**
   - **47 of 48 snares fall within 20 ms of a kick.** The snare is on beats 2 and 4, and the kick plays every beat
     plus 16th pickups.
   - **All 12 crashes coincide with a kick,** and 7 of them with a snare as well.
   - **For the river,** this means a Kick beam and a Snare beam launch on the same frame twice a bar. The shared
     beam system must keep them legible when they start together (§5.3).
7. **Timing, offline:**
   - **Onsets publish 1-6 ms early.** The analysis frame is time-stamped at its window centre.
   - **At 30 fps,** a transient shows on a frame -10 to +30 ms from the true onset (p50 +10.5 ms).
   - **What adds lag:** stacked smoothing, the classifier (+29 to +37 ms) and `macro.*` (+1 frame). Nothing else on
     the proposed paths does.
   - **Live,** with the stems as separate channels, every onset path is the measured 58 ms p50 / 82 ms p95 to light
     (PROGRESS). The class paths are 87-111 ms.

## 1. Inventory and synchronization

### 1.1 The ten files (`inventory-out.txt`)

All are 48 kHz, 24-bit PCM, with the chunks JUNK, fmt, data, LGWV, ResU, cue, LIST and bext. Every bext
TimeReference is 178,213,012 and the origin is "Logic Pro". Every LIST chunk reads "Tempo: 83.0". The casing and
spaces match the brief exactly; nothing is missing.

| File | Ch | Frames | Duration | Peak / RMS dBFS | Bounced | Real tempo | First / last sound (> -60 dBFS) | Note |
|---|---|---|---|---|---|---|---|---|
| `Sabertooth.wav` (mix) | 2 | 3,368,815 | 70.1836 s | -0.30 / -11.70 | 18:21:05 | **85.49** | 0.00 / 70.18 (cut at -12 dB, not faded) | +0.51 st vs the stems |
| `Crash.wav` | 2 | 3,368,815 | 70.1836 s | -4.38 / -31.59 | 18:27:03 | **85.49** | 2.81 / 62.16 | at the mix's speed |
| `HH.wav` | 2 | 3,368,815 | 70.1836 s | -4.47 / -33.16 | 18:25:02 | **85.49** | 2.81 / 70.09 | at the mix's speed |
| `BASS.wav` | 2 (L = R) | 3,469,880 | 72.2892 s | -0.36 / -17.06 | 18:23:58 batch | 83.00 | 2.36 / 72.29 | mono content in stereo |
| `Cloud Diving.wav` | 2 | 3,469,880 | 72.2892 s | -6.05 / -24.21 | batch | 83.00 | 2.94 / 70.42 | wide (side/mid -0.8 dB) |
| `Kick.wav` | 2 (L = R) | 3,469,880 | 72.2892 s | -6.27 / -23.18 | batch | 83.00 | 0.00 / 71.54 | |
| `Moog.wav` | **1** | 3,469,880 | 72.2892 s | -9.08 / -25.40 | batch | 83.00 | 3.66 / 71.71 | mono |
| `Snare.wav` | 2 (L = R) | 3,469,880 | 72.2892 s | -2.48 / -28.83 | batch | 83.00 | 3.62 / 72.01 | |
| `Toms.wav` | 2 | 3,469,880 | 72.2892 s | -7.79 / -33.83 | batch | 82.97 | 1.08 / 72.29 | fills only |
| `tone block.wav` | **1** | 3,469,880 | 72.2892 s | -1.58 / -28.16 | batch | 83.00 | 3.26 / 71.36 | mono |

- **The batch stems' last 2.1 s is music, not tail** (`tail-out.txt`): Bass, Kick, Snare and Toms all play to the
  file's end.
- **What the 2.1 s is:** the seven stems are the same 25 bars, played 3% slower.

### 1.2 Proving the speed difference (`tempo-out.txt`, `pitch-out.txt`)

- **Tempo:** the beat period comes from the onset-envelope autocorrelation over 8 beats.
  - The mix's low band, high band and broadband all give 701.9 ms (85.49 BPM). So does HH.
  - Kick, Snare, BASS and tone block give 722.9 ms (83.00); Toms gives 723.1 ms.
  - Every file comes to 25.00 bars.
- **Pitch:** the cross-correlation of log-frequency spectra puts the mix +0.509 semitones above Moog and above Cloud
  Diving. Bass reads +0.68 and tone block +0.19 (noisier, broadband and percussive); HH is noise.
- **So the cause is Varispeed:** speed and pitch +3%.

### 1.3 The engine's check on the files as delivered (probe `asis-roles`, `trace/asis-roles.log`)

```
stem 'Kick' (Kick.wav): it does not match the mix within +-500 ms of its offset (correlation 0.05) ... (all 9 alike, 0.00-0.05)
stems: 9 of 9 sum to the mix with a residual 0.0 dB below it
```

- **Drift through the engine:** I paired each kick onset with the mix's kick of the same index. The engine minus
  the mix clock is +288 ms at 10 s, +1,042 ms at 35 s and +1,956 ms at 65 s, a slope of **+29.99 ms/s**
  (`drift-engine-out.txt`).
- **No offset can fix it:** ADR-1632's lag search assumes a constant lag. A constant offset would have to be 0 s at
  the start and 2.1 s at the end.

### 1.4 Once the seven stems are varispeeded, they align

**Independent check:** I FFT-resampled the seven stems exactly from 3,469,880 to 3,368,815 frames
(`varispeed_fit.py`), then cross-correlated each against the mix in 7 s windows at sample level (`winlag-out.txt`).

| Stem | Lag in every 7 s window, samples at 48 kHz (|correlation|) |
|---|---|
| BASS | +302 to +314 (0.52-0.68), every window |
| Snare | +134 to +142 (0.09-0.13) |
| Moog | +28 to +30 (0.11-0.29); one window +62 |
| tone block | +11 to +12 (0.03-0.08) |
| Kick | +1,489 to +1,503 (negative correlation, -0.29 to -0.36: a waveform-period match, see the event check below) |
| Crash, HH (not resampled) | **0** in every window, at low correlation (0.02-0.08) |
| Cloud Diving | no stable lag (a pad: nothing to lock to) |

- **The lag is constant over the song** to within ±6 samples for every stem that correlates. So the ratio is exactly
  1.03, and there is no drift.
- **The per-stem lags differ by up to 6.4 ms** (BASS +306 samples). They are processing in the mix bus: the mix is
  not a linear sum of these stems.
  - A least-squares fit with one gain per stem leaves 0.17 dB of residual; Feline left 42.5 dB.
  - The fit gives BASS a negative gain.
- **Event level, the timing that matters to a light.** Kick and snare onsets, taken sample-accurately from the clean
  stems and mapped by t/1.03, against the mix's own transient in the instrument's band (`kickevt-out.txt`):
  - the kick is at -4.4 ms (median; -4.5 first half, -4.3 second half);
  - the snare is at -1.0 ms (median).
- **The engine's check on the resampled copies:**
  - **Bass:** "lines up 6.5 ms later (0.70)".
  - **Kick:** "38.2 ms later (0.36)". That is the same waveform-period match, and it is wrong at event level.
  - **The rest:** "does not match (0.00-0.12)".
  - **The residual:** 0.2 dB.
- **The check's verdict on this set is therefore not meaningful.** Crash and HH are exactly aligned (lag 0 in every
  window), yet they still read "does not match" (0.02 and 0.04). The other stems do not leave a residual the check
  can fit.
  - **Expect two standing warnings in any Sabertooth project** (Crash and HH) until a re-bounce from one session.
    They are harmless.

### 1.5 The rate-relabelled stems (probe `relabel-prop`; `relabel.py`)

- **What the copies are:** the seven batch stems with the fmt chunk's `sampleRate` and `byteRate` set for 49,440 Hz.
  `relabel.py` checks every byte: 4 bytes differ (offsets 96-97 and 100-101), and the data chunk is untouched.
- **What the engine says:** "49440 Hz beside a 48000 Hz mix; analysed at its own rate (nothing is resampled)". That
  is info severity.
- **The cost:** the check leaves these stems out of its sum ("2 of 9 sum to the mix"). So only the Crash and HH
  warnings remain.
- **Analysis at 49.44 kHz:**
  - the hop is 512 / 49,440 = 10.36 ms, against the mix's 10.67;
  - the window is 41.4 ms;
  - bands in Hz read the stem 3% higher, which is exactly the pitch the listener hears in the mix.
- **Kick through the engine:** 120 onsets before 70.18 s. The engine minus the mix clock is -4, -3 and -3 ms at 10,
  35 and 65 s, a slope of **0.00 ms/s**.
- **The Kick-fed beat clock** (probe `feedkick`, `"feeds": {"beat": "stem.kick"}`):
  - "85.51 bpm, 98 beats";
  - pulses +5.8 ms after the 85.49 grid (p10/p90 +4.1/+7.5), the same in both halves;
  - the nearest kick lands 6.3 ms before each pulse.

## 2. Loading through the existing workflow

### 2.1 Where the files are (owner decision, done)

The ten files are copied byte for byte (`cmp`-verified) to the owner's main checkout:
`/Users/natefaulkenberry/Documents/GitHub/av-gen/assets/audio/stems/sabertooth/<exact name>`.

- **Ignored:** `.gitignore` covers `assets/audio/stems/*.wav`, but not a subfolder. So I added
  `/assets/audio/stems/sabertooth/` with a comment to the shared `.git/info/exclude`. It applies to the main checkout
  and every worktree, and it commits nothing.
  - `git check-ignore -v` names that rule for the new paths, and `git status` is clean in both checkouts.
- **The folders were writable** (`drwxr-xr-x`) before I wrote.
- **This worktree links to the copies.** It has per-file symlinks at `assets/audio/stems/sabertooth/*.wav`, the same
  way the Feline stems are linked.
  - `tools/link-worktree-assets.sh` will link them in any new worktree, because it walks subfolders.
- **Nothing is committed.** The originals in `~/Desktop/sabertooth/` are untouched.

### 2.2 Proposed project setup (a Sabertooth twin beside the Feline projects)

- **The generator:** follow `examples/aurora-borealis/music.py`'s `apply()`, with a Sabertooth variant. Paths are
  relative to `examples/aurora-borealis/`.
- **The varispeed stems:** until the owner re-bounces, the seven batch stems are read from header-relabelled copies.
  - **Where:** in the same ignored folder, e.g. `assets/audio/stems/sabertooth/varispeed/<name>.wav`.
  - **How to make them:** `python3 relabel.py <that folder>`, run on the main checkout's copies, then symlink them
    into the worktree.
  - I have not created them in the repository. They exist only in my scratchpad.

```json
"assets": {
  "audio": "../../assets/audio/stems/sabertooth/Sabertooth.wav",
  "stems": [
    {"key": "kick",      "label": "Kick",         "role": "drums", "file": "../../assets/audio/stems/sabertooth/varispeed/Kick.wav"},
    {"key": "snare",     "label": "Snare",        "role": "other", "file": ".../varispeed/Snare.wav"},
    {"key": "toms",      "label": "Toms",         "role": "other", "file": ".../varispeed/Toms.wav"},
    {"key": "crash",     "label": "Crash",        "role": "other", "file": "../../assets/audio/stems/sabertooth/Crash.wav"},
    {"key": "hh",        "label": "HH",           "role": "other", "file": "../../assets/audio/stems/sabertooth/HH.wav"},
    {"key": "bass",      "label": "Bass",         "role": "bass",  "file": ".../varispeed/BASS.wav"},
    {"key": "cloud",     "label": "Cloud Diving", "role": "synth", "file": ".../varispeed/Cloud Diving.wav"},
    {"key": "moog",      "label": "Moog",         "role": "other", "file": ".../varispeed/Moog.wav"},
    {"key": "toneblock", "label": "Tone Block",   "role": "other", "file": ".../varispeed/tone block.wav"}
  ]
},
"feeds": {"beat": "stem.kick"}
```

**Why these roles and keys:**
- **Routes read `stem.<key>.*`, not `role.*`.**
  - Five roles cannot tell ten instruments apart: `role.other` is just the first `other` stem (Snare here).
  - Live input channels publish the same `stem.<key>.*` under their key (ADR-1521). Channels keyed `kick`, `snare`,
    ... drive the same signals later, with no change.
  - Pass 1's "read `role.*`" rule was for a three-stem band; it does not carry to a ten-stem kit.
- **Kick has the drums role, so the existing scene keeps working.**
  - `role.drums.kick` exists, so `signal.kick` and `kickRecent` still resolve, and the Feline twin is untouched.
  - The proposed signals do not use the class.
  - It costs about 300 ms of load (the classifier on one 70 s stem).
- **Moog is `other`, so that `role.synth` stays Cloud Diving.** Listing Moog after Cloud Diving as a second `synth`
  would work too.
- **Mono stems are fine:** Moog and tone block are analysed as mono, and their `width` reads 0.
- **`feeds.beat` comes from the Kick stem.** The mix's own grid locks at **114.44 BPM**, which is 4/3 of the tempo:
  105 of its 134 pulses are more than 50 ms from a kick (`beat-out.txt`). A `hits` feed is not needed.
- **`sonic`:** keep the scene's existing block, which serves the mix-only fallback.

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
- a fixed 60 ms minimum interval. Snare and crash decays re-fire at exactly that interval (58-62 ms), so every
  transient signal below adds its own refractory in the chain.

### 2.4 The classifier on single-instrument stems (probe `relabel-drums`, `classes-out.txt`)

| Stem (all role drums) | Onsets | kick class | snare class | hat class |
|---|---|---|---|---|
| Kick | 120 | 120, **+29 ms** after the onset (p90 +33) | 0 | 0 |
| Snare | 73 | 8 (wrong) | 48, **+37 ms** | 48 (wrong, on the onset) |
| Toms | 28 | 4 | 4 | 21 |
| Crash | 13 | 0 | 10 | 25 |
| HH | 375 | 0 | 61 (wrong) | 396 |

**Verdict:** the classifier is unnecessary on every stem and harmful on most of them: late, and wrong labels.

## 3. Per-instrument events, on the real audio

**Method:**
- `--sonic-trace` at 240 fps on the relabelled set, so each 10.4 ms hop gets its own frame.
- Events were read through `signal.*` envelopes (attack 0, a fall of 1,000 a second).
- The truth is a sample-level rise detector on the audio itself (`tr.py`, `events_all.py`), checked visually in the
  plots.
- All times are on `Sabertooth.wav`'s clock. Lists: `events-*.txt`.

### 3.1 Crash: 12 hits (`02-crash-hits.png`, `crash-out.txt`, `analyse2-out.txt`)

**Every crash time** (the engine's onset, mix clock; truth within ±11 ms):

| # | s | Bar.beat (85.49 BPM, from t = 0) | Peak (stem.crash.peak) | Context |
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

**What the stem does:**
- Each crash rings 3-4 s.
- The re-hits land on the decay of the previous hit (+8 to +20 dB jumps).
- The shortest gap is one beat (0.70 s). There are no crashes after 62 s.

**Detector:** `stem.crash.onset` plus a latch.
- **The raw onset** fires 13 times: one double at 25.325, 63 ms after the 25.262 hit.
- **With a refractory of 100, 200, 300 or 500 ms** it fires exactly the 12, with 0 false fires and a median offset of
  +0.5 ms.
- **Recommended refractory: 300 ms.** It is well under the 700 ms minimum gap, so no real hit can be missed.
- **Do not gate by `onsetStrength` ≥ 4:** that loses 3 of the 12 (25.26, 36.50, 47.73).
- **Strength:** the phrase-start crashes peak at 0.41-0.42, the re-hits at 0.17-0.28. `stem.crash.peak` on the onset
  frame (attack 0) gives 86% of the hit's 50 ms peak, so a held peak can size a star.

### 3.2 Kick and Snare (`events_all-out.txt`, `06-snare-doubles.png`)

- **Kick:** 120 hits (4.8 a bar).
  - **Where they fall:** 98 on the beat, 20 on the "a" (the 16th before a beat, gap 0.17-0.18 s) and 2 on the "e".
  - **The onset:** 120/120, 0 doubles, offset p10/50/90 -9.4 / -4.2 / +0.1 ms.
  - **Size:** every kick has the same velocity (peak 0.45-0.49), so there is nothing to size by.
- **Snare:** 48 hits, every one on beat 2 or 4 (gap 1.40 s).
  - **The raw onset** fires 73 times: 25 doubles, all at 58-62 ms (the minimum interval, re-firing on the snare's
    rattle).
  - **With a refractory of 100 or 150 ms** it fires 48/48, at -3.4 ms (median).
  - **Peak spread:** 1.6x (0.46-0.73), held from the onset frame.
- **Coincidence:** 47 of 48 snares fall within 20 ms of a kick (`coincide-out.txt`).

### 3.3 Moog: plucked notes with an 8th-note echo, then legato (`04-moog.png`)

**Articulation:**
- **Discrete played notes.** Each is a sharp attack, then a staircase decay. The 0.346 s steps are 8th-note delay
  repeats, about -5 dB each, still audible 2-3 s later.
- **From about 51 s,** longer notes with legato steps.
- **24 played notes** in all (`events-moog.txt`, rise A; 2 of its 26 fires, at 14.68 and 35.13, are extras):
  3.55, 8.78, 9.18, 14.43, 19.30, 20.37, 21.79, 23.88, 25.98, 28.78, 31.28, 32.65, 34.76, 36.90, 41.43, 42.80, 47.73,
  51.23, 53.35, 55.10, 58.96, 60.37, 64.58 and 66.02.

**Detectors** (`analyse2-out.txt`; truth = 38 rises, of which about 14 are echo repeats):

| Detector | Fires | On a played note | Comment |
|---|---|---|---|
| raw `stem.moog.onset` | 321 | 33 | fires on every echo step and timbre wobble (58-71 ms trains). Unusable |
| onset gated by `onsetStrength` ≥ 3 / 5 / 8 | 27 / 12 / 5 | 14 / 9 / 5 | loses notes |
| onset gated by an rms rise | 13-55 | 7-13 | the onset precedes the gate opening |
| **rms rise A:** `stem.moog.rms` trend 0.25 s / 15 ms, floor 0.003, ×1.6, latched 150 ms | **26** | **24 notes + 9.18** | **recommended**; -4.8 ms |
| rms rise C: the same at ×2.2 | 22 | 22, precision 1.00 | stricter: misses 9.18 and 20.37 (legato) |

**The sustain:** `stem.moog.level` with a 20 ms attack and 600 ms decay follows each note's body and decay
(`09-event-raster.png`, lower panel). It is the "gently illuminated for the duration of a note" layer.

### 3.4 Toms: six fills of roll-notes (`03-toms.png`)

**What a tom note is:** not a single hit. Each is a 0.2-1.3 s buzz roll, with the amplitude fluttering every 20-30
ms. The fills step up in level, and the **last note of every fill is the loudest**.
- **Peak:** 0.21-0.23 on the last note, 0.07-0.16 on the rest. The spread is 2.0x, and the onset frame carries the
  full peak.

**The 28 note starts** (`stem.toms.onset`, which the 100/150 ms refractory leaves unchanged):

| Fill | Note starts (s) |
|---|---|
| 1 | 1.05, 1.23, 1.58, 1.93, 2.45, **2.63** |
| 2 | 24.04, 24.38, 24.55, **24.91** |
| 3 | 35.26, 35.62, 35.79, 36.13, **36.32** |
| 4 | 41.76, **41.93** |
| 5 | 45.96, 46.15, 46.49, 46.84, 47.37, **47.55** |
| 6 | 68.95, 69.30, 69.48, 69.82, **70.01** |

The bold note in each fill is its loudest, the last.

- **The onset catches the note starts,** not the roll's flutter. My 78-rise "truth" counted the flutter and is not a
  fair reference.
- **The rms-rise gate keeps only 16,** so it is not wanted here.
- **For sustain, use `stem.toms.level`:** it climbs per note through a fill.

### 3.5 Tone block: 176 accents, syncopated (`05-toneblock.png`)

- **The pattern:** a 16th-note pattern, never on the beat (0 / 21 / 71 / 88 on the 16ths 1 / e / & / a), about 7
  accents a bar.
- **The accents and the ghosts:** the accents are ≥ -5 dB. 249 ghost notes sit 10-31 dB below them.
- **The onset catches exactly the accents:** 176/176 of those ≥ -5 dB, 0 of the ghosts, at -5.4 ms. The adaptive
  threshold is a built-in accent filter.
- **Peak:** 0.35-0.79 (2.2x spread), held from the onset frame.
- **The rests:** silent before 3.1 s, at 23.6-25.4 s and at 45.0-48.0 s. These are **exactly where the toms fills
  are**, so the two right-side instruments trade bars.
- **They rarely coincide:** only 16 of 176 tone-block accents, and 9 of 28 tom notes, fall on a kick.

### 3.6 Cloud Diving: 15 swells (`08-cloud-diving-envelopes.png`, `cloud-out.txt`)

**The swells** start at 2.97, 8.70, 14.20, 19.83, 25.43, 31.00, 36.73, 42.30, 47.90 (to 53.5, the long one), 54.27,
59.07, 63.40, 64.30, 65.40 and 67.77.
- The first eleven are full swells, one every 2 bars (5.6 s).
- Their rise to 90% takes 0.5-2.5 s, they hold for 2-3 s, and they **end abruptly** (cut, not faded).
- The last four (63-68 s) are short stabs at 0.3-0.7 of the level.

**Levels:** rms p50 0.063, p95 0.10, max 0.14. Use a gain of about 8 so that p95 is about 0.8 (gain 10 clips the
plateaus). It has no onsets, correctly.

**Smoothing** (per-frame change at 30 fps inside the swells, after their first second; lag is to 50% of each swell's
peak):

| `stem.cloud.rms` × 10, attack / decay ms | Jitter p50 / p99 per frame | Lag to 50% | Level left 0.5 s after the cut |
|---|---|---|---|
| raw | 0.058 / 0.311 | 0 | 0 |
| 80 / 400 | 0.013 / 0.069 | 33 ms | 0.23 |
| 150 / 600 | 0.009 / 0.044 | 67 ms | 0.37 |
| **300 / 1200** | **0.005 / 0.028** | **100 ms** | **0.61** |
| 600 / 2000 | 0.004 / 0.021 | 167 ms | 0.74 |
| 1500 / 4000 | 0.004 / 0.012 | (never peaks within a swell) | 0.87 |

- **For intensity, width and folds: 250-300 ms attack and 1,000-1,200 ms decay.**
  - It costs 100 ms against rises of 0.5-2.5 s, which is invisible.
  - It turns the abrupt cut into a roughly 1 s relaxation, which the brief wants.
  - Its p99 step of 0.028 a frame is no flicker.
- **For the hue / palette blend: 1,500 / 4,000 ms.** Its p99 is 0.012, inside pass 2's palette bar (step p99 < 0.02).
  It reads as a slow "how much has the synth been swelling" value: 0.25-0.7 across the song.
- **`stem.cloud.level`** (the adaptive level) is smoother (raw p99 0.080), but it reads each swell against the
  stem's own loudness. Use rms for absolute size.
- **The spring stage on top (1 Hz) was worse:** p99 0.54. Not recommended.

### 3.7 Bass and HH, for the later decision (`hh-bass-out.txt`)

- **HH:**
  - **Closed hats:** 374, steady 16ths from 2.8 s to the end (5.3 a second), 20 dB decay in about 28 ms. Per hit,
    this is the "continuous stream of flashes" the brief forbids.
  - **Open hats:** 29, once a bar at 1.75 beats into the bar (the "a" of beat 2; at 2.0 in 6 bars).
  - **The candidate:** that open hat is the one legible HH pattern, a once-a-bar micro-glint. The detector would
    need a sustained-energy test (an HH onset whose rms is still up 100 ms later). I have not built or measured it.
  - **The onset is good:** 365/365, 0.97 precise, at -1.3 ms.
- **Bass:**
  - **The notes:** 314 rises (4.5 a second, 16th riffs in 2-bar phrases). The onset catches 217 of them: every one
    ≥ 0 dB, 92 of 169 between -5 and 0 dB, and few repeated notes closer than 100 ms. It is precise (0.97), at
    -6.3 ms.
  - **The rests:** silences of more than 1 s at 10.7-12.6, 22.1-23.9, 44.8-46.5, 50.7-52.8, 61.6-63.2 and
    67.4-68.8 s, and others.
  - **The candidate:** a slow `stem.bass.rms` envelope (150/800 ms, pass 1's `bassBody` shape) for river flow
    energy. Hits would be too dense.

## 4. Timing

### 4.1 Granularity and where an onset lands

- **The analysis hop:** 512 samples at the file's rate. That is 10.67 ms on the mix, Crash and HH, and 10.36 ms on
  the relabelled stems, with a 2,048-sample window (42.7 / 41.4 ms).
- **Publishing (ADR-1520):** each frame takes the newest analysis frame at or before the playhead, and ORs the events
  since the last frame.
- **The time stamp is the window centre.** So an onset is stamped up to half a window before the transient. Measured
  against the true transient, the onset leads by:

  | Kick | Snare | Crash | HH | Tone block | Bass | Moog rise |
  |---|---|---|---|---|---|---|
  | -4.2 ms | -3.4 | +0.5 | -1.3 | -5.4 | -6.3 | -4.8 |

- **At 30 fps** (probe 2, `frame30-out.txt`), the frame that shows the event minus the true transient is:

  | | min | p50 | p90 | max |
  |---|---|---|---|---|
  | Kick | -10.3 | +10.5 | +23.0 | +29.5 ms |
  | Snare | -3.5 | +13.1 | +25.7 | +29.5 ms |
  | Crash | -6.3 | +10.6 | +28.5 | +30.3 ms |
  | HH | -9.3 | +14.0 | +26.7 | +33.4 ms |

  That is the frame quantisation (0-33 ms) less the window lead. A 30 fps offline render cannot do better without
  sub-frame event times.
- **The mix against the stems:** the mix's own kick transient sits -4.4 ms from the mapped stem, the snare -1.0 ms.
- **Over the full 70 s:**
  - kick drift is 0.00 ms/s through the engine with the relabelled stems;
  - the beat pulses are +5.9 / +5.8 ms against the grid in the first and second halves;
  - the windowed lags hold to ±6 samples.

  **Nothing drifts.**

### 4.2 What delays a visual attack (the proposed paths avoid all of it)

| Stage | Cost | Use |
|---|---|---|
| `stem.<key>.onset` → `signal.*` with attack 0 | **same frame** (ADR-1602) | every transient |
| envelope refractory / re-arm (ADR-1574) | none (it blocks re-triggers only) | every transient |
| rms trend detector (Moog, rise A) | measured -4.8 ms against the note's true attack | Moog notes |
| the classifier (`.kick/.snare/.hat`) | **+29 to +37 ms** offline (three hops), +30 to +44 ms live | none |
| attack > 0 on an event | + the attack, to full value | only designed swells |
| `macro.*` | +1 frame | none |
| the beat grid (Kick-fed) | +5.8 ms after the grid; the kick is 6 ms earlier | beat counting and indexing, not the accent itself |

### 4.3 Live, when the stems become separate channels

- **The accent paths:** each channel keyed as in §2.2 publishes the same `stem.<key>.*` (ADR-1521). So every accent
  rides the onset path, measured on this scene at **58.2 / 81.6 ms p50 / p95 to light**: 24.0 / 40.2 ms to the bus,
  then the frame (PROGRESS, sound to light).
- **The class paths would be 87-111 ms.** That is why none is proposed.
- **Ten sources** at about 16 µs per hop each, plus one drums source at +80-100 µs, come to about 0.26 ms per 10.7 ms
  hop on the analysis thread (about 2.5% of a core).
- **The device:** it needs 10 or more input channels (or a BlackHole 16ch loopback) and one shared clock.
- **The live beat tracker** on the kick channel: pass 2 measured that it is no faster than the onset path. So the
  grid is for counting, not for accents.
- **The varispeed problem does not exist live** (it is real audio on one clock). Two warnings:
  - Played live, the stems must be the same speed as the mix.
  - **Offline and live differ by a constant:** offline the light shows -10 to +30 ms from the sound; live it is 58 ms
    p50.

### 4.4 Rules for the bindings

- **Every transient:**
  - `stem.<key>.onset` (or the Moog rise) → a latching envelope with **attack 0**, its own refractory and a designed
    decay;
  - size from the stem's `peak`, held from the onset frame (attack 0);
  - no route chain.
- **Smoothing only where designed:** the Cloud Diving intensity, the hue and the Moog sustain.
- **No `.level` as an accent depth.** Pass 1 learned that it re-adds the wobble.
- **No classes and no `macro.*` on a path.**

## 5. Proposed mapping

### 5.1 The table

**Notes on the table:**
- Times are in ms. *Release* is the envelope's fall to 0. *Refr* is the latch's refractory.
- **Signal names** are proposals in the existing `signal.*` style. **Targets** are descriptive, for B to bind.
- Every input carries its stem's isolation depth (a control channel resting at 1, ADR-1612), as `music.py` does per
  part.

| File | Key / role | Signal (definition) | Visual role (brief) | Attack | Release | Refr | Cap / bound | Measured on Sabertooth |
|---|---|---|---|---|---|---|---|---|
| `Cloud Diving.wav` (varispeed) | `cloud` / synth | `cloudSwell` = `stem.cloud.rms` {gain 8, clamp, attack 300, decay 1200} | aurora intensity, curtain width / height, fold amplitude, inner detail, persistence | 300 | 1200 | none | adds to a non-zero baseline (quiet passages keep the aurora); ≤ the pass-2 flash budget | 15 swells; p99 step 0.028 a frame; 100 ms to 50% |
| | | `cloudHue` = `stem.cloud.rms` {gain 8, clamp, attack 1500, decay 4000} | dominant hue / palette blend (with the scene's existing `paletteAxis`) | 1500 | 4000 | none | a blend, never a jump; p99 step ≤ 0.02 | 0.25-0.7 over the song; p99 0.012 |
| `Crash.wav` | `crash` / other | `crashStar` = `stem.crash.onset` {linearfall, attack 0, **refr 300**, rearm 0.05}; size `crashSize` = `stem.crash.peak` {attack 0, decay 300} | shooting star launch; environment flash (aurora near the path, facets, river glint) | 0 | the star's own timeline (≈ 0.5-0.7 s streak); wash ≈ 400 | 300 | **2 stars alive at most** (re-hits come 0.70 s apart); a new crash takes the oldest slot | **12 hits** (§3.1), 0 false, +0.5 ms |
| `Moog.wav` (varispeed, mono) | `moog` / other | `moogNote` = `stem.moog.rms` {trend 0.25 s / 15 ms, floor 0.003, binary 1.6} → {linearfall, attack 0, refr 150} | LEFT mountains: a glint burst on a deterministic facet subset per note | 0 | 300-500 | 150 | one burst per note; subset index from a note counter or `beat.count` | 26 fires = 24 notes + 2 extras, -4.8 ms |
| | | `moogGlow` = `stem.moog.level` {attack 20, decay 600} | LEFT: a sustained gentle glow on the lit subset while the note rings (echo tail) | 20 | 600 | none | ≤ 0.3 of the glint peak | follows the 0.35 s echo steps |
| `Kick.wav` (varispeed) | `kick` / **drums** | `kickBeam` = `stem.kick.onset` {linearfall, attack 0, **refr 100**} | river beam launch (hot colour) + ripple | 0 | the beam's travel (B: ≈ 0.6-0.7 s, about a beat) | 100 | pool of 2-3 beams (the 16th pickups put two kicks 0.175 s apart); summed glow clamped | 120/120, 0 doubles, -4.2 ms |
| `Snare.wav` (varispeed) | `snare` / other | `snareBeam` = `stem.snare.onset` {linearfall, attack 0, **refr 100**}; size from `stem.snare.peak` | river beam launch (second colour, shorter and sharper) + ripple | 0 | shorter than the kick's (B: ≈ 0.4-0.5 s) | 100 | pool of 1-2; **coincides with a kick on 47 of 48 hits**: must read beside a simultaneous kick beam | 48/48 (the raw onset: 73), -3.4 ms |
| `Toms.wav` (varispeed) | `toms` / other | `tomHit` = `stem.toms.onset` {linearfall, attack 0, refr 100}, depth `tomSize` = `stem.toms.peak` {attack 0, decay 300} (depthMin 0.4, depthMax 1.3) | RIGHT mountains: substantial accents on prominent planes; the last (loudest) note of a fill the biggest | 0 | 400-600 | 100 | one subset per note, rotating; ≤ the flash budget | 28 notes in 6 fills; peak spread 2.0x |
| | | `tomBody` = `stem.toms.level` {attack 30, decay 500} | RIGHT: a broader restrained glow building through a fill | 30 | 500 | none | | rises per note |
| `tone block.wav` (varispeed, mono) | `toneblock` / other | `blockTick` = `stem.toneblock.onset` {linearfall, attack 0, refr 80}, depth `blockSize` = `stem.toneblock.peak` {attack 0, decay 200} | RIGHT mountains: small sharp edge glints, stepping through a facet sequence (the 16th pattern) | 0 | 120-200 | 80 | small-area only; a sequence index stepped per tick | 176 accents (about 2.5 a second), 0 ghosts, -5.4 ms |
| `BASS.wav` (varispeed) | `bass` / bass | (reserved) `bassFlow` = `stem.bass.rms` {gain 4, clamp, attack 150, decay 800} | optional: river flow energy | 150 | 800 | | must not touch the aurora's colour | 217 onsets / 314 notes; rests > 1 s |
| `HH.wav` | `hh` / other | (reserved) open-hat glint: an HH onset with sustained rms (not built) | optional: a once-a-bar micro-glint | 0 | 150 | 500 | small area | 374 closed (do not use per hit); 29 open, once a bar |
| `Sabertooth.wav` | (mix) | plays; feeds the scene's existing `sonic` / `response` fallbacks | | | | | | |
| | | `beat.*` from `feeds.beat = stem.kick` | `beat.count` for subset rotation (pass 2 R3); `beat.phase` | | | | | 85.51 BPM, +5.8 ms |

### 5.2 The mix-only fallback (`mixonly-out.txt`: the mix's detectors against the stems' truth)

| Mix signal | Fires | Against the stems |
|---|---|---|
| `response.kick` | 0 | none: a mix has no kick (ADR-1620) |
| `response.snare` | 20 | recall 0.31 against the snare |
| `response.hat` | 344 | 0.92 / 0.87 against HH |
| `response.onset` | 286 | catches 94% of kicks, but only 40% of its fires are kicks |
| beat grid | | 114.44 BPM, wrong |
| crash | | **nothing finds the crashes** (`response.onset` precision 0.04) |

**Proposal:**
- **Scene choice:** make each transient signal `firstActive` on its stem only, with no mix input. On a mix-only run,
  the stars, beams and glints simply do not fire.
- **What still plays:** the aurora runs on the existing mix context (`response.sustain`, `sonic.*`), and the scene
  keeps its baseline motion.
- **Why not a mix stand-in:** one would fire on the wrong instrument, which the brief's per-instrument identity
  forbids.
- **One exception:** the kick beam could fall back to `response.onset` gated by a high `onsetLow` share. It is
  unmeasured here, and I recommend against it.

### 5.3 Overlap rules this music needs

- **Kick and snare launch together on beats 2 and 4** (47/48). Give them distinct launch shapes or positions, for
  example:
  - a different speed;
  - the snare starting further downstream;
  - or a different lateral offset.

  Otherwise the snare reads as a brighter kick. The sum must stay inside the clamp.
- **Kick density:** 1.71 a second, with 16th pickups 0.175 s before a beat.
  - A beam that lives about one beat (0.70 s) keeps 1-2 alive.
  - Pass 2's ring-of-two with a 1.8 s life would drop most kicks. That rule was tuned to Feline's 0.50 s gaps and
    should be retired.
- **Crash:** groups of three (+1.41 s, +0.70 s). A star of 0.5-0.7 s plus a trail never needs more than two slots.
- **Right side:** toms and tone block trade bars (tone block rests during the fills). Moog on the left overlaps both
  freely: 9 of its 26 notes fall on kicks.

## 6. Cost

- **Load:** 9 stems in 0.52-0.55 s with Kick as drums (0.23 s with none). That is less than the timbre pass (1.1 s).
  The stem-set check takes 20-90 ms.
- **Memory:** about 1.2 MB of probe and about 1.7 MB of spectrum rows per 70 s stem (ADR-1631/1632 rates).
- **Per frame:** the proposed signals are about 12 chains, ns each (pass 1: 51 sources cost 0.01 ms).
- **Seek:** the replay rebuilds from 0 on each seek (ADR-1573). A 70 s song makes it 4 times cheaper than Feline.

## 7. Risks

1. **The relabelled stems are a workaround.**
   - **The check is blind to them:** a re-bounce that changed something else would not be caught.
   - **The two standing warnings are uninformative:** Crash and HH "do not match" because the mix is not a linear sum.
   - **The fix:** the owner's re-bounce (Q1). Until then, the event-level checks in §1.4 are the verification. A
     scene test should pin `stem.kick.onset` against the 85.49 BPM grid (≤ 10 ms, no slope).
2. **The relabelled copies must be regenerated if the source changes.** They are 4-byte-header copies of the files in
   `stems/sabertooth/`.
3. **The Moog detector depends on a trend ratio.** Rise A has 2 extras in 26 and may misfire on an unseen performance.
   - Rise C (×2.2) is the strict alternative: precision 1.00, but it misses 2 legato notes.
   - Live, the same chain runs on the channel, unchanged.
4. **The 30 fps window:** offline the light lands -10 to +30 ms from the sound (p50 +10.5). Nothing on the scene's
   side can tighten it.
5. **Snare and crash double fires:** every transient needs its refractory. Without it, the raw onset fires 25 snare
   doubles and a crash double.
6. **The mix-only twin loses every per-instrument accent.** That is by design (§5.2).
7. **Battery:** this work ran on battery; nothing here was timed on the GPU. B's and the implementer's GPU numbers
   need AC and the 60 Hz LG.

## 8. Open questions for the coordinator / owner

1. **Re-bounce, or keep the 49,440 Hz relabel?** The mix, Crash and HH are 3% faster and +0.5 semitones higher than
   the other seven.
   - **Was the Varispeed intentional?** If it was, ideally bounce the seven stems with it on. If it was not, bounce
     the mix, Crash and HH with it off, and the scene follows the slower speed.
   - **Either way,** one session's bounce makes the stem-set check meaningful again.
2. **Create the relabelled copies** under `assets/audio/stems/sabertooth/varispeed/` now, ignored? The owner's
   copy-assets permission arguably covers derived files; I did not assume it.
3. **Moog: rise A (catches all 24 notes, 2 extras) or rise C (strict, misses 2 legato notes)?**
4. **HH open-hat micro-glint:** worth building the sustained-energy detector once the primary mappings work?

## 9. Reproduce

```sh
R=~/Desktop/av-gen-review/aurora-borealis/pass3-research-a; BIN=<worktree>/build/release/src/avgen
SP=<a scratch dir>; mkdir -p $SP/relabel $SP/resampled
cd $R
python3 inventory.py; python3 tail.py; python3 tempo.py; python3 pitch.py      # §1.1-1.2
python3 sync.py; python3 envsync.py; python3 batchlag.py; python3 scaled.py     # constant-lag search fails; t/1.03 aligns
python3 varispeed_fit.py   # writes resampled npy to the scratchpad path inside it (edit SP there)
python3 winlag.py; python3 kickevt.py                                          # §1.4
python3 relabel.py $SP/relabel; python3 write_resampled.py $SP                 # copies for the probes
python3 make_probe.py $SP proj
for p in asis-roles asis-other relabel-drums relabel-prop resampled feedkick; do
  $BIN --project proj/$p.json --sonic-trace trace/$p-240.csv --fps 240 > trace/$p.log 2>&1; done
python3 make_probe2.py proj/probe2.json
$BIN --project proj/probe2.json --sonic-trace trace/probe2-240.csv --fps 240
$BIN --project proj/probe2.json --sonic-trace trace/probe2-30.csv --fps 30
python3 events_all.py; python3 crash.py; python3 detail.py; python3 strength_gate.py; python3 analyse2.py
python3 classes.py; python3 beat.py; python3 cloud.py; python3 raster.py
python3 overview.py; python3 crashplot.py; python3 detailplot.py               # plots 01-09
```

`drift-engine-out.txt`, `size-out.txt`, `coincide-out.txt`, `frame30-out.txt`, `mixonly-out.txt` and
`hh-bass-out.txt` came from inline snippets of the same `tr.py` helpers; each output file states what it measured.
