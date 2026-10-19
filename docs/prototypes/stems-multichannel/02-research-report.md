# Stems + multichannel: research report (Phase 0)

2026-10-19, branch `eng/stems-multichannel`, based on main `99d45f13`. This answers brief §3.5. Everything marked
**measured** was run on this machine (MacBook Pro, Apple M2 Max, 12 CPU cores, 64 GB, macOS 26). The probes are
throwaway programs in `build/probes/` (not committed); their outputs are quoted here. **Estimate** marks anything
that was not measured.

**No Universal Audio Volt was connected** during this phase. §4 lists what is published about the Volt 876 and
what has to be checked on the device. Nothing in this report is a result from the Volt.

---

## 1. The current architecture and its code paths

### 1.1 The live capture path (input device to bus)

| Step | Where | What it does | Thread |
|---|---|---|---|
| Enumerate | `audio::listCaptureDevices()`, `src/audio/audio_input.cpp:72` | Creates a throwaway `ma_context`, lists capture devices as `{name, id, isDefault}`, and clears duplicate "default" flags. It logs that CoreAudio once reported both "Volt 876" and "MacBook Pro Microphone" as default (lines 81-90). So the owner's Volt 876 has been enumerated on this machine before. | UI. The Live panel calls it every 3 s (`live_panel.cpp:80`) and the transport combo every 5 s (`control_panel.cpp:2006`). |
| Device id | `deviceIdString()`, line 40 | On Core Audio it returns miniaudio's `ma_device_id::coreaudio`, which miniaudio fills from `kAudioDevicePropertyDeviceUID` (miniaudio.h:33944). That is a stable id, **but `open()` never uses it**: it matches a case-insensitive **name substring** (`nameMatches`, line 36). | UI |
| Persist | `AppSettings::live.audioInput`, `src/app/settings.hpp:128` | The device name substring, stored per machine, not per project (ADR-1025). | UI |
| Open | `AudioInput::open()`, line 186 | Opens a miniaudio capture device with `f32`, `channels = 0` (the device's native count, as miniaudio counts it), `sampleRate = 0` (native) and a 128-frame period (ADR-1025: measured lower latency). The engine calls it from `Engine::useAudioInput()` (`engine.cpp:5038`), which replaces the playback runner with an input runner. | UI / main |
| Callback | `Impl::capture()`, lines 140-166 | Real-time clean: no allocation, locks, logging or miniaudio calls. It **averages every native channel to mono** (`scale = gain / channels`), tracks the block peak, writes 8192-frame chunks into the `AnalysisStream`, and bumps a frame counter. | Core Audio IO thread |
| Hand-off | `AnalysisStream`, `src/audio/analysis_stream.*` | A lock-free SPSC float ring (65,536 samples, about 1.37 s at 48 kHz) plus a 32-entry SPSC queue of discontinuity markers. On overflow `write()` **drops the newest samples** (`SpscRingBuffer::write` returns the count written). Nobody counts the drops today. | producer: IO; consumer: analysis |
| Analysis | `AnalysisRunner::threadMain()`, `src/analysis/analysis_runner.cpp:98` | One `jthread`. It reads up to a hop (512 samples) at a time and **sleeps 1 ms when the ring is empty**. Each frame goes through `Analyzer` → `BeatTracker` → `CausalOnsetDetector` (ADR-1060) → `FrameTap` (the Sonic timbre stage, ADR-1025) → event carry → history ring (under a mutex; the UI reads it) → `TripleBuffer<AnalysisFrame>`. It keeps an EMA of the per-hop cost in `averageHopMicros()`. | analysis |
| Publish | `Engine::update()` → `runner_->acquire()` → `publishFrame()` (`engine.cpp:6244`, `5112`) | The render thread takes the newest frame once per render frame. `LiveEventLatch` fires a carried event once, and `AudioSignals::publish()` writes the fixed `audio.*` vocabulary. With no new frame it clears the event pulses and keeps the levels. | main/render |

There is exactly **one runner per engine**, fed by either `player_->analysisStream()` (a file playing in the live
editor) or `input_->analysisStream()` (a live device). Both are created at `engine.cpp:4275` and `5051`.

### 1.2 The offline path (file to bus)

| Step | Where | What it does |
|---|---|---|
| Decode | `AudioFile::load()`, `src/audio/audio_file.cpp:12` | `ma_decoder` decodes to `f32` at the file's native rate and channels, so WAV, FLAC and MP3 come for free. It precomputes a mono downmix (the average of all channels). |
| Arrangement | `mixArrangement()`, `src/audio/arrangement.*` (ADR-103) | One or more clips are **mixed down** to one `AudioFile`. Clips at a different rate are resampled **linearly** and warned about. One plain clip is bit-identical to its file. Note: ADR-103's header names "a stem set" as a use. A mixdown is exactly what this brief forbids for stems, so stems cannot be clips. |
| Analyze | `AnalysisTrack::analyze()`, `src/analysis/analysis_track.cpp:206` | The same `Analyzer` over the whole file, with the side channel `(L - R) / 2` of the first two channels for `audio.width`. Then the passes that need the whole track: the Ellis beat tracker plus the ADR-896 grid refinement, the meter estimate, the ADR-898 band onsets, and the ADR-1060 causal onsets. It runs at `Engine::installAudio()` in **both** engine modes (`engine.cpp:4310`). |
| Consume | `Engine::consumeAnalysis()`, `engine.cpp:5136` | Walks a cursor (`SignalClock::analysisCursor`) over every frame whose centre is at or before the render time. It ORs the events of the batch with their strongest strength (so 30 fps loses no onset), feeds `MusicRuntime` every frame, and publishes the last frame of the batch. |
| Seek | `Engine::seekSeconds()` (`engine.cpp:~4445`), `Engine::ReplaySignals` (`engine.cpp:120-330`, ADR-870/901) | Offline with a track, a seek **replays the signal pipeline from zero** on a copy of `SignalClock` and its own bus, then copies the replayed bus values back **by index**. That only works because `declareFrameSignals()` declares the deterministic signals first and in a fixed order on both buses. Routes on pure-in-time sources and staging signals (ADR-930) are replayed through a `samplable_` mask over the engine bus's ids. **A signal that is not on the replay bus is not replayed:** a route on it lands reset after a seek. |
| Live playback of a file | `overlayTrackFields()`, `analysis_track.cpp:257` | In the editor, the live runner analyses what the device plays, and the whole-track beat and band-onset fields are overlaid at the same position, so the editor hears what a render hears. |

### 1.3 Signals, routes and parameters

- **`SignalBus`** (`src/signals/signal_bus.*`, ADR-011): name → dense id, `float` value, an event flag, a `[min, max]`
  range and an artist-facing label. `declare()` is idempotent, and **ids live for the life of the bus**: nothing can
  be undeclared. `find()` records that a name was asked for (ADR-951), so a producer can skip work nobody routes.
  The bus is main-thread only. `clearEvents()` runs at the end of every frame.
- **`AudioSignals`** (`src/signals/audio_signals.*`): the fixed `audio.*` vocabulary, with hardcoded names. It has no
  prefix parameter.
- **Sources** (`src/signals/source.hpp`): LFO, envelope, noise, random, timeline, control, macro and publish, each with
  `<kind>.<name>[.<output>]` names (`lfo.wobble`, `env.kick`, `control.cc1`, `macro.k1`, `fx.heroGlow.gain`). Settings
  live under `sources/<name>/...`. `pureInTime()` (ADR-901) decides whether a seek can replay them.
- **Routes** (`src/params/modulation.hpp`, `ModRoute`): `source` is a **signal name** resolved to a `SignalId` by
  `Modulator::bind()`. Then come the processor chain, `amount`, the op, an optional `depthSource`, and the target
  parameter component. A route on a name that does not exist yet stays unbound and resolves on a later `bind()`.

**Traced end to end, `audio.bass` → a scene parameter (offline):**
1. `AnalysisTrack::analyze` → `Analyzer::computeFrame()` sums `|X_k|^2` over the 20-150 Hz bins, takes the square
   root, and divides by the running maximum (4 s decay): `frame.bands[0]` (`analyzer.cpp`, "bands").
2. `Engine::update` → `consumeAnalysis(clock_, bus_, renderTime)` → `AudioSignals::publish` →
   `bus.set(bass, frame.bands[0])` (`audio_signals.cpp:61`).
3. `sources_.update`, then `Modulator::evaluate` (`modulation.hpp:115`): for each route with `sourceId == audio.bass`,
   chain(value) × amount → op onto `IParameter` final.
4. The scene reads the parameter's final value when it builds the frame.

Live, step 1 becomes the runner thread's `Analyzer` and step 2 becomes `publishFrame()`. Steps 3-4 are identical.

**So the architecture already supports many signals from many producers. The pieces that are single-source are the
producers: one `AnalysisRunner`, one `AnalysisTrack`, one `SignalClock` cursor, one `AudioSignals` vocabulary, and
one mono capture stream.** The bus, the routes, the modulator, the parameters, the UI's route-source picker (it lists
the bus's `infos()`) and the liveness rules are all name-based, so they take new names without change.

### 1.4 Configuration, telemetry and tests

- **Enablement:** `--input [name]` (`application.cpp:2231`), the transport combo, the Live panel's "Audio input"
  combo (`live_panel.cpp:476`, saved to settings), and `liveSonicOpensSavedInput()` (ADR-1408).
- **Telemetry:** `EngineStats::analysisHopMicros` and `analysisFrames`, plus `avgen --live-profile` (ADR-1090/1490):
  frame p50/p99, GPU p50/p95/p99, quality levels and missed frames, live or headless.
- **Tests:**
  - `tests/unit/test_audio_input.cpp` SKIPs without a device or permission.
  - `test_analysis_stream`, the analyzer tests, and `tests/integration/test_engine_seek_parity.cpp` (seek equals
    play).
  - The TSan set is in `tools/ci/sanitizer-plan.txt`.
  - `tools/ci/generate-audio.sh` regenerates the repository's own scores (stdlib Python and numpy 2.0.2, pinned in
    `tools/ci/requirements.txt`).
- **CI runs on macOS 26 runners, a self-hosted Windows runner (`windows.yml`), and an ubuntu reporting job.** So
  `avgen_core` must stay portable: any Core Audio code has to sit behind `__APPLE__`, with miniaudio still the
  capture backend elsewhere.
- **Packaging:**
  - `avgen` is a plain executable, not an `.app` bundle. There is no `Info.plist` and no entitlements file.
  - Microphone permission (TCC) is therefore granted to the responsible process (the terminal or IDE that launched
    it). It was already granted here: the probes captured the built-in microphone with real signal.
  - Nothing in this work changes that.
  - A future `.app` bundle would need `NSMicrophoneUsageDescription` and, with the hardened runtime, the
    `com.apple.security.device.audio-input` entitlement (§3.8).

## 2. Stereo assumptions and extension points

| # | Assumption | Where | Kind |
|---|---|---|---|
| 1 | The live input is averaged to **one** mono stream; per-channel data is discarded at the callback. | `audio_input.cpp:142-158` | **Hard**: the only capture-side collapse. |
| 2 | Capture channel count = what miniaudio reports. **Measured: on a multi-stream device that is the first stream only** (§3.2). | miniaudio 0.11.25 Core Audio backend | **Hard, and silent.** |
| 3 | Device selection by name substring. | `AudioInput::open`, `settings.live.audioInput` | Fragile: two devices that share a substring. |
| 4 | One `AnalysisRunner`, one `AnalysisStream` and one `TripleBuffer` per engine. | `engine.hpp` (`runner_`), `engine.cpp:4275/5051` | Single-source, but the class itself is reusable per source. |
| 5 | One `AnalysisTrack` (`track_`), one cursor (`SignalClock::analysisCursor`). | `engine.cpp`, `engine.hpp:90` | Single-source. |
| 6 | One fixed `audio.*` vocabulary with literal names. | `audio_signals.cpp:14-56` | Single-source; easy to parametrise. |
| 7 | `audio.width` = side/mid of the **first two** channels; mono files say `stereo = false`. | `analysis_track.cpp:221`, `analyzer.cpp` | **Stereo-specific feature.** Meaningless for a mono stem or a mono input channel. |
| 8 | `AudioFile::mono()` = the average of all channels. | `audio_file.cpp:118` | Fine for stems (a stereo stem is analysed as its mono sum, like the mix). |
| 9 | `AudioPlayer` outputs a fixed channel count, plays one file, and feeds one mono analysis stream. | `audio_player.cpp:176` | Playback stays exactly this. Stems are never played. |
| 10 | Meters: one peak bar. | `control_panel.cpp:2024`, the Live panel | UI. |
| 11 | The beat clock, `music.*`, `sonic.*`/`timbre.*`/`notes.*`, `response.*` (ADR-1510, in flight), `AudioHistory` (the GPU spectrum ring) and the reactivity proposer all read **the** frame. | `engine.cpp` `advanceClock`, `updateAudioHistory`, `src/sonic/*`, `src/directing/reactivity*` | Intentionally main-mix only in this work (see §5.6). |

**Extension points that already exist:**
- `SignalBus::declare` with arbitrary names, labels and event flags.
- `Analyzer` is self-contained and movable; one instance per source works without change.
- `AnalysisTrack::analyze` takes any `AudioFile`.
- `AnalysisStream` is a complete SPSC sample ring with discontinuity markers, reusable per channel.
- The ADR-930 staging-signal precedent shows how signals outside the replay prefix are marked replayable.
- `Engine::consumeAnalysis`'s batch/OR logic is the pattern for per-stem consumption.

## 3. Apple documentation, probes and how they apply here

### 3.1 Capture API: AUHAL is what is already running

miniaudio's Core Audio backend **is** an AUHAL (`kAudioUnitSubType_HALOutput`) capture: `EnableIO` on input bus 1
and `CurrentDevice`, with the client format set on the output scope of bus 1 (miniaudio.h:35828-36000). So
"AUHAL vs miniaudio" is not a choice between two OS APIs. It is a choice between **miniaudio's AUHAL wrapper**
and **our own narrow AUHAL wrapper**. TN2091 (Apple, "Device input using the HAL Output Audio Unit") is the canonical
recipe for either one, and its two rules apply directly:
1. The AUHAL's internal converter does only *simple* conversions, **not sample rate**. Capture at the device's
   nominal rate; miniaudio follows that rule too (miniaudio.h:35907-35948).
2. Input is pulled with `AudioUnitRender` from the input callback into a buffer list you own.

Apple's sample code and HAL utilities (`CAHALAudioDevice.cpp`, `CAPlayThrough/AudioDevice.cpp`) query
`kAudioDevicePropertyStreamConfiguration` per scope to count channels. That is the **sum of `mNumberChannels` over
the AudioBufferList's buffers, one buffer per stream**. The SDK header says so directly (`AudioHardware.h`):
*"returns the stream configuration of the device in an AudioBufferList … which describes the list of streams and the
number of channels in each stream. This corresponds to what will be passed into the IOProc."*

**AVAudioEngine** (`inputNode`) was considered. It also sits on AUHAL. On macOS it follows the system default
input unless you reach into its audio unit to set `kAudioOutputUnitProperty_CurrentDevice`. It gives no clean
per-device control and pulls in an Objective-C++ dependency. It buys nothing here.

### 3.2 Multi-stream devices: measured (miniaudio issue #312)

miniaudio counts channels from `kAudioDevicePropertyPreferredChannelLayout` (`ma_get_AudioObject_channel_count`,
miniaudio.h:34132). It then picks a client format from `kAudioStreamPropertyAvailableVirtualFormats` **queried on
the device object** (miniaudio.h:34070). Those are per-stream formats, so on a device with several input streams no
listed format carries the device's full channel count. `ma_find_best_format__coreaudio` falls back to the first
supported one, which has the **first stream's** channel count. AUHAL is then told to deliver that many channels and
drops the rest.

**Probe** (`build/probes/probe_devices.cpp`): a **private aggregate device** (`kAudioAggregateDeviceIsPrivateKey = 1`,
visible only to the creating process and gone when it exits). This is a genuine multi-stream input device. A known
tone is played into BlackHole 2ch (L 440 Hz, R 1250 Hz) while the device is captured three ways. **Measured:**

| Aggregate (stream order) | Core Audio reports | miniaudio, `channels = 0` (as `AudioInput` opens it) | miniaudio, `channels = 3` | miniaudio + 10-line patch | Own AUHAL, all channels |
|---|---|---|---|---|---|
| Mic (1) + BlackHole 2ch (2) | 3 ch in 2 streams (1+2), layout 3 ch | **1 ch** (the mic). The tone channels are **lost**. | 3 ch, **all three are copies of the mic** (identical RMS 0.03285): miniaudio upmixed one internal channel | 3 ch: mic, 440 Hz, 1250 Hz | 3 ch: mic, 440 Hz, 1250 Hz |
| BlackHole 2ch (2) + Mic (1) | 3 ch in 2 streams (2+1) | **2 ch** (440, 1250). The mic is lost. | n/a | n/a | 3 ch: 440, 1250, mic |

So, on this miniaudio version:
- **Every channel after the first stream is silently dropped.**
- **Asking for more channels fabricates them.** That is the worst failure for this brief: the channels look plausible
  and carry the wrong source.
- Single-stream devices (BlackHole 2ch, the MacBook mic) capture correctly: both paths give identical per-channel
  RMS on BlackHole 2ch (0.11147 vs 0.11154, the 0.25 s settling skip aside).

The patch (`build/probes/patched/miniaudio.h`): for capture, keep AUHAL's device-side channel count when the chosen
format has fewer. That one change makes miniaudio deliver all three channels in the right order. See plan §2.1 for
the decision.

**What this means for the Volt 876:** if it presents its analog and ADAT inputs as more than one Core Audio input
stream, the current `AudioInput` hears only the first stream. Whether it does is **unknown until the device is
connected**. The probe's `list` mode prints exactly that (`kAudioDevicePropertyStreamConfiguration`, per-stream
starting channel, virtual and physical formats) and is the first hardware check in the plan.

### 3.3 Formats, interleaving, channel layout

- **What the HAL streams report:** BlackHole and the built-in mic report 32-bit float virtual formats.
- **Client side:** with our own AUHAL we choose **float32, non-interleaved** on the client side. `AudioUnitRender`
  then fills one buffer per channel, so there is **no deinterleave in the callback**. Measured: the probe's AUHAL
  arm captured 3 channels this way.
- **miniaudio:** delivers **interleaved** frames. `AudioInput`'s callback already walks them.
- **Channel numbering:** the AUHAL device-side channel order is the stream order: stream *k* starts at
  `kAudioStreamPropertyStartingChannel` (measured: 1 and 2 for the 1+2 aggregate).
- **No remapping:** AUHAL does not remap channel layouts by itself (an Apple engineer on the developer forums,
  thread 832462). We never need a layout remap: a source is "device channel *n*", numbered as the device numbers
  it, which is what an interface's front panel and its own console show.
- `kAudioOutputUnitProperty_ChannelMap` could restrict which device channels AUHAL converts. It is an optimisation
  for very wide devices that we do not need at 24 channels (§9: copying 24 channels costs about 3.6 µs per 128-frame
  callback).

### 3.4 Sample rate

- AUHAL cannot convert rates (TN2091). The capture runs at the device's nominal rate.
- `AudioInput` already passes `sampleRate = 0`. Nothing in the engine asks it for another rate.
- The `Analyzer` is configured per source with `sampleRate` (`AnalyzerConfig::sampleRate`). Its bands are defined in
  Hz, and frame times are `frameIndex / sampleRate`. **So analysis needs no resampling: each source is analysed at
  its own rate and published in seconds.**
- That holds offline too: a 44.1 kHz stem beside a 48 kHz mix is analysed at 44.1 kHz (§5.3). Its hop is 11.6 ms
  instead of 10.7 ms. The existing linear resampler in `arrangement.cpp` is not needed and is **not** used for stems:
  it is audible on music (its own warning), and analysis does not need a common rate.
- Changing the device's nominal rate is a system-wide action (TN2091; miniaudio's `allowNominalSampleRateChange`). AV
  Gen must **not** do it. A rate change made elsewhere (Audio MIDI Setup, the UA console) is a **format-change event**:
  listen to `kAudioDevicePropertyNominalSampleRate` and `kAudioDevicePropertyStreamConfiguration`, then restart the
  capture. On the Volt 876 this also changes the **channel count**: ADAT drops from 8 to 4 channels per port above
  48 kHz (S/MUX).

### 3.5 Real-time safety

- **The rule set:** no allocation, locks, syscalls, logging, Objective-C messaging or unbounded work on the IO
  thread. The industry reference is Ross Bencina, "Real-time audio programming 101: time waits for nothing"
  (http://www.rossbencina.com/code/real-time-audio-programming-101-time-waits-for-nothing). TN2091's callback does
  only `AudioUnitRender` into a preallocated list.
- **`AudioInput` already complies.** The design keeps the same properties:
  - every per-channel ring is preallocated at open;
  - the callback does copy + peak only;
  - the channel → stream table is immutable while running (it is swapped only with the device stopped);
  - the counters are relaxed atomics.

### 3.6 Device lifecycle and notifications

**Listeners:**
- Device add/remove: `kAudioHardwarePropertyDevices` on the system object.
- Per device: `kAudioDevicePropertyDeviceIsAlive`, `kAudioDevicePropertyNominalSampleRate`,
  `kAudioDevicePropertyStreamConfiguration` (input scope), `kAudioDeviceProcessorOverload` and
  `kAudioDevicePropertyIOStoppedAbnormally`.
- The SDK header documents `ProcessorOverload` as *"notified when the AudioDevice detects that an IO cycle has run
  past its deadline … usually sent from the AudioDevice's IO thread"*. So its listener must only bump an atomic.
- Apple's `AudioDeviceNotify` sample (`CAHALAudioDevice.cpp`) and VLC (2014 commit) found `IsAlive` alone unreliable
  and also check the device list.

**Probe** (`build/probes/probe_unplug.cpp`): the private aggregate is **destroyed mid-capture**. This simulates an
unplug; a real USB unplug is a hardware test. **Measured:**

| | miniaudio capture | Own AUHAL capture |
|---|---|---|
| `IsAlive` listener | fired 5 ms after destroy | fired 5 ms after destroy |
| Device-list listener | fired about 70 ms after destroy | fired about 75 ms after destroy |
| `ma_device` notification | "stopped" fired, **but `ma_device_get_state` still reports *started*** | n/a |
| Callbacks after destroy | **kept coming**: 538 in 1.5 s | **kept coming**: 133 in 1.5 s (512-frame periods) |
| `kAudioOutputUnitProperty_IsRunning` | n/a | still 1 |

**Conclusion:** neither "the callback stopped" nor miniaudio's device state is a reliable disconnect signal. The
`IsAlive` listener (fast) plus the device-list listener (authoritative) are. The engine has to turn either one into a
visible "disconnected" state and stop publishing that device's signals (§6.6). Whether the callbacks after the
destroy carried silence or stale data was **not** inspected. The design treats them as invalid either way.

### 3.7 Latency, buffer size, xruns, timestamps

- **Buffer size and latency:** `kAudioDevicePropertyBufferFrameSize` is per device. `AudioInput` asks for 128 frames
  (2.67 ms at 48 kHz); the measured callback rate is 375/s on BlackHole. `kAudioDevicePropertyLatency` and
  `kAudioDevicePropertySafetyOffset` (input scope) were read: BlackHole 0/0, built-in mic 0/50 frames, iPhone
  Continuity mic 6000/100. These are informational. Analysis latency is dominated by the 2048-sample window
  (42.7 ms) and the hop.
- **Xruns:** Core Audio does not hand an input client an "overrun" flag. The evidence is:
  - `kAudioDeviceProcessorOverload` (the HAL IO cycle missed its deadline);
  - a jump in `AudioTimeStamp::mSampleTime` between consecutive callbacks. Measured: 0 gaps over 750 callbacks in
    each AUHAL probe arm. **Only our own AUHAL path sees `mSampleTime`; miniaudio does not pass the timestamp
    through.**
- **Consumer behind:** the ring is full and samples are dropped, which `SpscRingBuffer::write` already reports as a
  short count.
- **Timestamps:**
  - Today the analysis position is `framesCaptured()` (a sample count since open), stamped by a discontinuity
    marker at frame 0.
  - With per-channel rings fed from **one callback**, every channel of a device shares one sample clock, so their
    frames are aligned by construction.
  - A detected gap (sample-time jump or ring overflow) is published as a **discontinuity marker at the true
    position**, using the existing `markDiscontinuity`. The analyzer resets there instead of splicing two separated
    pieces of audio into one window.

### 3.8 Permissions and packaging

- **No change is needed for this work.** TCC microphone access covers every input device, BlackHole included. It is
  granted to the launching process. The probes captured real signal from the built-in microphone.
- **Bundling later:** `NSMicrophoneUsageDescription`
  (https://developer.apple.com/documentation/bundleresources/information_property_list/nsmicrophoneusagedescription)
  and, under the hardened runtime, `com.apple.security.device.audio-input`
  (https://developer.apple.com/documentation/bundleresources/entitlements/com_apple_security_device_audio-input).
  These are recorded for when AV Gen ships as a bundle.

### 3.9 References

| Reference | Used for |
|---|---|
| TN2091, Device input using the HAL Output Audio Unit: https://developer.apple.com/library/archive/technotes/tn2091/_index.html | The AUHAL capture recipe; no SRC in AUHAL; `AudioUnitRender` in the input callback |
| macOS SDK `CoreAudio/AudioHardware.h`, `AudioHardwareBase.h` (Xcode SDK on this machine) | `StreamConfiguration`, `ProcessorOverload`, `IOStoppedAbnormally`, `DeviceIsAlive`, `IsPrivateKey` semantics, quoted above |
| Core Audio Utility Classes, `CAHALAudioDevice.cpp`: https://developer.apple.com/library/archive/samplecode/CoreAudioUtilityClasses/Listings/CoreAudio_PublicUtility_CAHALAudioDevice_cpp.html | Channel counting from StreamConfiguration; `IsAlive` usage |
| CAPlayThrough `AudioDevice.cpp`: https://developer.apple.com/library/archive/samplecode/CAPlayThrough/Listings/AudioDevice_cpp.html | Per-scope StreamConfiguration; device setup |
| AudioDeviceNotify `CAHALAudioDevice.cpp`: https://developer.apple.com/library/archive/samplecode/AudioDeviceNotify/Listings/PublicUtility_CAHALAudioDevice_cpp.html | Device-list notifications |
| Apple Developer Forums 69328 (WebRTC): https://developer.apple.com/forums/thread/69328 | `kAudioDeviceProcessorOverload` listener in production code |
| Apple Developer Forums 832462: https://developer.apple.com/forums/thread/832462 | AUHAL/AUConverter do not remap channel layouts automatically |
| VLC commit (2014): https://mailman.videolan.org/pipermail/vlc-commits/2014-April/024851.html | `IsAlive` alone is unreliable; check the device list |
| Apple support, aggregate devices: https://support.apple.com/guide/audio-midi-setup/create-an-aggregate-device-ams7c093f372/mac | User-facing aggregate devices (the owner can combine interfaces) |
| miniaudio issue #312: https://github.com/mackron/miniaudio/issues/312 | Multi-stream capture; reproduced and explained above |
| miniaudio 0.11.25 source (`.cache/cpm/miniaudio/02a4/miniaudio.h`, lines cited) | The mechanism of #312, notifications, format choice |
| R. Bencina, Real-time audio programming 101: http://www.rossbencina.com/code/real-time-audio-programming-101-time-waits-for-nothing | Callback rules |
| BlackHole: https://github.com/ExistentialAudio/BlackHole | Loopback test device (2ch installed; 16ch approved but needs a sudo password to install) |

## 4. The Volt 876 (hardware not connected)

**Detected model:** none. The device was not connected. The source comment at `audio_input.cpp:81-84` records that
CoreAudio once enumerated a capture device named **"Volt 876"** on this machine, so the owner's unit is a Volt 876.
That matches the owner's note in `01-coordinator-prep.md` §0.

**Published capabilities** (to be confirmed on the device):
- Retailer listings (Markertek, Adorama, Scan, Zenpro): 24 inputs / 28 outputs, 32-bit/192 kHz, USB 2.0 Type-C.
- 8 analog combo XLR/TRS inputs with preamps.
- 2 optical ports of ADAT: 16 channels at 44.1-48 kHz, 8 at 88.2-96 kHz, 4 at 176.4-192 kHz (S/MUX).
- Up to three units can be linked over ADAT.
- **Retailers disagree on the analog output breakdown, and "24 inputs" includes the ADAT channels.** So a single
  unit with nothing on its optical ports has **8 physical inputs that can carry signal**.

UA's official documents exist and should be read alongside the device:
- [Volt 876 Hardware Manual](https://help.uaudio.com/hc/en-us/articles/41288385879956-Volt-876-Hardware-Manual)
- [FAQ: Volt 876](https://help.uaudio.com/hc/en-us/articles/41353692489236-FAQ-Volt-876) (it covers driver needs
  and clocking)
- [UAD Console for Volt](https://help.uaudio.com/hc/en-us/articles/41288447947284-UAD-Console-for-Volt-Manual)
- [Volt Specifications](https://help.uaudio.com/hc/en-us/articles/4409522227092-Volt-Specifications)
- [product page](https://www.uaudio.com/products/volt-876-usb-audio-interface)

They were found by search but **not read in full in this phase**: no fetch tool was available. So none of their
content is asserted here.

**To verify on the device (plan milestone H):**
1. The reported name, UID and transport type.
2. Input channel count and **stream configuration**: one stream, or analog + ADAT streams. This decides whether
   today's `AudioInput` already hears all 8 analog inputs.
3. Nominal rate and the available rates; the channel count at 48 vs 96 kHz.
4. Whether a driver or the UA console is needed for all inputs to appear.
5. Independent capture of each analog input, with a physical signal per input: a mic, a line source or a mixer bus
   into each of inputs 1-8, one at a time, checking that only the matching channel moves.
6. Unplug and replug behaviour.
7. The ADAT channels need an ADAT source (another interface or preamp with optical out). Without one they can only
   be checked for enumeration and silence, which the plan marks as such.

## 5. Proposed offline-stem architecture

### 5.1 Data model (project file)

`assets.stems`, an array beside `assets.audio`/`assets.audioClips` (never inside the clip list, so a stem can never be
mixed):

```json
"stems": [
  {"key": "drums", "label": "Drums", "file": "stems/drums.wav"},
  {"key": "bass",  "label": "Bass",  "file": "stems/bass.wav", "offset": 0.0},
  {"key": "synth", "label": "Synth Lead", "file": "stems/synth.flac", "offset": -0.0125, "spectral": true}
]
```

- **`key`** is the signal identity: `[a-z0-9_]+`, unique within the project. It is made from the label when the stem
  is added (`"Synth Lead"` → `synth_lead`; a duplicate gets `_2`, `_3`) and is **stored, never re-derived**. Renaming
  the label does not move a route. The key is editable explicitly, and the UI warns that routes follow it (§8).
- **`offset`**, in seconds on the project timeline (positive = the stem starts later). Explicit only. **There is no
  correlation-based guessing** (brief §5.3).
- **`spectral`** (optional): whether the stem publishes the five bands and centroid. Default: true offline. §9 shows
  offline cost is no reason to omit them; the bus count is the only cost.

### 5.2 Validation before analysis (per file)

- **Decode:** `AudioFile::load` reports the error with its path. Recorded fields: sample rate, channels, frame count,
  duration, and format/bit depth as miniaudio reports it (miniaudio exposes the decoded format; the container's
  native bit depth is read from the WAV/FLAC header where available).
- **Sample validity:** a scan for NaN/Inf, the peak, and an all-zero check.
- **Diagnostics are explicit, never silent:**
  - a rate different from the mix: info, handled (§5.3);
  - a duration different from the mix by more than one hop: warning, with both durations;
  - an offset that puts the stem past the end of the mix: warning;
  - an all-silent stem: warning;
  - an undecodable file: error.
  Each diagnostic names the file. Matching durations are **not** reported as "aligned"; the UI says "alignment: as
  specified (offset X)".

### 5.3 Alignment, rates, missing data

- **Time domain:**
  - A stem frame at stem time `t_s` is at project time `t_s + offset`.
  - Every stem is analysed at its **own** rate, and frame times are in seconds (`frameIndex / rate`).
  - Mixed rates need no resampling, so a different rate is not rejected.
- **Outside a stem's extent** (before `offset`, after `offset + duration`): the stem publishes **0 for levels and
  no events**, and `stem.<key>.active = 0`. It does not clamp to the first or last frame, which
  `AnalysisTrack::at()` would do. The first and last half-window (21 ms) of a stem are its own analysis of the
  zero-padded edge. That is the same edge behaviour the mix has.
- **A stem that fails to load:** its signals are still declared (routes stay bound), it publishes 0 with
  `active = 0`, and the error is shown with the file name. The mix keeps playing.

### 5.4 Analysis and storage

- **One `AnalysisTrack` per stem would be wrong on memory.** Measured on a 3.5-minute stereo stem:
  - `AnalysisTrack` frames take **48.7 MB/min**, because every frame keeps two 1025-float vectors (`magnitude`,
    `spectrum`).
  - The decoded PCM takes another **121 MB**.
  - Four stems of a 4-minute song would hold about **1.2 GB**.
- **Plan:** a compact `StemTrack`, a plain array of a small `StemFrame`: rms, peak, level, 5 bands, centroid,
  onset, onsetStrength, and kick/snare/hat when the hits tier is on. That is about 64 bytes per frame, about
  0.35 MB/min (**estimate** from the struct size).
- **How it is filled:** the existing `Analyzer` (+ `CausalOnsetDetector` for the hits tier) runs over the stem, and
  the PCM is **released after analysis**. A stem is never played, so its samples are not needed afterwards.
- **What is skipped:** the beat tracker, meter and ADR-898 band-onset passes. They are whole-mix concepts and are
  not run per stem.
- **Cost, measured:** `AnalysisTrack::analyze` takes 2.2-2.4 s per 3.5-minute file (single thread, all passes).
  - The stem pass skips the beat, meter and band-onset passes. Its cost should be about the Analyzer alone, about
    0.32 s per 3.5-minute stem (**estimate**: 16.2 µs/hop × 19.7k hops), plus causal onsets when the hits tier is
    on (≈ +1.6 s).
  - Stems run **in parallel**, one short-lived worker thread per stem, joined before they are installed, beside
    the mix analysis. The engine owns no `JobSystem`; the app's `JobSystem` is for user jobs.
  - Prepared before render, held in memory. Not on demand: on-demand analysis would make seek-equals-play depend on
    a cache's state.
- **Determinism:** the Analyzer is chunking-invariant and deterministic (ADR-004). The stem pass is a pure function
  of (file samples, offset, config). Tests assert properties with tolerances (onset within ±1 hop of a known event),
  never bit-exact floats across machines.

### 5.5 Publishing

- **`StemSignals`** declares `stem.<key>.*` and publishes from a cursor per stem, kept in `SignalClock` (so the
  ADR-870 replay copies it with the clock). The cursor batches exactly like `consumeAnalysis`: every frame at or
  before render time, events OR-ed.
- **The same function serves** the offline render, the editor (a pure function of the transport second; the frames
  are precomputed, so there is no per-stem runner while a file plays) and a seek's replay.
- **Seek equals play:** the replay bus declares the same stem names. Values are copied back **by name through each
  bus's own `StemSignals` ids**, not by index, because stems are declared after the fixed prefix. The stem ids are
  added to the `samplable_` mask (the ADR-930 staging precedent). `test_engine_seek_parity` gets a stem case.

### 5.6 What stems do not drive (first iteration)

`music.*`, the beat clock, `sonic.*`, `timbre.*`, `notes.*`, `response.*`, `AudioHistory` and the reactivity proposer
stay on the main mix. A per-stem beat tracker or Sonic character is a separate decision. ADR-1510 (in flight on
`eng/response-range`) keeps per-source state in `ResponseModel`, so a per-stem `response.*` is possible later, but it
is not proposed here.

## 6. Proposed live-capture architecture

### 6.1 Layers

```
CaptureDevice (one opened device; backend: CoreAudioAuhal on macOS | Miniaudio elsewhere | Synthetic in tests)
   IO callback: per enabled channel -> its own AnalysisStream (SPSC ring); mono downmix -> the main AnalysisStream
                 (exactly today's average-of-all-channels unless a main pair is chosen); peak per channel;
                 sample-time gap / overflow -> discontinuity marker + counters
ChannelAnalysis (ONE analysis thread for all channel sources; the existing AnalysisRunner keeps the main stream)
   per source: Analyzer (+ CausalOnsetDetector for the hits tier) -> TripleBuffer<StemFrame> + event carry
Engine (main thread)
   main: runner_ -> audio.* (unchanged)        sources: acquire each -> StemSignals.publish -> stem.<key>.*
UI: Live panel "Inputs" section: device, channel list with meters, label/key, mono|pair, hits tier, status
```

**A live source publishes the same `stem.<key>.*` vocabulary as an offline stem.** A scene routed on `stem.drums.*`
offline therefore performs live when the mixer's drum bus is patched to an input labelled `drums`. That is the
reason for one namespace (§8).

### 6.2 The capture-backend decision (detailed in the plan, §2.1)

Recommended: a **narrow AUHAL backend on macOS**, with miniaudio kept as the backend on other platforms and as the
fallback. The reasons, all measured or read from code:
1. It captures every channel of multi-stream devices; miniaudio 0.11.25 does not.
2. It delivers `AudioTimeStamp::mSampleTime` for gap detection, which miniaudio does not pass through.
3. It can listen to `IsAlive`, the device list, the nominal rate, the stream configuration and processor overload.
4. It gives non-interleaved buffers with no deinterleave.

The miniaudio patch is a credible smaller alternative; the plan compares the two.

### 6.3 Channel mapping

- **A source is one of two shapes:**
  - **mono:** device channel *n*;
  - **pair:** channels *n* and *n+1*, analysed as **their mono sum** (L+R)/2, exactly as the main mix and a stereo
    stem are. A pair also publishes `stem.<key>.width`, the same side/mid measure as `audio.width`, because it is
    meaningful for a stereo bus.
- **Pairs never average unrelated channels by accident:** a pair exists only when the user creates one.
- Unassigned channels are not analysed (no ring, no analyzer). If a rig needs two channels, only two get analysis
  state, whatever the device's width (brief §2 non-goal).
- **The main mix:** the existing mono downmix of all channels is kept as the default, so the BlackHole 2ch path is
  byte-identical. A setting **main = channels n/n+1** covers a mixer that sends its main bus to inputs 7/8 and
  stems to 1-6.

### 6.4 Device identity

- Devices are selected and stored by the **Core Audio UID** (`kAudioDevicePropertyDeviceUID`; miniaudio already
  exposes it as `ma_device_id.coreaudio`), with the name kept for display.
- The existing `settings.live.audioInput` name substring keeps working: a name that matches no UID is matched by
  substring, as today. This is the existing workflow, not a shim.
- The source map is stored **per machine** in settings (a rig is per machine, ADR-1025), keyed by device UID.

## 7. Threading and buffer ownership

| Object | Owner | Producer | Consumer | Capacity / policy |
|---|---|---|---|---|
| `CaptureDevice` + backend | Engine (main thread opens and closes) | -- | -- | Opening and closing happen with the IO stopped (`AudioOutputUnitStop` + uninitialise / `ma_device_uninit`), so the callback is provably not running when any table it reads is replaced. |
| Main `AnalysisStream` | `CaptureDevice` | IO callback | `AnalysisRunner` (existing) | 65,536 samples (1.37 s), as today. Drop-newest on overflow, now **counted**. |
| Per-source `AnalysisStream` | `CaptureDevice`, one per enabled source, allocated at open | IO callback | `ChannelAnalysis` thread | 16,384 samples (341 ms, **proposal**: 32 hops of headroom; to be checked against the measured worst consumer stall). Drop-newest, counted, followed by a discontinuity marker at the true frame so the analyzer resets instead of splicing. |
| Per-source peak, drops, gaps | `CaptureDevice` | IO (relaxed atomics) | UI | -- |
| Per-source `TripleBuffer<StemFrame>` + event carry | `ChannelAnalysis` | analysis thread | main thread | Latest wins. Events are carried until acquired (the ADR-1060 pattern) so none is lost between render frames. A stale event cannot persist: the carry clears once the render thread has acquired a frame holding it. |
| `StemSignals` (bus ids) | Engine | main thread | routes | Main thread only, like the bus. |

**Stopped or behind:**
- **No new frame** for a source this render frame: keep the levels, drop the events (as `audio.*` does).
- **No frame for more than 250 ms**, or the device is dead or disconnected: publish **silence** (0 levels, no events)
  and `active = 0`. This is the brief's "a dropped buffer must not hold a transient asserted".

**One analysis thread for all channel sources, not one per source:**
- **Per-source cost (measured):**
  - The Analyzer runs at 16 µs/hop, CausalOnsetDetector included about 95-114 µs/hop.
  - Eight sources at the hits tier come to about 0.9 ms per 10.7 ms hop, under 10% of one core.
  - 24 sources at the levels tier come to about 0.4 ms per hop.
- **Why not more threads:** one thread keeps scheduling predictable and avoids 24 sleeping threads waking every
  millisecond. The `AnalysisRunner` polls every 1 ms when idle, and the channel thread uses the same pattern.
- The main source keeps its own runner and thread, exactly as now.

## 8. Source labelling and signal naming

The repository's convention is `<namespace>.<name>[.<field>]`, all lower camel case (`audio.onsetLow`, `lfo.wobble`,
`control.cc1`).

| Signal | Range / unit | Kind | Cadence and timing | At boundaries / missing |
|---|---|---|---|---|
| `stem.<key>.active` | 0/1 | value | per render frame | 0 outside the stem's extent, failed to load, channel absent, device disconnected or stale |
| `stem.<key>.rms` | 0..1 (full-scale sine 0.707) | value | per analysis hop (512 samples), window 2048, centred | 0 when inactive |
| `stem.<key>.peak` | 0..1 | value | as rms | 0 |
| `stem.<key>.level` | 0..1 on a fixed dB scale, 0 = -60 dBFS, 1 = 0 dBFS, 1 s smoothing (ADR-897's law, broadband) | value | per hop | 0 |
| `stem.<key>.onset` | event, strength 0..1 | event | the hop it is detected in; OR-ed over a render frame's batch | never fired while inactive; the carry clears on reset |
| `stem.<key>.onsetStrength` | 0..4 | value | per hop | 0 |
| `stem.<key>.bass` `.lowMid` `.mid` `.highMid` `.treble` | 0..1, auto-gained (running max, 4 s) like `audio.<band>` | value | per hop | 0 |
| `stem.<key>.centroid` | 0..1 log-normalised | value | per hop | 0 |
| `stem.<key>.kick` `.snare` `.hat` | events (ADR-1060 causal detector) | event | hits tier only | as onset |
| `stem.<key>.width` | 0..2 side/mid | value | pairs and stereo stems only | 0 for mono |

- **Labels:**
  - Each signal gets a `SignalBus` label such as `"Drums: level (steady, not auto-gained)"`, so the route picker
    reads like the existing `audio.*` labels.
  - The key is stable. The label is free text: renaming it updates the bus labels in place (`setLabel`), and the
    ids and routes do not move.
- **Duplicate labels:** allowed. Keys stay unique (`drums`, `drums_2`).
- **Removing a source:** its signals stay declared (the bus cannot undeclare) and publish 0 / no events with
  `active = 0` until the project reloads. Its routes are reported unresolved on the next project load, through the
  existing liveness rule for an unresolvable source.
- **Missing:** like removed, plus a diagnostic.
- **Collision safety:** the `stem.` namespace is new and no existing producer uses it (grep). Keys exclude `.`, so a
  key cannot forge a field name.

## 9. Performance budget and benchmark methodology

### 9.1 Measured now (this machine, release build `99d45f13`)

| What | Result | Probe |
|---|---|---|
| Time-domain RMS + peak over one 512-sample hop | **0.59-0.60 µs** | `bench_analyzer` on 60 s of Feline Footwear |
| `Analyzer` (FFT 2048, bands, centroid, flux, onset, levels), per frame | **16.2-16.4 µs** | same |
| + `CausalOnsetDetector` + `BeatTracker` (the runner's per-frame work, minus history and publish) | **94-114 µs** (causal 77-90, beat 0.7) | same |
| `AnalysisFrame` copy (two 1025-float vectors) | 0.11 µs | same |
| Callback split, mono downmix + N per-channel rings, 128 frames | **0.43 µs (2 ch), 1.24 µs (8 ch), 3.59 µs (24 ch)** average, against a 2,667 µs period | `bench_callback` (a normal-priority thread. Its worst cases of 2-4 ms are OS preemption of a non-real-time thread, not cost, and are not a callback measure) |
| `AnalysisTrack::analyze`, 3.5 min stereo 48 kHz | **2.19-2.38 s**, 19.1-19.7k frames, **165-171 MB of frames**, 117-121 MB PCM | `bench_track` on Feline Footwear, Trench and Hyperborea |
| AUHAL capture, 128-frame period on BlackHole | 375 callbacks/s, 0 sample-time gaps in 2 s | `probe_devices` |

**Consequence for the tiers (brief §3.4/§7.4):**
- The FFT is not the expensive stage. The **causal hit classifier** is, at about 5x the Analyzer.
- **Levels tier (default for every source):** Analyzer → rms, peak, level, onset, bands, centroid. About 16 µs/hop.
  - A time-domain-only tier would save 15 µs/hop per source (0.14% of a core). That does not justify a third tier
    and another UI choice.
  - **"Spectral on/off" is a publication choice** (fewer bus signals), not a cost choice.
- **Hits tier (opt-in per source, e.g. a drum bus):** + CausalOnsetDetector → kick/snare/hat. About +80-100 µs/hop.
- **Main-mix only:** the beat tracker, Sonic, timbre and response.

### 9.2 Live benchmarks (Phase 6)

**Matrix:**
- **Scenes:** Cymatic World (the demanding one; 41-47-op material program, 15.5-22.1 ms GPU headless) and one
  lighter live scene.
- **Settings:** 1080p output, target 30 fps, `avgen --live-profile --mode live --projection off`, 30 s measured,
  fixed start, Auto quality.
- **Arms:**
  1. The current stereo input (BlackHole 2ch). **Pre-change baseline, from this same base commit, before any
     capture code changes.**
  2. 4 sources, levels tier.
  3. 8 sources, levels tier.
  4. 8 sources, levels + spectral publication + hits on all 8.
  5. 8 sources, hits on 2.
  6. The most demanding: arm 4 plus MIDI (`avgen_sonic_probe resonator`) plus the projection window.
- **Inputs per arm:**
  - Arms 2-5 on the **synthetic 8-channel backend** (deterministic signals, no hardware). It is driven by a timer
    thread at real time, to load the system like a device.
  - Then on **BlackHole 16ch**, once installed, with a known multichannel signal played into it.
  - Then on the **Volt** when connected.
- **Order:** interleaved A/B pairs, with the order reversed on the repeat (ADR-1490 practice).
- **Measured per arm:**
  - frame p50/p99, GPU p50/p95/p99, quality level shares, misses (the live profile);
  - the callback time and max (an IO-thread histogram, written only from the callback via relaxed atomics, read by
    the profiler);
  - sample-time gaps and processor-overload count;
  - per-source ring drops and occupancy max;
  - the analysis thread's per-hop µs (EMA and max) and its CPU %;
  - main-thread time in the publish step;
  - RSS;
  - **event-to-signal latency.** The synthetic backend stamps an impulse at a known host time; the engine records
    the host time at which `stem.<key>.onset` first fires on the bus. Reported as p50/p99.
- **Rates:** 48 kHz, plus 44.1/96 kHz where the synthetic backend and BlackHole support them.

**Acceptance:**
- No new missed frames beyond the baseline's run-to-run spread (±3 ms noise is documented in ADR-1490's report).
- 0 ring drops and 0 sample-time gaps in steady state at 8 sources.
- Callback mean under 5% of its period.
- Bounded memory: RSS steady after 60 s.

These are thresholds against measured baselines, not an invented CPU budget.

### 9.3 Offline benchmarks

- Load-plus-analysis time and peak RSS for: the mix only; the mix + 3 stems; the mix + 8 stems.
- Durations: the generated 20 s fixture, plus a 3.5-minute case built from the three permitted tracks as stand-in
  "stems". Those files are not stems musically; the case measures cost only.
- The headless render frame time with stems is checked to be unchanged within noise.

## 10. Risks, alternatives and open questions

**Risks:**
1. **The Volt's stream layout is unknown** (multi-stream or not). The design does not depend on the answer: the
   AUHAL backend handles both. But the first hardware check tells whether today's path was already deaf to some
   inputs.
2. **USB unplug behaviour** may differ from the aggregate simulation. The engine reacts to `IsAlive` and the device
   list, not to the callbacks stopping. To be confirmed on the Volt.
3. **Format change under the IO** (rate or channel count from the UA console). Handled as stop → re-read → restart,
   on the main thread, from a listener-set flag. The listener itself only sets an atomic.
4. **Memory** if stems were stored as `AnalysisTrack`s (measured 1.2 GB for 4 stems). Avoided by the compact
   `StemTrack` and releasing PCM.
5. **The seek replay and new signals:** ids are copied back by index today. Stem ids must be mapped by name, or a
   seek would shuffle values between signals. A seek-parity test case is in the plan.
6. **BlackHole 16ch install needs an admin password** (the cask runs `installer` via sudo). It is not installed. The
   aggregate-device probe already covers multi-stream. BlackHole 16ch would add a 16-channel single-stream loopback.
7. **Contention with ADR-1510** (`src/sonic/response.*`): this work does not touch it.
8. **Windows builds:** the AUHAL backend is `__APPLE__`-only. The miniaudio backend keeps the feature compiling and
   working on WASAPI, where multichannel arrives as one stream. That is not tested here; there is no Windows device.

**Alternatives considered and set aside:**
- **One `AnalysisRunner` per channel:** N threads, N sleep-polls, a full beat tracker and timbre tap per channel.
  More cost and more threads for features we do not want per channel.
- **Stems as arrangement clips:** they would be mixed into playback, which the brief forbids.
- **Resampling stems to the mix rate:** not needed (§3.4), and the only resampler in the tree is linear.
- **Correlation-based auto-alignment:** forbidden by the brief. Alignment is explicit.
- **AVAudioEngine:** §3.1.
- **Changing the device's nominal rate:** system-wide; not ours to do.

**Open questions:**
- The Volt 876's stream layout, driver requirement and unplug behaviour. These need the hardware.
- Whether the owner wants `stem.*` to also reach `response.*`/Sonic later. Not in this scope.
- Whether a project should be able to **name** the live source map (so a scene can say "expects drums, bass"), or
  whether the per-machine map is enough. This plan does the per-machine map only.
