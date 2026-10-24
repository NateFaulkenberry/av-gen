# Aurora Borealis 12: musical reactivity and performance for pass 2 (Research Agent B)

Brief: `10-brief-pass2.md` §4-8, §10 and §12 (Subagent B). Branch `scene/aurora-borealis` at 44aefe92. This is research
only: no production code changed. Agent A's `11-research-pass2-render.md` did not exist when this was written, so the
scene-side parameters below have descriptive names (*river pulse slot*, *edge kick gain* and so on). Bind them in one
pass once A's names exist.

- **Binary:** `build/release/src/avgen` in this worktree, built from 02d93bf6. Every later commit on the branch is docs.
- **Evidence:** `~/Desktop/av-gen-review/aurora-borealis/pass2-research-b/`:
  - `make_probe.py` writes the probe: the live project plus every candidate signal below, using only existing chain
    stages;
  - `analyse.py`, `analyse2.py`, `hat_ref.py`, `nov.py`, `kick_times.py`, `s2l_grid.py` and `plot.py`, with their
    outputs `*-out.txt`;
  - `perf/`: the headless and live profiles and the per-phase CPU traces;
  - `s2l-grid/`: the live drum-loop run;
  - two plots: `01-pass2-signals-and-comets.png` and `02-river-ring-60-68s.png`.
- **The stems** were read in place through symlinks, decoded into memory and never copied.
- **Conditions of every GPU and live number here:**
  - battery power and the built-in 120 Hz panel. The LG UltraFine was not connected, and the Mac ran on battery
    throughout (`perf/conditions.txt`).
  - So the **live** numbers here are not comparable with the pass-1 acceptance table. They are valid as A/B comparisons
    within one run, and the CPU diagnosis (§5) does not depend on the display.
  - Headless numbers do not depend on the display.

## 0. Summary

1. **The pass-1 signal map is sound, and every new effect can be bound to it with existing chain stages.** The new
   effects need new *combinations* (a ring of two river slots, a comet cue, a beat-indexed edge subset), not new
   analysis. The binding table is in §3.
2. **A real defect in pass 1: the palette flickers at frame rate.**
   - **What:** `signal.synthBright` takes the raw per-frame `role.synth.rms` as its depth (depthMax 12). In groove B,
     `palette/position` moves a median **0.087 per frame**, with a p99 of 0.39 and a maximum of 0.59.
   - **Fix:** a smoothed presence as the depth. It cuts the per-frame movement 60-fold (p99 0.006) and keeps the
     ordering of the sections (§2.6).
   - **Why it matters for pass 2:** the synth's colour feeds the edges, the water and the pulses, so fix this first.
3. **River pulses: a ring of two slots.**
   - **The rule:** a kick launches a pulse only when a slot is free and the other pulse is at least half-way along.
   - **The result on Feline:** one launch a second (half time), at most two pulses alive, both alive 57% of the time.
   - **Why not one slot:** the kicks are 0.50 s apart (p50). With one slot, either the pulse is shorter than a second
     (which barely reads as travel) or three of four kicks are ignored.
   - **Why not three:** a third slot would only be needed for a pulse life over 2 s.
   - **Caps:** the snare adds a short flare to the pulses already in the river, never a new full pulse, and the summed
     glow is clamped.
4. **The comet: one cue signal, three section detectors and a phrase fallback.** It fires 5 times on Feline:
   - **48.27 s:** the drop;
   - **87.53 s:** groove A, from the fallback;
   - **114.6 s:** groove B, when the hats double;
   - **151.0 s:** the lift, from the fallback;
   - **185.33 s:** the drums stop.

   The cooldowns are 20 s on the cue and 8 s between comets, so at most one comet is alive. The trigger is
   deterministic offline. The lift is not detectable on the stems (§2.4), so the fallback catches it on time; that is a
   timer landing on a phrase boundary, not detection.
5. **Latency, measured live with the Feline drum stem on the loopback.** Pass 1 left this path unmeasured.
   - **The grid path is no faster than the onset path at the median**, and it is worse in the tail:
     - grid (`beat.pulse`): bus p50 +25-30 ms;
     - onset: bus p50 +28-32 ms;
     - `kickWave`: light p95 128-132 ms against 83 for the onset.
   - **Why:** the live tracker phase-locks to the hops on which onsets are *detected*, so its predictions inherit the
     onset delay. And it lands only 75-80% of the kicks within ±150 ms; the rest fall back to the class, which runs
     +40 ms after the onset.
   - **What can be cut:**
     - the snare accent's 40 ms attack (stacked smoothing);
     - the class on any sharp accent;
     - the CPU's slow regime (point 6).

     The frame pipeline (the frame wait, the GPU and vsync) is still about 2/3 of the total.
6. **The live CPU p99 is not work. The main thread runs about 2.4× slower in runs of several seconds.**
   - **The evidence:**
     - every phase slows together (correlation 0.85-0.96 between phases);
     - a slow frame is followed by a slow frame (autocorrelation 0.81);
     - with one other core kept busy (`yes > /dev/null`), the editor's CPU work goes from p50 2.75-5.6 / p99 8.1-9.0
       ms to **p50 2.2 / p99 4.7 ms**, and frames over 4.5 ms go from 32-58% to 1%.
   - **The cause:** the main thread's core placement and clock at a 10% duty cycle. Apple Silicon schedules by QoS and
     load.
   - **The fix:** not a scene budget. The engine should mark its frame threads for performance (§5.3). Headless CPU
     work already meets the 5 ms p99 (3.06 ms at 60 s and at 152 s).

## 1. What the research says, and what this engine already has

Sources are listed at the end, by number.

### 1.1 Separate context from events, and give each a different job

**The practice.**
- **Synesthesia** separates audio into four families: Levels (smoothed), Hits (transients), Presence (trend: "tracks
  rising or falling activity without reacting to each individual sound") and Time (accumulating clocks that run faster
  when a band is loud) [1].
- **Its guidance:** drive motion with clocks, not with raw levels.
- **VJ writing:**
  - an energy/beat/structure/stem ladder;
  - "do not make every beat trigger the same flash";
  - "let quieter sections breathe: constant motion makes the drop feel smaller" [2].

**This engine already has every one of those families:**

| Family | Synesthesia [1] | AV Gen |
|---|---|---|
| Levels | `syn_BassLevel` | chain `attackMs`/`decayMs` on `role.*.rms/level` (`arrangement`, `bassBody`, `synthBed`) |
| Hits | `syn_Hits` | `role.drums.kick/snare/hat`, `role.*.onset`, envelopes (`linearfall`, `peakhold`) |
| Presence | `syn_Presence` | the chain's **trend** stage (ADR-1575): fast over slow, floored |
| Time | `syn_BassTime` | the chain's **integrate** stage (ADR-1041): a rate becomes a phase that never jumps |
| Beat clock | `syn_BeatTime`, `syn_BPMTwitcher` | `beat.count`, `beat.phase`, `beat.bar`, `beat.phrasePulse` |

**What that means for pass 2:**
- **Context sets state:** palette, flow strength, edge intensity floor.
- **Events set accents:** the kick, snare and bass note.
- **Structure sets rare events:** comets, and phrase-gated changes.
- **A speed is never routed directly:** a varying speed goes through `integrate`. Pass 1 E4 learned that routing a
  speed makes the phase jump.

### 1.2 Cooldowns, hysteresis and rare triggers

**The research.**
- **Online onset detection:** a peak must exceed a local threshold *and* come at least a minimum interval after the
  previous one (the `wait` of Böck, Krebs and Schedl's online peak picker, used by librosa) [3].
- **Hysteresis (a Schmitt trigger):** fire high, re-arm low, so a value hovering at the threshold cannot chatter [4].
- **Structural boundaries:** found by *novelty*, meaning a change of texture relative to the recent past (Foote's
  checkerboard kernel on a self-similarity matrix) [5].
  - That is non-causal, but the causal analogue is a short average over a long one, which is exactly the trend stage.

**This engine already has all three:**
- the refractory period (`envelopeRefractoryMs`);
- the re-arm level (`envelopeRearm`, ADR-1574);
- the trend stage.

So a rare trigger can be authored as "novelty (trend) → binary threshold → latched envelope with a long refractory".
§2.4 measures candidates built exactly that way.

### 1.3 Perceptual tolerance: how late a light may be

- **The asymmetry:** people tolerate light that comes *before* the sound better than light that comes after it.
- **Simultaneity window:** for young listeners, roughly 50 ms of audio lead to about 150 ms of visual lead is judged
  simultaneous [6].
- **For a flash and a beep:** the audio-lead detection threshold was 79-111 ms, depending on the stimulus (van Eijk et
  al. 2008, cited in [6]).
- **Broadcast (ITU-R BT.1359):**
  - sound *leading* the picture becomes **detectable at about +45 ms** and **unacceptable at about +90 ms**;
  - sound lagging becomes detectable at -125 ms and unacceptable at -185 ms [7].
  - ATSC IS-191 tightens this to +15 / -45 ms at the encoder input [7].
- **AV Gen always shows light after sound**, so it sits on the sensitive side.
- **The 45 ms target (T10, ADR-1605)** is BT.1359's detectability bound, and acceptability is about 90 ms.
- **Consequences:**
  1. A sharp, high-contrast impact should ride a path whose light p50 is under about 70 ms and whose p95 is under about
     90 ms. On this rig that is the onset path.
  2. An event 100-130 ms late, such as the drum class, should drive only soft or slow responses: a bloom rise, a
     travelling front, a colour shift.
  3. Rare slow events such as comets, palette steps and pulse launches with a soft front are insensitive to tens of
     milliseconds.

### 1.4 Over-reaction and flash fatigue

**Habituation.**
- A repeated transient at the same location stops capturing attention (short-term habituation of onset capture). A
  novel event stays salient [8].
- So an accent on every beat on every edge fades from attention within seconds, while an accent that moves (a
  different subset each beat) or is rare keeps its weight.
- This is the research behind the brief's "avoid having every detected event illuminate every edge".

**Safety (WCAG 2.3.1).**
- **The rule:** no more than **3 general flashes in any one second**, where a flash is a pair of opposing changes of at
  least 10% relative luminance over a combined area above about 25% of a 10° field (.006 sr) [9].
- **Saturated red has its own, stricter rule.** Rose Lift's low colour (#ff6fd0) is pink, not saturated red, so it does
  not count.
- **The risk here:**
  - Feline's kick is 2.0-2.35 a second and its snare 0.8-1.6.
  - If both drove a large-area luminance swing, the combination would exceed 3 a second.
- **Measured on the pass-1 final clip (44-62 s, the build into the drop):**
  - frame-to-frame full-frame relative luminance change max 0.014;
  - largest swing within any 267 ms window 0.018 (`luma-final-clip.txt`).
- **Rules that keep it far below:**
  - large-area accents (the aurora pulse, edges at broad scale, the water) stay under 0.05 relative luminance swing;
  - only small-area accents (the snare's edge subset, hat sparkle, the comet head) may swing harder.
  - §6 adds the same luminance audit as a gate on every stage's clip.

### 1.5 Prediction and latency in the beat tracker

- **Causal beat trackers predict the next beat** (BTrack: `beatDueInCurrentFrame`) [10]. This engine's `BeatTracker`
  does too: `nextBeatTime_`, with a phase-locked loop.
- **But its phase locks to the hop on which an onset is detected** (`beat_tracker.cpp` l. 435-443). So its predictions
  carry the same detection delay as the onset itself.
- **Prediction removes the analysis delay only if it is told to lead.** That is pass 1's "phase lead" option. The
  coordinator declined it to keep offline and live identical.
- §4 measures this: grid and onset land within 5 ms of each other at the median.

## 2. Audit of the pass-1 signals against the Feline stems

The trace is the live project plus the candidates: `--sonic-trace --trace-finals --fps 30`, three stems, drum feeds.
The trace runs to 438 s, the camera loop; only 0-210.5 s is used. Section boundaries are 30 §2.

### 2.1 What is clean (re-measured; pass 1's numbers reproduce exactly)

| Signal | Count / rate | Quality | Use in pass 2 |
|---|---|---|---|
| `role.drums.kick` | 406 | the reference (four on the floor: 88-89 on every beat of the bar, `analyse-out.txt` §A) | backstop only (+40 ms live) |
| `signal.kickWave` | 362 rises; 2.0 a second in every groove | 0.99 / 0.89 against the class (pass 1) | the kick accent and the river launch |
| `role.drums.snare` → `signal.snareFold` | 235; 216 rises | the class; 66% on beats 2 and 4, 14% on the 16th before beat 2 | the snare accent (drop the attack, §4.3) |
| `role.drums.hat` | 546 | precision 0.92-0.98 against an offline HF-flux reference; 98% within 40 ms of a 16th | selective sparkle, gated to the 16th-hat sections (§2.3) |
| `response.hatRate` → `signal.hatDrive` | 0.17 groove A, 0.44-0.50 groove B / lift / pedal | the clearest drums-only texture change | sparkle amount; the comet's texture detector |
| `signal.bassHit` | 2.45 a second (groove A) | recall 0.97 (pass 1); 92% peak on the onset frame | edge bass wave front; water highlight strength |
| `signal.bassBody` | 0 in the intro and outro | smooth (0.87 maxima a second) | broad edge intensity; pulse width |
| `signal.arrangement` | 0.23 intro → 0.93-0.95 grooves → 0.41 outro | per-frame change p99 0.003 | intensity floors; the comet gate |
| `signal.synthBed` | 0.31 calls, 0.46 groove A, 0.64 riser | p99 0.21 a frame (the onset term) | water persistence |
| `signal.synthBright` | lift 1.00, calls 0 | **jitters: p99 0.36 a frame (§2.6)** | colour, **after the fix** |
| `beat.phase` / `beat.count` | offline: the drum grid | exact integers; bar-aligned offline (§2.5) | edge subset selection (§3.2) |

### 2.2 Snare: is there anything faster than the class?

- **The class lands one frame after the drum onset** (p10/50/90 33 / 33 / 67 ms).
- **On the onset frame itself, the stem's `highMid` band separates snares:** p50 0.61 at snare onsets against 0.33
  elsewhere.
- **Gated by the backbeat** (the bar phase within ±0.055 of beats 2 and 4), "onset with `highMid` > 0.43" has
  precision 0.90 and recall 0.52.
- **As a two-tier signal** like `kickWave` (`snareFast` = max[onset gated by backbeat and bright band, the class],
  refractory 150 ms):
  - recall 0.93 and precision 0.95 against the class;
  - **one frame (33 ms) earlier for 54% of snares.**
- **But live the bar phase is arbitrary.**
  - The live clock counts beats from the moment the tracker locks (ADR-1581), so beats 2 and 4 are a coin toss.
  - With the wrong parity it would fire on kick onsets with a bright click.
- **Recommendation:**
  - Do **not** use the backbeat tier live until the engine has a bar-phase alignment for live input.
  - Keep the class for the snare, and remove its 40 ms attack (§4.3).
  - Offline-only use is possible, but it would make live and offline differ.

### 2.3 Hi-hat: is the detector good enough for sparse sparkle?

- **Against an offline 7-16 kHz flux reference** (`hat_ref.py`):
  - **precision 0.92-1.00 in every section**;
  - recall is 0.15-0.31 in the eighth-hat sections and 0.56-0.74 in the 16th sections.
  - The reference also counts other drums' high-frequency energy, so its recall is a lower bound.
- **Timing:** 98% within 40 ms of a 16th, and on the onset's own frame (pass 1).
- **Verdict: good enough for restrained sparkle.**
  - When it fires it is a hat, on time.
  - It under-fires in the sparse sections, which is the thinning the brief wants anyway.
- **The rules:**
  - gate the sparkle by `hatDrive` (≥ 0.3, so only groove B, the lift and the pedal), never per hit everywhere;
  - choose the edges by 16th position (§3.2), so consecutive hats light different edges;
  - cap it at a handful of edges, small-area only.
  - Measured `hatSpark` (refractory 100 ms, fall 6 a second) rises 1.85 a second in groove A and 3.5-3.6 a second in
    groove B and the lift. It is small-area, so WCAG's area exemption covers it.

### 2.4 Comet: candidate triggers and their times on Feline

All candidates are built from existing stages. Times are when the latched signal crosses 0.5. Section names are 30 §2.
"Big moments" per the owner's own scan map (30 §3, "full" scans) are the drop at 47.85 s, the 1:47-1:51 build into
groove B, and 2:39 (157-162 s).

| Candidate | Definition (stems) | Fires on Feline (s) | Verdict |
|---|---|---|---|
| C1 phrase, latched 30 s | `beat.phrasePulse`, refractory 30 s | 0.3, 32.0, 63.7, 95.5, 127.2, 158.9, 190.7 | regular, blind to the music; live the phrase phase is arbitrary |
| C2 arrangement rise | `arrangementStem` trend 16 s / 2 s ×1.15, latched 30 s | 4.6, 48.9 | the arrangement's own 1.5/3 s smoothing delays the trend (the drop shows at 52-54 s unlatched) |
| C3 swell, latched 30 s | `signal.swell` | 28.0, 60.7, 97.4, 133.5, 180.2 | catches mid-phrase filter swells, misses the drop; only 9 of the owner's 18 swells are detectable at all (pass 1) |
| C4 hat-rate jump | `response.hatRate` trend 12 s / 1.5 s ×1.5 | 4.6, 44.7, 114.6 | 114.6 = groove B (+3.3 s); the others are the intro and build |
| C5 brightness rise | `synthBright` trend 16 s / 2 s ×1.25 | 36.3, 108.8, 149.4, 196.3 | noisy (the jitter of §2.6) |
| C6 any stem stepping up | max of per-stem rms trends 8 s / 0.5 s ×1.8 | 0.3, 35.3 (latched); unlatched 21 rises: 0.3, 5-8, **8.3** (bass in), 12-19, **20.9** (synth in), 35-37, **48.1** (drop), 53 | catches entries within 0.25-0.8 s; too eager in the intro |
| C7 phrase-gated novelty | `phrasePulse` gated by any of C2/C4/C5 held 6 s | 8.2, 32.0, 55.8, 111.3, 151.0, 198.6 | right moments for gradual changes (111.3, 151.0), but a sudden drop fires a phrase late (55.8) |
| **Per stem, unlatched** (`nov.py`) | synth rms 8 / 0.5 ×2.5 | **48.3 only** | a clean drop detector |
| | hat rate 8 / 0.5 ×1.8, gated arrangement ≥ 0.85 | **114.6 only** | groove B |
| | drums out: no kick for 2.5 s, gated arrangement ≥ 0.6 | **185.33** | the riser after the drums stop |
| | the synth's centroid, treble or highMid trends | nothing at 151-162 (centroid 0.67 → 0.69) | **the lift and 2:39 are not detectable** on the auto-gained bands: pass 1's swell finding again |

**Recommended: `signal.cometCue` plus a phrase fallback (§3.5). On Feline it fires at 48.27, 87.53, 114.6, 151.0 and
185.33 s.**
- **Five comets, never closer than 26 s.**
- **Three are detected:** the drop, groove B and the drums stopping.
- **Two come from the fallback:**
  - 87.53 s: a phrase boundary in groove A, swell 7's phrase;
  - 151.0 s: the lift's first bar.
- **The lift is a coincidence of the 36 s timer:** 114.6 + 36 lands on the phrase boundary at 150.99. If the owner
  wants a comet at 2:39 reliably, it needs either the unnormalised spectral-tilt feature pass 1 proposed (risk 2) or an
  authored cue.

**The trace after the song:** the cue's drums-out detector also fires at 210.5 s. The stems end there, and the
arrangement falls back to the mix's density. That is outside the song; gate it on `role.drums.active` to be tidy.

### 2.5 The bar grid offline and live

- **Offline:** the drum grid is bar-aligned. Kicks fall on all four beats, snares on beats 2 and 4 (78 and 77 of 235),
  and the phrase pulses are at 0.24 + 7.93n s (27 of them).
- **Live:** `beat.count`, `beat.bar` and `beat.phrasePulse` count from wherever the tracker locked (ADR-1581).
  - Any rule that needs *which* beat of the bar (the downbeat, the backbeat, the phrase) is exact offline and arbitrary
    live.
  - **The rules in §3 only rely on beat *phase* (where we are within a beat) and on beat *counts* used as an index.**
    The index only has to change every beat, not be aligned to the bar.
  - The one exception is the comet fallback's phrase pulse. Live it lands on some bar boundary, which is acceptable for
    a rare event.

### 2.6 The palette flicker (a pass-1 defect)

**The cause.**
- `signal.synthBright` = centroid {1.5 s / 3 s} × depth(`role.synth.rms`, depthMin -0.2, depthMax 12), then clamp.
- The depth is applied after the chain, frame by frame, from the raw RMS.

**Measured in groove B (111-150 s):**

| | per-frame change p50 | p99 | max |
|---|---|---|---|
| `signal.synthBright` | 0.081 | 0.364 | 0.547 |
| `signal.paletteAxis` | 0.036 | 0.164 | 0.247 |
| `final:palette/position` | **0.087** | **0.393** | **0.592** |
| `signal.arrangement` (for comparison) | 0.001 | 0.003 | 0.004 |

`palette/position` spans 0..3 across the four looks, so this is a frame-rate colour shimmer between looks. It may be
part of the "groove B close to blowing out" note in PROGRESS.

**The fix** (tested in the probe as `synthBright2`):
- **The depth:** `signal.synthPresence` = `role.synth.rms` {gain 12, offset -0.2, clamp 0..1, attack 300, decay 1500}
  as the depth (depthMin 0, depthMax 1).
- **Per-frame change:** p50 0.0014 and p99 0.0059 (60× calmer).
- **Section medians:**

  | Calls | Groove A | Groove B | Lift | Pedal | Riser | Outro |
  |---|---|---|---|---|---|---|
  | 0.08 | 0.26 | 0.41 | **0.80** | 0.54 | 0.38 | 0.47 |

  The ordering holds. Raise the centroid gain from 5 to about 6.2 to put the lift back at 1.0.
- Pass 1's test S6 (lift median ≥ 0.8, calls ≤ 0.1) still passes as is.

## 3. Bindings for the new effects

**The rules carried over from pass 1:**
- Smoothing lives in the signal; routes carry amounts.
- Accents have a 0 ms attack.
- No `macro.*` and no `.level` with a depth on an accent.
- The control channels `drums`, `bass` and `synth` stay the per-part depth on every input.

**New rules:**
- **R1. One stem, one job per effect.** No effect reads all three parts at accent rate (brief §10). The table gives
  each effect at most one accent per part.
- **R2. Large-area accents only on the kick.** The aurora pulse already rides it. The snare and hats light only small
  subsets.
- **R3. Rotate the subset, don't repeat it.** An accent that lights edges picks *which* edges from `beat.count` (and
  the 16th position for hats). It is a published integer, so this needs no CPU state and is deterministic offline. A
  shader hash of the index picks a different subset each beat, which fights habituation (§1.4).
- **R4. A travelling front is an envelope used as a position** (ADR-1652's pattern): `front = 1 - env`, brightest at
  launch. The launch must be visible on its first frame (an origin in view), or travel adds latency.
- **R5. Never route a speed.** Route a strength, or integrate a rate into a phase.

### 3.1 The binding table

Times in ms. *Release* is the envelope's fall to 0 (or the one-pole decay). *Cooldown* is the latch's refractory
period. Measured rates are Feline groove A / groove B.

| Effect | Aspect | Signal (existing / **new**) | Attack | Release | Cooldown | Cap / bound | Measured on Feline |
|---|---|---|---|---|---|---|---|
| **Mountain edges** | broad intensity (bass) | `bassBody` | 150 (in signal) | 800 | none | edge intensity floor + ≤ 0.4 × bassBody | 0 in intro/outro, 0.64-0.69 grooves |
| | bass wave travelling along ridge sections | `bassHit` as front position (R4) | 0 | 417 (fall 2.4 a second) | 130 (in signal) | one front; size from the onset-held `bassSize` (0.4-1.3) | 2.45 / 2.67 notes a second |
| | kick pulse (broad, brief) | `kickWave` | 0 | 333 | 200 | the near/mid ridgeline set only, ≤ 0.05 relative luminance over the frame | 2.0 a second; light p50 64-71 live (this rig) |
| | snare accent (sharp, selective) | `snareFold` **with attack 0** (see §4.3) | 0 | 286 (fall 3.5 a second) | **150 (new)** | a subset of 3-6 ridge sections chosen by hash(`beat.count`) (R3) | 1.25 / 1.21 a second |
| | hat sparkle (only if `hatDrive` ≥ 0.3) | **`hatSpark`** = `role.drums.hat` {linearfall 6 a second, refractory 100, rearm 0.05}, gated by **`hatOn`** = `hatDrive` binary 0.3 | 0 | 167 | 100 | 1-3 short edges chosen by hash(`beat.count` × 4 + 16th index); small area | 3.5-3.6 a second in groove B and the lift; off in groove A |
| | palette | **`synthBright` (fixed, §2.6)** → `paletteAxis` (existing) | 1500 | 3000 | none | blend, never a jump | lift 0.80 → 1.0 after the gain change |
| **Water flow** | flow strength (distortion, streak gain) | `synthBed` × 0.6 + `bassBody` × 0.4 (sum) | 250 / 150 | 900 / 800 | none | base strength ≥ 0.5 when silent: flow never stops | synthBed 0.46 / 0.41; bassBody 0.64 / 0.69 |
| | flow speed | **constant** (or **`flowPhase`** = integrate(base + 0.3 × `bassBody`)) | n/a | n/a | n/a | phase only through integrate (R5) | |
| | reflected-light persistence and colour | `synthBed` (persistence), `paletteAxis` (colour) | 250 | 900 | none | | |
| | drum disturbance (a short bright ripple band, not a circular ripple) | `snareFold` × 0.6 max `kickWave` × 0.3 (max) | 0 | 290-333 | 150 / 200 | disturbance amplitude ≤ 0.3 of the base distortion; no geometry | ≤ 3.3 accents a second combined, all small amplitude |
| **River pulses** | launch | **`pulseA`, `pulseB`**: a ring of two (§3.3) | 0 | life 1800 (fall 0.556 a second) | each slot latched 1800; B only once A is at least half-way | **2 alive at most**; summed glow **clamped to 1.0** (sum max 1.74 unclamped) | 174 launches, 0.92 a second in groove A, gaps p5/50/95 0.97 / 1.0 / 1.5 s |
| | snare: a short secondary accent | `snareFold` → *pulse flare* (brightness of the existing fronts, × ≤ 0.5) | 0 | 286 | 150 | no new pulse; adds to the fronts, inside the clamp | 1.2 a second |
| | width (bass) | `bassBody` → width (+≤ 40%). Speed stays fixed: a speed that changed mid-flight would bend a front already travelling, and holding a per-launch speed needs state the chains don't have | 150 | 800 | none | width ≤ 1.4 × base | 0.64 / 0.69 |
| | colour and persistence | `paletteAxis` (colour); `synthBed` → tail length | 1500 / 250 | 3000 / 900 | | | |
| **Celestial layer** | palette drift only | `paletteAxis` (0.2 depth); `arrangement` → brightness ±10% | 1500 | 3000 | none | never a beat accent | |
| | brief glow on a big event | `comet` (below), shared | 400 | 2000 | 8000 | ≤ +15% | 5 a song |
| **Comet** | trigger | **`comet`** (§3.5) | 0 (launch) | the comet's own timeline, 3-4 s | **cue 20 s; fallback 36 s; any two comets ≥ 8 s** | **1 alive** (8 s > life) | 48.27, 87.53, 114.6, 151.0, 185.33 s |
| | environment wash | **`cometGlow`** = `comet` {attack 300, linearfall 0.5 a second} → aurora intensity +0.2, river glint, edge colour accent | 300 | 2000 | as the comet | adds inside `auroraLight`'s range; ≤ 0.05 relative luminance a frame | |

**Parameter names to bind once A's report exists:** *edge bass gain*, *edge wave front*, *edge kick gain*, *edge snare
gain*, *edge subset index*, *edge sparkle gain*, *flow strength*, *flow phase*, *water disturbance*, *river pulse front
A/B*, *river pulse flare*, *river pulse width*, *comet trigger*, *comet glow*.

### 3.2 The beat-indexed subset (R3), concretely

- **Route `beat.count` to an *edge subset index* parameter** (amount 1).
  - It is an exact integer offline, and the tracker's count live.
  - The shader uses `hash(floor(index) + ridgeId)` against a threshold to pick about 15% of the ridge sections.
  - So the kick's and the snare's accents land on a different set each beat. The snare on beat 2 never repeats the kick
    on beat 1's set.
- **For hats,** `beat.count × 4 + floor(beat.phase × 4)` picks a set per 16th. That takes a signal summing `beat.count`
  × 4 and `beat.phase` × 4; the shader floors it.
- **No CPU state is needed and seek equals play.**
- **The alternative, a counter made by integrating the snare event:** it is frame-rate dependent, because an impulse
  integrates to dt. Rejected.

### 3.3 River pulses: why a ring of two

**Kick density on Feline:** 2.0-2.35 kicks a second, with gaps p5/50/95 of 0.43 / 0.50 / 0.70 s (`analyse-out.txt`
§D). Pulses alive at a launch, for a pulse life L:

| Launch rule | Launches | L 0.8 s | 1.2 s | 1.6 s | 2.4 s |
|---|---|---|---|---|---|
| every kick (`kickWave`) | 362 | 2 | 3 | 4 | 5-6 |
| every other kick (refractory 900 ms) | 185 | 1 | 2 | 2 | 3 |
| bar downbeat only | 93 | 1 | 1 | 1 | 2 |
| **ring of two (A, then B once A is half-way), L 1.8 s** | 174 | | | **2 max** | |

**Readability:** a pulse needs about 1.5-2 s to read as *travelling* down a long river. Every kick would mean 4-6
overlapping pulses, a lit strip, which is exactly what the brief forbids.

**Why not one slot:**
- one slot with a bar cooldown discards 3 of every 4 kicks;
- on the bar's downbeat it is musically nice offline, but live the downbeat is arbitrary (§2.5).

**The ring of two:**
- **What it gives:** a half-time pulse (one a second) that does not depend on the bar phase. It is visibly tied to
  every other kick, and the in-between kicks still drive the edges and the aurora (R1: the river does not need every
  kick).
- **How it is built, from existing stages** (`make_probe.py`):
  - `pulseA` = `kickWave` binary 0.95, latched {refractory 1800, linearfall 1/1.8};
  - `aHalf` = 1 - 2·`pulseA` > 0 (true once A has fallen below 0.5);
  - `pulseB` = `kickWave` gated by `aHalf`, latched the same.
- **Measured:**
  - 88 + 86 launches;
  - min gap 0.47 s (an off-grid class kick), p50 1.0 s;
  - both alive 57% of the time;
  - sum max 1.74, so **clamp the summed contribution** (in the shader or with a clamp on a `riverSum` signal).
  - The launches fall on beats 2 and 4 more often (59 + 65 against 23 + 27). That is an artefact of the start phase,
    not a rule.
- **No third slot:** it is only needed if L > 2 s at this launch rate.
- **Speed:** keep the speed fixed per scene, with the front at `(1 - env) × river length in view`. A bass-driven speed
  would bend a front already in flight. Bass sets the width instead (it reads as energy).
- **Bound on work:** two uniforms (the fronts), two intensities and one flare. The cost is constant, whatever the music
  does.

### 3.4 Synth colour and the water

- Synth colour enters only through `paletteAxis`, the fixed one. Don't route the raw synth into any colour.
- The water's persistence (streak length or reflected-light decay) follows `synthBed`. It reads 0.19 in the outro and
  0 in the intro, so give it a floor: the brief requires flow during quiet passages.

### 3.5 The comet trigger (to put in `music.py`)

```python
def trend(slow_s, fast_ms, floor, level):
    return {"trendSeconds": slow_s, "trendFastMs": fast_ms, "trendFloor": floor,
            "threshold": "binary", "thresholdLevel": level}
def latch(refr_ms, fall, rearm):
    return {"attackMs": 0.0, "envelope": "linearfall", "envelopeFallPerSecond": fall,
            "envelopeRefractoryMs": refr_ms, "envelopeRearm": rearm}
# after signal.arrangement and signal.kickRecent exist:
sig("arrHigh",   [inp("signal.arrangement", {"threshold": "binary", "thresholdLevel": 0.85})]),
sig("arrMid",    [inp("signal.arrangement", {"threshold": "binary", "thresholdLevel": 0.6})]),
sig("cDrop",     [inp("role.synth.rms", trend(8.0, 500.0, 0.03, 2.5))]),                        # 48.27
sig("cTexture",  [inp("response.hatRate", trend(8.0, 500.0, 0.08, 1.8), gate="signal.arrHigh")]),  # 114.6
sig("kickHeld",  [inp("role.drums.kick", {"envelope": "peakhold", "envelopeHoldMs": 2500.0,
                                          "envelopeFallPerSecond": 1000.0})]),
sig("cDrumsOut", [{"constant": 1.0}, inp("signal.kickHeld", amount=-1.0)], combine="sum",
    chain={"threshold": "binary", "thresholdLevel": 0.5}),
sig("cDrumsOutG",[inp("signal.cDrumsOut", gate="signal.arrMid")]),                                # 185.33
sig("cometCue",  [inp("signal.cDrop"), inp("signal.cTexture"), inp("signal.cDrumsOutG")], combine="max",
    chain=latch(20000.0, 0.5, 0.3)),
# fallback: a phrase boundary after 36 s with no cue, only in a full arrangement
sig("cueRecent", [inp("signal.cometCue", {"envelope": "peakhold", "envelopeHoldMs": 36000.0,
                                          "envelopeFallPerSecond": 1000.0})]),
sig("cueQuiet",  [{"constant": 1.0}, inp("signal.cueRecent", amount=-1.0)], combine="sum",
    chain={"threshold": "binary", "thresholdLevel": 0.5}),
sig("quietFull", [inp("signal.cueQuiet", gate="signal.arrHigh")]),
sig("cFallback", [inp("beat.phrasePulse", gate="signal.quietFull")], chain=latch(36000.0, 0.5, 0.05)),  # 87.53, 151.0
sig("comet",     [inp("signal.cometCue"), inp("signal.cFallback")], combine="max",
    chain=latch(8000.0, 0.4, 0.3)),                                                               # one alive
```

- **Bounded:**
  - the `comet` latch ignores triggers for 8 s, longer than a comet's life, so the scene never holds more than one;
  - the cue's own 20 s and the fallback's 36 s keep the count at a handful (5 on a 3.5-minute song; about 1.4 a
    minute).
- **Disable independently:** a *comet enable* amount on the comet's routes (0 = off). The signals cost nothing when
  unused.
- **Deterministic:** a pure function of the stems and the grid. Seek equals play (ADR-1602 control replay). Add it to
  `test_aurora_borealis.cpp`: `comet` rises exactly at those five frames, ±1 frame.
- **Live:**
  - the detectors work on live channels, since they use role levels and the hat rate;
  - the fallback's phrase boundary is the tracker's (arbitrary bar);
  - the drums-out detector needs the drums to have been playing (arrangement ≥ 0.6).

**Fallback for the owner:** if a comet on the lift (2:39) matters, the honest options are:
- a per-stem spectral-tilt feature (pass 1, open question 3);
- or a phrase-indexed rule that only holds offline.

## 4. Latency

### 4.1 Where the time goes (measured; this rig: battery, 120 Hz panel, live GPU p50 26 ms)

**The run:** `s2l-grid/`. The Feline drum stem from 48-100 s was played into BlackHole by a scratch miniaudio player,
stamped on the host clock like `avgen_sonic_probe` and corrected by its 480-frame period.
- **AV Gen:** the live project plus the probe's signals, run as `tools/sound_to_light.sh` does (live profile, Auto,
  `--input-sources drums@2:drums`, so `role.drums` and the beat feed are the live input).
- **The reference:** 105 kick attacks from the waveform's broadband envelope. They sit within ±5 ms of the structure
  map's beat grid (p10/50/90 -3 / +2 / +7 ms).
- **The window:** the first 8 s are left out (tracker lock).

| Path | Rule | n / miss | Bus p50 / p95 | Light p50 / p95 |
|---|---|---|---|---|
| `stem.drums.onset` | first rise in [0, 200] | 100 / 5 | **32.4** / 179* | **68.1** / 210* |
| same | nearest in [-150, 200] | 105 / 0 | 28.0 / 47.7 | 62.6 / 83.3 |
| `beat.pulse` (live grid) | nearest | 82 / **23** | 25.3 / 42.3 | 58.8 / 78.3 |
| `signal.kickWave` (grid or class) | first | 74 / 31 | 34.8 / 94.5 | 70.8 / 132.1 |
| same | nearest | 95 / 10 | 29.4 / 93.3 | 64.3 / 128.5 |
| `stem.drums.kick` (class) | first | 94 / 11 | **72.7** / 100.1 | **106.6** / 137.5 |
| `signal.snareFold` (class + 40 ms attack) | first | 48 / 57 (snares only) | 69.2 / 87.7 | 101.6 / 124.9 |
| emulated "onset inside the beat window, else class" | first | 94 / 11 | 31.8 / 89.0 | 67.6 / 122.2 |

\* The onset's p95 under "first rise" is a hat 16th before the kick being taken as the kick's own rise. "Nearest"
removes that.

**Findings:**
1. **The live grid is as late as the onset path** (25-30 ms against 28-32 ms at the bus), as the tracker's code
   predicts (§1.5). It gains nothing over the onset at p50.
2. **The live tracker on Feline's drum stem lands only 82 of 105 kicks within ±150 ms**, and its tempo dips to half time
   early (bpm p10 60.5). So `kickWave`'s tail is the class: p95 light 128 ms, beyond BT.1359's 90 ms acceptability.
3. **The class is +40 ms behind the onset** for kicks (73 against 32) and +37 ms for snares. Pass 1 measured 30-44.
4. **Gating the onset by the live beat phase** does not fix the kick: the gate misses 30% of kicks and lets in 35 hats,
   because the live phase is loose.
5. **The frame pipeline:** from bus to light is about 36 ms at p50 here (frame wait + CPU + a live GPU stretched to 26
   ms + vsync on 120 Hz).

### 4.2 What adds latency on each path

| Stage | Onset path | `kickWave` | snare (`snareFold`) | How known |
|---|---|---|---|---|
| input buffer (BlackHole 128 frames) | 0-2.7 | same | same | editor log |
| analysis window, hop, decision | about 11 | same | same | ADR-1605 |
| class decision | none | none on grid hits; +40 on class hits (about 20% of kicks live) | **+37** (snare deferred 4 hops = 43 ms by design, `kDeferFrames`) | §4.1 |
| tracker phase | | locks to onset detection: ±0 at p50, loose live | | §1.5, §4.1 |
| signal chain | 0 | 0 (attack 0) | **+40 ms to full value** (attackMs 40) | `music.py` |
| routes, macros | 0 | 0 | 0 | routes carry no chain; no `macro.*` |
| frame wait | 0-33, mean 16.7 | same | same | |
| update to present (CPU) | about 5; +3 in the slow regime (§5) | same | same | |
| GPU span | live 26 here (headless 12-13; stretched by the governor, ADR-1611) | same | same | |
| vsync | 0-8.3 at 120 Hz, 0-16.7 at 60 Hz | same | same | |

### 4.3 Proposed reductions

Each is measurable with this rig.

1. **Snare: drop the 40 ms attack** (`snareFold` `attackMs` 40 → 0).
   - It is a smoothing stage stacked on a class that is already 37 ms late, and the full value lands one more frame
     later.
   - **Saves:** about 33-40 ms to peak.
   - **Cost:** the "bloom" rise is lost.
   - **If the art wants a bloom:** draw it in the shader from the envelope (`glow = env × (1 - env) × 4` peaks
     immediately), not by delaying the value.
2. **Kick: keep `kickWave` for the accent**, but never bind a *sharp* full-frame flash to it. Its p95 is the class.
   - The river launch and the aurora pulse are soft travelling fronts (§1.3, point 3), so they tolerate it.
   - A sharp edge kick flash could take `stem.drums.onset`-based timing (light p50 63, p95 83) if A wants sharpness.
     But it must stay small-area, because the onset also fires on hats and snares.
3. **Never use the class (`role.drums.kick/snare`) alone for sharp accents.** That is the pass-1 rule, re-confirmed
   live.
4. **The comet, palette and flow paths are seconds by design.** Don't optimise them.
5. **The engine levers** (not scene work; for the coordinator):
   - **(a) the CPU slow regime (§5):** −3 ms p50, more at p95 [est. from the 2.4× ratio over a ~2.5 ms update-to-record
     section];
   - **(b) the beat tracker's phase-lock strength:** it is 0.5. A tighter lock (or locking to the drum class, not every
     onset) on a drum source might raise the 82/105 [untested];
   - **(c) the phase lead:** declined; still the only way to beat the onset path.
6. **Measure the GPU's live stretch before trusting a "light GPU" lever.**
   - Here the live GPU p50 was 26 ms against 12-13 ms headless, in the same scene.
   - Pass 1's "keep the GPU short" buys latency only where the governor does not stretch it (ADR-1611).

## 5. The live CPU p99 (about 10 ms against a 5 ms target): diagnosis

### 5.1 Measurements

| Run | CPU work p50 / p95 / p99 / max | frames > 4.5 ms | Notes |
|---|---|---|---|
| headless Ultra, start 60 s (`perf/hl-ultra-60`) | 1.84 / 2.18 / 3.06 / 5.39 | | GPU p50 / p95 13.06 / 14.86 |
| headless Ultra, start 152 s (`perf/hl-ultra-152`) | 1.58 / 2.44 / 3.06 / 4.26 | | GPU 11.67 / 13.84 |
| live profile, Auto, start 60 s (`perf/live-60`) | 4.92 / 6.86 / **9.46** / 30.1 | | GPU p50 12.7, p90 28.1 (stretched); 17 misses / 901 (120 Hz panel) |
| editor + projection, `--profile-csv`, run 1 | 2.75 / 6.90 / 8.07 / 10.6 | 32% | |
| same, run 2 | 5.62 / 7.14 / 9.01 / 27.0 | 58% | |
| **same, with one other core kept busy (`yes > /dev/null`)** | **2.21 / 2.59 / 4.68** / 18.1 | **1%** | frame p99 35.6 against 37.5-37.9 ms |

**The live profile's medians by category:**

| Category | ms |
|---|---|
| UI build and record | 1.51 |
| scene update | 0.70 |
| pass encoding | 0.55 |
| lights | 0.22 |
| outputs | 0.24 |
| objects and culling | 0.20 |
| signal bus, modulation and MIDI | under 0.07 together |

The analysis is not on the main thread live.

### 5.2 Diagnosis

The tail is not a phase. It is the whole main thread running slower:
- **Every phase slows together:** in the top 1% of frames each phase is 2.5-4.6× its own median, and the log ratios of
  engine, UI, record and submit correlate at 0.85-0.96.
- **The slow frames come in multi-second runs:** lag-1 autocorrelation of the CPU time 0.81, with runs of over 50
  consecutive frames.
- **Frames after a long idle are the slow ones:** after a ≥ 29 ms idle, the median is 2.5 ms when the core is fast, but
  6.0 ms in a slow run.
- **Keeping one other core busy removes it** (p99 9.0 → 4.7 ms).

This is the signature of the main thread being placed on an efficiency core, or of the performance cluster clocking
down, when the frame thread works about 10% of each 33 ms period.
- On Apple Silicon a thread's QoS is a hint, and placement and frequency follow load [11, 12].
- Apple documents user-interactive QoS for main-thread work [12].
- AV Gen sets no QoS, workgroup or activity anywhere: no `pthread_set_qos_class`, `os_workgroup` or
  `beginActivity` in `src/`.
- The same governor is behind the GPU stretch (live GPU p90 28 ms against 13 ms headless; ADR-1362 and ADR-1611).

**What it costs:**
- **Frames:** nothing yet. 10 ms leaves 23 ms of the period.
- **Latency:** about 3 ms at p50 in a slow run (the CPU between bus and present).
- **Risk:** it hides a real CPU regression behind noise.

The 5 ms p99 target (P5) is met headless (3.06). It is not a scene budget problem.

### 5.3 Proposals (engine, for the coordinator; none are scene work)

1. **Measure CPU work under a pinned clock state**, so the gate measures work:
   - **headless** (already about 3 ms p99), and
   - **live with the busy-core control:** report both, and judge P5 on the controlled run.
2. **Try, in this order, on an engine branch** (each is small and reversible; A/B with the editor `--profile-csv` run
   above):
   - **(a)** `pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0)` on the main thread and any render or
     analysis thread;
   - **(b)** `NSProcessInfo beginActivityWithOptions: NSActivityUserInitiated | NSActivityLatencyCritical` while live
     input or projection is on;
   - **(c)** a workgroup for the frame loop. The Audio Workgroup API is the public interval-workgroup route; on macOS 14
     or later, a `CAMetalDisplayLink`-paced loop is another.

   The acceptance test: the editor run's share of frames over 4.5 ms falls from 32-58% to about 1% with no busy core.
3. **Don't spend scene effort on the CPU tail.** The pass-2 effects add uniforms and shader terms, not CPU. Their CPU
   budget is "no new per-frame CPU work above 0.1 ms in total".

## 6. Performance plan

### 6.1 Conditions (every number)

- The M2 Max on **AC power**, with the **60 Hz LG UltraFine** connected (the T1 miss counter is not comparable on the
  120 Hz panel).
- A release build.
- **`tools/gpu-lock.sh`** around every `avgen` run, held only for that run (Agent A shares it).
- Record `pmset -g batt` and the display in each stage's folder.

### 6.2 The 8 stages of brief §12, with the existing tools

**One switch per effect.** Each new effect must have one project parameter that turns it off: an amount at 0 that
skips the work in the shader (the ADR-1652 identity rule: the pulse at 0 is byte-identical). Then:
- a stage is a **project variant** made by the generator (`build.py --stage N` or an override JSON), not a code
  revert;
- stages 6 and 7 are the same project with one switch flipped.

**Per stage, in this order:**

1. **Headless Ultra, five windows:**
   - the windows: 20 s (calls, dim), 60 s (groove A), 110 s (fills into B and a swell), 152 s (lift, the brightest), 186
     s (riser, comet 5);
   - the command:

     ```
     tools/gpu-lock.sh build/release/src/avgen --project <stage.json> --live-profile --mode headless --quality ultra \
       --target-fps 30 --size 1920x1080 --start <t> --warmup-frames 150 --measure 15 \
       --json <d>/hl-<t>.json --trace <d>/hl-<t>.csv --no-text
     ```

   - **Record:** GPU p50, p95 and max; CPU work p50 and p99; triangles and draw calls (constant across a run means no
     regeneration); pipeline compiles; the `scene` pass.
   - **Run stages N and N-1 interleaved, 3 reps each** (ABABAB). Apple's clock states are bimodal (ADR-1652's
     measurement), so a single A/B can mislead by about 1 ms.
2. **Force the event states:**
   - the comet: a window starting 1 s before 48.27 s and before 185.33 s;
   - river pulses: any groove window has both slots alive 57% of the time;
   - for the worst case, a variant with the *pulse flare*, comet and snare subset at their maximum constant values.

   Record GPU max. The brief asks for "representative aurora peaks and combined effect states".
3. **Live, Auto, 3 interleaved reps:**
   - `--live-profile --mode live --quality auto --warmup 5 --measure 30` at 60 s and 152 s;
   - **record:** misses per 900 (T1), frame p99 (T2), the share at Medium or better and the share at Ultra (T3);
   - **plus the CPU control:** one rep with the busy core (§5.3, point 1).
4. **At stage 8 only, the whole song live:** 210 s; the trend (slope < 0.5 ms a minute), the max interval, and comet
   frames' intervals listed.
5. **Sound to light at stages 1, 5 and 8:**
   - `tools/sound_to_light.sh` (transients);
   - the drum-loop run of §4.1 for the kick and river launch (the scripts in `pass2-research-b/`; promote them to a tool
     if the coordinator wants it).
6. **A flash audit at every stage:**
   - the clip of each stage (60-70 s and 150-160 s), through `ffmpeg … signalstats` as in §1.4;
   - **gate:** full-frame relative-luminance swing ≤ 0.05 in any 267 ms window (pass 1: 0.018).
7. **The signal tests (CPU, no GPU), extending `test_aurora_borealis.cpp`:**
   - the comet's five times (±1 frame);
   - river ≤ 2 alive and the sum clamp;
   - the snare refractory;
   - the `synthBright` per-frame change p99 < 0.02;
   - `--audit-routes` 0 dead;
   - seek equals play on every new final.

**The stages:**

| Stage | Variant | Must hold |
|---|---|---|
| 1 Existing baseline | 44aefe92 project | today's numbers: headless GPU p50 11.7-13.1 at 60 and 152 s on Ultra (this run), CPU p99 3.1 |
| 2 Revised landscape | land only | GPU Δ ≤ +1.0 ms p50; tris and draws constant over the run |
| 3 + mountain edges | 2 + edges on | Δ ≤ +0.8 |
| 4 + moving water | 2 + water flow on (edges on or off: run both) | Δ ≤ +0.7 |
| 5 + river pulses | 4 + pulses | Δ ≤ +0.4 with both slots alive |
| 6 Celestial on / off | final candidate ± sky layer | Δ ≤ +0.5 |
| 7 Comets on / off | final candidate ± comets | Δ idle 0; Δ ≤ +0.5 while one is alive |
| 8 Final combined | all accepted | headless Ultra GPU p50 ≤ 17 ms and p95 ≤ 1.3 × p50 in every window; live T1 ≤ 5, T2 ≤ 40, Ultra or High ≥ 90%; CPU work (controlled) p99 ≤ 5 |

### 6.3 Per-effect GPU budget (headless Ultra, 1080p, p50)

- **The baseline:** 11.7-13.1 ms here (pass 1: 13.8 p50 overall, 11.5-14.8 by window). Almost all of it is the `scene`
  pass (12.0 of 12.5 ms at 60 s), which includes the sky and aurora march.
- **The ribbon pulse** costs about +0.9-1.0 ms while it is up (ADR-1652).

| Item | Budget (ms) | Why that number |
|---|---|---|
| landscape (geometry, valley, haze) | +1.0 | about 45k triangles and 200 draws today; a valley at 2-3× the triangles is still small next to the aurora march |
| mountain edges | +0.8 | a term in the land's program (edge mask from barycentrics or a baked per-vertex edge attribute), no extra pass |
| water flow | +0.7 | one or two extra normal and streak samples in the water program; no new reflection |
| river pulses (2 slots + flare) | +0.4 | per-pixel Gaussians along the river parameter, in water and land programs |
| celestial layer | +0.5 | procedural, sky pixels only, no extra pass |
| comet (1 alive) | +0.5 while alive, 0 idle | a ribbon or line strip plus a head sprite; the environment wash is uniforms |
| **total** | **+3.9 → about 17 ms p50** | |
| reserve | about 16 ms to the 33.3 ms period | the engine's headroom (brief §2); live the governor stretches the GPU anyway, so the reserve is what keeps T1 |

If a stage overruns its line, profile that effect first (`--trace` pass columns; `--compare levers` for an A/B). Don't
take the overrun from the reserve.

## 7. Risks

1. **The live bar phase is arbitrary** (§2.5). Anything that needs the downbeat or backbeat (the snare's backbeat tier,
   bar-launched pulses, phrase-gated comets) is exact offline and arbitrary live. The recommendations avoid it, except
   the comet fallback.
2. **The live tracker is loose on this drum stem:** 82/105 kicks within ±150 ms, a half-time dip. `kickWave`'s tail is
   the class. The river launch and the aurora pulse are soft enough to tolerate it; a sharp flash would not be.
3. **The lift is not detectable.** The 151.0 s comet is the fallback's timer. A change of arrangement before 114.6 s
   would move it.
4. **The palette-flicker fix changes the look** (synthBright's section values drop until the gain is re-tuned). It
   needs an art check, and S6 must be re-run.
5. **This rig's live numbers are battery and 120 Hz.** All live acceptance must be re-run on AC with the LG display
   (§6.1).
6. **More signals** (about 15 new, mostly gates) lengthen the seek's control replay a little. Measure the scrub at 3:00
   (pass 1, risk 8).
7. **The CPU tail fix is an engine change** with a platform-specific API; it needs its own ADR and an A/B.

## 8. Open questions

1. **River launch:** a ring of two at half time (recommended), or one slot on the bar downbeat (exact offline,
   arbitrary live)?
2. **Comets:** is 5 a song right? Must 2:39 get one reliably? If so, either the spectral-tilt feature or an offline-only
   rule.
3. **Fix the palette flicker now**, as a pass-1 bug, before the art tuning of pass 2? Recommended.
4. **The snare's 40 ms attack:** drop it (sharper, earlier), or keep the bloom by drawing it in the shader?
5. **The CPU tail:** schedule the QoS / activity A/B on an engine branch (two-agent cap)?
6. **Agent A's names:** §3.1 needs one binding pass.

## 9. Reproduce

```sh
R=~/Desktop/av-gen-review/aurora-borealis/pass2-research-b
W=/Users/natefaulkenberry/Documents/GitHub/av-gen-borealis
cd $R
python3 make_probe.py $W probe.json
$W/build/release/src/avgen --project probe.json --sonic-trace probe-30.csv --trace-finals --fps 30
python3 analyse.py; python3 analyse2.py; python3 nov.py; python3 hat_ref.py; python3 plot.py
# headless and live profiles, the editor phase CSVs (with and without `yes > /dev/null &`): see perf/*.log commands in s5
# the live drum-loop run (needs BlackHole; player.c is a scratch miniaudio player: cc -O2 -I<miniaudio dir> player.c -o player -framework CoreAudio -framework AudioToolbox -framework CoreFoundation, and set PLAYER in s2l-grid/run.sh):
python3 kick_times.py   # then the broadband refinement (s4.1) -> kick_attack_bb.npy
AVGEN_GPU_LOCK_TIMEOUT=3600 $W/tools/gpu-lock.sh bash s2l-grid/run.sh
python3 s2l_grid.py s2l-grid first; python3 s2l_grid.py s2l-grid nearest
```

## Sources

1. Synesthesia, "Audio uniforms" (Level, Hits, Presence, Time, Beat, BPM families):
   https://app.synesthesia.live/docs/ssf/audio_uniforms.html
2. HeavyM, "How to sync visuals with music: audio-reactive projection mapping"
   (https://www.heavym.net/how-to-sync-visuals-with-music-audio-reactive-projection-mapping-made-simple/); and the
   reactivity-ladder guidance in https://freebeat.ai/articles/how-to-create-an-audio-reactive-video-for-your-music-without-manual-editing
   (a vendor blog: general guidance only).
3. Böck, Krebs and Schedl, "Evaluating the online capabilities of onset detection methods", ISMIR 2012
   (https://ir.webis.de/anthology/2012.ismir_conference-2012.9); librosa `peak_pick` (the `wait` rule):
   https://librosa.org/doc/dev/api/generated/librosa.util.peak_pick.html
4. Hysteresis and the Schmitt trigger: https://blogs.plymouth.ac.uk/embedded-systems/glossary-2/hysteresis-and-the-schmitt-trigger-glossary-entry/
5. Müller, FMP notebooks, "Novelty-based segmentation" (Foote's checkerboard kernel):
   https://audiolabs-erlangen.de/resources/MIR/FMP/C4/C4S4_NoveltySegmentation.html
6. Audio-visual simultaneity windows and asymmetry (reviews citing van Eijk et al. 2008, Dixon and Spitz 1980, Conrey
   and Pisoni 2003): https://pmc.ncbi.nlm.nih.gov/articles/PMC4451240 and
   https://www.frontiersin.org/journals/psychology/articles/10.3389/fpsyg.2015.00736/full
7. ITU-R BT.1359 (detectability +45 / -125 ms, acceptability +90 / -185 ms, sound leading positive):
   https://itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.1359-0-199802-S!!PDF-E.pdf ; ATSC IS-191 and its criticism:
   https://www.tvtechnology.com/opinions/av-synchronization-how-bad-is-bad ; https://arxiv.org/pdf/2212.01686
8. Habituation of attentional capture by repeated onsets: Turatto and colleagues, e.g.
   https://cris.unibo.it/retrieve/handle/11585/1021290/bcf3dd8c-566f-420f-b2e7-2620c72b7b86/Valsecchi%20%26amp%3B%20Turatto%202025.pdf ;
   repetition suppression and novelty: https://ntblab.yale.edu/wp-content/uploads/2015/01/Turk-Browne_FHN_2008.pdf
9. W3C, "Understanding SC 2.3.1: Three Flashes or Below Threshold":
   https://www.w3.org/WAI/WCAG21/Understanding/three-flashes-or-below-threshold.html
10. Stark, Davies and Plumbley, "Real-time beat-synchronous analysis of musical audio", DAFx-09; BTrack:
    https://code.soundsoftware.ac.uk/projects/btrack
11. Howard Oakley, "What is Quality of Service, and how does it matter?" (QoS and P/E core placement on Apple Silicon):
    https://eclecticlight.co/2025/05/09/what-is-quality-of-service-and-how-does-it-matter/
12. Apple, "Tuning your code's performance for Apple silicon":
    https://developer.apple.com/documentation/apple-silicon/tuning-your-code-s-performance-for-apple-silicon ;
    CAMetalLayer latency discussion: https://developer.apple.com/forums/thread/711033
