# Stems + multichannel: implementation plan (Phase 1)

2026-10-19. This builds on `02-research-report.md` (cited below as R§n). ADRs 1520-1529 belong to this branch.
**No production code is written until gate 1 is approved.**

## 1. Shape of the solution

**The smallest architecture that serves both offline and live, measured against the code:**
- **One analysis-source vocabulary on the bus, `stem.<key>.*` (R§8).** An offline stem file and a live input channel
  publish it identically, and every route, the modulator and the route picker take it unchanged.
- **One compact per-source analysis product (`StemFrame`).** It is computed by the **existing `Analyzer`**, plus
  the existing `CausalOnsetDetector` when the "hits" tier is on.
- **Offline:** a `StemTrack` per stem, prepared at load, consumed by a cursor in `SignalClock`, the same way
  `consumeAnalysis` consumes the mix. So seek equals play by construction, plus the replay mapping (R§5.5).
- **Live:** the capture callback splits the channels it already walks into per-source SPSC rings (the existing
  `AnalysisStream`). One new analysis thread steps all sources. The main mix, `audio.*`, the runner, the beat
  clock, Sonic and response are **untouched**.
- **Capture:** one device and one callback. The backend sits behind a seam: Core Audio AUHAL on macOS (recommended,
  §2.1), miniaudio elsewhere, and a synthetic backend for tests.

**Not built:** source separation, stem playback, summing stems into playback, correlation auto-alignment, a mixer
UI, a per-stem beat tracker or Sonic, a sample-rate converter, or system sample-rate changes.

## 2. Choices with real alternatives

### 2.1 How to capture every channel of a multi-stream device (the miniaudio #312 question)

Measured (R§3.2): miniaudio 0.11.25 hears only a multi-stream device's first stream. Asking it for more channels
fabricates copies. A 10-line patch and a raw AUHAL capture both get every channel right.

| | A. Status quo (miniaudio as is) | B. miniaudio + patch | **C. Narrow AUHAL backend (macOS), miniaudio elsewhere** |
|---|---|---|---|
| Every channel of a multi-stream device | **No**: silent loss, or fabricated copies | Yes (measured on a 1+2 aggregate) | Yes (measured, both stream orders) |
| Code | 0 | 10 lines patched into a CPM-fetched header (`CPMAddPackage ... PATCHES`), re-applied on every upgrade | About 300 lines of `__APPLE__` Core Audio in `src/audio/` (estimate, from the probe's 120-line AUHAL arm plus listeners) |
| Per-callback `AudioTimeStamp` (gap detection) | No | No: miniaudio does not pass it through | Yes (`mSampleTime`; measured 0 gaps in 750 callbacks) |
| Device lifecycle signals | miniaudio "stopped" fires, but its state stays *started* and callbacks continue (measured) | same | Our own `IsAlive` / device-list / rate / stream-config / overload listeners. These are needed under B as well, since miniaudio does not offer them |
| Stream layout (which channels are analog vs ADAT) | No | No: needs our own HAL queries anyway | Yes: the same HAL queries the backend already makes |
| Buffers | interleaved | interleaved | non-interleaved: no deinterleave |
| Windows / other | yes | yes | unchanged miniaudio there (WASAPI delivers multichannel as one stream) |
| Risk to today's BlackHole 2ch path | none | low: the patch only acts when the chosen format has fewer channels than AUHAL's device side, which never happens on single-stream devices | moderate: a different wrapper on macOS for every input. Mitigated by test M5-T6 (same samples as miniaudio on BlackHole 2ch) |
| Dependency on upstream | -- | carries a fork until upstream fixes #312 (open since 2021, tagged feature request) | none |

**Recommendation: C.**
- B still needs the HAL enumeration and listeners C needs, so B's real saving is the capture itself. In exchange it
  carries a patched third-party header and has no timestamps.
- C reuses what miniaudio already does internally (it *is* AUHAL), so this does not introduce a new OS API (brief
  §3.2).
- **Fallback if the owner prefers B:** the seam below is identical; only the macOS backend changes.

**What either way requires:** a `CaptureBackend` seam with three implementations. The synthetic one is what the
tests drive; the brief §8 requires the tests to exercise the same routing and analysis code as real capture.

### 2.2 Per-channel analysis threading

| | One `AnalysisRunner` (thread) per channel | **One `ChannelAnalysis` thread for all channel sources** |
|---|---|---|
| Cost | N threads, each polling every 1 ms when idle. Each carries a BeatTracker, history mutex and timbre tap that are not wanted per channel | One thread. Measured per-source work: 16 µs/hop (levels tier), about 95-114 µs (hits tier). 8 sources at the hits tier come to about 0.9 ms per 10.7 ms hop |
| Isolation | One slow channel cannot delay another | A slow hop delays the others by at most its cost. At these numbers that is under 1 ms |
| Code | Reuses the runner unchanged | About 150 lines: a loop over sources, each with its stream, analyzer and triple buffer |

**Recommendation: one thread**, with the per-hop time measured (mean and max) so the choice can be revisited if the
data says so.

### 2.3 Offline stem storage

| | `AnalysisTrack` per stem | **Compact `StemTrack`** |
|---|---|---|
| Memory | Measured **48.7 MB/min of frames, plus the PCM (121 MB per 3.5 min stereo)**. About 1.2 GB for 4 stems of a 4-minute song | About 64 B/frame (estimate), about 0.35 MB/min. PCM released after analysis |
| Time | Measured 2.2-2.4 s per stem (beat, meter and band-onset passes included) | Analyzer pass only, about 0.3 s per 3.5 min (estimate from the measured 16 µs/hop), +1.6 s with hits. Stems analysed in parallel |
| Reuse | All | The same Analyzer and CausalOnsetDetector. Only the storage differs |

**Recommendation: `StemTrack`.**

### 2.4 Namespace for live channels

`input.<key>.*` beside `stem.<key>.*`, or **one `stem.<key>.*` for both**. One namespace means a scene routed on
`stem.drums.level` offline performs live with the drum bus on an input labelled `drums`. That matters for an engine
whose scenes are made offline and played live (ADR-1025). It costs nothing and removes a duplicate vocabulary.
**Recommendation: one namespace.** In a session that has both (a file playing with stems while an input is open),
live input wins for a key it defines and the stem is shown as shadowed. This mirrors the engine's rule that live
input replaces the file runner.

### 2.5 A linked pair

Two options:
- Analyse L and R independently and expose both.
- Analyse **the pair as one source**: features of the mono sum (L+R)/2, plus `width` = side/mid RMS.

**Recommendation: the pair as one source.** That is exactly how the main mix and a stereo stem are analysed, so a
live stereo bus and the same bus as a stereo stem file publish comparable numbers. Two unrelated channels are never
summed unless the user makes them a pair. Independent L/R is still available by making two mono sources.

### 2.6 Sample-rate policy for stems

Analyse every stem at its own rate. Frame times are in seconds, and bands are in Hz (R§3.4). A stem at a different
rate from the mix is accepted, with an info diagnostic. **Not chosen:** resampling to the mix rate. It is not needed,
and the only resampler in the tree is linear (ADR-103's own warning).

### 2.7 Test fixtures

| | C++ generator inside the tests | **Python generator, run by the build into `build/`** |
|---|---|---|
| Brief | -- | "preferably Python" |
| Determinism | yes | yes: numpy 2.0.2 is pinned in `tools/ci/requirements.txt`, the seed is fixed, and floats are written as 32-bit WAV via the stdlib `wave` module |
| CI | always there | `add_custom_command` produces `build/<preset>/test-fixtures/stems/` before `avgen_tests` builds, so a test never SKIPs for want of it. A missing Python is a configure-time warning and an explicit test SKIP naming the cause |

**Recommendation: Python at build time.**

## 3. Milestones

Each milestone gets its own commit with its tests, and a one-line result to the coordinator.

**Legend:**
- **[CI]** fully automated, no hardware;
- **[local]** automated but needs this Mac's devices (BlackHole 2ch, private aggregate), SKIPs elsewhere;
- **[Volt]** needs the Volt connected;
- **[signal]** needs physical signals or external routing.

### M0. Baselines (no code)

- **Measure:** `avgen --live-profile --mode live --projection off --size 1920x1080 --target-fps 30` on **Cymatic
  World** and one lighter live scene.
  - The **existing BlackHole 2ch input** open; 30 s measured, two interleaved repeats, from this base commit.
  - Headless offline frame time on one project with audio.
  - `AnalysisRunner::averageHopMicros` live.
- **Output:** a table in `04-baseline.md`. **[local]**, under `tools/gpu-lock.sh`.
- **Acceptance:** the numbers exist and their run-to-run spread is stated.

### M1. Deterministic fixtures (brief §6) [CI]

- **Files:**
  - `tools/make_stem_fixture.py` (new, numpy + stdlib);
  - `tests/CMakeLists.txt` (custom command + dependency);
  - `tests/support/stem_fixture.hpp` (finds the directory and parses `fixture.json`).
- **Content:** 48 kHz, 20 s.
  - **drums:** kick impulses at known times, snare at others, hats on 1/8 subdivisions.
  - **bass:** notes with known on/off.
  - **synth:** short-attack plucks and sustained pads.
  - **pad:** a fourth stem, for arbitrary counts.
  - Deliberate gaps, overlaps and silence.
- **Derived files:**
  - `mix.wav`: a stereo mix of the stems with documented gains and pans. Generated only.
  - Variants: `bass_offset.wav` (shifted by +250 ms, with `offset` metadata), `synth_44k1.wav` (44.1 kHz), a 1 s
    short stem, an all-silent stem, a corrupt file (truncated header), `fixture.json` (every event time, label,
    gain, pan, offset and seed).
- **Tests:** the fixtures decode, and the metadata agrees with the decoded files (rates, frame counts, channels).
- **Acceptance:**
  - a byte-identical regeneration (two runs, same sha256);
  - under 1 s to generate (**estimate**);
  - no audio committed.

### M2. Stem analysis core (brief §5.3-5.4) [CI]

- **Files:**
  - `src/analysis/stem_track.{hpp,cpp}` (new): `StemFrame`, `StemTrack::analyze(const AudioFile&, StemAnalysisConfig)`
    (Analyzer, plus CausalOnsetDetector when `hits`), `frameAt(projectSeconds, offset)` returning
    `std::optional` (none outside the extent);
  - `src/audio/stem_validation.{hpp,cpp}` (new): `StemReport` (rate, channels, frames, duration, decoded format,
    NaN/Inf/silence scan, diagnostics against the mix: rate, duration, extent);
  - `src/signals/stem_signals.{hpp,cpp}` (new): `StemSignals::declare(bus, key, label, features)`,
    `publish(bus, const StemFrame&)`, `publishInactive(bus)`, `clearEvents(bus)`, `relabel`. Key slugging and
    dedup is a free function.
- **Interfaces:**
  - `struct StemFrame { double time; float rms, peak, level, onsetStrength, centroid, width; std::array<float,5> bands; bool onset, kick, snare, hat, stereo; }`
  - `struct StemSource { std::string key, label; std::filesystem::path file; double offset; bool spectral, hits; }`
- **Tests** (`tests/unit/test_stem_track.cpp`, `test_stem_signals.cpp`), on the M1 fixture:
  - each stem's onsets fall within ±1 hop (10.7 ms) + half a window of the metadata's events;
  - **a stem's events do not appear in the other stems' signals** (drums' kick times are not onsets in bass,
    outside overlap windows);
  - bass `level` is above the floor inside notes and falls within 1 s (its smoothing) after them;
  - silent and absent spans publish 0 and no onset;
  - the offset stem's onsets shift by exactly the offset;
  - the 44.1 kHz stem's events land at the same seconds;
  - the corrupt file yields a decode diagnostic naming it;
  - running twice gives identical frames (same machine);
  - property tolerances, no golden floats.
- **Acceptance:** all pass; memory per stem-minute measured and recorded.

### M3. Engine and project integration, offline (brief §5.1-5.6) [CI]

- **Files:**
  - `src/app/engine.{hpp,cpp}`:
    - `stems_` (sources + tracks + reports);
    - `setStems(std::vector<StemSource>)`;
    - analysis at `installAudio`/`setStems`, in parallel worker threads, joined;
    - `SignalClock::stemCursors`;
    - `consumeStems()` beside `consumeAnalysis()`, called from `update()` and `ReplaySignals::build()`;
    - the replay copies stem values back by name;
    - stem ids marked samplable;
  - project save/load (`assets.stems`), with `tools/check_project_integrity.py` aware of the key.
- **Behaviour:**
  - playback is the mix only;
  - `stem.<key>.*` is published per render frame in both engine modes (offline: render time; live editor with a
    file: the transport second);
  - absent or failed stems publish inactive.
- **Tests** (`tests/integration/test_stems_offline.cpp`, and a case in `test_engine_seek_parity.cpp`):
  - the installed playback `AudioFile` is byte-identical with and without stems (**no summing**), and its
    `AudioPlayer` source is the mix;
  - a headless render with a route `stem.drums.onset → x` moves `x` on drum events only;
  - **seek to t equals play to t** for stem values and a stem route with smoothing;
  - frame timestamps stay consistent at 24/30/60 fps (event counts equal across rates);
  - a missing stem: warning naming the file, the signals stay 0, the mix still plays;
  - save → load round-trips keys, labels and offsets; renaming a label keeps routes bound;
  - duplicate labels get distinct keys.
- **Perf:** load time and RSS with 0/3/8 stems (fixture and the 3.5-minute stand-in); headless frame time unchanged
  within noise.
- **Rollback:** the feature is inert without `assets.stems`. Reverting the commit restores main's behaviour.

### M4. Offline stems UI (brief §10) [CI for logic, manual for look]

- **Where:** a compact **Stems** section beneath the audio lane list in the existing audio panel (the Sequence panel's
  audio area, where clips are edited).
  - Per stem: label (editable), key (shown; editable behind a small "edit key" affordance with the routes warning),
    file, offset (seconds), status (rate/channels/duration, and the warning or error text), spectral/hits toggles,
    remove.
  - Add stem…, and a preparation status ("analysing 3 stems…").
  - `stem.*` appears in the existing route-source picker with its labels; no new picker.
- **Tests:** the UI logic functions that the panel calls (key edits, add/remove) are unit tested, as the repository
  does for panel logic (`ui_logic`). The look is checked in a screenshot for the owner in
  `~/Desktop/av-gen-review/72-stems-multichannel/`.

### M5. Capture seam, synthetic backend, channel analysis, engine live sources (brief §7, §8) [CI]

- **Files:**
  - `src/audio/audio_input.{hpp,cpp}`: `AudioInput` keeps its public API and today's mono path, and gains
    `setSources(std::vector<InputSource>)`, `channelStream(i)`, `channelPeak(i)`, `stats()`;
  - `src/audio/capture_backend.hpp` (new): `CaptureBackend` (`open(deviceId)`, `start`, `stop`, `channels()`,
    `sampleRate()`, callback `onBlock(const float* const* channels, frames, sampleTime, hostTime)`);
  - `src/audio/capture_miniaudio.cpp`: the existing miniaudio code, moved, with deinterleave in its callback;
  - `src/audio/capture_synthetic.{hpp,cpp}` (new, **test-only**):
    - not in any device list;
    - its device id is `synthetic:<n>`, and it is constructed only by tests and the benchmark flag;
    - deterministic per-channel generators (impulses at known frames, sines at per-channel frequencies), driven by
      a caller (tests) or a real-time timer thread (benchmarks);
  - `src/analysis/channel_analysis.{hpp,cpp}` (new): one thread; per source an `AnalysisStream` reader, `Analyzer`
    (+ causal for hits), `TripleBuffer<StemFrame>` and event carry; per-hop µs EMA and max;
  - `src/app/engine.cpp`: live sources declared as `stem.<key>.*` and published from `ChannelAnalysis` each frame;
    the stale rule (no frame for 250 ms → inactive); shadowing a file stem of the same key.
- **The callback:** for each enabled source, copy (mono) or sum/2 (pair) into a preallocated scratch and write it to
  the ring; per-channel peak; the main mono downmix exactly as today.
  - An overflow sets a pending flag. The next write that fits is preceded by `markDiscontinuity(trueFrame)`.
  - Counters are relaxed atomics.
  - Sources change only with the device stopped.
- **Tests** (`tests/unit/test_multichannel_input.cpp`, `test_channel_analysis.cpp`), 8 synthetic channels:
  - channel *k*'s impulse train reaches source *k* only;
  - no swaps (distinct frequencies identified by band);
  - no sums (a pair's mono sum equals (L+R)/2 within float tolerance);
  - all enabled channels processed;
  - disabling source 3 leaves the others' frame counts and values unchanged;
  - a source on an absent channel (9 on an 8-channel device) is rejected with a diagnostic;
  - stopping capture → inactive within the stale window, and no event asserted after;
  - a forced consumer stall → drops counted, a discontinuity marked, no event spliced across;
  - main `audio.*` from the synthetic downmix equals today's downmix of the same input.
- **TSan:** the new tests join `tools/ci/sanitizer-plan.txt`'s concurrency set.
- **M5-T6 [local]:** on BlackHole 2ch, the main mono stream through the miniaudio backend and through the new path
  carry the same samples (cross-correlation peak at lag L, then sample equality after alignment; tolerance 0).
- **Acceptance:** all of the above; the callback's per-block cost measured with the synthetic backend at 8 and 24
  channels.

### M6. Core Audio backend and device model (R§3, R§6.4) [CI for parsing; local for devices; Volt pending]

- **Files:**
  - `src/audio/capture_coreaudio.mm` (or `.cpp` with the C API; `__APPLE__` only): AUHAL as in the probe.
    - float32 non-interleaved, the device's channel count from `StreamConfiguration`, the nominal rate, a 128-frame
      buffer request;
    - `mSampleTime` gap detection;
    - listeners: system device list; per device `IsAlive`, `NominalSampleRate`, `StreamConfiguration`,
      `ProcessorOverload`, `IOStoppedAbnormally`. Each listener only sets an atomic. The main thread acts on it:
      mark disconnected; on a format change, stop → re-read → restart, keeping the source map, and drop sources
      whose channel no longer exists with a diagnostic;
  - `src/audio/audio_devices.{hpp,cpp}`: `listCaptureDevices()` gains `uid`, `channels`, `streams` (channels per
    stream), `sampleRate` and `rates` on macOS via the HAL; elsewhere via miniaudio. It replaces the per-call
    `ma_context` scan on macOS with a cached list refreshed on the device-list listener. That removes the Live
    panel's 3-second polling scan (`listCaptureDevices` at UI rate today).
  - Selection by UID, with today's name substring kept as the match for a value that is not a UID.
- **Tests:**
  - [CI] UID/name matching logic; stream-table → channel numbering; format-change handling against the synthetic
    backend (it can change its channel count and rate on command).
  - [local] a private aggregate (mic + BlackHole 2ch) opens with **3** channels, and a known tone lands on channels
    2-3. The test creates and destroys the aggregate itself; SKIP without BlackHole 2ch.
  - [local] destroying the aggregate mid-capture → disconnected within 250 ms, sources inactive, no crash, and
    reopening another device works.
- **Acceptance:** BlackHole 2ch through the AUHAL backend gives the same result as M5-T6; the aggregate test passes;
  the UI-thread enumeration cost is measured.

### M7. Live UI and settings (brief §10, §11) [CI for logic; screenshot for look]

- **Live panel,** under "Audio input": a compact **Inputs** table, shown when the device has more than 2 channels
  or any source is defined.
  - Per row: channel number(s) (with the stream boundary marked, e.g. "9-24 ADAT"), meter, label, key,
    mono/pair (pairs with the next channel), hits toggle.
  - Status line: "capturing 8 of 24 channels at 48 kHz", "disconnected: Volt 876 (waiting for it)", drops/gaps
    since open.
  - The device combo shows each device's channel count.
  - A disconnected configured device stays listed as "(not connected)", and the engine never silently switches to
    another device: the brief §11, mirroring the projection display's "not connected" convention
    (`live_panel.cpp`).
  - Main mix: "all channels" (default) or a pair.
- **Settings:** `live.audioInput` (UID or name) and `live.inputSources[uid] = [{channel, pair, key, label, hits}]`.
- **Tests:** the settings round-trip; the source-map apply/remove logic.

### M8. Benchmarks (brief §9) [local, under the GPU lock]

- The R§9.2 matrix.
  - Arms 1-6 with the synthetic backend driven in real time (`--input synthetic:8`, a flag accepted only with
    `--live-profile`).
  - The same arms on BlackHole 16ch if the owner installs it.
  - Arms 1, 3 and 6 on the Volt when connected.
- Event-to-signal latency from the synthetic backend's stamped impulses.
- Results go into `05-benchmarks.md` against M0.
- **A regression is profiled to its cause and fixed before anything compensating is added.**

### H. Volt hardware validation (brief §3.3) [Volt] [signal]: pending until the owner connects it

1. `probe_devices list`, or the M6 enumeration: name, UID, transport, channels, **stream configuration**, rates.
   Repeat at 96 kHz.
2. Capture each analog input 1-8 independently with a physical signal (a mic or line source moved input by input, or
   eight mixer bus outputs). Check that only the matching source moves. Record the signal used per input.
3. ADAT 9-24: enumeration and silence only, unless an ADAT source exists.
4. Unplug/replug during capture: disconnect shown, no crash, recovery on replug.
5. Arms 1, 3 and 6 of the benchmark on the Volt.

**Nothing here is claimed until it is run.** I message the coordinator to ask the owner to connect the Volt when M6
is done.

### M9. Docs, audit, final checks (brief §12)

- **Docs:** `docs/` user and engine docs (stems workflow, alignment policy, fixture generation, signal names and
  timing, live device/channel setup, Volt test instructions, external routing for independent sources, benchmark
  procedure, limitations). ADRs in 1520-1529 for the decisions in §2.
- **Audit:** the final diff for dead code, duplicated analysis and unneeded abstractions.
- **Checks:** clean reconfigure + full build; `tools/ci/run-suite.sh cpu-assets`, `tools/ci/run-suite.sh gpu` and
  `python3 tools/check_project_integrity.py`, all exit 0, with counts.
- **Report:** `06-report.md`, per brief §12-13: what was done, what wasn't, hardware tests pending, benchmarks,
  scenes and workflows actually tested.

## 4. Classification summary

| Work | No hardware | This Mac's devices | Volt | Physical signals / routing | Fully in CI |
|---|---|---|---|---|---|
| M1-M3 fixtures, stem analysis, offline engine | yes | -- | -- | -- | yes |
| M4 UI logic / M7 settings logic | yes | -- | -- | -- | yes |
| M5 seam, synthetic 8-channel, channel analysis | yes | -- | -- | -- | yes (incl. TSan) |
| M5-T6, M6 aggregate + destroy tests | -- | BlackHole 2ch, mic | -- | -- | no (SKIP in CI) |
| M0/M8 live benchmarks | -- | GPU, BlackHole | arms on the Volt | -- | no |
| H Volt | -- | -- | yes | yes (a source per input; ADAT source for 9-24) | no |
| Independent musical sources live | -- | -- | -- | a mixer with discrete bus outputs into the inputs (documented in M9) | no |

## 5. Failure modes and rollback

| Failure | Behaviour |
|---|---|
| Stem decode error | Diagnostic naming the file; signals inactive; the mix unaffected |
| Stem shorter or longer than the mix, or at another rate | Diagnostic; publishes inside its extent only |
| The project's stem keys collide | Deduplicated on load with a warning |
| Live device absent at start | The saved source map is kept, the device shown "(not connected)", its sources inactive; no silent substitution |
| Device disappears / format changes | Disconnected → inactive. A format change restarts capture; sources on vanished channels are dropped with a diagnostic |
| Consumer falls behind | Ring drop counted, a discontinuity marked, analyzer reset; no spliced windows, no stuck events |
| Callback | Never blocks: rings, atomics, no allocation |
| Rollback | Every milestone is a separate commit. M3/M5 are inert without `assets.stems` / defined sources. The backend switch (M6) can be reverted alone to put macOS back on miniaudio |

## 6. Questions for gate 1

1. **Backend: C (own AUHAL, recommended) or B (patched miniaudio)?**
2. **One `stem.<key>.*` namespace for offline stems and live inputs (recommended)?**
3. **BlackHole 16ch** needs the owner to type an admin password (`brew install --cask blackhole-16ch` in Terminal).
   It is optional: the private-aggregate tests cover multi-stream, and the synthetic backend covers 8+ channels.
4. **The live source map:** per machine only (recommended, like the device choice), or also named in the project?
