# Aurora Borealis 02: stems, musical mapping, latency and performance (Research Agent B)

Brief: `00-brief.md` (Subagent B). Branch `scene/aurora-borealis` at d6f4f07a (main 544254d1 plus the brief).
Research only: no production code changed. Agent A's `01-research-scene.md` did not exist when this was written, so
the scene parameters below have descriptive names (§3.5 maps them onto the existing aurora effect where one fits).

- **Binary:** release `avgen` at 544254d1 (the Living Prism cut, the same engine as this branch), from the
  `av-gen-cut` worktree's build, run read-only. This worktree has no build yet.
- **Probe:** `~/Desktop/av-gen-review/aurora-borealis/research-b/`:
  - `make_probe.py` writes the probe projects;
  - `load.py`, `analyse.py` and `analyse2.py` do the analysis, and their outputs are kept as `*.txt`;
  - two plots: `01-feline-stem-signals.png` (the whole song) and `02-zoom-44-60s.png`.

  Everything is reproducible with the commands in §7. Nothing under `assets/` was copied or committed.
- **Ground truth for the bass:** the owner-approved note list: `bass_notes()` in `~/Desktop/Feline
  Footwear/make_project.py`, 436 notes (the baked clouds of the Feline video, 29 §12-13). The structure map is
  `docs/prototypes/rainbow-world/30-feline-structure-and-plan.md` §2. The owner's 18 swells are 30 §3.

## 0. Summary

1. **Nothing essential is missing from the engine.** Every binding below is a `signal.*` source (ADR-1602) over
   `role.*` (ADR-1630), plus the `feeds` block (ADR-1580/1581). They need no new audio code, no macros and no `env.*`.
   The same bindings will work on live channels unchanged, because `role.*` follows a live input source first.
2. **Use the drum stem's beat grid: `"feeds": {"beat": "role.drums", "hits": "role.drums"}`.**
   - **On the mix,** the offline grid runs at 120.81 BPM. Over the song the phrase pulse drifts from +24 ms to
     +177 ms late against the bar grid. By 150-183 s the kick lands 67 ms before `beat.pulse`.
   - **Fed from the drum stem,** it runs at 120.99 BPM. Phrase pulses stay within 10-24 ms of the bar grid, and the
     kick is a steady +17-33 ms after the pulse.
3. **The kick: fire on the grid and confirm with the class.**
   - **The class is late:** on Feline, `role.drums.kick` lands 1 frame (median) after the drum stem's onset at
     30 fps, and 2 frames in 132 of 406 kicks.
   - **The fast path cannot name a kick:** the onset register reads a low share of 0.00 at the median kick. On the
     onset frame the stem's bass band reads 0.01, so a kick cannot be told on its onset frame (ADR-1620 found the
     same).
   - **`kickWave` instead:** `beat.pulse` gated by "a kick within the last 0.6 s", with the class as a backstop.
     - Measured: 364 fires, precision 0.99, recall 0.89 against the class.
     - It fires one frame (33 ms) before the class at the median, on the attack.
4. **Bass: real note hits, not bass noise.**
   - **The bass stem's onsets are clean:** recall 0.97 and precision 0.91 against the owner-approved notes. They
     land a median 20 ms after the note's foot. The loudest 20% of notes have recall 1.00.
   - **Rainbow World's live note chain reproduces both of the owner's complaints** (the depth is the live level,
     applied every frame):
     - 289 of 465 notes peak 2 or more frames after the onset ("laggy");
     - it adds 5.8 upward steps a second against 2.5 notes a second ("smaller undulations").
   - **The proposed `bassHit` holds its size from the onset frame:** 427 of 465 notes peak on the onset frame, at
     2.5 steps a second.
5. **Song context: absolute stem levels and the synth's brightness, not the adaptive levels.**
   - `level` and `response.*` read each source against its own loudness, so they are flat across sections. The
     drop (0.71) reads lower than the calls (0.77).
   - **`arrangement`,** from the stems' absolute RMS, reads:

     | Section | Intro | Pedal | Calls | Build | Grooves | Drums stop | Synth alone |
     |---|---|---|---|---|---|---|---|
     | `arrangement` | 0.23 | 0.53 | 0.59 | 0.62 | 0.93-0.95 | 0.73 | 0.41 |

   - **`synthBright`** reads 0 in the calls, 0.34 in groove A, 0.65 in groove B, **1.00 in the lift** and 0.67 in
     the pedal.
   - **`response.hatRate`,** fed from the drums, separates groove A (0.16) from the 16th hats of groove B and the
     lift (0.43-0.48).
6. **The synth swells are only half detectable live.**
   - The best latched detector (the synth stem's treble trend) fires 16 times and catches 9 of the owner's 18 swells.
   - The groove swells are filter swells: a change of spectral tilt at a flat level. The auto-gained bands flatten
     them.
   - So the swell is a secondary accent. The palette and slow state come from `arrangement` × `synthBright`, and the
     phrase pulse supplies the structure.
7. **Analysis cost per frame is negligible.**
   - **In the trace:** 51 signal and envelope sources cost 0.01 ms of engine update a frame.
   - **In the live profile (headless, 1080p30):** CPU work p50 is 0.47 ms with the three stems and 0.57 ms without,
     which is within noise. That is the orb scene, the engine's floor, at GPU p50 3.3 ms.
   - **What costs:**
     - load: 2.0 s to analyse the stems, 1.97 s of it the drum classifier;
     - live later: about 0.2 ms of analysis-thread time per 10.7 ms hop for three sources.
8. **Latency:** T10 (≤ 45 ms p50 sound to light) is decided by the picture, not the analysis (ADR-1605).
   - **This scene's biggest lever is a light GPU.** With both latency levers on, Event Horizon (GPU about 11 ms)
     reaches 47.5 / 63.6 ms and Rainbow World (GPU about 24 ms) 59.7 / 73.9.
   - **Author every impact on an onset or grid path**, with a 0 ms attack and no macro hop.
   - **Grid accents might reach T10.** The live beat tracker's phase is unmeasured (§4.3).

## 1. The pipeline (the only one to use)

### 1.1 From sound to signal, per family

At 30 fps, one frame is 33.3 ms. "Same frame" means the family publishes on its input's own frame.

| Family | What it is | Where | Latency (file playback / live) | Cost |
|---|---|---|---|---|
| **Stems** (`assets.stems[]`, ADR-1520) | Files analysed beside the mix and never played. Offline, the whole file is analysed at load and the newest frame at or before the playhead is published | `Engine::setStems`, `StemSignals::publishTrack` | **File:** read at the playhead from windows centred at or before it. The broadband onset lands -3 to +1 frame of the true onset. Measured here: the bass onset is +20 ms (median) after the note's foot | **Load:** 2.0 s for 3 × 210 s stems. The drums classifier is 1.97 s on its own; bass and synth together take 0.52 s. **Per frame:** a lookup, about 1 µs |
| **Live input sources** (ADR-1521) | Channels of the input device, each analysed by `ChannelAnalysis` (one thread) | `audio::CoreAudioCapture`, `analysis::ChannelAnalysis` | **Live:** input buffer 0-2.7 ms, window and hop about 11 ms, then the frame wait (ADR-1605) | **Per hop (10.7 ms):** 16 µs at the levels tier, about 158 µs at the hits tier (a drums source) |
| **Roles** `role.<role>.*` (ADR-1630) | The first **active** source of each role, value for value. A live input wins while one is open, and the file stems then read inactive | `declareRole`, `publishRoles` | Same frame as the source | Copies only |
| **Classes** `role.drums.kick/snare/hat` | The drums source only: the ADR-1060 classifier | `StemAnalyzer` hits tier | **Feline, file, 30 fps:** kick and snare 1 frame after the onset (median), 2 frames for about a third. Hats on the onset's own frame. **Live (ADR-1605):** kick +30 ms and snare +44 ms behind the onset | Included above |
| **Onset register** `.onsetLow/Mid/High` (ADR-1604) | How an onset's energy gain divides between low, mid and high, held until the next onset | `CausalOnsets::registerShares` | Same frame as the onset | 3 floats |
| **Feeds** (ADR-1580/1581/1631) | `beat`, `hits`, `kick/low/snare/hat/onset`, `levels` and `history` from a stem or role instead of the mix | `Engine::rebuildFeeds` | `beat`: offline, the stem's whole-file grid. Live, a causal `BeatTracker` on that source | Live: one beat tracker on the source |
| **`response.*`** (Sonic, ADR-1062) | Hits, envelopes and adaptive levels. Needs a `sonic` block | `SonicRuntime` | Same frame as its feed | Small |
| **`signal.*`** (ADR-1602) | A named conditioned signal. Each input is a route's front half (chain, amount, depth, gate). Inputs combine by `firstActive`, `sum`, `mean` or `max`, then an optional output chain | `ConditionedSignalSource`, the rack's second pass | **Same frame as its inputs**, so a route reading it sees it then. A signal reading another must be listed after it | About 30 ns per chain-frame (B2). It makes the control replay run on every seek |
| **Chain stages** (ADR-011/900/1574/1575) | delay, normalise, **trend** (fast over slow), gain/offset, curve, clamp, threshold, attack/decay, **envelope** (peakhold/linearfall with **refractory/rearm** latch), remap, spring, integrate | `ProcessorChain::process` | An event ramps on its own frame. An N ms attack adds N ms to the full value. Each smoothing stage you stack adds its own lag | ns |
| **`env.*`** (ADR-1601) | An ADSR source | `EnvelopeSource` | Now rises on the trigger frame (it was +1 frame) | Small. **Not needed:** a signal's chain covers it |
| **`macro.*`** (ADR-1573) | Knobs published from the routes' finals of the frame before | `MacroSource` | **+1 frame by construction.** Do not use it on a path | |
| **Control rests** (ADR-1612) | `control.<ch>` publishes a declared rest until its knob moves; MIDI pickup | `ControlMap::channelRests` | Same frame | |
| **`music.*`** (mix) | Causal structure events | `MusicRuntime` | On Feline: `music.drop` 0, `build` 0, `energyRise` 0, `break` 1 (207 s), `section` every 32 s (the counted grid). **Not musical structure here: do not use** | |
| **`sonic.*` / `timbre.*`** (mix) | Sonic Character | timbre pass | **Load:** 138.6 µs per analysis frame (2.7 s for the song); live on the analysis thread with `sonic.live` | Mix only |

**Two traps:**
- **Signal inputs and the stems' `active`.** A firstActive input is active when its source's `<prefix>.active` reads
  above 0.5. A file stem is active over its whole span, silence included: 100% of frames on all three Feline stems,
  195-210 s included. So a silent stretch of a stem never falls back to the mix. The kick stops honestly in the drumless
  outro. A missing stem file falls back.
- **The aurora effect has its own mix-driven audio response (ADR-939).**
  - **What the shader reads:** its per-frame band terms (`audio/bass`, `lowMid`, `mid`, `high`, `beat`), the
    16-bin spectrum (`audio/spectrumShape`) and the glints (`audio/glints`) all read the **mix** from the frame block.
  - **If Agent A uses the aurora effect:** set `audio/sensitivity` to 0, `audio/spectrumShape` to 0 and the glints to
    a constant, and drive it only through routes from the signals below. Otherwise the mix leaks in, which is the
    parallel path the brief forbids.
  - **Its "+ Add aurora" button** installs six `audio.*` routes. Do not use it.

### 1.2 The kick phase-out as it stands (ADR-1620 option B and ADR-1630)

- **A mix has no kick.** `response.kick` fires only from a fed drums source. `audio.onsetLow` has left the bus.
- **`signal.kick`** (`tools/drum_signals.py`) is `role.drums.kick` while a drums-role source is active. Otherwise it
  is `beat.pulse` × 0.7, gated by `signal.drumming` (the mix's hat or snare in the last 750 ms).
- **Re-measured here on Feline:**

  | Run | `signal.kick` fires | Precision / recall against the drum stem's class |
  |---|---|---|
  | Three stems | 406 | 1.00 / 1.00, frame for frame |
  | Mix only | 359 | 0.93 / 0.82 (ADR-1620 measured 0.68 / 0.60 at 60 fps against a stricter window) |

- **The mix's snare and hat are unchanged.** ADR-1620 measured them at 0.93 / 0.73 and 0.85 / 0.94.

### 1.3 How a live scene declares its sources

- **The index entry** has `"live": true`, and the project has `"sonic": {"live": true, ...}`.
  `tests/integration/test_project_system.cpp` ("Examples > Live lists exactly the sonic.live projects") enforces that
  they agree, and that a live entry's project exists in a checkout (stems are local and gitignored).
- **Opening a `sonic.live` project turns live input on (ADR-1025).**
  - While an input is open, `role.*` follows the live input sources (`live.inputSources[].role`,
    `--input-sources <key>@<ch>:<role>`), and the file stems read inactive.
  - The Rainbow World live project has no `assets.audio`; it plays whatever is on the input.
- **The stem-driven reference** is `examples/rainbow-world/blender/music.py`. Its 14 signals read
  `stem.<key>.*` with mix fallbacks.
  - The owner's `~/Desktop/Feline Footwear/make_project.py` re-sources three of them (`signal.scan`, `scanFront`,
    `bassNote`) with baked timelines.
  - Aurora should read `role.*`, not `stem.<key>`, so that a performer's channel keys (`kit`, `in3`) work.
- **Recommendation (to settle in the plan):** follow Rainbow World's pattern. One generator writes two projects that
  share every signal and route:
  - **`aurora-borealis.json` (live):** `sonic.live`, no audio, the index entry `"live": true`;
  - **a stem twin for development and validation:** the Feline mix plus the three stems with roles, the same
    `feeds`, and `sonic.live` false. It is what the traces, the renders and the live profile run.

  When live channels arrive, nothing in the signals changes. `live.inputSources` gives the channels the roles
  `drums`, `bass` and `synth`, and `feeds.beat` / `feeds.hits = "role.drums"` then name the live drum source.

## 2. Feline Footwear's stems, measured

- **The files:** `assets/audio/stems/feline-footwear (Drums|Bass|Synth).wav` and the mix
  `assets/audio/feline-footwear.wav`. All are 48 kHz, 24-bit stereo, 210.50 s long.
- **The stem-set check (ADR-1632):** the three stems sum to the mix with a residual 42.5 dB below it, and no lag is
  found, so they are aligned.
- **The traces:** `--sonic-trace` at 30 and 60 fps. Events were read through envelope sources (instant attack), because
  the trace samples after a frame's events are cleared.

### 2.1 Per stem

| | Drums | Bass | Synth |
|---|---|---|---|
| RMS dB p5 / p50 / p95 (non-silent frames) | -49.9 / -26.7 / -7.7 (42 dB range; a transient source) | -42.0 / -18.5 / -9.3 (33 dB) | -43.3 / -18.1 / -13.8 (30 dB; the bed is flat) |
| Onsets over the song | 1,124 (406 kicks, 235 snares, 546 hats) | 465 (436 owner notes) | 296 |
| Clean | the classes (the reference), `onset`, `response.hatRate` fed from the drums | `onset` (0.97 / 0.91 against the notes), `rms` and `peak` as sizes | `rms` (absolute), `centroid` over bars, `onset` in the intro calls |
| Noisy or misleading | `level` pulses with every hit (feeds guidance: a ripple of 0.65); `bass` band at the onset frame (0.01); the register (low share 0.00 at kicks) | `level` between notes: 1.5 local maxima a second against 2.4 notes; a depth on it re-adds the wobble | the auto-gained `highMid` and `treble` (they saturate at 1.00 at every swell's peak); `onset` in the grooves (2.4 a second: chord stabs) |

### 2.2 By section (121 BPM; bar *n* = 0.2425 + (n-1) × 1.98347 s; 4-bar phrases of 7.93 s)

| Section | s | Kick / snare / hat per s | Bass onsets / s | Synth onsets / s | RMS dB drums / bass / synth |
|---|---|---|---|---|---|
| Intro (drums alone) | 0.2-8.2 | 2.02 / 0.63 / 1.26 | 0 | 0 | -28.8 / silent / silent |
| Bass pedal | 8.2-20.1 | 2.01 / 0.67 / 1.09 | 2.01 | 0 | -27.1 / -21.3 / silent |
| Calls | 20.1-39.9 | 2.02 / 1.06 / 1.16 | 1.82 | 1.57 | -26.1 / -22.9 / -35.1 |
| Build | 39.9-47.9 | 2.01 / 1.64 / 1.89 | 2.39 | 1.01 | -25.3 / -17.4 / -41.1 |
| **Groove A (drop 47.85)** | 47.9-101.4 | 2.35 / 1.25 / 1.90 | 2.56 | 2.41 | -26.7 / -17.8 / -18.4 |
| Fills | 101.4-111.3 | 1.92 / 0.81 / 1.51 | 2.32 | 1.41 | -28.7 / -23.5 / -17.0 |
| **Groove B (111.32)** | 111.3-151.0 | 2.29 / 1.36 / **5.24** | 3.00 | 1.08 | -27.2 / -17.5 / -17.1 |
| **Lift (peak 2:39)** | 151.0-166.9 | 2.27 / 1.64 / **5.80** | 2.77 | 0.76 | -26.1 / -18.5 / -17.5 |
| Pedal | 166.9-182.7 | 2.33 / 2.08 / 4.22 | 2.52 | 2.71 | -25.6 / -17.4 / -17.6 |
| Drums stop (riser) | 182.7-194.6 | 0.08 / 0 / 0 | 1.94 | 0.59 | silent / -21.2 / -14.5 |
| Synth alone | 194.6-210.5 | 0 | 0 | 0.57 | silent / silent / -18.3 |

**Where things are:**
- **Big bass hits.** By the owner-approved list's own strength (scaled to the p95 note), the strongest notes, ≥ 1.0,
  are all in the bass pedal (15) and the calls (7). That is the F pedal that opens the song and that the owner asked
  to see clearly (29 §12).
  - Later, the line is a steady tresillo (2.3-3.0 notes a second), and "big-feeling" hits there are relative.
  - That is why `bassHit` is sized from the note's own peak (§3.3) and not from a song-wide threshold.
- **Drum fills.** Bar 56 at 109.33 s has 1 kick and 7 onsets, against a groove median of 13 a bar. The fill at
  101.4 s (30 §2) keeps its onset count and shows as fewer kicks.
  - `kickWave` stands down about one beat after the kicks stop (600 ms hold), so the fills read as a held breath in
    the aurora. At most one grid pulse past the last kick fakes a kick.
- **Swells:** the owner's 18 (30 §3). The groove ones are filter swells: synth RMS goes from 0.09-0.15 before to
  0.18-0.23 at the peak, and the centroid rises about 0.05. See §2.3.
- **The sixteenth hats** (groove B, the lift and the pedal) are the clearest arrangement change that only the drums
  show.

### 2.3 What detects the swells (the owner's 18; a hit = a fire between onset - 0.6 s and peak + 0.3 s)

| Detector (latched, refractory 4-6 s) | Fires | Hits / 18 | False | Median fire after onset |
|---|---|---|---|---|
| `role.synth.rms` trend (Rainbow World's scan trigger: 8 s / 450 ms, 1.25×) | 8 | 4 | 4 | |
| `role.synth.rms` trend, best of 40 settings | 11 | 6 | 5 | 1.57 s |
| `role.synth.highMid` trend, best | 18 | 10 | 8 | 0.80 s |
| **`role.synth.treble` trend, 4 s / 250 ms, 1.35×** (in the engine, §3.3 `swell`) | **16** | **9** | **7** | **0.45 s** |
| a continuous trend amount (no latch), any band | | | | per-swell peaks 0.04-0.66, while outside the swells p90 is 0.47-0.73: no contrast |

- **The seven unmatched fires:**
  - one is musical: 28.0 s, an intro call the owner did not list;
  - three fall mid-phrase in groove A: 54.2, 60.7 and 68.9 s;
  - 97.4 s;
  - two are in the riser: 180.2 and 192.5 s.
- **The cause:** the bands are auto-gained per stem, and a filter swell changes tilt, not level. A raw share of the
  energy above 2 kHz (30 §1's measure) is not on the bus.
- **Consequence:** the swell stays a secondary accent, and nothing structural hangs on it (§3.2).

## 3. The musical mapping

**The principle (brief §4):** context (sections, energy, brightness) sets the palette and the slow state; events
(kick, snare, bass notes, swells) set the accents.

**The authoring rules that follow from §1-2:**
- **Smoothing lives in the signal only.** Routes carry an amount and no chain. Two stacked smoothers delay and erase
  accents.
- **Accents have a 0 ms attack.** Only the decay is shaped. The owner's laggy-cloud note (29 §13) is the reason.
- **No `macro.*` on any path.** It is +1 frame.
- **Every accent rides an onset or the grid,** never `.level` with a depth. Classes serve only where a bloom hides
  the lag (the snare).
- **A palette is a blend, never a jump.** It changes at the speed of the context signals (1.5 s attack, 3 s decay).
  An optional step of the "look" state happens only on a phrase pulse.

### 3.1 Feeds (the project's top-level block)

```json
"feeds": {"beat": "role.drums", "hits": "role.drums"}
```

- **`beat`:** the drum stem's grid (§0.2). Live, the drum input's causal tracker.
- **`hits`:** `response.kick/snare/hat/hatRate` come from the drum stem.
  - `signal.drumming` (the mix stand-in's gate) then reads the stem's snare and hat.
  - The trace is identical to the unfed run on `signal.kick`: 406 kicks.
- **On a mix-only run, both are one project warning** ("role.drums" names nothing), and the mix feeds as before. The
  warning is accepted; the alternative is a third project.

### 3.2 Context (slow state; palette)

Settings are chain JSON as in `music.py`. `UNIT` = clamp 0..1. Measured medians are per section, from the stem trace.

| Signal | Definition (stems) | Fallback (no source of the role) | Measured | Drives |
|---|---|---|---|---|
| `signal.arrangement` | `sum`, then `UNIT`, of: `role.drums.rms` {gain 100, UNIT, attack 1500, decay 3000} × 0.3; `role.bass.rms` {gain 50, UNIT, 1500/3000} × 0.3; `role.synth.rms` {gain 8, offset -0.1, UNIT, 1500/3000} × 0.4 | a second signal, firstActive, gate `role.synth.active`: `sonic.density.slow` remapped 0.60-0.88 to 0-1 (mix medians 0.61 intro, 0.77 groove A, 0.88 lift) | 0.23 intro; 0.53 pedal; 0.59 calls; 0.62 build; **0.93-0.95 grooves**; 0.73 drums stop; 0.41 synth alone | palette axis A (dusk to full); overall aurora intensity floor; how many curtains read; how high they reach; star visibility (inverse); haze luminance |
| `signal.synthBright` | `role.synth.centroid` {gain 5, offset -2.6, UNIT, attack 1500, decay 3000}, depthSource `role.synth.rms` (depthMin -0.2, depthMax 12: a silent synth reads 0), output `UNIT` | `sonic.brightness.slow` remapped 0.45-0.62 (mix: 0.43 calls, 0.52 groove B, **0.61 lift**, 0.58 pedal) | 0 calls; 0.34 groove A; 0.65 groove B; **1.00 lift**; 0.67 pedal; 0.72 riser | palette axis B (cool indigo/violet to magenta / pale luminous pink at the lift); the aurora's top colour; the upper ribbons' brightness |
| `signal.synthBed` | Rainbow World's `synthStem` on `role.synth.*`: `level` {gain 2.2, offset -0.95, UNIT, 250/900} × 0.3 + `rms` {gain 7, offset -0.8, UNIT, 250/900} × 0.8 + `onset` {linearfall 1.6/s, attack 20} × 0.35, `sum`, `UNIT` | `response.sustain` {gain 2, offset -0.75, UNIT, 250/1200} × 0.9 | 0.31 calls; 0.46 groove A; 0.41 groove B; 0.64 riser; 0.23 outro | ribbon width and persistence; the curtains' flow speed; secondary-colour intensity; base river reflection gain |
| `signal.hatDrive` | `response.hatRate` (fed from the drums) {attack 500, decay 2000} | the same (the mix's hat detector is retained): 0.19 groove A, 0.48 groove B and lift | **0.16 groove A against 0.43-0.48 groove B / lift**; 0 in the outro | fine filament and striation shimmer amount, and sparkle. **Not per hit** |
| `beat.phrasePulse` (the drum grid) | as published (≤ 24 ms from the bar grid) | the mix grid (drifts up to +177 ms) | 27 a song | a slow fold or drift reset per phrase; the only moment an optional look/state step may happen |

**Palette, a suggestion for the art pass** (Agent A or the implementer owns the look). Blend four or five curated
states on the two context axes:
- arrangement low → **dusk indigo** (the intro and outro);
- synth present, brightness low → **violet / lavender** (the calls);
- arrangement high, brightness mid → **electric blue + magenta** (the grooves);
- brightness high → **magenta to pale luminous pink** (the lift);
- the pedal's falling brightness returns towards violet.

Rainbow World's look auto-advance at phrase boundaries (`look.py`, CC 9/10) is the precedent for stepping states on
the grid.

### 3.3 Events (accents)

| Signal | Definition (stems) | Fallback | Measured (Feline, 30 fps) | Drives | Attack / release |
|---|---|---|---|---|---|
| `signal.kickRecent` (helper) | firstActive: `role.drums.kick` {peakhold, **hold 600 ms**, fall 1000/s}; else `signal.drumming`; output {binary at 0.5} | `signal.drumming` | The probe ran at 1,100 ms, which gave 4 phantom grid pulses: two beats after the last kick at 182.77 s, and after the fills. Emulated from the trace's events, 600 ms gives 2 phantoms and recall 0.90 | the gate below | |
| **`signal.kickWave`** | `max` of [`beat.pulse` gated by `signal.kickRecent`, `role.drums.kick`]; output {linearfall 3.0/s, attack 0, refractory 200 ms, rearm 0.05}. The class backs up an off-grid kick, and the refractory stops it doubling the grid pulse | the same on the mix grid, which is `signal.kick`'s stand-in | **364 fires, precision 0.99, recall 0.89;** fires 33 ms **before** the class (p50) and 0 at p90; mix 357 fires, 0.93 / 0.82; stands down one beat after the kicks stop (fills, 182.7 s) with the 600 ms hold | **a broad luminance/colour wave through the lower aurora and into the river.** Drive the wave's travel from the envelope (front = 1 - env, as Rainbow World's scan front), not one flash: per-pixel in the shader, no CPU per element | 0 ms attack; 333 ms ramp. Gone well before the next beat (496 ms) |
| **`signal.snareFold`** | `role.drums.snare` (class) {attack 40, linearfall 3.5/s} | `response.snare` (the mix: 0.93 / 0.73) | 235 snares; the class lands 1 frame after the onset (p50 33 ms, p90 67) | a localised fold, edge brightening, a sharper ripple on the reflection | a 40-150 ms bloom (the owner liked Rainbow World's 150 ms clap bloom, 30 §9), so the class lag reads as the bloom's rise; 280-750 ms fall |
| **`signal.bassSize`** (helper) | `role.bass.peak` {gain ≈ 1.5 (tune; 2.5 saturated at 1.3 in the probe), UNIT, attack 0, decay 500} | absent | | the depth below | |
| **`signal.bassHit`** | `role.bass.onset` {linearfall 2.4/s, attack 0, refractory 130 ms, rearm 0.05} (Rainbow World's `NOTE_CHAIN`), depthSource `signal.bassSize` (depthMin 0.4, depthMax 1.3) | **none.** On a mix no signal finds the notes: the best, `response.onset` sized by its low register, has recall 0.35 and precision 0.58. Leave it absent, so the scene relies on `bassBody` | **465 rises; peaks on the onset frame for 427 of 465** (Rainbow World's chain: 120); recall 0.97, precision 0.91 against the owner's 436 notes (0.95 without the size depth, whose late rises count as extra), +20 ms; size against note strength r = 0.38 (Rainbow World's chain: 0.01) | **the amplitude/depth of the big lower-curtain folds; a broad wave; the intensity of aurora colour in the river.** No geometry change on the mountains | 0 ms attack; fall over about 0.4 s; notes ≥ 130 ms apart retrigger (the line's closest is 150 ms) |
| `signal.bassBody` | `role.bass.level` {gain 2, offset -0.8, UNIT, attack 150, decay 800} | `response.bass`, same chain | 0.87 local maxima a second in groove A (smooth); 0 in the intro and outro | slow ribbon fold scale; lower-curtain brightness; reflection colour intensity: the continuous bass layer under the hits | slow on purpose: the owner's "smooth out reactivity" |
| `signal.swell` | `role.synth.treble`, gate `role.synth.active`, {trendSeconds 4, trendFastMs 250, trendFloor 0.1, threshold binary 1.35, attack 0, decay 400, peakhold hold 1500, fall 0.6/s, refractory 6000, rearm 0.3}, output `UNIT` | `response.sustain` with Rainbow World's mix trigger (8 s / 1000 ms, 1.1×), or none | 16 fires, 9 of the owner's 18 (§2.3); fires 0.45 s after the swell onset (median) | the ribbons spread and brighten (bloom); push palette axis B a step | about 1.5 s hold and 1.7 s fall; it may add a spring (0.5 Hz) as the scan does |

**What is deliberately not used:**
- per-hit hat flashes (`hatDrive` instead);
- `role.synth.onset` in the grooves (2.4 a second);
- the onset register (useless on this drum stem);
- `music.*`;
- `.level` with a depth on any accent.

**Isolation (brief Milestone D: "isolate stem contributions using existing patterns"):** use control channels with a
rest (ADR-1612).
- **The channels:** `"control": {"channels": [{"name": "drums", "rest": 1}, {"name": "bass", "rest": 1},
  {"name": "synth", "rest": 1}]}`, plus MIDI/OSC bindings.
- **Where they act:** as `depthSource` (0..1) on each stem's signal inputs. That is one place per stem, not per route.
- **The result:** muting a part is one knob, and every channel rests at 1 when a project loads.
- **Verification** is `--sonic-trace --trace-finals`, not a visual overlay. Nothing is left on screen.

### 3.4 With the mix only, and with stems missing (all measured, except the live column)

| Condition | Kick | Snare | Hats | Bass | Synth / context | Beat |
|---|---|---|---|---|---|---|
| 3 stems | `kickWave` 0.99 / 0.89 | the class | `hatRate` fed from the drums | `bassHit` and `bassBody` from the stem | `arrangement`, `synthBright` and `synthBed` from the stems | drum grid |
| **Mix only** | the grid stand-in gated by `drumming`, 0.93 / 0.82 | `response.snare` | the mix's `hatRate` (values match the fed ones within 0.05) | no hits; `bassBody` = `response.bass` (its medians fall from 0.72 in the intro to 0.25 in groove B: adaptive) | `sonic.density.slow` and `sonic.brightness.slow`; `response.sustain` | the mix grid (drifts; one feed warning) |
| No drum stem | as the mix | as the mix | as the mix | stem | stems | mix |
| No bass or synth stem | stem | stem | stem | as the mix | as the mix (the `arrangement` fallback is gated on the synth's `active`) | stem |
| A stem present but silent | the stand-in stands aside (ADR-1630: 0 kicks); the source is active | | | | the stems' absolute RMS reads the silence honestly | |
| **Live channels (later)** | `role.drums` = the first drums input; the classifier always runs | same | same | `role.bass` input | `role.synth` input | the input's causal tracker (unmeasured) |

### 3.5 Where this lands on the existing aurora effect (if A uses it)

The keys are the effect's: `src/world/effects/kinds/aurora_effect.cpp`.

| Musical idea | Candidate parameters |
|---|---|
| `kickWave` | `appearance/edgeBrightness`, or a wave-front parameter the effect lacks today. A travelling wave needs a phase input: a small, justified shader addition, or the ribbon jitter |
| `bassHit`, `bassBody` | `shape/waveAmplitude`, `shape/curtainHeight`, `appearance/intensity` (the lower layer) |
| `snareFold` | `shape/turbulence` (a brief kick), `appearance/edgeBrightness` |
| `hatDrive` | `appearance/filaments`, `appearance/sparkle` |
| `synthBed` | `shape/ribbonWidth`, `shape/flowSpeed`, `appearance/opacity` |
| `synthBright`, `arrangement` | `appearance/lowColor`, `midColor`, `topColor` (the palette blend), `appearance/horizonGlow`, `shape/curtainCount` (stepped on a phrase only: it is discrete) |
| River and terrain | whatever reflection and tint parameters A chooses, from the same signals. The palette must stay coherent across sky, water and land |

## 4. Performance and latency

### 4.1 Analysis-side CPU cost of the bindings (measured)

| Where | Measurement | Result |
|---|---|---|
| Control layer per frame | `--sonic-trace` on the probe: 51 sources (the 4 `drum_signals` sources, 29 candidate signals, 18 envelope markers) | engine update **0.01 ms** a frame; Sonic 1.5 µs a frame |
| Real frame loop | `--live-profile --mode headless`, 1920x1080, 30 fps, `--start 60`, 150 warm-up and 450 measured frames, under `tools/gpu-lock.sh`, orb scene | CPU work p50 / p95 / p99: **0.47 / 1.57 / 2.09 ms with the 3 stems**, 0.57 / 1.14 / 3.42 without. The same within noise. GPU p50 3.3 ms (the engine floor at 1080p) |
| Route evaluation | B2 (02 §3.2) | about 30 ns per chain-frame; 1,000 routes cost 0.04 ms |
| Load | the log | stems analysed in 2.0 s (the drums classifier 1.97 s; bass and synth 0.52 s); stem-set check 41-43 ms; project load 6.7 s, mostly the mix's audio and the timbre pass (2.7 s) |
| Memory | ADR-1631/1632 | about 3.4 MB probe and about 5 MB spectrum rows per 210 s stem |
| Live channels (later) | ADR-1521 | per 10.7 ms hop: about 158 µs (drums, hits tier) + 2 × 16 µs (bass, synth) + one beat tracker. About 2% of one core, **on the analysis thread**, not the render thread |
| Seek | ADR-1602/1168 | any `signal.*` makes the control replay rebuild the rack from 0 on every seek: about 51 chains × 5,400 frames to 3:00. A few ms of chains [est]; measure it in the UI (§5) |

**Conclusion:** the bindings cost nothing the frame budget can see. The scene's budget is the GPU, which is Agent A's
side. The scene must never regenerate geometry for a musical response: every accent is a uniform.

### 4.2 Sound to light for the proposed accents

**Measured anchors (ADR-1605/1609/1610; live, 1080p30, M2 Max, 60 Hz):**

| Scene | GPU span | Path | Light p50 / p95, both latency levers on |
|---|---|---|---|
| Rainbow World | about 24 ms | `response.onset` | **59.7 / 73.9** ms |
| Event Horizon | 11-13 ms | `response.onset` | **47.5 / 63.6** ms |
| Rainbow World, before the levers | | the kick class / the snare class | 103 / 122 and 119 / 129 ms |
| Rainbow World, before the levers | | `stem.drums.onset` | 73.1 / 91.3 ms |

- The audio side, sound to bus, is about 28 ms p50.
- The rest is the frame:
  - the wait for the next update: 0-33 ms, mean 16.7;
  - update to present: 4.7 ms on Rainbow World with late latch;
  - the GPU span;
  - the vsync wait: 3.1 ms p50 with ADR-1609.

**Estimates for this scene, at a GPU span like Event Horizon's [est; to be measured, §5]:**

| Accent | Path | File playback (measured, trace) | Live [est] |
|---|---|---|---|
| `kickWave` (grid) | `beat.pulse` from the drum source's grid | on the attack: 1 frame before the class, +17-33 ms after the grid pulse | no analysis delay (the tracker predicts). The frame wait plus the pipeline: about 30-45 ms if the live tracker's phase holds. **It may meet T10.** Unmeasured: the rig's periodic probe makes the tracker lock to it (ADR-1605 did not report the grid) |
| `kickWave` (class backstop, off-grid kicks) | `role.drums.kick` | +1-2 frames after the onset | about 75-85 ms |
| `bassHit` | `role.bass.onset` | +20 ms after the note's foot | the onset path: about 47-55 ms |
| `snareFold` | the class | +1 frame after the onset | about 90 ms to the attack; the bloom's 40 ms rise is perceptually part of it |
| context signals | | seconds, by design | the same |

**What this scene can do about latency, in order of effect:**
1. **Keep the GPU span short.** The step from Rainbow World to Event Horizon (24 to 11 ms of GPU) is worth about 12 ms
   at p50: the largest lever a scene owns.
   - ADR-1611's caveat: at 30 fps Apple's governor stretches a heavy frame to fill the period, so pinning a lower
     ladder level does not buy latency. Only genuinely light work does.
   - **Suggested target:** headless Ultra GPU p50 ≤ 12-15 ms. That also gives the brief's headroom: 33.3 ms for the
     whole engine.
2. **Author impacts only on onset and grid paths**, with a 0 ms attack and no macro hop (§3).
3. **Keep the editor UI light while performing.** Late latch (ADR-1610) already moves the UI's time before the
   update, but a heavy UI still lengthens the CPU frame.
4. **Optional, needs the owner's decision:** a phase lead for grid accents in live mode only.
   - **What it does:** `kickWave` would read `beat.phase` with a threshold (as Rainbow World's grass kick does,
     `music.py` l. 123-140) shifted by L ≈ 0.1 beat, so that the wave peaks on the heard beat after the pipeline's
     about 50 ms.
   - **What it costs:** a project-level number that offline renders must not use; it would lead there.
   - **Engine support:** none (there is no latency compensation, ADR-1025). A control channel or a live/offline
     project difference could hold L.
5. **Not available to a scene:** 60 fps (owner's 30 fps rule), a smaller analysis window (detector retuning, M), and
   input buffer changes (ADR-1550).

### 4.3 Profiling plan and acceptance tests ("sustained 30 FPS at 1080p")

**Conditions for every number:**
- the M2 Max, on AC power, with the **60 Hz LG UltraFine** connected. It was connected and on AC on 2026-10-24.
  - On the built-in 120 Hz panel the miss counter is not comparable with the baseline (ADR-1610's failed re-check).
- a release build;
- `tools/gpu-lock.sh` around every `avgen` run, held briefly: Agent A shares the lock.

**The tools:**
- `tools/ci/live-perf.sh`;
- `tools/live_perf_baseline.py` (run, check and write);
- `docs/perf/live-baseline.json` (written 2026-10-23, c7a211f5);
- `avgen --live-profile`, whose `--trace` gives per-frame `frameMs`, `gpuMs`, `cpuWorkMs`, the per-pass
  `sceneMs`/`depthMs`/`shadowMs`/`cullMs`/`otherMs`, triangles and draw calls (ADR-1571).

**Steps:**
1. **Baseline before the scene (P0).**
   - Run `tools/ci/live-perf.sh --reps 3 --out <dir>` to establish today's numbers for Rainbow World, THE RIFT and
     Event Horizon on this machine.
   - Run the orb probe's headless profile as the engine floor: measured here at GPU p50 3.3 ms and CPU work p50
     0.47 ms.
   - Record the revision.
2. **At each milestone (A static, B aurora, C reflections, D stems):**
   - **Headless Ultra:** `--live-profile --mode headless --quality ultra --warmup-frames 150 --measure 15 --size
     1920x1080 --target-fps 30 --trace <csv> --json <json> --start <t>`.
   - **The windows:** 60 s (groove A, the tool's default), 150-166 s (the lift: brightest aurora, maximum `synthBright`
     and hats), 107-113 s (the build into B, with a swell and a fill), 183-194 s (riser), and 20-40 s (calls: a dim
     aurora).
   - **Record:** GPU p50, p95 and max, CPU work p50 and p99, the pass breakdown, triangles and draw calls.
   - **Force the high-cost states** with the isolation channels at 1 and a `--start` inside the lift.
3. **Live Auto, 3 interleaved reps:** `--live-profile --mode live --quality auto --warmup 5 --measure 30` at 60 s and
   at 152 s. Then **one whole-song run** of 210 s, for spikes and growth.
4. **Add the scene to `SCENES` in `tools/live_perf_baseline.py`**, as the stem twin, so the stems path is the one
   measured. `write` the baseline only on purpose, after review.

**Acceptance (all measured, none estimated):**

| # | Test | Threshold |
|---|---|---|
| P1 | live misses per 900 frames (T1), median of 3 | ≤ 5 (target ≤ 2) |
| P2 | live frame interval p99 (T2) | ≤ 40 ms |
| P3 | share of live frames at Medium or better (T3) | ≥ 90%. **Target: Ultra or High ≥ 90%**, so the ladder is not hiding the cost |
| P4 | headless Ultra GPU (T4) | p50 ≤ 29.3 and p95 ≤ 1.5 × p50 (the gate). **Target: p50 ≤ 15 ms** (headroom and latency) in every window above, the lift included |
| P5 | CPU work | p99 ≤ 5 ms. No per-frame geometry regeneration: triangles and draw calls constant over a run |
| P6 | whole song | no upward trend in frame time over 210 s (slope of a linear fit < 0.5 ms per minute); max interval reported |
| P7 | switching | load / play / unload 10 times (`test_project_switch_gpu.cpp` pattern): no growth in resident memory or frame time |
| P8 | regression | `live-perf.sh` passes T9 against the baseline that includes Aurora |

### 4.4 Signal acceptance tests (trace-based, headless, no GPU)

Patterned on `tests/unit/test_rainbow_world_music.cpp` and `[adr1602]`. Run on the stem twin at 30 fps, the whole song.

| # | Test | Expectation (measured in this probe) |
|---|---|---|
| S1 | no route reads `stem.*`, `role.*`, `response.*`, `audio.*` or `macro.*` directly: every musical input is a `signal.*` | structural |
| S2 | `kickWave` against `role.drums.kick` | precision ≥ 0.95, recall ≥ 0.85, not later than the class (probe: 0.99 / 0.89, -33 ms) |
| S3 | `bassHit` against the baked note list | recall ≥ 0.95, precision ≥ 0.9, peak on the onset frame for ≥ 90% (probe: 0.97 / 0.91, 92%) |
| S4 | no stuck values | every accent returns below 0.05 within 1 s of its last trigger; `kickWave` is 0 from one beat after the last kick (183.3 s) to the end |
| S5 | no excessive triggering | `bassHit` ≤ 1 trigger per 130 ms; `swell` ≥ 6 s apart; `kickWave` ≤ 1 per 200 ms |
| S6 | context | `arrangement` groove medians ≥ 0.85 and intro / outro ≤ 0.45; `synthBright` lift median ≥ 0.8 and calls ≤ 0.1 |
| S7 | mix-only and missing-stem twins load | only the expected `feeds` warning; S2-S4 hold for the mix's targets (the stand-in kick ≥ 0.9 precision) |
| S8 | seek equals play | the `[adr1573]` pattern on the scene: every final identical after a seek |
| S9 | `--audit-routes` | 0 dead routes. Note that the audit does not see dead chains inside a signal (ADR-1602) |
| S10 | sound to light | `tools/sound_to_light.sh <aurora live project> <out>`, with `kickWave`, `bassHit` and `snareFold` added to `sound_to_light.py`'s path list. Report p50 / p95 against T10 and against Event Horizon. The grid accent needs its own method (§4.2) |

## 5. Risks

1. **The live beat tracker is unmeasured on the drum input.**
   - `kickWave` depends on the grid's phase. The trace measures only the offline whole-file grid.
   - If the causal tracker wanders, the grid pulses are early or late. The class backstop only covers off-grid kicks.
   - **Mitigation:** measure the phase error live (kick class minus `beat.pulse`) in a `--sonic-live-log` run of the
     drum stem played into BlackHole. If it is poor, fall back to plain `signal.kick` (class, +1 frame).
2. **The swell is only about 50% detectable.**
   - If the art leans on it, half the owner's swells will be missed.
   - Keep it secondary. A genuine fix is an unnormalised high-frequency share (a spectral tilt) per stem on the bus, a
     small analysis addition. Only do it if the owner wants swells as a primary event.
3. **`feeds` warnings on a mix-only run.** Accepted noise; the mix then feeds everything.
4. **The aurora effect's built-in mix response (ADR-939)** must be zeroed, or the mix leaks into a stem-driven scene.
5. **A depth from a live level re-introduces the noise** (§0.4). Any new accent must use a held size, not `.level`.
6. **The GPU governor:** a heavy aurora pass both risks T3 and raises latency. The light-GPU target is load-bearing
   for both.
7. **Hidden dead chains.** The route audit does not look inside `signal.*`. S4-S6 are the guard.
8. **Seek cost** grows with signal count and song position. Measure the scrub at 3:00 in the UI, with the owner's
   memory of 90 s re-simulations in mind.

## 6. Open questions for the coordinator / owner

1. **Two projects** (a live project, plus a stem twin that plays the Feline mix with stems), as Rainbow World does?
   Where does the twin live: Examples > Worlds, or development only?
2. **A phase lead for grid accents live (§4.2, item 4):** wanted? It means a live-only number in the project.
3. **Swells:** secondary accent only, or worth the small analysis addition (an unnormalised spectral-tilt feature per
   stem) to make them primary?
4. **Should a palette state step on phrase pulses** (discrete looks), or blend continuously on the two context axes
   only?
5. **Agent A's parameter names:** the table in §3.3 uses descriptive targets. It needs one pass to bind them once
   `01-research-scene.md` exists.

## 7. Reproduce

```sh
R=~/Desktop/av-gen-review/aurora-borealis/research-b
BIN=<a release build at 544254d1 or later>/src/avgen
python3 $R/make_probe.py <this worktree> $R/proj      # writes stems3, mix, no-drums, drums-only
# add "feeds": {"beat": "role.drums", "hits": "role.drums"} to proj/stems3.json (as analysed)
for p in stems3 mix no-drums drums-only; do
  $BIN --project $R/proj/$p.json --sonic-trace $R/$p-30.csv --fps 30
done
$BIN --project $R/proj/stems3.json --sonic-trace $R/stems3-60.csv --fps 60
cd $R && python3 analyse.py && python3 analyse2.py
# the frame-loop cost (GPU, brief):
tools/gpu-lock.sh $BIN --live-profile --mode headless --project $R/proj/stems3.json \
  --audio assets/audio/feline-footwear.wav --no-audio --target-fps 30 --size 1920x1080 --start 60 \
  --warmup-frames 150 --measure 15 --json lp.json --trace lp.csv
```
