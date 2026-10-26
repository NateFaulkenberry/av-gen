# Audio Response Lab 03: final report

This follows the owner's Gate 1 decisions (2026-10-26):
- add fast mid and high levels;
- flag a beat clock that cannot show lock;
- no text-mesh ticket;
- keep the Diagnostics menu placement;
- carry out the whole Next list.

The plan and its threshold reasoning are in `01-plan.md`, and Gate 1 is in `02-gate1-report.md`. How to run the lab,
add a fixture and read a failure is in `docs/audio-response-lab.md`. ADRs 1720-1726.

**Media:** `~/Desktop/av-gen-review/96-audio-response-lab/02-final/` (file list at the end).

## 1. What changed in production (each with its tests)

| ADR | Change | Why |
|---|---|---|
| 1721 | `--sonic-trace` writes the frame's events (it read 0 in every event column) plus `audio.*` and `control.*`; `--sonic-live-log` writes `audio.*` whole | A trace that cannot see an event cannot measure one |
| 1722 | `"diagnostic": true` in the examples index: in Examples > Live > Diagnostics, not in the live scene switcher | The owner kept this placement |
| 1723 | `response.mid` (250 Hz-4 kHz) and `response.high` (4-16 kHz): fast dB followers, each against its own adaptive reference. `audio.*Level` unchanged | A short-release mid/high level, for Prismatic Dream's glints |
| 1724 | `beat.source`: 0 none, 1 MIDI clock, 2 file grid (no lock state), 3 live tracker. The tracker itself is unchanged | Show the clock honestly; the tracker is separate work |
| 1725 | `avgen_audio_lab_replay` (the live analysis path, deterministic, from a file); the probe's `file` scenario and `loopback.sh` | Measure the live tracker and the live latency |
| 1726 | A drum fixture from the owner's real one-shots, never committed | Retest the snare/hat finding |

## 2. The suite: 283 gates pass on eight fixtures; the findings are reported, not gated

`python3 tools/audio_lab/lab.py run`: the seven generated categories, plus the real-drum fixture on a machine with the
samples.

**A gate** is something the system promises:
- each indicator equals its signal;
- values are at rest after silence, once the signal's own documented release has run out;
- probes stay within their bounds;
- the fast levels are band-selective;
- digital silence has no onset;
- a steady noise floor has no onset;
- held notes and drones do not retrigger;
- two attacks a refractory apart are two events;
- a silent source shows no activity;
- no source leaks into another;
- onset precision and recall on clean material, offline and on the live path.

**A finding** is a measured quality the owner has made separate work: the beat clock, and a detector in noise.

`tests/integration/test_audio_lab_e2e.cpp` runs every category in the CPU suite, and every gate must pass.

| Fixture | Gates | Findings that pass | `response.onset` P / R, file path | Timing on the hop clock, median / p95 | The same on the live path (replay) |
|---|---|---|---|---|---|
| sparse | 34/34 | 0/0 | 1.00 / 1.00 | -10.7 / +0.3 ms | 1.00 / 1.00 |
| dense | 28/28 | 2/5 | 1.00 / 0.99 | -9.5 / -5.5 ms | 1.00 / 0.99 |
| noisy | 25/25 | 3/9 | 0.93 / 0.57 | -10.0 / -1.3 ms | 0.93 / 0.57 |
| syncopated | 31/31 | 0/7 | 1.00 / 0.99 | -11.3 / -6.4 ms | 1.00 / 0.99 |
| dynamic | 34/34 | 2/7 | 1.00 / 0.99 | -12.0 / -6.7 ms | 1.00 / 0.99 |
| isolation | 57/57 | 2/5 | 1.00 / 0.97 | -11.3 / -6.7 ms | 1.00 / 0.97 |
| adversarial | 34/34 | 4/6 | 1.00 / 0.88 | -13.3 / -8.8 ms | 1.00 / 0.88 |
| drums-real | 40/40 | 0/0 | 1.00 / 1.00 | -12.0 / -6.7 ms | 1.00 / 1.00 |

Negative timing is the centre-stamped file path publishing up to half a window early (`01-plan.md` §4).

## 3. Findings

### 3.1 The beat clock: file grid and live tracker, measured separately (not fixed: separate work)

| Fixture | File grid: tempo, F, lock | Live tracker (causal replay): tempo, F, lock after the segment starts | Live tracker (BlackHole loopback) |
|---|---|---|---|
| syncopated, 110 BPM | **144.6 BPM**, F 0.28, never | **144.6 BPM**, F 0.19, never | F 0.15, never |
| dynamic, 120 BPM (0-44 s) | **95.9 BPM**: one global tempo, from the second section | **60.0 BPM (half time)**, F 0.80 overall, locks after **22 s** | locks after 25.5 s |
| dynamic, step to 96 BPM at 44 s | 95.9 | 96.0, relocks in **3.7 s** | 3.75 s |
| dense, 128 BPM | 127.9 BPM but **F 0.0**: right tempo, wrong phase throughout | 127.8 BPM, F 0.72, locks after 15 s | |
| isolation, 120 BPM | 120.0, F 0.99 | 119.9, F 0.67, locks after 12 s | locks after 6 s |

**Confidence and silence:**
- In the dynamic fixture's 6 s of silence the live tracker's confidence falls below 0.1 after **4.3 s**. Its clock
  keeps pulsing in the meantime: **11 pulses in silence**.
- In silences under 3 s (syncopated, isolation) it holds 0.6-0.8 throughout.
- The file grid's confidence is one number for the whole file. The dial now says so (FILE GRID: NO LOCK STATE).

**What this says:**
- Neither clock follows syncopation.
- The file grid cannot follow a tempo change at all.
- The live tracker follows a tempo change well (3.7 s), but locks slowly on a quiet four-on-the-floor start (22-26 s,
  at half time first).
- Neither clock says "I have lost it" quickly enough for a live scene.

### 3.2 The drums classifier: every real closed hat also fires the snare class (ADR-1726)

On real one-shots, each alone:
- all three real closed hats fire **snare** as well as hat: 11 of 12 hits;
- one of four real snares (an acoustic Birch) also fires hat, 4 of 4;
- the Slate and Supraphonic snares are clean;
- the kicks are clean (one stray snare on an acoustic kick).

On the Feline drum stem, which has no ground truth, a hat fired within one hop of only 4 of 235 snares.

So the Gate 1 finding ("hat on every snare") came from the synthetic snare. The real defect runs mostly the other way.
Latency after the attack: kick +23 ms, snare +29 ms.

### 3.3 Onset detection in noise and in adversarial material

**Broadband noise, hits by kind:**

| SNR | kicks caught by `response.onset` | kicks caught by `response.low` | snares caught by `response.onset` |
|---|---|---|---|
| 30 dB | 2 of 3 | 1 of 3 | 3 of 3 |
| 20 dB | 0 of 3 | 1 of 3 | 3 of 3 |
| 10 dB | 0 | 2 of 3 | 0 |
| 0 dB | 0 | 0 | 0 |

`response.onset` loses kicks to broadband noise first. The steady noise floor never triggers.

**The first audible samples after a reset fire an onset.** The noise floor fading in from digital silence at the file
start fired at 32 ms, at about -76 dBFS: a level-free detector with no history.

**Adversarial material:**
- A pure held tone and a bass drone do not retrigger (one onset, and zero for the drone: the mix onset misses bass, as
  at Gate 1).
- -60 dBFS noise fires nothing.
- A 30 Hz snare roll gives 2 events (the 60 ms refractory caps it).
- Click pairs 30, 50 and 80 ms apart separate. 15 ms apart they merge, by design.
- Percussion outside the expected bands (25 Hz thumps, 14 kHz ticks) is detected: 7 of 7.

### 3.4 The fast levels (ADR-1723) on the dense fixture

| Section | `response.bass` | `response.mid` | `response.high` |
|---|---|---|---|
| low only | 0.79 | 0.11 | 0.08 |
| mid only | 0.00 | **0.76** | 0.00 |
| high only | 0.00 | 0.00 | **0.48** |
| everything | 0.78 | 0.54 | 0.34 |

Both are at rest within 0.04 s of the measured silence. The slow `audio.midLevel` read 0.52 in the high-only section
(left over from the section before) and takes up to 14 s to reach 0.

### 3.5 Cross-scene transfer (brief §8): Event Horizon and Rainbow World

`tools/audio_lab/transfer.py`:
- **Method:** each scene runs on the sparse, dense and dynamic fixtures and on silence of the same length. The
  audio-driven part of each route final is the difference of the two runs.
- **Rainbow World:** its scene is generated by Blender and is not in git. It was read from the owner's main checkout,
  read only.

| Family | Event Horizon | Rainbow World |
|---|---|---|
| **transient** (`response.low/onset`, `*Env`) | 39 components. Stale after 2.5 s of silence: none. They answer 85% of firm attacks within 120 ms (`response.onset` routes 100%). Release median 233 ms (`response.onset` 100 ms, `response.low` 317 ms, `lowEnv` 633 ms) | no direct routes: it reads them through `signal.*` |
| **level** (`response.bass`) | 9 components; correlation with `response.level` 0.87; 3 of 9 hold 5-9% of their range 2.5 s into silence (their own 900 ms decays on top of the bass's documented release) | through `signal.swell` / `signal.breath` (scene-specific) |
| **beat** (`beat.phase/phrase/pulse`) | | 12 + 60 components. They keep moving in silence, because the file grid runs on through it (ADR-1581, by design). Read as "stale", that is correct: nothing says the beat has gone (§3.1) |
| **interpreted** (`visual.*`, `sonic.*`) | 108 components; 40 hold up to 60% of their range 2.5 s into silence. `visual.heat` = max(`response.sustain`, `sonic.energy`), and the character's slow tiers hold | |
| **scene-specific** (`signal.*`, `control.*`, timeline) | | 507 components, stateful by design: a world scan and look changes persist. **Not expected to transfer** |

**What transfers and what does not:**
- **Transfers:** the transient controls mean the same in any scene. They answer attacks, return to rest and never
  hold, and their release is the route's own chain.
- **Transfers with care:** the level controls follow the music (correlation 0.87), but a long route decay holds a
  little into silence.
- **Inherently scene-specific:**
  - the interpreted controls (character tiers are slow by design);
  - the beat controls (they follow the clock, which runs through silence);
  - Rainbow World's conditioned signals and their state.

### 3.6 Timing, end to end

| Stage | Measured |
|---|---|
| Ground truth → analysis event (file path, hop clock) | -11 ms median (centre-stamped: up to half a window early) |
| Analysis event → conditioned signal → route output → parameter final | the same `Engine::update`: 0 frames (the trace's rows; the mapping gates hold to 0.001) |
| Parameter final → rendered frame (offline render) | 0 frames: rendered brightness correlates 0.92-1.00 with the final at lag 0, and lag 0 beats ±1 frame for every checked indicator (4 clips) |
| Ground truth → bus, live analysis (causal replay, 30 fps) | 27-33 ms median, 33-50 ms p95 (the fixtures put many attacks on frame boundaries, so this is partly frame-quantised) |
| Sound → bus, live device (BlackHole loopback, 1080p30) | 32-53 ms median, 55-59 ms p95 |
| Sound → present call, live device | 36-56 ms median, 58-62 ms p95 |

## 4. Performance (the lab scene, 1080p30, Apple M2 Max)

| Run | Power | Display | Frame p50 / p95 / p99 | Late frames | GPU p50 / p95 | CPU work p50 |
|---|---|---|---|---|---|---|
| Gate 1 scene, final materials (`02-gate1-report`) | AC | 60 Hz | 33.32 / 35.10 / 38.0 ms | 1 of 450 | 11.7 / 16.9 ms | 8.8 ms |
| Final scene (158 nodes, 68 routes) | **battery** | **built-in 120 Hz**: the LG 60 Hz was not attached | 33.36 / 34.44 / 36.2 ms | 1 of 600 | 7.0 / 12.1 ms | 8.8 ms (UI 5.0) |

- The 1080p30 target is met in both. The final run was not on AC and not on the reference 60 Hz display, so it is
  recorded as such and not as the reference number.
- The text labels remain the largest geometry cost (407k triangles; about 7.5 ms of GPU in Gate 1's A/B). The owner
  accepted it.
- The loopback runs (§3.6) were also on battery, logged in each run's `conditions.log`.

## 5. Human-review checklist (what numbers cannot settle)

Watch a clip with the sound on:
1. Does each ONSET lamp flash read as one discrete event, not a glow? (dense, syncopated)
2. Does a held note show as a sustained level with no flicker on the lamps? (sparse 21-25 s)
3. Does the board return to rest within a second of silence: every meter empty, every lamp at rest, the sphere small?
   (sparse 25-30 s, dynamic 28-34 s)
4. Do the syncopated accents light ON at their own times, not on the beat? (syncopated)
5. In isolation, does only the column of the source that plays light up? (isolation 0.5-12 s)
6. After the tempo change (dynamic 44 s), does the pink marker visibly drift against the grey intended beats, while the
   FILE GRID lamp says it cannot know? (It should: the file grid has one tempo.)
7. Do the HIGH meter glints follow the hats and fall between them, with the grey slow bar lagging behind? (dense 8-12 s)

## 6. Limits

- **Not verified by eye:** the lab opened from the GUI's menu (the loader and menu grouping are tested, and the
  loopback ran the project live in the editor loop), and any ImGui overlay (none added; brief §2G is open).
- **The replay is the live analysis, not the live engine:** there is no device, no scheduling, and not the live Sonic
  response model.
- **Live-device numbers come from BlackHole**, which adds no device latency. An interface adds its own (3.1 ms on the
  Volt 876, Living Prism 02 §6.1).
- **The noisy fixture's SNR** is the hit's peak against the noise's RMS, at 48 kHz.

## 7. Media (`~/Desktop/av-gen-review/96-audio-response-lab/02-final/`)

| File | What |
|---|---|
| `dense-synchronised-diagnostic.mp4` | 24 s: the fast mid/high meters in the low-only, mid-only and high-only sections, then everything; pairs 60 and 40 ms apart |
| `dynamic-synchronised-diagnostic.mp4` | 60 s: the quiet intro, build, loud section, silence, recovery and tempo step. The FILE GRID lamp, and the pulses drifting off the intended beats after 44 s |
| `dense-still-10s.png`, `dynamic-still-46s.png` | One frame each |
| `results/summary.md`, `results/results.json` | Every fixture: every check (gate or finding), every raw list |
| `transfer/transfer.md`, `transfer/transfer.json` | Cross-scene transfer, by family and by source |
| `loopback/<fixture>/loopback.json`, `conditions.log` | The live runs: latency, live tracker, power source |
| `*-pixel-check.json` | Rendered brightness against the final, at lags 0 and ±1 frame |
| `live-profile-1080p30-final.txt` | The final scene's live profile |
| `drums-real-classifier.txt` | The per-one-shot classifier table |
