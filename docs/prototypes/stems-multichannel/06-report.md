# Stems + multichannel: final report

Branch `eng/stems-multichannel`, 2026-10-19. This follows brief §12-13. Research is in `02-research-report.md`, the
plan (approved at gate 1) in `03-implementation-plan.md`, the M0 baseline in `04-baseline.md`, the benchmarks in
`05-benchmarks.md` and the hardware procedure in `07-hardware-test.md`. The decisions are ADR-1520, ADR-1521 and
ADR-1522.

## 1. What was built

| Milestone | Commits | What |
|---|---|---|
| M0 | `e3544de6` | The pre-change live baseline: Cymatic World and Neon Vector on the BlackHole 2ch input with music, 1080p30 |
| M1 | `5c53a176` | `tools/make_stem_fixture.py`. The build generates a deterministic mix + 4 stems + 5 variants + `fixture.json` into `build/<preset>/test-fixtures/stems`, or the first test generates them on a CI test VM |
| M2 | `9d88b775` | `StemAnalyzer` / `StemTrack`, stem keys and file checks, the `stem.<key>.*` vocabulary. Log-spectral onsets; the level read ADR-1510's way |
| M3 | `9cb9a833`, `fee91422` | `assets.stems` in the project; analysis at load, in parallel, while decoding (no PCM held); publication in both engine modes; seek equals play through the replay |
| M4 | `212a85d5` | "Stems (analysis only)" in the Audio clips popup |
| M5 | `dc5e6409` | The capture seam; per-source rings in `AudioInput`'s callback; the synthetic 8-channel backend; `ChannelAnalysis` (one thread); live sources as `stem.<key>.*` |
| M6 | `21d1381d`, `93bdc41a` | The Core Audio AUHAL backend; per-stream device layout; disconnect, reconnect and format-change handling; `avgen --input-meter` |
| M7 | `9807f05d`, `cba58ae0` | The Live panel's Inputs table; the per-machine channel map and main-mix pair; `--input-sources`, `--input synthetic:<n>` |
| M8 | `20cc8af1`, `a65e251c` | Live-source telemetry; the 28-run benchmark matrix; offline memory before and after |
| M9 | this report, `74a62e59` and later | ADR-1520/1521/1522, help pages, `docs/audio.md`, the hardware procedure, final checks |

Main was merged twice: `1bddf349` (ADR-1510) and `c3828f06` (the generative pass).

## 2. How it behaves

**Offline.**
- A project's mix plays exactly as before: a test compares the installed buffer sample for sample.
- Stems listed in `assets.stems` are analysed at their own sample rate, while decoding, and published as
  `stem.<key>.*` at every render frame and every seek-replay step.
- Inactive (0, no events) outside a stem's span and for a failed or missing file.
- Every file problem is a project warning naming the file.

**Live.**
- With no sources, an input is opened exactly as before (miniaudio, the average of all channels).
- With sources, macOS opens the device through the AUHAL backend, which captures every channel of every input stream.
- Each source (a channel or a linked pair) is analysed on one shared thread and published as `stem.<key>.*`, the same
  names an offline stem uses.
- Sources read inactive on an absent channel, on a disconnect, or after 250 ms without audio.
- A disconnected device is never replaced. The same UID is reopened when it returns.

## 3. Tests (all automated unless marked)

| Tag / file | What it holds | Where it runs |
|---|---|---|
| `[stems][fixture]` `test_stem_fixture.cpp` | The fixture decodes to the recorded rate, channels, length and sha256 | CI (test-time generation) |
| `[stems][analysis]` `test_stem_analysis.cpp` | Events within 35 ms; each stem's events are its own; sustained energy and silence; offsets; 44.1 kHz; width; determinism; chunking and streaming invariance; hits recall | CI |
| `[stems][signals]` `test_stem_signals.cpp` | Keys; every file and mix diagnostic; the vocabulary; relabel keeps ids; inactive | CI |
| `[stems][engine]` `test_stems_offline.cpp` | No summing; events at 24/30/60 fps with equal counts; edge, silent and missing stems; **seek equals play** (the test fails if the replay's stem publish is removed); save, relabel, duplicates | CI |
| `[multichannel][threading]` `test_multichannel_input.cpp` | 8 channels to 8 sources, sample-exact, no swaps or sums; pairs; main mix (all, or a pair); absent channels; removal keeps the others contiguous; drop and gap discontinuities; channel analysis finds each channel's clicks on its own source; the engine's stale, disconnect and recovery | CI, and in the TSan set |
| `[coreaudio]` `test_coreaudio_capture.cpp` | A private two-stream aggregate captures every channel in place; destroy reads disconnected; on BlackHole 2ch AUHAL and miniaudio carry the same samples; the engine's path switch; replug of the same UID | **local**: SKIPs without BlackHole 2ch |
| `[settings]` `test_app_settings.cpp` | The channel map and main pair round-trip; a bad row is dropped | CI |

**Scenes and workflows actually exercised:**
- **Live profiles:** Cymatic World and Sonic Abstract Neon Vector only (M0 and M8).
- **Offline engine tests:** the orb scene with stem routes (`orb/impulse`, `orb/emissive`).
- **Headless memory runs:** the orb with Feline Footwear.
- No other scene was run with stems or input sources. In particular, no scene ships routes on `stem.*`.

## 4. Benchmarks (summary; full tables in `05-benchmarks.md`)

| | Result |
|---|---|
| Live frame pacing (28 runs, 1080p30, 2 scenes, 7 arms) | 33.3 ms p50 in every run; misses 0-9 of 900 (M0 baseline 2-9). No regression found |
| Drops / gaps / overloads | 0 / 0 / 0 in every run |
| Analysis thread | levels tier 1-3.5% of a core (4-8 sources); hits on all 8: 10.4-12.1%; hits on 2: about 5.5% |
| Core Audio callback (BlackHole 2ch, AUHAL) | 1.5-1.9 µs mean, 4-13 µs max, against 2,667 µs |
| Callback, synthetic (planar microbenchmark) | 0.75 / 2.76 / 8.75 µs with 2 / 8 / 24 sources |
| Onset to bus (live) | p50 5.8-7.0 ms, p99 11.7-15.7 ms (plus up to one hop, 10.7 ms, for the window) |
| Offline: 8 stems of 3.5 min | 2.1-3.6 s to analyse; peak RSS unchanged from no stems (879-900 MB; it was 2.1 GB before analysing while decoding) |

## 5. Hardware: the Volt 876 (connected 2026-10-19, 14:27-15:21)

**Discovery** (`avgen --list-audio-devices` and the HAL probe):
- **Name and id:** "Volt 876", id `AppleUSBAudioEngine:Universal Audio:Volt 876:25362061501116:1,2`, transport USB.
- **Inputs:** **32 input channels in ONE Core Audio stream**, float32, 48 kHz. That is 32, not the 24 the retail listings
  give. Input latency and safety offset are each 74 frames.
- **One stream** means miniaudio's multi-stream limitation (#312, ADR-1522) does not affect this interface: the plain
  path hears all 32 channels too.

**What the 32 channels carried, with nothing plugged in at first (meter at 14:27):**

| Channels | Level | Reading |
|---|---|---|
| 1-2 | about -58 dBFS | preamp noise, or a source left connected |
| 3-8 | about -104 dBFS | the analog floor |
| 9-28 | exact digital silence | the ADAT inputs, with no ADAT source |
| 29-30 | about -4 dBFS | a live signal |
| 31-32 | about -25 dBFS | a live signal |

Channels 29-32 look like UA's loopback or virtual returns of what the Mac plays. That was not confirmed with the owner.
**Practical consequence:** the default main mix (the average of all 32 channels) on this interface is dominated by the
loopback and reads about 30 dB below any single input. This is why the Live panel's Main mix pair exists (`cba58ae0`).

**Per-input independence** (the owner moved a microphone through inputs 1-8; `build/volt/owner-meter-151811.log`,
from 15:18:11):

| Input | Lit (s) | Peak on it | Highest other channel 1-8 |
|---|---|---|---|
| 1 | 1-29 | 0.0 dBFS | about -57 dBFS (the ch 1/2 noise floor) |
| 2 | 30-39 | 0.0 dBFS | -57.3 dBFS |
| 3 | 40-52 | 0.0 dBFS | -57.7 dBFS |
| 4 | 53-61 | -5.6 dBFS | -57.1 dBFS |
| 5 | 63-70 | -5.4 dBFS | -57.7 dBFS |
| 6 | 71-79 | 0.0 dBFS | -56.6 dBFS |
| 7 | 80-88 | -5.6 dBFS | -57.5 dBFS |
| 8 | 104-115 | -1.9 dBFS | -57.7 dBFS |

- Each input reached its own channel and only that one; no crosstalk was above the noise floor.
- The 0.0 dBFS peaks are the gain the owner set (clipping at the preamp), not a fault.
- Throughout: 0 drops, 0 gaps, 0 overloads. The callback averaged 17.0-17.5 µs, max 39.3 µs, at 128 frames with 32
  sources.

**Unplug and replug: found two defects, then fixed** (`5b5bc4d4`).
- **Disconnect detection worked:** the pull at about 165 s read DISCONNECTED within a second, with one sample-time gap.
- **Defect 1, no reopen in the meter:** `--input-meter` never reopened after the replug. It had no reopen logic; only
  the engine did. It now reopens the same id once a second when the device is back.
- **Defect 2, callbacks after the disconnect:** AUHAL kept calling the input callback at about 94 blocks/s (the
  default input's 512-frame period) with silence. The callback mean drifted from 17 to 175 µs as those 4×-longer
  blocks entered it.
  - The callback now forwards nothing once the device is gone.
  - The engine and the meter stop that device's IO as soon as they see the disconnect.
  - The private-aggregate test reproduces it: without the guard, blocks went from 115 to 143 in 300 ms.
- **Re-verifying both on the Volt (meter and engine path, a real unplug and replug) is pending.** The owner unplugged
  the Volt at about 15:21. The procedure is in §5.1.

**Not tested on the Volt:**
- ADAT channels 9-24: there is no ADAT source.
- Rates other than 48 kHz.
- The Volt benchmark arms: they were queued behind another agent's GPU suite and cancelled when the Volt was unplugged.

### 5.1 Pending: reconnect on the Volt

With the Volt plugged in and a microphone on input 1, run each of these for 120 s:
- `avgen --input-meter <id> 120`
- `AVGEN_HARDWARE_INPUT=<id> AVGEN_HARDWARE_SECONDS=120 avgen_tests "[hardware]"`

During each run: pull the USB cable at about 30 s and replug it at about 50 s. Expected:
- DISCONNECTED, then RECONNECTED (meter) or `connected` again (engine);
- "sources with audio" above 0 again after the replug;
- the block count frozen while the device is out.

## 6. Not done, and limitations

- **Volt hardware:** the re-test of reconnect after the fix (§5.1), the ADAT channels (they need an ADAT source), and
  the benchmark arms on the Volt.
- **Rates:** live tests and benchmarks ran at 48 kHz only. A stem at 44.1 kHz was tested offline.
- **Main mix only:** stems and live sources do not feed the beat clock, `music.*`, `sonic.*`, `response.*`,
  `AudioHistory` or the reactivity proposer.
- **Not implemented:**
  - per-source gain (the input gain applies to every source);
  - automatic offset detection, deliberately (the brief forbids it);
  - stem playback, solo or mute.
- **Windows and other platforms:** they keep the miniaudio path. Multichannel sources there go through miniaudio's
  interleaved capture, which is untested (no device) and has no multi-stream fix or lifecycle listeners.
- **UI:**
  - The Inputs table and the Stems section were built and their logic tested.
  - A window-only screenshot confirms the Live panel's audio section and device combo. The Inputs table sat below the
    fold and was not seen.
  - Visual review by the owner is outstanding.
- **Device enumeration** costs the UI thread about 2.4 ms per call, every 3 s, as before plus the HAL layout query.
  A listener-driven cache would remove it; not done.
- **Disconnect latency:** a destroyed aggregate read disconnected about 360 ms after the destroy. The Volt's USB
  unplug read disconnected within the meter's 1 s resolution.
- **The plain path on a multi-stream device:** with no sources, it still hears only the first stream, as before this
  work (ADR-1522). Ticking any source, or choosing a main pair, moves it to the Core Audio path.

## 7. Final checks

Run after a clean reconfigure (`cmake --preset release --fresh`) and a full build. Exit codes and counts are in §7.1.

### 7.1 Results

All four were run on 2026-10-19 on the branch with current main merged (`c3828f06`) and the warning fixes
(`97585cd4`).

| Check | Exit | Counts |
|---|---|---|
| `cmake --preset release --fresh` and `cmake --build build/release` | 0 / 0 | -- |
| `tools/ci/run-suite.sh cpu-assets` (under `tools/gpu-lock.sh`) | **0** | PASS: 4,383 cases, 4,363 passed, 0 failed, 19 skipped, 1 expected failure, in 24 m 11 s |
| `tools/ci/run-suite.sh gpu` | **0** | PASS: 679 cases, 678 passed, 0 failed, 1 skipped, in 47 m 04 s |
| `python3 tools/check_project_integrity.py` | **0** | 0 problems |

The one `FAILED:` line in the CPU logs (`test_character_lab_slopes.cpp:187`) is the suite's expected failure, which the
summary counts as `expected-failures 1`; it is not one of this branch's cases.
