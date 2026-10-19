# Coordinator prep notes (2026-10-19)

The coordinator gathered these before the agent started. They are a head start, **not** a substitute for
the brief's Phase 0. Verify every claim in the code and the sources before relying on it, and correct this
file if anything is wrong.


## 0. Owner update, 2026-10-19: the device is the **Volt 876**

The owner says the target is https://www.uaudio.com/products/volt-876-usb-audio-interface, not a desktop Volt.

### Published figures

These come from retailer listings; confirm them on UA's spec page and against the device itself.
- **Channels:** 24 inputs and 28 outputs, at 32-bit/192 kHz.
- **Inputs:** 8 analog combo XLR/TRS inputs with preamps, plus 16 ADAT inputs over 2 optical ports.
- **ADAT capacity:** 16 channels at 44.1-48 kHz, 8 at 88.2-96 kHz, 4 at 176.4-192 kHz (S/MUX).
- **Other:** USB 2.0 Type-C and MIDI DIN. Up to three units can be linked over ADAT.
- **Sources:**
  - https://www.markertek.com/product/ua-volt-876/universal-audio-volt-876-usb-recording-studio-24-in-28-out-audio-interface-8-preamps-mac-win
  - https://www.adorama.com/universal-audio-volt-876-usb-c-rackmount-audio-interface/p/uavolt876

### What this means for the plan

- **Eight real inputs.** Eight independent physical inputs ARE testable on real hardware once it is connected.
  The synthetic 8-channel backend is still required for CI.
- **Several streams.** A 24-input interface may present its inputs as several Core Audio streams (analog and
  ADAT), which is exactly the miniaudio #312 risk (section 4.1). Check
  `kAudioDevicePropertyStreamConfiguration` on the real device early.
- **Sample rate.** The channel count changes with sample rate (ADAT halves at 96 kHz), so the device format can
  change under you. Handle it as a format-change event.

### Owner permissions

- **BlackHole 16ch:** you may install it (for example `brew install --cask blackhole-16ch`) to test
  multichannel capture without the Volt.
- **Other installs:** ask the coordinator before installing anything else.

## 1. Hardware on this machine right now

`system_profiler SPAudioDataType`, run 2026-10-19, reported:

| Device | Input channels | Rate |
|---|---|---|
| BlackHole 2ch | 2 | 48 kHz |
| MacBook Pro Microphone | 1 | 48 kHz |
| iPhone Microphone (Continuity) | 1 | 48 kHz |
| Microsoft Teams Audio | 1 | 48 kHz |
| ZoomAudioDevice | 2 | 48 kHz |

There are also outputs only: the MacBook Pro Speakers and a Multi-Output Device.

**No Universal Audio Volt was connected.** Hardware validation is pending until the owner plugs it in. Ask the
coordinator to request it when you reach that milestone, rather than assuming.

## 2. Volt models: published I/O

These figures come from third-party reviews. Confirm them against UA's own spec pages and, above all, the device's
reported channel count.

| Model | Inputs | Notes |
|---|---|---|
| Volt 1 | 1 | 1 combo input |
| Volt 2 | 2 | 2 combo inputs |
| Volt 176 | 1 | Same I/O as the Volt 1, plus 1176-style compressors |
| Volt 276 | 2 | Same I/O as the Volt 2, plus 1176-style compressors |
| Volt 476 | 4 (4×4) | Sources say two mic preamps plus two line inputs |
| Volt 476P | 4 (4×4) | Four mic preamps; 4 XLR/¼″ combo inputs, 6 TRS outputs, 24-bit/192 kHz, USB 2.0 |

So only the 476 and 476P can exercise more than 2 physical inputs. Sources:
- https://www.soundonsound.com/reviews/universal-audio-volt-476p
- https://www.mixonline.com/?p=135476
- https://americansongwriter.com/universal-audio-volt-476p-review/

## 3. The current capture path (read from the code)

### Capture library

- The library is **miniaudio 0.11.25**, header-only via CPM (`cmake/Dependencies.cmake`). Its Core Audio backend uses
  AUHAL internally. The implementation is in `src/audio/miniaudio_impl.cpp`.
- Note: `.cache/cpm/miniaudio/02a4/miniaudio.h` is the copy found on disk.

### AudioInput (`src/audio/audio_input.{hpp,cpp}`, ADR-021)

- **Device list.** `listCaptureDevices()` returns `{name, id, isDefault}`. On Core Audio, `id` is built in
  `deviceIdString()`; check whether that is the device UID (stable) or something weaker. `open()` selects a device
  by a case-insensitive **name substring**, not by a stable id.
- **Format.** `config.capture.format = ma_format_f32`, with `config.capture.channels = 0`, which means the device's
  native channel count.
- **Period.** A 128-frame period, chosen deliberately for latency on BlackHole. See the comment near line 210.
- **Callback.** The callback (lines ~129-165) is real-time clean: no allocation, locks, logging or exceptions. It
  **averages all native channels to mono** (`scale = gain / channels`), tracks the block peak, and writes one
  mono `AnalysisStream`.
- **Downmix point.** This is the single stereo/multichannel-to-mono collapse on the live path.
- **Channel count.** `channels()` exposes the native count, but nothing downstream uses per-channel data.

### AnalysisStream (`src/audio/analysis_stream.{hpp,cpp}`)

- An SPSC lock-free ring of mono floats, with discontinuity markers (up to 32 pending).
- Overflow policy: `write()` drops samples when the consumer is behind.
- One producer, one consumer.

### AnalysisRunner (`src/analysis/analysis_runner.{hpp,cpp}`)

- **Pipeline.** One background `jthread`: stream → `Analyzer` → `TripleBuffer<AnalysisFrame>`.
- **Stages.** It includes a `BeatTracker` and a `CausalOnsetDetector` (ADR-1060: kick, snare and hat live), plus
  an event-carry mechanism so onsets aren't lost between render frames.
- **Tap.** A `FrameTap` hook runs per frame on the analysis thread. The Sonic timbre stage (ADR-1025) uses it.
- **Instances.** It is created in `src/app/engine.cpp` at about line 4275 (from `player_->analysisStream()`) and
  about line 5051 (from `input->analysisStream()`). That makes **exactly one runner per engine, for one mono source**.
- **Telemetry.** `averageHopMicros()` exists for performance telemetry. Use it to measure the per-channel analyzer
  cost before deciding the tiers (brief §3.4).

### AnalyzerConfig (`src/analysis/analyzer.hpp`)

- 48 kHz, a 2048-sample window and a 512-sample hop (10.7 ms).
- Five default bands, running-max band normalisation, spectral-flux onsets, and the ADR-897 long-term levels.

### Offline (`src/analysis/analysis_track.{hpp,cpp}`, ADR-012)

- `AnalysisTrack::analyze(AudioFile, …)` runs the **same Analyzer** over a whole file. It also runs the offline
  Ellis beat tracker, the meter estimate (ADR-896) and band onsets (ADR-897/898).
- It uses the file's **side channel** for `audio.width`. That, plus stereo width, is the main stereo-specific feature.
- Frames are addressed by time with `at(seconds)`.
- So a stem is naturally "one more AnalysisTrack". Check memory first: frames per second times the size of
  AnalysisFrame times the number of stems.

### AudioFile (`src/audio/audio_file.{hpp,cpp}`)

- It decodes through `ma_decoder` at the file's native rate and channels, so WAV, FLAC and MP3 come from miniaudio
  for free.
- It precomputes a mono downmix. `fromInterleaved()` exists for tests and generators.
- Check how a file at a rate other than the project's is handled, and whether `ma_decoder` can resample on load.
  That decides the brief's §5.3 sample-rate policy.

### Signals (`src/signals/audio_signals.{hpp,cpp}`, ADR-004 §5)

- `AudioSignals::declare(bus)` declares a **fixed vocabulary with hardcoded `"audio.*"` names**: rms, peak, the
  five bands, centroid, flux, onsetStrength, onset, tempo, beat*, the band levels, energy, onsetRate, width,
  onsetLow, onsetMid and onsetHigh.
- `publish(bus, frame)` publishes it.
- `SignalBus::declare(name, …)` accepts arbitrary names, so a prefixed vocabulary per source is mechanically easy.
  For example, parametrise `declare(bus, prefix)` to produce `stem.<label>.rms`; check the repo's naming
  conventions first.

### Downstream consumers of the single source

These consumers assume a single source:
- the Sonic layer: `src/sonic/*` (`response.*`, `sonic.*`, `timbre.*`, `notes.*`), via `SonicRuntime` / `LiveSonic`;
- the reactivity proposer: `src/directing/reactivity*.cpp`;
- the UI panels.

Decide explicitly whether stems feed only the plain `audio`-style vocabulary per stem (the likely first
milestone), or the Sonic/response layer too. ADR-1510, the adaptive response reference landing now on
`eng/response-range`, keeps per-source state in `ResponseModel`, which matters if response.* ever goes per stem.

## 4. Risks already visible

1. **miniaudio multi-stream capture on Core Audio.** In miniaudio issue #312, an interface whose inputs are split
   across several Core Audio *streams* appears as one device, but channels beyond the first stream come back
   silent. The maintainer tagged it as a feature request. https://github.com/mackron/miniaudio/issues/312
   - **Channel count:** miniaudio's channel count comes from the device's channel layout
     (`ma_get_AudioObject_channel_count`, using `AudioChannelLayoutTag_GetNumberOfChannels`), not from
     `kAudioDevicePropertyStreamConfiguration`'s summed `mNumberChannels`.
   - **Volt 476 / 476P:** verify whether they present one 4-channel stream or several, with
     `kAudioDevicePropertyStreamConfiguration`, and whether miniaudio reports all 4 channels.
   - **If it falls short:** the choice is between a narrow AUHAL capture path (TN2091) and a miniaudio patch. That
     is a real architectural decision for the plan, and it needs hardware or an aggregate device to test. A Core
     Audio **aggregate device** built from BlackHole 2ch plus another input, or **BlackHole 16ch** if the owner
     installs it, could stand in for multi-stream and multichannel hardware tests without the Volt. Ask before
     installing anything.
2. **Device identity.** Selecting by name substring is fragile with two similar devices. Core Audio's
   `kAudioDevicePropertyDeviceUID` is stable across replugs and reboots (GStreamer and WebKit use it as the
   persistent id).
   - **Removal detection:** listen on `kAudioHardwarePropertyDevices`. `kAudioDevicePropertyDeviceIsAlive` alone
     is unreliable; VLC had to add a device-list check.
   - **What miniaudio offers:** check what it exposes here (`ma_device_notification`, stop/reroute
     notifications).
   - **Sources:**
     - https://mailman.videolan.org/pipermail/vlc-commits/2014-April/024851.html
     - https://developer.apple.com/library/archive/samplecode/AudioDeviceNotify/Listings/PublicUtility_CAHALAudioDevice_cpp.html
3. **AUHAL channel maps.** An Apple engineer on the developer forums says AUHAL/AUConverter don't automatically
   remap channel layouts; you build the map yourself (`kAudioFormatProperty_ChannelMap`) or deinterleave in your
   own callback. Since the callback already walks the interleaved frames, deinterleaving there is the simplest.
   - **Sources:**
     - https://developer.apple.com/forums/thread/832462
     - TN2091 (AUHAL input): https://developer.apple.com/library/archive/technotes/tn2091/_index.html
4. **One runner per engine today.** N live sources means N analyzers. Choose between N runner threads and one
   analysis thread stepping N analyzers; measure before choosing (brief §3.4/§9). The beat tracker and the Sonic
   layer probably stay on the main mix only.
5. **The stereo-width feature.** `audio.width` needs two channels. A mono stem or a mono live channel should
   publish 0 or omit it; define which.

## 5. Coordinator-imposed rules (in addition to the brief)

- **Gate 1 is a hard stop.** Hand me the research report and the plan, and wait for approval before production
  code, as the brief requires. I'll show it to the owner.
- **Live constraints.**
  - Don't regress the live scenes' 30 fps: profile with `avgen --live-profile`. The ADR-1490 tooling,
    `--projection off` and the MIDI counts, are documented in
    `docs/prototypes/resonator-quality/10-report-live-performance.md`.
  - Never block the audio callback.
  - Keep seek equal to play for offline signals.
- **Test audio.** Use only generated fixtures and the local tracks in `assets/audio/`: feline-footwear, trench and
  hyperborea. Never use all-you-got. Never commit audio files; generate fixtures into `build/`.
- **Scratch and GPU.** Keep scratch in the worktree's `build/`; a reboot wipes /tmp. Use
  `tools/gpu-lock.sh` for GPU jobs. Don't wrap `run-suite.sh gpu` in it, because it takes the lock itself.
- **Merge.** One branch, merged into main when you hand back.
