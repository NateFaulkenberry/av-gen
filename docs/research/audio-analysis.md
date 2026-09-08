# Audio I/O, Decoding, and Analysis — Research Notes

Status: research phase, no code decisions locked. Date: 2026-09-08.
Target: native C++ real-time audiovisual engine (audio -> analysis -> control signals -> modulation -> scene parameters).
Dev machine: macOS 26 / Apple M2 Max / CMake 4.0 / Apple clang 21. macOS first; Windows and Linux must remain possible.
License policy: permissive (MIT/BSD/zlib/Boost/public domain) strongly preferred; GPL/AGPL/LGPL flagged explicitly below.

Every claim that matters carries a citation block in the form:
`Source | URL | accessed 2026-09-08 | learned | relevance | confidence/limitations`.
A consolidated Sources list is at the end.

---

## Table of contents

1. Part A — Audio I/O and decoding
   1. Library survey (miniaudio, dr_libs, libsndfile, PortAudio, RtAudio, SDL3, JUCE, CoreAudio, libsoundio, Ogg/Vorbis/Opus)
   2. Real-time threading model
   3. Sample-accurate playback position for render sync
   4. Timeline-addressed analysis for offline rendering
2. Part B — Analysis algorithms
   1. STFT / windows / hop / latency
   2. Time-domain level: RMS, peak
   3. Frequency bands
   4. Spectral descriptors: centroid, rolloff, flatness, contrast, flux
   5. Onset detection
   6. Beat tracking, tempo, beat phase
   7. Chroma
   8. Pitch (YIN)
   9. Envelope followers and smoothing coefficients
   10. Loudness (BS.1770 / EBU R128), A-weighting, psychoacoustic descriptors
3. Part B — Library survey (FFT libraries, analysis libraries)
4. Recommended stack (milestone 0.1 vs later)
5. Analysis signal model and per-signal processing chain
6. Testing strategy and determinism requirements
7. Sources

---

## 1. Part A — Audio I/O and decoding

### 1.1 Library survey

#### miniaudio

- What it is: single-file C library (header + implementation macro) for playback, capture, decoding, mixing, resampling, with a high-level `ma_engine` / `ma_sound` graph on top of a low-level `ma_device` callback API.
- Version: 0.11.25 (published 2026-03-03).
- License: choice of public domain or MIT No Attribution (MIT-0).
- Backends: WASAPI, DirectSound, WinMM (Windows); Core Audio (macOS/iOS); ALSA, PulseAudio, JACK, OSS (Linux); sndio/audio(4)/OSS (BSD); AAudio/OpenSL|ES (Android); Web Audio / AudioWorklets (Emscripten).
- Decoding: built-in WAV, FLAC, MP3. The built-in decoders are David Reid's own dr_wav / dr_flac / dr_mp3 code compiled into miniaudio (same author, same repository family); custom decoders can be plugged in through the decoding backend vtable. Vorbis is not built in; it is available by including `stb_vorbis.c` (public domain) before the miniaudio implementation, or via the `extras/decoders` custom backends for libvorbis and libopus.
- Resampling: linear (default, lowest latency, lowest quality) and Speex (higher quality, more latency, performs heap allocations internally). Default decoder config initialises with `ma_resample_algorithm_linear`, Speex quality 3.
- Callback rules documented by the author: never call `ma_device_init/uninit/start/stop` from inside the data callback (deadlock). The library also ships `ma_pcm_rb` / `ma_rb`, single-producer single-consumer lock-free ring buffers, which is exactly what the analysis handoff needs.
- Playback position: `ma_sound_get_cursor_in_pcm_frames(&sound, &cursor)` returns the current PCM frame cursor of a sound; at the `ma_device` level you own the frame counter yourself (see 1.3).

Citations:
- Source: releasealert.dev mirror of GitHub releases for mackron/miniaudio | https://releasealert.dev/github/mackron/miniaudio | accessed 2026-09-08 | learned: latest version 0.11.25 published 2026-03-03 | relevance: maintenance is current | confidence: high (mirrors GitHub tags); limitation: third-party mirror.
- Source: miniaudio official site | https://miniaud.io/ | accessed 2026-09-08 | learned: single-file C library, no external deps, public domain or MIT-0 | relevance: license fits policy | confidence: high.
- Source: miniaudio Programming Manual | https://miniaud.io/docs/manual/index.html | accessed 2026-09-08 | learned: built-in WAV/FLAC/MP3, custom decoders, backend list, "never stop or start the device from inside the callback ... will result in a deadlock", `ma_sound_get_cursor_in_pcm_frames` | relevance: callback constraints and position API | confidence: high.
- Source: GitHub mackron/miniaudio README | https://github.com/mackron/miniaudio | accessed 2026-09-08 | learned: license choice "public domain or MIT No Attribution"; `ma_engine_play_sound` high-level API; "Resampling, including custom resamplers" | confidence: high.
- Source: miniaudio issue #286 and API reference (`ma_decoder_config_init`) | https://github.com/mackron/miniaudio/issues/286 , https://miniaudio.docsforge.com/master/api/ma_decoder_config_init/ | accessed 2026-09-08 | learned: linear resampler = least latency/poorest quality; Speex = higher quality, slower, allocates on heap; default is linear with Speex quality 3 | relevance: avoid Speex path in the RT thread | confidence: medium-high (issue text + generated API docs).
- Note on stb_vorbis / extras: based on the miniaudio repository layout (`extras/decoders/libvorbis`, `extras/decoders/libopus`, and stb_vorbis hook in `miniaudio.h`). Confidence: medium — the README fetch did not surface this text; verify against the header of the pinned version before relying on it.

#### dr_libs (dr_wav, dr_flac, dr_mp3)

- Single-file public-domain / MIT-0 decoders by the miniaudio author. dr_mp3 is derived from minimp3. dr_wav 0.14.5 dated 2026-03-03, so actively maintained in lock-step with miniaudio.
- Use case for us: none directly if we adopt miniaudio (the same code is embedded). Useful if we ever want decoders without the device layer (e.g. a headless offline-render CLI), or for writing WAV (dr_wav has a writer).

Citations:
- Source: GitHub mackron/dr_libs README and headers | https://github.com/mackron/dr_libs , https://github.com/mackron/dr_libs/blob/master/dr_wav.h | accessed 2026-09-08 | learned: "Public domain, single file audio decoding libraries"; dr_wav v0.14.5 (2026-03-03); dr_wav and dr_flac offer public domain or MIT-0; dr_mp3 based on minimp3 | confidence: high.

#### libsndfile

- Version 1.2.2 (2023-08-13). License LGPL-2.1-or-later. Broad format coverage (WAV/AIFF/AU/CAF/FLAC/Ogg Vorbis/Opus via libvorbis+libopus, MP3 via mpg123 since 1.1.x).
- License implication: LGPL requires that users can relink against a modified libsndfile. Practically: ship it as a dynamic library (`.dylib`/`.dll`/`.so`) or, if static, provide object files/relink instructions. On macOS a bundled `.dylib` inside the app bundle is fine; notarization is unaffected. This is a manageable but real distribution burden that miniaudio avoids entirely.
- Verdict: not needed for 0.1 (miniaudio + stb_vorbis covers WAV/FLAC/MP3/OGG). Keep as a later option for exotic formats or an offline tool where LGPL is a non-issue.

Citations:
- Source: Wikipedia "Libsndfile" and vcpkg port page | https://en.wikipedia.org/wiki/Libsndfile , https://vcpkg.link/ports/libsndfile | accessed 2026-09-08 | learned: latest stable 1.2.2 (2023-08-13), LGPL-2.1-or-later; vcpkg still ships 1.2.2 as of 2026 | confidence: high on version/license; medium on format list (from general knowledge of libsndfile 1.1+).

#### PortAudio

- C library, callback-based streaming API with a blocking-read/write alternative. Backends: CoreAudio, WASAPI, DirectSound, WDM-KS, ASIO, ALSA, JACK, OSS, PulseAudio (recent main). License is the PortAudio MIT-style license.
- Latest tagged stable: v19.7.0 (2021-04-06). The repository is active (commits in mid-2026, PRs preparing 19.8/19.9), but a 5-year-old tag means most people build from `master`.
- Timing: the callback receives `PaStreamCallbackTimeInfo` (input ADC time, current time, output DAC time) which is the hook for sample-accurate sync.
- Verdict: excellent, battle-tested, permissive, but it only does device I/O; no decoding, no resampling, no mixing. Since miniaudio provides all of those under one license, PortAudio is a fallback if miniaudio's CoreAudio/WASAPI behaviour ever turns out inadequate (e.g. ASIO on Windows, which miniaudio does not support).

Citations:
- Source: PortAudio download page | https://files.portaudio.com/download.html | accessed 2026-09-08 | learned: latest stable v19.7.0 released 2021-04-06 (`pa_stable_v190700_20210406.tgz`) | confidence: high.
- Source: GitHub PortAudio/portaudio and pulls page | https://github.com/PortAudio/portaudio , https://github.com/PortAudio/portaudio/pulls | accessed 2026-09-08 | learned: repository updated June 2026; PRs for v19.8/v19.9 in flight | relevance: project alive despite old tag | confidence: medium-high.
- Source: Wikipedia "PortAudio" | https://en.wikipedia.org/wiki/PortAudio | accessed 2026-09-08 | learned: MIT-style license, backend list | confidence: high.

#### RtAudio

- C++ classes (single header + cpp) providing a common real-time I/O API across ALSA, JACK, PulseAudio, OSS, CoreAudio, DirectSound, ASIO, WASAPI. MIT-like license.
- Latest release 6.0.1 (2023-08-01); 6.0.0 (2023-07-21) changed device selection and dropped C++ exceptions in favour of error callbacks.
- Verdict: solid alternative to PortAudio with a nicer C++ surface; same limitation (I/O only). Keep as a fallback.

Citations:
- Source: GitHub thestk/rtaudio releases and doc/release.txt | https://github.com/thestk/rtaudio/releases , https://github.com/thestk/rtaudio/blob/master/doc/release.txt | accessed 2026-09-08 | learned: 6.0.1 on 1 Aug 2023, 6.0.0 on 21 Jul 2023 with major API changes, "C++ exceptions no longer used"; MIT-like license since v2.1 | confidence: high.
- Source: GitHub thestk/rtaudio README | https://github.com/thestk/rtaudio | accessed 2026-09-08 | learned: backend list | confidence: high.

#### SDL3 audio

- SDL 3.4.16 stable released 2026-09-02; zlib license.
- Model: everything revolves around `SDL_AudioStream`. Streams accept any format, convert and resample, and are bound to a logical device; the device pulls from all bound streams and mixes. Optional get/put callbacks (`SDL_SetAudioStreamGetCallback`, `SDL_SetAudioStreamPutCallback`, or a callback passed to `SDL_OpenAudioDeviceStream`). Callbacks "may run from any thread"; the stream lock is held during the callback.
- Decoding: only `SDL_LoadWAV`; no compressed formats.
- Verdict: only attractive if SDL3 is already the windowing layer. Its audio thread guarantees are looser (callback thread not specified) and there is no decoder. Not recommended as the primary audio layer unless SDL3 becomes the platform layer for the renderer.

Citations:
- Source: SDL3 wiki CategoryAudio, SDL_AudioStream, SDL_OpenAudioDeviceStream, SDL_SetAudioStreamGetCallback | https://wiki.libsdl.org/SDL3/CategoryAudio , https://wiki.libsdl.org/SDL3/SDL_OpenAudioDeviceStream , https://wiki.libsdl.org/SDL3/SDL_SetAudioStreamGetCallback | accessed 2026-09-08 | learned: stream model, logical devices, "SDL3 does not directly decode compressed formats", callbacks may run from any thread with stream lock held | confidence: high.
- Source: Wikipedia "Simple DirectMedia Layer" and GitHub libsdl-org/SDL releases | https://en.wikipedia.org/wiki/Simple_DirectMedia_Layer , https://github.com/libsdl-org/SDL/releases | accessed 2026-09-08 | learned: SDL 3.4.16 stable 2026-09-02; zlib license; odd/even versioning | confidence: high.

#### JUCE

- Full application framework with `juce_audio_devices` (CoreAudio/WASAPI/ASIO/ALSA/JACK), `juce_audio_formats` (WAV/AIFF/FLAC/OGG/MP3/CoreAudio formats), `juce_dsp` (FFT, windowing, IIR/FIR, convolution).
- License (JUCE 8): dual AGPLv3 / commercial. Commercial tiers per the JUCE 8 EULA: Starter (free, up to $20k annual revenue/funding), Indie (up to $300k; $40/month or $800 perpetual per user), Pro (no cap; $175/month or $3500 perpetual per user), Educational (free, non-commercial). Revenue cap counts all sources including donations and sponsorship. The JUCE business has been owned by Spotify since October 2024.
- Verdict: AGPL is incompatible with the "permissive" goal for a closed or mixed-license product, and the free tier caps revenue. JUCE is not recommended; the pieces we need (FFT, file decoding, device I/O) are all available permissively elsewhere. If the project ever ships as a plugin host or plugin, revisit.

Citations:
- Source: JUCE 8 End User Licence Agreement | https://juce.com/legal/juce-8-licence/ | accessed 2026-09-08 | learned: tiers, revenue limits ($20k / $300k / unlimited), pricing ($40/mo, $800, $175/mo, $3500), educational restrictions | confidence: high for numbers as of today; pricing changes over time.
- Source: GitHub juce-framework/JUCE LICENSE.md and Wikipedia "JUCE" | https://github.com/juce-framework/JUCE/blob/master/LICENSE.md , https://en.wikipedia.org/wiki/JUCE | accessed 2026-09-08 | learned: dual AGPLv3/commercial; Spotify ownership since Oct 2024 | confidence: high on AGPL; medium on ownership date (Wikipedia).

#### CoreAudio direct (macOS)

- The lowest-level option on macOS: AUHAL (`kAudioUnitSubType_HALOutput`) render callback or `AudioDeviceIOProc`; AVAudioEngine for a higher-level graph; `AudioTimeStamp` carries `mSampleTime` and `mHostTime` for each I/O cycle. Real-time threads run under `THREAD_TIME_CONSTRAINT_POLICY`; Apple's guidance (QA1715 and WWDC20 "Meet Audio Workgroups") forbids blocking, allocation, and Objective-C messaging on the render thread, and provides `os_workgroup_join` so auxiliary real-time threads (e.g. an analysis thread) are scheduled together with the device's I/O thread.
- Verdict: no third-party license at all, best latency and timing data, but macOS-only and a lot of boilerplate. Not the primary layer, but two things are worth using directly even with miniaudio: (a) Audio Workgroups for any helper real-time thread, and (b) `mHostTime`/`mSampleTime` if we ever need tighter A/V sync than a frame counter gives.

Citations:
- Source: Apple Developer Documentation "AudioDeviceIOProc"; Apple Technical Q&A QA1715; WWDC20 session 10224 "Meet Audio Workgroups"; "Adding Asynchronous Real-Time Threads to Audio Workgroups" | https://developer.apple.com/documentation/coreaudio/audiodeviceioproc , https://developer.apple.com/library/ios/qa/qa1715/_index.html , https://developer.apple.com/videos/play/wwdc2020/10224/ , https://developer.apple.com/documentation/audiotoolbox/adding-asynchronous-real-time-threads-to-audio-workgroups | accessed 2026-09-08 | learned: render callback runs on a real-time thread with a time slice; avoid file/network I/O, allocation, ObjC messaging, blocking; workgroups obtained from AUHAL/AURemoteIO and joined with `os_workgroup_join` | confidence: high.

#### libsoundio

- MIT; backends JACK, PulseAudio, ALSA, CoreAudio, WASAPI, dummy. No GitHub releases published; last tagged version 2.0.0 (2019); Debian ships 2.0.0-3; 103 open issues; the original author no longer wants to maintain the Debian package.
- Verdict: effectively in maintenance-only / dormant state. Do not adopt.

Citations:
- Source: GitHub andrewrk/libsoundio and its releases page; Debian tracker | https://github.com/andrewrk/libsoundio , https://github.com/andrewrk/libsoundio/releases , https://tracker.debian.org/pkg/libsoundio | accessed 2026-09-08 | learned: MIT, six backends, "There aren't any releases here", 103 open issues, Debian 2.0.0-3 | confidence: high on facts; the "dormant" judgement is mine.

#### Ogg / Vorbis / Opus decoders

- libogg, libvorbis 1.3.7: BSD-3-Clause (Xiph). stb_vorbis: public domain / MIT (stb dual license), single file, pure decoder, slower and less robust than libvorbis on malformed input but zero build friction. libopus: BSD-3-Clause; opusfile (Ogg Opus file decoding on top of libopus + libogg): BSD-3-Clause.
- Plan: stb_vorbis for OGG in 0.1 (it slots directly into miniaudio); libopus + opusfile via miniaudio's custom decoder backend later if Opus input is required.

Citations:
- Source: GitHub xiph/vorbis; nothings/stb stb_vorbis.c; Opus license page; GitHub xiph/opusfile | https://github.com/xiph/vorbis , https://github.com/nothings/stb/blob/master/stb_vorbis.c , https://opus-codec.org/license/ , https://github.com/xiph/opusfile | accessed 2026-09-08 | learned: libvorbis BSD-3; stb_vorbis public domain (Sean Barrett, 2007) with stb's MIT alternative; Opus reference code "three-clause BSD"; opusfile BSD-3 | confidence: high.

#### Summary table (Part A)

| Library | Version (date) | License | Platforms | Decoding | Verdict |
|---|---|---|---|---|---|
| miniaudio | 0.11.25 (2026-03) | Public domain / MIT-0 | macOS, Win, Linux, BSD, iOS, Android, Web | WAV, FLAC, MP3 built in; Vorbis via stb_vorbis; Opus/Vorbis via extras | Primary |
| dr_libs | dr_wav 0.14.5 (2026-03) | Public domain / MIT-0 | any | WAV, FLAC, MP3 | Embedded in miniaudio |
| libsndfile | 1.2.2 (2023-08) | LGPL-2.1+ | any | very broad | Later / offline tools only |
| PortAudio | 19.7.0 (2021-04), active master | MIT-style | any | none | Fallback I/O |
| RtAudio | 6.0.1 (2023-08) | MIT-like | any | none | Fallback I/O |
| SDL3 audio | 3.4.16 (2026-09) | zlib | any | WAV only | Only if SDL is platform layer |
| JUCE 8 | 8.x | AGPLv3 / commercial | any | broad | Not recommended |
| CoreAudio | OS | Apple SDK | macOS/iOS | via AudioToolbox | Use workgroups/timestamps selectively |
| libsoundio | 2.0.0 (2019) | MIT | desktop | none | Dormant, skip |
| libvorbis / stb_vorbis / libopus / opusfile | 1.3.7 / stb / 1.5.x / 0.12 | BSD-3 / PD | any | Vorbis / Opus | stb_vorbis in 0.1; Opus later |

### 1.2 Real-time threading model

Constraints (from Bencina's canonical article and Apple's QA1715): the audio callback runs on a high-priority, time-constrained thread and must complete within its buffer period; anything with unbounded or unpredictable latency is forbidden — memory allocation/free, mutexes and any lock that can be held by a lower-priority thread (priority inversion), file/network I/O, `printf`, Objective-C messaging, system calls that may page, algorithms with bad worst-case complexity. Communication with other threads must be wait-free from the audio thread's perspective: single-producer/single-consumer lock-free ring buffers and atomics.

Design for this engine:

```
                     +---------------------+   SPSC ring (PCM, ~250 ms)   +--------------------+
 device callback --> | RT thread            | ---------------------------> | analysis thread    |
 (miniaudio)         |  - pull decoded PCM  |                              |  - STFT / features |
                     |    from decoder ring |   atomic<uint64> frames      |  - onset/beat      |
                     |  - write to device   | ---------------------------> |  - publishes       |
                     |  - copy to analysis  |                              |    FeatureFrame    |
                     |    ring, bump counter|                              +---------+----------+
                     +---------------------+                                         |
                                                                triple-buffer / seqlock (latest frame)
                                                                                     v
                                                                           render thread (60-120 Hz)
                                                                           - reads latest FeatureFrame
                                                                           - interpolates to frame time
```

- Decoder thread (or the analysis thread) pre-decodes into a PCM ring; the RT thread only copies. miniaudio's `ma_engine` already does async decoding for `ma_sound` on its own job threads; if we use `ma_engine`, we attach the analysis tap as a node in the node graph or via the engine's data callback and still only copy from the callback.
- The analysis thread is a normal thread (or joined to the CoreAudio workgroup on macOS) that pulls whole hops from the PCM ring; it may allocate at startup only, and should pre-allocate FFT scratch, window tables, and feature history.
- Feature frames go to the render thread through a triple buffer or seqlock of POD structs; never a mutex shared with the audio callback. A bounded SPSC queue of timestamped `FeatureFrame`s (one per hop) is better than "latest only" because the renderer can then interpolate/extrapolate to its own frame time.
- No wall-clock anywhere in analysis. Time is expressed in PCM frames at the analysis sample rate (see 1.4).

Citations:
- Source: Ross Bencina, "Real-time audio programming 101: time waits for nothing" (2011) | http://www.rossbencina.com/code/real-time-audio-programming-101-time-waits-for-nothing | accessed 2026-09-08 | learned: no blocking, no locks, no allocation, no bad worst-case algorithms, no waiting on external events in the callback; use lock-free queues | relevance: defines the RT contract | confidence: high (canonical reference).
- Source: Apple QA1715 and WWDC20 10224 (cited in 1.1) | same URLs | accessed 2026-09-08 | learned: identical constraints from Apple; workgroups for helper RT threads | confidence: high.
- Source: SDL3 SDL_SetAudioStreamGetCallback wiki (cited in 1.1) | accessed 2026-09-08 | learned: SDL callbacks "may run from any thread" — one reason not to use SDL audio as the RT layer | confidence: high.

### 1.3 Sample-accurate playback position for render sync

1. The RT callback maintains `std::atomic<uint64_t> framesRendered` (relaxed store after the copy) plus an `std::atomic<uint64_t> callbackHostTimeNs` captured at callback entry (`mach_absolute_time` / `clock_gettime(CLOCK_MONOTONIC)`). Both are single-writer, so a seqlock or a packed 128-bit struct published via two atomics with a sequence counter avoids torn reads.
2. Position at any wall-clock instant on the render thread:
   `pos_frames(t) = framesRendered_at_callback + (t - callbackHostTime) * sampleRate - outputLatencyFrames`, clamped to `[framesRendered_prev, framesRendered_at_callback + bufferFrames]`. `outputLatencyFrames` comes from the device (miniaudio reports the period size; CoreAudio exposes `kAudioDevicePropertyLatency` + safety offset; PortAudio gives `outputBufferDacTime`).
3. With `ma_engine`, `ma_sound_get_cursor_in_pcm_frames` gives the decoded cursor of a sound, which is what "timeline position" means for the visual side; it advances in the mixer thread and is safe to poll from any thread. Combine with (1)-(2) for sub-buffer interpolation.
4. Keep transport state (play/pause/seek) as an atomic command queue consumed by the RT thread; seeks change `timelineOffsetFrames` so that `timelineFrame = framesRendered - startFrames + timelineOffsetFrames`.
5. Feature frames are stamped with the PCM frame index of the hop center (`frameIndex = hopIndex * hop + fftSize/2`), never with wall time, so render-side lookup is `feature(timelineFrame)`.

Citations:
- Source: miniaudio manual (`ma_sound_get_cursor_in_pcm_frames`) and PortAudio `PaStreamCallbackTimeInfo` (outputBufferDacTime) via PortAudio docs; Apple `AudioDeviceIOProc` docs (AudioTimeStamp with mSampleTime/mHostTime) | https://miniaud.io/docs/manual/index.html , https://github.com/PortAudio/portaudio , https://developer.apple.com/documentation/coreaudio/audiodeviceioproc | accessed 2026-09-08 | learned: cursor API; DAC-time in PortAudio callback; CoreAudio timestamps | confidence: high for existence of these APIs; the interpolation formula is standard practice, not from a single source.

### 1.4 Timeline-addressed analysis for offline rendering

Requirement: an offline render (e.g. 4K at 60 fps to disk) must produce identical visuals to the live run, at any frame rate, in any chunking.

- Decode the entire file once to float PCM at a fixed analysis rate (default: the file's native rate, resampled to 48 kHz only if we decide on a fixed internal rate; the choice must be a single project-wide constant to keep features comparable). `ma_decoder_read_pcm_frames` in a loop, or `ma_decoder` with `ma_decoder_get_length_in_pcm_frames` to preallocate.
- Analysis is a pure function `FeatureTimeline analyze(const float* pcm, uint64 frames, AnalysisConfig)`. It walks hops by frame index and emits one `FeatureFrame` per hop with `frameIndex`. The live path calls the exact same per-hop kernel `analyzeHop(state, const float* hop)` from the analysis thread as data arrives from the ring buffer; the only difference is who feeds the samples.
- Non-causal features (Ellis DP beat tracking, global tempo, global loudness normalization) are computed only in the offline/pre-analysis pass; in live mode they are replaced by causal approximations (see 2.6) or by a pre-analysis pass over the whole file when the file is known in advance (the common case for a music-driven visualizer: the track is loaded, not streamed). Design decision: when a file is loaded, always run the full offline analysis in a worker thread and cache the `FeatureTimeline` (serialize to disk keyed by file hash + config hash); live capture from a microphone/loopback is the only path that needs the causal-only mode.
- Render-side sampling: `sample(timeline, frame)` interpolates between the two neighbouring hops (linear for continuous signals, hold for events, phase-aware for `beatPhase`). Offline render calls `sample(timeline, frameForVideoFrame(n))` where `frameForVideoFrame(n) = round(n * sampleRate / fps)`.
- Determinism: fixed FFT size/hop/window, fixed float32 arithmetic order (no `-ffast-math` in analysis code, or fixed reduction order explicitly), same FFT library on all platforms (pffft/KissFFT are deterministic given identical build flags; vDSP is not bit-identical to pffft, so if vDSP is used it must be behind a flag and not the reference path for golden tests).

---

## 2. Part B — Analysis algorithms

### 2.1 FFT / STFT: windows, hop, latency

- STFT: `X[m,k] = sum_n x[n + mH] w[n] e^{-2*pi*i*k*n/N}` with window length N, hop H (samples), frame rate `fs/H`. Frequency resolution `fs/N` Hz per bin; time resolution ~N/fs. Wider N = better frequency resolution but smears transients; narrower N = better time localization but coarser bins (uncertainty trade-off).
- Windows: Hann is the default (good side-lobe suppression, main lobe 4 bins, COLA at 50%/75% overlap); Hamming for slightly narrower main lobe; Blackman-Harris when leakage must be minimal (peak detection); rectangular only for transient/impulse tests. The spectrum of a windowed signal is the convolution of signal and window spectra, which is what causes leakage.
- Practical settings at 44.1/48 kHz for a visualizer:
  - Fast path: N = 1024, H = 256 or 512 -> 5.3-11.6 ms hop, 21.5-23.4 ms window, ~43-47 Hz bins. Good for onsets, RMS, bands, flux.
  - Tonal path: N = 4096 (or 8192), H = 1024 -> ~10.8 Hz bins for chroma/pitch/low bass; latency ~46-93 ms. Run both if CPU allows (two STFTs on 2 channels at 48 kHz is trivial on M2 Max).
  - Latency budget: analysis latency = N/2 (window center) + device output latency + hop jitter. At N=1024, H=256 latency is ~10.7 ms + device; compensate visually by timestamping features at window center and letting the renderer look up features at `timelineFrame + lookahead` when a pre-analyzed timeline exists (offline mode has zero effective latency because we can read the future).
- Magnitude: use power `|X|^2` for flux/bands (cheaper, no sqrt), magnitude `|X|` for centroid/rolloff; log-compression `log(1 + gamma*|X|)` (gamma ~ 10..1000) before flux/onset improves robustness (used in SuperFlux and Ellis).
- Real FFT of size N produces N/2+1 bins; use a real-input FFT (pffft `PFFFT_REAL`, KissFFT `kiss_fftr`) and precompute the window table.

Citations:
- Source: Müller, FMP Notebooks, "Discrete Short-Time Fourier Transform (STFT)" (Section 2.1.4 of Müller, *Fundamentals of Music Processing*, Springer 2015) | https://www.audiolabs-erlangen.de/resources/MIR/FMP/C2/C2_STFT-Basic.html | accessed 2026-09-08 | learned: STFT definition, hop size semantics, time/frequency resolution trade-off | confidence: high.
- Source: MetricGate spectrogram notes and CMUSE window calculator (secondary) | https://metricgate.com/docs/spectrogram-time-frequency/ , https://www.cmuse.org/window-function-calculator/ | accessed 2026-09-08 | learned: Hann vs rectangular leakage; convolution-of-spectra explanation | confidence: medium (secondary sources; consistent with textbooks such as Harris 1978).

### 2.2 RMS and peak

- Block RMS over a hop or window: `rms = sqrt(mean(x[n]^2))`. Windowed RMS with the analysis window is smoother. Convert to dBFS: `20*log10(rms)` (full-scale sine has RMS -3.01 dBFS). For control signals, use linear RMS normalized to [0,1] plus optional dB mapping.
- Peak: `max |x[n]|` per hop; sample peak understates inter-sample peaks — true peak requires 4x oversampling (BS.1770) and is only needed for metering, not for visuals.
- True-RMS envelope follower (Q library has one) is the continuous-time alternative: square -> one-pole low-pass with time constant -> sqrt.

Citations:
- Source: ITU-R BS.1770-5 (2023-11) via Wikipedia "LKFS" | https://en.wikipedia.org/wiki/LKFS | accessed 2026-09-08 | learned: true-peak measured with oversampling | confidence: high.
- Source: Cycfi Q docs | https://cycfi.github.io/q/q/v1.5-dev/index.html | accessed 2026-09-08 | learned: Q provides a "True-RMS envelope follower" | confidence: high.

### 2.3 Frequency bands

Typical five-band split used by web-audio visualizers (p5.sound `getEnergy` presets) and mixing-engineer conventions:

| Band | Hz | Note |
|---|---|---|
| bass | 20-140 (kick/bass) | some use 60-250; sub-bass 20-60 |
| lowMid | 140-400 | body of snare/guitars |
| mid | 400-2600 | vocals |
| highMid | 2600-5200 | presence |
| treble | 5200-14000 | air/hi-hats |

Implementation: sum power spectrum bins whose center frequency falls in each range (bin k center = `k*fs/N`), divide by bin count (mean power) or by total power (relative band energy), then log-compress. Better: a log-spaced filterbank (e.g. 24-40 bands, 1/3-octave or mel/Bark) built once as a sparse matrix `B[b,k]` with triangular weights; five named bands become sums over filterbank rows so that adding more bands later needs no new code. Compute both absolute (for `audio.bass`) and relative (`audio.bassRel = bass / total`) forms; relative bands are more stable across loudness changes.

Citations:
- Source: p5.sound reference for `p5.FFT.getEnergy` presets (bass 20-140, lowMid 140-400, mid 400-2600, highMid 2600-5200, treble 5200-14000) | https://p5js.org/reference/p5.sound/p5.FFT/ | accessed 2026-09-08 (not fetched in this session; values from the library's documented presets) | relevance: widely used defaults for visualizers | confidence: medium — verify the exact numbers against the p5.sound source when implementing; the boundaries are conventions, not standards.

### 2.4 Spectral descriptors

All defined per frame over magnitude `|X[k]|` (or power) and bin frequencies `f_k`; references: Peeters 2004 (CUIDADO feature set) and Jiang et al. 2002 for contrast.

- Spectral centroid: `C = sum(f_k |X_k|) / sum(|X_k|)` (Hz). Brightness proxy. Normalize for control by `log2(C/20)/log2(20000/20)`.
- Spectral spread (bandwidth): `sqrt(sum((f_k - C)^2 |X_k|) / sum(|X_k|))`.
- Spectral rolloff: smallest `K` such that `sum_{k<=K} |X_k|^2 >= p * sum |X_k|^2`, p = 0.85 (also 0.95). Output as Hz.
- Spectral flatness (Wiener entropy): `geomean(P_k) / mean(P_k)` on power `P_k`; 1 = white noise, ~0 = pure tone. Compute geomean as `exp(mean(log(P_k + eps)))`. Often reported per band.
- Spectral contrast (Jiang 2002, librosa): split spectrum into octave sub-bands (e.g. 6 bands from 200 Hz), in each band take the mean of the top alpha-fraction (alpha ~ 0.02-0.2) of sorted magnitudes as "peak" and bottom fraction as "valley", contrast = `log(peak) - log(valley)`. Distinguishes noisy vs harmonic content per band.
- Spectral flux: `F[m] = sum_k H(|X[m,k]| - |X[m-1,k]|)` with half-wave rectifier `H(x) = (x + |x|)/2` (positive changes only, Dixon 2006 / Bello 2005). Normalize by N. Using log-magnitudes and a max-filtered previous frame turns this into SuperFlux (2.5).

Citations:
- Source: G. Peeters, "A large set of audio features for sound description (similarity and classification) in the CUIDADO project", IRCAM tech report, 2004 | http://recherche.ircam.fr/anasyn/peeters/ARTICLES/Peeters_2003_cuidadoaudiofeatures.pdf | accessed 2026-09-08 | learned: definitions of centroid, spread, skewness, kurtosis, slope, crest, flatness, rolloff | relevance: canonical definitions | confidence: high.
- Source: D.-N. Jiang, L. Lu, H.-J. Zhang, J.-H. Tao, L.-H. Cai, "Music type classification by audio spectrum contrast", IEEE ICME 2002 | https://ieeexplore.ieee.org/document/1035731 ; librosa `spectral_contrast` docs https://librosa.org/doc/main/generated/librosa.feature.spectral_contrast.html | accessed 2026-09-08 | learned: octave sub-bands, peak/valley strength, log-domain difference | confidence: high.
- Source: Wikipedia "Spectral flux" and Dixon 2006 (below) | https://en.wikipedia.org/wiki/Spectral_flux | accessed 2026-09-08 | learned: half-wave-rectified L1 difference definition | confidence: high.

### 2.5 Onset detection

Pipeline: STFT -> (filterbank, log compression) -> onset detection function (ODF) -> adaptive threshold / peak picking -> onset events + continuous onset strength.

- Bello et al. 2005 tutorial: taxonomy of ODFs (energy, spectral, phase, complex-domain, probabilistic), and the standard post-processing: smoothing, adaptive threshold (moving median/mean + delta), peak picking.
- Dixon 2006 "Onset Detection Revisited": compares spectral flux, weighted phase deviation, complex difference; concludes that half-wave-rectified spectral flux on magnitude performs as well as phase/complex methods and is simpler. Peak picking rule (widely copied, e.g. librosa `peak_pick`): `f[n] >= max(f[n-w1 .. n+w2])`, `f[n] >= mean(f[n-w3 .. n+w4]) + delta`, and `n - last_onset > wait`.
- Böck and Widmer 2013 SuperFlux: spectral flux on a log-magnitude, log-frequency (quarter-tone / 24-bands-per-octave) spectrogram where the *previous* frame is replaced by its maximum-filtered version (max over +/- 1 neighbouring bins, `diff_max_bins = 3` in madmom), and the difference is taken against a frame `mu` hops back (typically 1-2, i.e. `lag = 2` in librosa's example). This suppresses vibrato/tremolo false positives by up to 60%. Peak-picking: threshold on the ODF (madmom default 0.5 for SuperFlux-scaled ODF), `combine = 30 ms` minimum inter-onset gap, optional pre/post max and average windows; 100 fps ODF is madmom's default frame rate.
- Implementation notes for us:
  - Compute `onsetStrength` every hop (continuous signal for visuals), and `onset` as a Boolean/impulse with a `strength` value.
  - Adaptive threshold = moving median over ~0.3-0.5 s (robust) plus `delta` relative to local range; expose `delta`, `wait`, and `sensitivity` as user parameters.
  - Real-time: use causal windows only (post_max = post_avg = 0 -> ~0 latency beyond one hop; allowing 1-2 frames of lookahead (10-20 ms) noticeably improves peak picking and is acceptable if the visual is compensated). Offline: symmetric windows.
  - Band-limited onsets (kick vs snare vs hat) come for free by computing flux on filterbank sub-ranges (e.g. `onset.low` from <150 Hz, `onset.high` from >5 kHz).

Citations:
- Source: J. P. Bello, L. Daudet, S. Abdallah, C. Duxbury, M. Davies, M. Sandler, "A tutorial on onset detection in music signals", IEEE TSAP 13(5), 2005, pp. 1035-1047 | https://hajim.rochester.edu/ece/sites/zduan/teaching/ece472/reading/Bello_2005.pdf | accessed 2026-09-08 | learned: ODF taxonomy and post-processing framework | confidence: high.
- Source: S. Dixon, "Onset detection revisited", DAFx-06, Montreal, 2006 | https://www.dafx.de/paper-archive/2006/papers/p_133.pdf | accessed 2026-09-08 | learned: spectral flux, weighted phase deviation, complex difference perform similarly; HWR spectral flux recommended | confidence: high.
- Source: S. Böck, G. Widmer, "Maximum filter vibrato suppression for onset detection", DAFx-13, Maynooth, 2013 | https://www.dafx.de/paper-archive/2013/papers/09.dafx2013_submission_12.pdf ; reference implementation https://github.com/CPJKU/SuperFlux | accessed 2026-09-08 | learned: max-filtered previous frame, log-frequency filterbank, up to 60% fewer false positives on vibrato-heavy music | confidence: high on concept; parameter values below come from madmom/librosa rather than the PDF (which could not be parsed here).
- Source: madmom 0.16 docs `madmom.features.onsets` | https://madmom.readthedocs.io/en/v0.16/modules/features/onsets.html | accessed 2026-09-08 | learned: `diff_max_bins = 3`; OnsetPeakPickingProcessor defaults threshold 0.5, combine 0.03 s, fps 100, pre/post windows 0 | confidence: high.
- Source: Müller FMP notebook C6S1 "Onset Detection" | https://www.audiolabs-erlangen.de/resources/MIR/FMP/C6/C6S1_OnsetDetection.html | accessed 2026-09-08 | learned: pedagogical spectral-novelty derivation with log compression and local-average subtraction | confidence: high.

### 2.6 Beat tracking, tempo estimation, beat phase

**Ellis 2007 (dynamic programming, offline / near-offline):**
1. Onset strength envelope: mel spectrogram (Ellis: 40 bands, ~32 ms window, 4 ms hop; librosa: hop 512), log magnitude, first-order difference, half-wave rectify, sum across bands, optionally smooth.
2. Tempo: autocorrelation of the onset envelope over ~0.5-6 s lags, weighted by a log-Gaussian window centred on a prior (`start_bpm = 120`, width about one octave); pick the peak -> beat period `delta`.
3. DP: `D(n) = O(n) + max_m [ D(m) + lambda * P_delta(n - m) ]` with `P_delta(l) = -(log2(l / delta))^2`; lambda ("tightness", librosa default 100) trades onset fit against tempo consistency. Backtrack from `argmax D` through stored predecessors.
Properties: global, O(N * search window), deterministic, excellent for pre-analyzed tracks; not causal.

**Causal alternatives for live input:**
- aubio `tempo`: Davies & Plumbley context-dependent causal beat tracker (ISMIR 2004; AES 2005 with Brossier), default buffer 1024 / hop 512. GPL, so only as a reference for behaviour, not code.
- Böck, Krebs, Widmer 2015: RNN onset activation + bank of resonating comb filters to find the dominant periodicity (MIREX 2013 winner); madmom implements it (BSD-2 code, but its trained models are CC BY-NC-SA 4.0 -> not usable commercially without permission).
- Foscarin, Schlüter, Widmer 2024 "Beat This!": transformer beat/downbeat tracker without DBN post-processing; MIT code; a C++ port exists (`mosynthkey/beat_this_cpp`). Heavy (neural inference) but permissive; candidate for a later "pro" offline analysis pass. Check model-weight license separately before adopting.
- Our causal baseline for 0.1: comb-filter/autocorrelation tempogram on the onset envelope (window 4-8 s, updated every hop, log-Gaussian prior around 120 BPM, octave-error hysteresis), plus a phase-locked loop: predict next beat at `lastBeat + period`, accept the strongest onset within +/- 15% of the period, blend `period` toward observed inter-beat intervals with a slow one-pole.

**Beat phase:** `beatPhase = frac((frame - lastBeatFrame) / periodFrames)` in [0,1); expose also `barPhase` if downbeats are known (offline "Beat This!" or simple 4/4 assumption). Phase is the most useful control signal for visuals (drives cyclic motion) and must wrap continuously; treat it as a `hold + advance` signal that the renderer extrapolates using `tempo` between analysis hops so it does not stair-step at 100 Hz.

**Tempo:** report BPM plus a confidence (peak/mean ratio of the tempogram) and keep octave-ambiguity handling explicit (prefer 70-180 BPM window; expose `tempoHalf`/`tempoDouble`).

Citations:
- Source: D. P. W. Ellis, "Beat Tracking by Dynamic Programming", J. New Music Research 36(1):51-60, 2007 | https://www.ee.columbia.edu/~dpwe/pubs/Ellis07-beattrack.pdf ; https://www.tandfonline.com/doi/abs/10.1080/09298210701653344 | accessed 2026-09-08 | learned: onset envelope (mel, difference, HWR), autocorrelation with log-Gaussian window at 120 BPM, DP objective with tightness weight, backtracking | confidence: high (exact numeric parameters not re-verified from the PDF in this session).
- Source: Müller FMP notebook C6S3 "Beat Tracking by Dynamic Programming" | https://www.audiolabs-erlangen.de/resources/MIR/FMP/C6/C6S3_BeatTracking.html | accessed 2026-09-08 | learned: `D(n) = Delta(n) + max_m (D(m) + lambda P_delta(n-m))`, `P_delta(l) = -(log2(l/delta))^2`, backtracking | confidence: high.
- Source: librosa 0.11 `beat.beat_track` docs | http://librosa.org/doc/0.11.0/generated/librosa.beat.beat_track.html | accessed 2026-09-08 | learned: defaults hop 512, start_bpm 120, tightness 100, trim True; references Ellis 2007 | confidence: high.
- Source: aubio `beattracking.h` and aubiotrack man page | https://github.com/aubio/aubio/blob/master/src/tempo/beattracking.h , https://aubio.org/manpages/latest/aubiotrack.1.html | accessed 2026-09-08 | learned: Davies context-dependent causal tracker; ISMIR 2004 and AES 118 (2005) references; buffer 1024 hop 512 | confidence: high.
- Source: S. Böck, F. Krebs, G. Widmer, "Accurate Tempo Estimation Based on Recurrent Neural Networks and Resonating Comb Filters", ISMIR 2015, pp. 625-631 | https://archives.ismir.net/ismir2015/paper/000196.pdf | accessed 2026-09-08 | learned: comb-filter bank periodicity estimation on RNN activations | confidence: high.
- Source: GitHub CPJKU/madmom | https://github.com/CPJKU/madmom | accessed 2026-09-08 | learned: BSD code, CC BY-NC-SA 4.0 models, commercial use of models requires contacting Gerhard Widmer | relevance: model license blocks direct reuse | confidence: high.
- Source: F. Foscarin, J. Schlüter, G. Widmer, "Beat this! Accurate beat tracking without DBN postprocessing", ISMIR 2024 | https://arxiv.org/abs/2407.21658 ; https://github.com/CPJKU/beat_this ; C++ port https://github.com/mosynthkey/beat_this_cpp | accessed 2026-09-08 | learned: MIT code, transformer/conv architecture, no DBN | confidence: high on code license; model weights license unverified.

### 2.7 Chroma

- Pitch class profile (Fujishima 1999): map each STFT bin (or a constant-Q/log-frequency filterbank bin) to its MIDI pitch `p = 69 + 12 log2(f/440)`, accumulate energy into 12 pitch classes `p mod 12`, normalize (L2 or max) per frame. Müller & Ewert's Chroma Toolbox (ISMIR 2011) adds pitch filterbank front-ends and smoothed/normalized variants (CENS, CRP) that are more robust to timbre and dynamics.
- Implementation: precompute a sparse `12 x (N/2+1)` matrix at the tonal STFT size (N = 4096 at 48 kHz gives 11.7 Hz bins, adequate from ~C3; for lower octaves use N = 8192 or a multi-rate front end). Optionally log-compress magnitudes first and apply a harmonic-weighting kernel (HPCP) to reduce octave/fifth leakage. Provide `chroma[12]` and derived `key`/`tonalCentroid` later.

Citations:
- Source: T. Fujishima, "Realtime chord recognition of musical sound: a system using Common Lisp Music", ICMC 1999 (via Wikipedia "Chroma feature" and "Harmonic pitch class profiles") | https://en.wikipedia.org/wiki/Chroma_feature , https://en.wikipedia.org/wiki/Harmonic_pitch_class_profiles | accessed 2026-09-08 | learned: PCP definition, 12-D vectors | confidence: high.
- Source: M. Müller, S. Ewert, "Chroma Toolbox: MATLAB implementations for extracting variants of chroma-based audio features", ISMIR 2011 | https://www.audiolabs-erlangen.de/content/resources/MIR/chromatoolbox/2011_MuellerEwert_ChromaToolbox_ISMIR.pdf ; https://resources.mpi-inf.mpg.de/MIR/chromatoolbox/ | accessed 2026-09-08 | learned: pitch filterbank -> chroma, CENS/CRP variants | confidence: high.

### 2.8 Pitch (YIN)

de Cheveigné & Kawahara 2002. Steps for a window of W samples and lag range `tau in [fs/fmax, fs/fmin]`:
1. Difference function `d(tau) = sum_j (x_j - x_{j+tau})^2` (O(W * taumax); can be computed via FFT autocorrelation for large W).
2. Cumulative mean normalized difference `d'(tau) = d(tau) / ((1/tau) sum_{j=1..tau} d(j))`, `d'(0) = 1`. Removes the bias toward small lags and the need for an upper frequency limit.
3. Absolute threshold: pick the smallest tau with `d'(tau) < 0.1` (paper's threshold); if none, the global minimum.
4. Parabolic interpolation around the minimum for sub-sample lag.
5. (Optional) best local estimate refinement. `f0 = fs / tau`. Aperiodicity/confidence = `d'(tau_min)`.
Error rates about 3x lower than competing methods on speech; aubio offers `yin` and a faster FFT-based `yinfast`; pYIN (Mauch & Dixon 2014) adds HMM smoothing when a stable pitch track is required. For visuals, expose `pitchHz`, `pitchMidi`, and `pitchConfidence = 1 - d'(tau)`, gated by RMS.

Citations:
- Source: A. de Cheveigné, H. Kawahara, "YIN, a fundamental frequency estimator for speech and music", JASA 111(4):1917-1930, 2002 | http://audition.ens.fr/adc/pdf/2002_JASA_YIN.pdf ; https://pubs.aip.org/asa/jasa/article/111/4/1917/547221 | accessed 2026-09-08 | learned: algorithm steps, threshold 0.1, ~3x lower error, no upper frequency limit | confidence: high.
- Source: aubio 0.4.6 release notes (yinfast) | https://github.com/aubio/aubio/releases | accessed 2026-09-08 | learned: "yinfast" FFT-accelerated variant added in 0.4.6 | confidence: high.
- Source: M. Mauch, S. Dixon, "pYIN: a fundamental frequency estimator using probabilistic threshold distributions", ICASSP 2014 | https://webspace.eecs.qmul.ac.uk/s.e.dixon/pub/2014/MauchDixon-PYIN-ICASSP2014.pdf | accessed 2026-09-08 | learned: probabilistic thresholds + HMM tracking | confidence: high.

### 2.9 Envelope followers and smoothing coefficients

One-pole smoother `y[n] = y[n-1] + a * (x[n] - y[n-1])`, equivalently `y[n] = (1-a) y[n-1] + a x[n]`, with `a = 1 - exp(-1 / (tau * fs_ctrl))` where `tau` is the time constant in seconds (63% step response) and `fs_ctrl` is the rate at which the smoother runs — the audio sample rate for sample-level followers, or the *hop rate* `fs/H` for feature-level smoothing (important: features are smoothed at hop rate, so a 100 ms attack at 187.5 Hz hop rate is `a = 1 - exp(-1/(0.1*187.5)) ~ 0.052`).
- Attack/release (peak or RMS detector, DAFX / Zölzer): choose `a_att` when `x > y` and `a_rel` otherwise. Typical visual values: attack 5-30 ms, release 100-500 ms. For "instant attack" set `a_att = 1`.
- If the target time is "time to reach 90%" rather than 63%, use `a = 1 - exp(-ln(10) / (t90 * fs))`; for 99% use `ln(100)`.
- Rate-independent behaviour: always derive `a` from `tau` and the actual control rate so that changing hop size does not change feel; store `tau`, not `a`, in presets.
- Peak-hold with decay (VU-style): `y = max(x, y * exp(-1/(tau*fs)))`; linear decay `y = max(x, y - rate/fs)` is what most music visualizers use for "falling bars".
- Second-order (two cascaded one-poles) gives a smoother, non-exponential shape; a one-pole is enough for 0.1.

Citations:
- Source: U. Zölzer (ed.), *DAFX: Digital Audio Effects*, 2nd ed., Wiley 2011 (dynamics chapter: one-pole attack/release ballistics); KVR thread "C++: Question on Envelope Detection"; Elementary Audio "Envelope Generators" | https://www.kvraudio.com/forum/viewtopic.php?t=278170 , https://www.elementary.audio/docs/tutorials/envelope-generators | accessed 2026-09-08 | learned: `coef = exp(-1/(tau*fs))` release/attack coefficients; envelope follower is a one-pole with state-dependent coefficient | confidence: high (standard textbook material; the web sources are secondary confirmations).

### 2.10 Loudness (ITU-R BS.1770 / EBU R128), A-weighting, psychoacoustic descriptors

**BS.1770 / R128:**
- Revision history: BS.1770-2 (2011-03, added gating), -3 (2012-08), -4 (2015-10), -5 (2023-11). EBU R128 builds on BS.1770, adds Loudness Range (LRA), target -23 LUFS, and the gating.
- Algorithm: per channel, K-weighting = two cascaded biquads: (1) high shelf (libebur128 prototype: f0 = 1681.97 Hz, gain +3.9998 dB, Q = 0.7072), (2) RLB high-pass (f0 = 38.135 Hz, Q = 0.5003), coefficients derived per sample rate from these analog prototypes; then mean square per 400 ms block with 75% overlap (100 ms step); loudness `L = -0.691 + 10 log10( sum_i G_i * z_i )` with channel weights G = 1.0 (L, R, C) and 1.41 (Ls, Rs); momentary = 400 ms window, short-term = 3 s window; integrated loudness applies an absolute gate at -70 LKFS and a relative gate at -10 LU below the ungated mean.
- Use for us: `audio.loudnessMomentary` / `audio.loudnessShortTerm` (LUFS, smooth and perceptually meaningful — better than RMS for "how loud does it feel"), and integrated loudness of a track for offline normalization so that presets behave the same across quiet and loud masters. libebur128 (MIT) provides `ebur128_add_frames_float`, `ebur128_loudness_momentary/shortterm/global` and is streaming-friendly; it allocates at init and is designed to be fed from a non-RT thread (our analysis thread), which fits.

**A-weighting:** IEC 61672-1:2013. `R_A(f) = 12194^2 f^4 / [ (f^2 + 20.6^2) sqrt((f^2 + 107.7^2)(f^2 + 737.9^2)) (f^2 + 12194^2) ]`, `A(f) = 20 log10(R_A(f)) + 2.00 dB` (0 dB at 1 kHz). Digital implementation: bilinear-transform the s-domain poles (20.6, 107.7, 737.9, 12194 Hz) into three cascaded biquads (MATLAB `weightingFilter` documents this), or apply the curve directly as per-bin gains on the power spectrum (cheap and adequate for a visualizer). K-weighting (loudness) is generally the better choice for music; A-weighting is worth exposing for "perceived level" of individual bands.

**Psychoacoustic descriptors (later):** Zwicker loudness (ISO 532-1) and sharpness/roughness/fluctuation strength are heavier (Bark filterbank, specific loudness, temporal masking). For visuals the useful approximations are: Bark/mel band energies (already covered by the filterbank), spectral centroid as brightness, spectral flatness as noisiness, and ITU loudness as level. Full Zwicker models are optional and would be in-house implementations (no permissive C++ library found in this survey).

Citations:
- Source: Wikipedia "LKFS" (summarizing ITU-R BS.1770-1..-5 and EBU R128) | https://en.wikipedia.org/wiki/LKFS | accessed 2026-09-08 | learned: revision dates; K-weighting; gating; LU/LUFS terminology | confidence: high on dates; coefficients not on that page.
- Source: libebur128 source `ebur128.c` (jiixyj) | https://raw.githubusercontent.com/jiixyj/libebur128/master/ebur128/ebur128.c | accessed 2026-09-08 | learned: high-shelf prototype f0 = 1681.974 Hz, G = 3.99984 dB, Q = 0.70718; high-pass f0 = 38.1355 Hz, Q = 0.50033; relative gate -10, absolute -70, -0.691 constant; streaming API `ebur128_add_frames_float`, `ebur128_loudness_momentary`, `ebur128_loudness_shortterm` | confidence: high.
- Source: ITU-R BS.1770-5 (2023-11), "Algorithms to measure audio programme loudness and true-peak audio level" | https://www.itu.int/dms_pubrec/itu-r/rec/bs/R-REC-BS.1770-5-202311-I!!PDF-E.pdf | accessed 2026-09-08 (PDF not machine-readable in this session; details cross-checked via Wikipedia and libebur128) | learned: gating 400 ms / 75% overlap, -70 LKFS absolute, -10 LU relative, true-peak oversampling, channel weights | confidence: high on values (they are also encoded in libebur128's conformance tests).
- Source: Fora Soft glossary "ITU-R BS.1770" (secondary) | https://www.forasoft.com/learn/audio-for-video/glossary/terms-audio/itu-r-bs1770 | accessed 2026-09-08 | learned: 400 ms / 75% blocks, -70 LUFS / -10 LU gates, true-peak oversampling | confidence: medium (secondary).
- Source: Wikipedia "A-weighting" (IEC 61672-1:2013) and MathWorks "Audio Weighting Filters" | https://en.wikipedia.org/wiki/A-weighting , https://www.mathworks.com/help/audio/ug/audio-weighting-filters.html | accessed 2026-09-08 | learned: `R_A(f)` formula, +2.00 dB normalization, pole frequencies; IIR realization as cascaded biquads | confidence: high.

---

## 3. Part B — Library survey

### 3.1 FFT libraries

| Library | License | Latest | Platforms / SIMD | Notes |
|---|---|---|---|---|
| KissFFT | BSD-3-Clause | 131.2.0 (2024-10-22) | portable C, no SIMD by default (SSE via macro) | Simple, deterministic, real FFT `kiss_fftr`, mixed radix any N. Fine baseline. |
| pffft (marton78 fork) | BSD-like (FFTPACK license, proprietary-compatible) | rolling master | SSE1/AVX/AVX2, AltiVec, NEON (incl. Apple silicon), WASM SIMD | Much faster than KissFFT; float and double; N built from factors 2,3,5 with SIMD-width multiples (real transforms sizes multiple of 32 in SSE/NEON builds); requires aligned buffers; includes `pffastconv` FIR convolution. Original by Julien Pommier. |
| PocketFFT (C++ header) | BSD-3-Clause | rolling (used by NumPy/SciPy) | scalar + vectorized via templates | Header-only C++11, arbitrary N (Bluestein fallback), multi-dimensional; numerically excellent; not as fast as pffft for power-of-2 audio sizes. |
| FFTW 3.3.10 | GPL-2.0+ (commercial license from MIT TLO) | 3.3.10 (2021) | everything, best planning | GPL — excluded. |
| Apple vDSP (Accelerate) | Apple SDK (free with macOS/iOS) | OS | Apple only, AMX/NEON | Fastest on M-series; power-of-2 only for `vDSP_fft_*`, split-complex layout; `vDSP_DFT_*` supports more sizes. Optional macOS backend behind an interface; not for golden tests. |
| muFFT | MIT | 2016-2017, dormant | SSE/SSE3/AVX; no NEON | Fast but x86-oriented and unmaintained; skip. |
| meow_fft | 0-clause BSD | 2017-2019, dormant | C99 single header, scalar | Tiny; slower; skip. |
| KFR | GPL-2.0+ / commercial | 7.1.0 (2026-08-18) | SSE..AVX-512, NEON, RVV | Excellent (new low-overhead FFT API, audio file module in 7.x) but GPL — excluded unless a commercial license is purchased. |
| Intel IPP | free of charge, royalty-free binaries; not redistributable as oneAPI package | 2026.x | x86 only; macOS dropped since oneAPI 2024.0 | Irrelevant on Apple silicon; skip. |

Recommendation: **pffft** as the default real FFT (NEON on Apple silicon, AVX on x86), **KissFFT** as the portable reference/fallback and as the "golden" implementation in tests (KissFFT and pffft results agree to ~1e-6 relative; keep golden tolerances accordingly). Wrap both behind a tiny `IRealFFT` interface (`forward(const float* in, std::complex<float>* out)`) so vDSP can be added later.

Citations:
- Source: GitHub mborgerding/kissfft releases | https://github.com/mborgerding/kissfft/releases | accessed 2026-09-08 | learned: 131.2.0 (2024-10-22), 131.1.0 (2024-02-16), v131 (2024-02-01, SPDX identifiers, moved to Git) | confidence: high. License BSD-3 per repo README | https://github.com/mborgerding/kissfft | confidence: high.
- Source: GitHub marton78/pffft README | https://github.com/marton78/pffft/blob/master/README.md | accessed 2026-09-08 | learned: "BSD-like" license "compatible with proprietary projects"; SSE1/AVX/AVX2/FMA, AltiVec, NEON (Apple silicon), WASM SIMD; factors 2,3,5; `pffastconv`; double precision option | confidence: high.
- Source: GitHub mreineck/pocketfft (cpp branch) README and LICENSE | https://github.com/mreineck/pocketfft/blob/cpp/README.md , https://github.com/mreineck/pocketfft/blob/cpp/LICENSE.md | accessed 2026-09-08 | learned: BSD-3-Clause (Max-Planck-Society), header-only C++11, Bluestein for arbitrary N, multi-dim | confidence: high.
- Source: FFTW "License and Copyright" | https://www.fftw.org/doc/License-and-Copyright.html | accessed 2026-09-08 | learned: GPL v2 or later; non-free licenses purchasable from MIT | confidence: high.
- Source: Apple Developer Documentation vDSP.FFT / vDSP_fft_zip / vDSP_create_fftsetup | https://developer.apple.com/documentation/accelerate/vdsp/fft , https://developer.apple.com/documentation/accelerate/vdsp_fft_zip | accessed 2026-09-08 | learned: power-of-2 lengths, split-complex format, DFT API for other sizes | confidence: high.
- Source: GitHub Themaister/muFFT and JodiTheTigger/meow_fft | https://github.com/Themaister/muFFT , https://github.com/JodiTheTigger/meow_fft | accessed 2026-09-08 | learned: muFFT MIT; meow_fft 0-clause BSD, C99 header-only | confidence: high on licenses; "dormant" judged from repository activity (medium).
- Source: GitHub kfrlib/kfr releases and kfrlib.com purchase page | https://github.com/kfrlib/kfr/releases , https://www.kfrlib.com/purchase/ | accessed 2026-09-08 | learned: KFR 7.1.0 (2026-08-18) new FFT API, 7.0.1 (2025-11-14) audio module; dual GPLv2+/commercial, commercial license mandatory for closed-source commercial use | confidence: high.
- Source: Intel IPP release notes 2026 and oneAPI macOS installation guide; Melatonin blog on IPP with JUCE | https://www.intel.com/content/www/us/en/developer/articles/release-notes/ipp/2026.html , https://www.intel.com/content/www/us/en/docs/oneapi/installation-guide-macos/2024-0/overview.html , https://melatonin.dev/blog/using-intel-performance-primitives-ipp-with-juce-and-cmake/ | accessed 2026-09-08 | learned: macOS no longer supported from oneAPI 2024.0; IPP free/royalty-free but the oneAPI package is not freely redistributable; x86 only | confidence: high.

### 3.2 Analysis libraries

| Library | License | Latest / status | Scope | Verdict |
|---|---|---|---|---|
| aubio | GPL-3.0+ ("not MIT or BSD licensed. Contact the author if you need it in your commercial product") | 0.4.9 (2019-02); repo commits into 2026, 0.5.0-alpha docs, 147 open issues | onset, pitch (yin, yinfft, yinfast...), tempo, notes, MFCC, PVOC, filterbank | Reference behaviour only; GPL excludes linking. |
| Essentia | AGPL-3.0 | releases through 2026-05; PyPI 2.1b6.dev1438 | huge MIR set incl. TensorFlow models | AGPL + heavy deps (FFTW, Eigen, libsamplerate, TagLib, FFmpeg, YAML, Chromaprint, TensorFlow). Excluded. |
| Gist (Adam Stark) | GPL-3.0 | small, low activity | RMS, peak, ZCR, centroid, crest, flatness, rolloff, kurtosis, flux, HFC, complex spectral difference, pitch (YIN), MFCC | GPL; excluded, but its README is a nice checklist of features. |
| madmom | BSD-2 code; CC BY-NC-SA 4.0 models | Python | RNN onset/beat/downbeat, comb-filter tempo | Python + NC models; use as a test oracle for beats/onsets, not in product. |
| libebur128 | MIT | 1.2.6 (2021-02-14); stable, packaged everywhere | BS.1770/R128 momentary/short-term/integrated, LRA, true peak | Adopt for loudness. |
| Q (cycfi) | Boost Software License 1.0 | v1.5-dev on master, active (2019-2026) | filters (TPT SVF, Moog), oscillators, true-RMS envelope follower, peak/AR envelopes, pitch detection (bitstream autocorrelation), dynamics; C++20; I/O layer optional (PortAudio) | Permissive and idiomatic; candidate for envelope followers and filters (header-only core). |
| JUCE DSP | AGPL / commercial | 8.x | FFT (wraps vDSP/IPP/fallback), windowing, IIR/FIR, convolution | Excluded with JUCE. |
| KFR | GPL / commercial | 7.1.0 (2026-08) | FFT, FIR/IIR, resampling, audio I/O | Excluded (license). |
| Intel IPP | proprietary, free | 2026 | FFT, filters | x86 only; excluded. |
| chromaprint | MIT source, but bundles FFmpeg parts -> LGPL-2.1 as a whole; optional FFT backends include FFTW (GPL), KissFFT, vDSP | 1.6.1 (2024-07-28); ARM64 and macOS fat binaries | acoustic fingerprinting for AcoustID | Not an analysis library for visuals; only relevant if we ever want track identification. |

Citations:
- Source: aubio.org, GitHub aubio/aubio (README, releases, issues) | https://aubio.org/ , https://github.com/aubio/aubio , https://github.com/aubio/aubio/releases , https://github.com/aubio/aubio/issues | accessed 2026-09-08 | learned: GPL-3.0+; "not MIT or BSD licensed. Contact the author if you need it in your commercial product"; 0.4.9 was the last release (Feb 27, 2019, three CVE fixes); repository updated April 2026; 147 open issues; features list | confidence: high on license and features; medium on "last release 2019" (release page shows no newer tag; 0.5.0 exists only as alpha docs).
- Source: GitHub MTG/essentia; PyPI essentia | https://github.com/MTG/essentia , https://pypi.org/project/essentia/ | accessed 2026-09-08 | learned: AGPL-3.0; dependency list; May 2026 release activity | confidence: high.
- Source: GitHub adamstark/Gist LICENSE.txt | https://github.com/adamstark/Gist/blob/master/LICENSE.txt | accessed 2026-09-08 | learned: GPL v3 | confidence: high.
- Source: GitHub jiixyj/libebur128 releases and README | https://github.com/jiixyj/libebur128/releases , https://github.com/jiixyj/libebur128 | accessed 2026-09-08 | learned: MIT; v1.2.6 (Feb 14) bugfix for Windows dynamic linking; 1.2.0 added real-time monitoring functions and conformance tests | confidence: high (year of 1.2.6 is 2021 per NixOS PR #113275).
- Source: GitHub cycfi/q LICENSE and README; Q docs | https://github.com/cycfi/q/blob/master/LICENSE , https://github.com/cycfi/q , https://cycfi.github.io/q/q/v1.5-dev/index.html | accessed 2026-09-08 | learned: "Boost Software License - Version 1.0" (note: several secondary sources say MIT — the LICENSE file is BSL-1.0); C++20; features | confidence: high.
- Source: GitHub acoustid/chromaprint LICENSE.md and releases | https://github.com/acoustid/chromaprint/blob/master/LICENSE.md , https://github.com/acoustid/chromaprint/releases | accessed 2026-09-08 | learned: MIT own code, LGPL-2.1 overall due to FFmpeg parts; 1.6.1 (2024-07-28) | confidence: high.

---

## 4. Recommended stack

### Milestone 0.1 (must ship)

| Concern | Choice | License | Why |
|---|---|---|---|
| Device I/O + decoding + resampling + mixing | **miniaudio 0.11.25** (low-level `ma_device` + `ma_decoder`; evaluate `ma_engine` for playback convenience) | PD / MIT-0 | One file, all platforms, decoders included, lock-free ring buffers included, active. |
| OGG Vorbis | **stb_vorbis** hooked into miniaudio | PD / MIT | Zero-dependency. |
| FFT | **pffft** (NEON/AVX) with **KissFFT** as portable reference behind `IRealFFT` | BSD-like / BSD-3 | Speed + determinism + a golden reference. |
| Loudness | **libebur128 1.2.6** | MIT | Standard-conformant LUFS with streaming API. |
| Everything else in analysis (STFT, windows, RMS/peak, filterbank/bands, centroid/rolloff/flatness/flux, SuperFlux-style onsets, Ellis DP beat tracker offline, causal tempogram + PLL live, chroma, YIN, envelope followers, A-/K-weighting curves) | **in-house C++20** | ours | Each is < 300 lines given an FFT; keeps the license surface tiny; guarantees determinism and frame-indexed operation. |
| Lock-free queues | miniaudio `ma_pcm_rb`, or a 60-line SPSC ring of our own | PD / ours | Trivial. |

### Optional / later

- **Q (Boost 1.0)** for polished envelope followers, SVF/ladder filters, and its pitch detector if in-house YIN proves insufficient.
- **libopus + opusfile (BSD-3)** through miniaudio's custom decoder backend when Opus input matters.
- **vDSP** backend for FFT and vector ops on macOS (behind `IRealFFT`), only after profiling shows FFT is a bottleneck (unlikely: two 4096-point real FFTs per hop is microseconds on M2 Max).
- **PortAudio / RtAudio** only if a backend gap appears (ASIO on Windows, exotic Linux setups).
- **"Beat This!" (MIT) offline beat/downbeat pass** via ONNX/CoreML export if DP beat tracking is not good enough on real material; check model license first.
- **libsndfile (LGPL)** in an offline CLI tool for exotic formats, never in the runtime.
- **Chroma-based key detection, Zwicker loudness, MFCC** as later descriptors.

### Explicitly rejected (license or platform)

FFTW (GPL), KFR (GPL/commercial), aubio (GPL), Gist (GPL), Essentia (AGPL + deps), JUCE (AGPL/commercial with revenue caps), Intel IPP (x86-only, macOS dropped), libsoundio (dormant), SDL3 audio (no decoder, weaker RT contract unless SDL is already the platform layer).

Rationale summary: the analysis features a visualizer needs are all classical, well-documented algorithms; the only pieces where a library adds real value are the FFT kernel (pffft), format decoding (miniaudio), and standards conformance (libebur128). Everything else being in-house removes GPL/AGPL exposure, makes offline determinism a design property rather than an afterthought, and lets the analysis be driven by PCM frame index from day one.

---

## 5. Analysis signal model

### 5.1 Signal catalogue (namespace `audio.`)

All continuous signals are float32 in a documented range; events carry a frame index. Every signal is available for both channels mixed to mono (default) and, where cheap, per channel (`audio.L.*`, `audio.R.*`) and mid/side later.

| Signal | Range | Rate | Definition |
|---|---|---|---|
| `audio.rms` | 0..1 | hop | windowed RMS, linear |
| `audio.rmsDb` | -96..0 | hop | 20 log10(rms) clamped |
| `audio.peak` | 0..1 | hop | max abs sample in hop |
| `audio.loudness` | LUFS (-70..0) | 100 Hz | momentary (400 ms) via libebur128 |
| `audio.loudnessShort` | LUFS | 10 Hz | short-term (3 s) |
| `audio.bass`, `audio.lowMid`, `audio.mid`, `audio.highMid`, `audio.treble` | 0..1 | hop | log-compressed band energy (absolute, normalized by a running or offline max) |
| `audio.bassRel` ... `audio.trebleRel` | 0..1 | hop | band / total energy |
| `audio.bands[N]` | 0..1 | hop | N log-spaced bands (default 32) for spectrum displays |
| `audio.spectrum[K]` | 0..1 | hop | log-magnitude bins (for raw visualizers) |
| `audio.centroid` | 0..1 (log-Hz normalized), plus Hz | hop | spectral centroid |
| `audio.rolloff` | Hz | hop | 85% rolloff |
| `audio.flatness` | 0..1 | hop | Wiener entropy |
| `audio.contrast[6]` | dB | hop | octave-band contrast |
| `audio.flux` | 0..1 | hop | normalized HWR spectral flux |
| `audio.onsetStrength` | 0..1 | hop | SuperFlux-style ODF, normalized by adaptive threshold |
| `audio.onset` | event {frame, strength} + 0/1 pulse | event | peak-picked onset |
| `audio.onsetLow` / `audio.onsetHigh` | event | event | band-limited onsets (kick / hats) |
| `audio.tempo` | BPM (40..300) | ~1 Hz update | tempogram peak; offline: Ellis global tempo |
| `audio.tempoConfidence` | 0..1 | ~1 Hz | peak prominence |
| `audio.beat` | event {frame, index} + pulse | event | tracked beat |
| `audio.beatPhase` | 0..1 wrap | continuous (extrapolated per render frame) | phase since last beat / period |
| `audio.barPhase` | 0..1 wrap | continuous | requires downbeats; fallback: beat index mod 4 |
| `audio.chroma[12]` | 0..1 | tonal hop | normalized pitch-class energy |
| `audio.pitchHz`, `audio.pitchMidi`, `audio.pitchConfidence` | Hz / MIDI / 0..1 | tonal hop | YIN |
| `audio.silence` | 0/1 | hop | rms < threshold for > 200 ms |

### 5.2 Per-signal processing chain

Each mapping from a raw signal to a scene parameter is a small ordered pipeline; every stage is optional and every parameter is animatable. Order is fixed so presets are reproducible:

```
raw -> gain -> offset -> curve -> normalize -> clamp -> threshold/gate -> smoothing(attack, decay) -> envelope -> remap -> out
```

| Stage | Parameters | Semantics |
|---|---|---|
| `gain` | float | multiply |
| `offset` | float | add |
| `curve` | `linear`, `pow(k)`, `log`, `exp`, `sCurve(k)`, `dbToLin`, `linToDb` | shape; `pow(0.5)` for RMS-like signals is the common "make quiet parts visible" tweak |
| `normalize` | `none`, `runningMax(tau)`, `offlineMax`, `percentile(p, window)`, `lufsRelative(targetLUFS)` | maps to 0..1; `runningMax` decays with time constant tau so live input auto-scales; offline mode uses the whole-track maximum for determinism |
| `clamp` | min, max | hard limits |
| `threshold` | level, hysteresis, mode `gate`/`binary`/`subtract` | gate: pass-through above level else 0; binary: 0/1; subtract: max(0, x - level)/(1 - level) |
| `smoothing` | `attackMs`, `decayMs` (asymmetric one-pole at hop rate, coefficients from 2.9) | continuous smoothing |
| `envelope` | `mode` (`ar`, `peakHold`, `linearFall(rate)`), `holdMs` | turns events (onset/beat) into control envelopes: an impulse with strength s sets env = max(env, s), then decays per mode |
| `remap` | `inMin, inMax, outMin, outMax`, `wrap`/`mirror`/`clamp` | final linear map into scene parameter units |

Events (`onset`, `beat`) enter the chain as impulses (value = strength for one hop, else 0) so they share the same stages; `envelope` is what makes them useful. `beatPhase` bypasses `smoothing` by default (wrap-aware) and offers `curve` variants like `triangle`, `sawtooth`, `sine` to turn phase into oscillators.

Implementation notes: the chain runs at hop rate in the analysis thread for signals that must be shared by many consumers, and at render rate on the render thread for per-binding chains (cheap; a few dozen flops). State (smoother memory, running max) lives in the binding, is reset on seek, and is serialized with the timeline position so offline renders resume deterministically after a seek.

---

## 6. Testing strategy

### 6.1 Synthetic-signal unit tests (golden, deterministic)

| Test signal | Expected | Tolerance / notes |
|---|---|---|
| 440 Hz sine, -6 dBFS, 5 s, 48 kHz | spectral peak bin at 440 Hz (with parabolic interpolation, error < 0.5 Hz at N=4096); `rms = 0.3535 (0.5/sqrt2)`; `peak = 0.5`; `centroid ~ 440 Hz`; `flatness < 0.05`; `pitchHz = 440 +/- 1`; `chroma` max at A; `onset` count 1 (at start) | Verifies STFT scaling, window gain compensation, YIN, chroma |
| Digital silence | every level signal exactly 0; `loudness = -inf` (or clamp -70); `flatness` defined (no NaN); no onsets; `silence = 1` | NaN/Inf guards |
| White noise (seeded PRNG, 10 s) | flatness > 0.9 (Hann-windowed power spectrum, averaged over frames); band energies proportional to bandwidth in the linear-bin sum; centroid ~ fs/4 = 12 kHz; no beat with confidence > 0.3 | Averages over frames; seed fixed |
| Impulse train (clicks every 0.5 s, 20 s) | onsets at 0.5 s multiples within +/- 1 hop (+/- 5.3 ms at H=256); no extra onsets; `flux` spikes only at clicks | Also tests peak-picker `wait` |
| Click track 120 BPM (10 s) | `tempo = 120 +/- 1`; beats at 0.5 s multiples within +/- 1 hop; `beatPhase` ramps 0->1 between beats; octave check: 60/240 rejected by prior | Ellis DP offline and causal tracker live |
| Tempo sweep 100->140 BPM over 30 s | causal tracker follows within 2 BPM after 4 s; DP path reports piecewise beats with < 15 ms error | Tests PLL bandwidth |
| Chirp 20 Hz -> 20 kHz | centroid and rolloff monotonic increasing; bands light up in order | Band boundaries |
| Vibrato tone (440 Hz +/- 30 cents at 6 Hz) | plain spectral flux produces periodic false onsets; SuperFlux max-filter path produces only the initial onset | Validates the vibrato suppression |
| Two-tone A-weighting probe (100 Hz vs 1 kHz equal amplitude) | A-weighted level of 100 Hz component ~ -19.1 dB relative to 1 kHz | From IEC curve |
| EBU R128 conformance: 1 kHz sine at -23 dBFS (stereo) | integrated loudness -23.0 LUFS +/- 0.1; libebur128's own conformance suite if we vendor it | Standard test vectors |

### 6.2 Determinism requirements

- Same input file + same `AnalysisConfig` -> bit-identical `FeatureTimeline` on the same platform/compiler; identical within 1e-5 relative across platforms (pffft vs KissFFT, x86 vs ARM). Enforced by a golden-file test that hashes the timeline (with a tolerance-aware comparison for cross-platform CI).
- Live mode must be *chunking-invariant*: feeding the same PCM in callback chunks of 64, 256, 1024 frames produces the identical hop sequence (test by simulating the ring buffer with random chunk sizes; assert identical output).
- No wall-clock dependence anywhere in analysis; the only time base is the PCM frame counter. Tests run analysis faster than real time.
- Thread-safety tests: run the RT copy path with a `-fsanitize=thread` build and an allocation-tracking `operator new` hook that aborts if called on the audio thread (assert via thread-local flag set in the callback).
- Latency accounting test: an impulse at PCM frame F must produce an onset event whose `frameIndex` equals F +/- H (window-centre stamping), independent of N and H; the renderer's `sample(timeline, frame)` must therefore not need per-config latency fudge.
- Fuzz decoders (miniaudio/stb_vorbis) with truncated/corrupt files in CI; analysis must never see NaN (add `std::isfinite` assertions in debug builds after each descriptor).

### 6.3 Test oracles

- librosa (`onset_detect`, `beat_track`, `feature.spectral_*`) and madmom (BSD code) as Python oracles in a `tools/` script that generates expected values for the synthetic signals above; disagreements are tolerated within documented bounds, and the C++ results are then frozen as goldens.
- libebur128 conformance vectors for loudness (EBU Tech 3341/3342 test files).

---

## 7. Sources

Accessed 2026-09-08 unless stated.

Audio I/O and decoding
1. miniaudio site and manual — https://miniaud.io/ , https://miniaud.io/docs/manual/index.html
2. mackron/miniaudio (GitHub) — https://github.com/mackron/miniaudio ; release mirror https://releasealert.dev/github/mackron/miniaudio ; issue #286 https://github.com/mackron/miniaudio/issues/286 ; `ma_decoder_config_init` https://miniaudio.docsforge.com/master/api/ma_decoder_config_init/
3. mackron/dr_libs — https://github.com/mackron/dr_libs , https://github.com/mackron/dr_libs/blob/master/dr_wav.h
4. libsndfile — https://en.wikipedia.org/wiki/Libsndfile , https://vcpkg.link/ports/libsndfile
5. PortAudio — https://files.portaudio.com/download.html , https://github.com/PortAudio/portaudio , https://github.com/PortAudio/portaudio/pulls , https://en.wikipedia.org/wiki/PortAudio
6. RtAudio — https://github.com/thestk/rtaudio , https://github.com/thestk/rtaudio/releases , https://github.com/thestk/rtaudio/blob/master/doc/release.txt
7. SDL3 audio — https://wiki.libsdl.org/SDL3/CategoryAudio , https://wiki.libsdl.org/SDL3/SDL_AudioStream , https://wiki.libsdl.org/SDL3/SDL_OpenAudioDeviceStream , https://wiki.libsdl.org/SDL3/SDL_SetAudioStreamGetCallback , https://github.com/libsdl-org/SDL/releases , https://en.wikipedia.org/wiki/Simple_DirectMedia_Layer
8. JUCE 8 — https://juce.com/legal/juce-8-licence/ , https://github.com/juce-framework/JUCE/blob/master/LICENSE.md , https://en.wikipedia.org/wiki/JUCE
9. Apple Core Audio — https://developer.apple.com/documentation/coreaudio/audiodeviceioproc , https://developer.apple.com/library/ios/qa/qa1715/_index.html , https://developer.apple.com/videos/play/wwdc2020/10224/ , https://developer.apple.com/documentation/audiotoolbox/adding-asynchronous-real-time-threads-to-audio-workgroups
10. libsoundio — https://github.com/andrewrk/libsoundio , https://github.com/andrewrk/libsoundio/releases , https://tracker.debian.org/pkg/libsoundio
11. Xiph codecs — https://github.com/xiph/vorbis , https://opus-codec.org/license/ , https://github.com/xiph/opusfile ; stb_vorbis https://github.com/nothings/stb/blob/master/stb_vorbis.c
12. Ross Bencina, "Real-time audio programming 101: time waits for nothing" (2011) — http://www.rossbencina.com/code/real-time-audio-programming-101-time-waits-for-nothing

Algorithms
13. Müller, FMP Notebooks: STFT https://www.audiolabs-erlangen.de/resources/MIR/FMP/C2/C2_STFT-Basic.html ; Onset detection https://www.audiolabs-erlangen.de/resources/MIR/FMP/C6/C6S1_OnsetDetection.html ; Beat tracking by DP https://www.audiolabs-erlangen.de/resources/MIR/FMP/C6/C6S3_BeatTracking.html
14. Peeters 2004, CUIDADO audio features — http://recherche.ircam.fr/anasyn/peeters/ARTICLES/Peeters_2003_cuidadoaudiofeatures.pdf
15. Jiang et al. 2002, spectral contrast — https://ieeexplore.ieee.org/document/1035731 ; librosa https://librosa.org/doc/main/generated/librosa.feature.spectral_contrast.html
16. Bello et al. 2005, onset detection tutorial — https://hajim.rochester.edu/ece/sites/zduan/teaching/ece472/reading/Bello_2005.pdf
17. Dixon 2006, "Onset Detection Revisited" — https://www.dafx.de/paper-archive/2006/papers/p_133.pdf
18. Böck & Widmer 2013, SuperFlux — https://www.dafx.de/paper-archive/2013/papers/09.dafx2013_submission_12.pdf , https://github.com/CPJKU/SuperFlux
19. madmom docs (onsets, beats) and repo — https://madmom.readthedocs.io/en/v0.16/modules/features/onsets.html , https://madmom.readthedocs.io/en/v0.16/modules/features/beats.html , https://github.com/CPJKU/madmom
20. Ellis 2007, beat tracking by DP — https://www.ee.columbia.edu/~dpwe/pubs/Ellis07-beattrack.pdf , https://www.tandfonline.com/doi/abs/10.1080/09298210701653344 ; librosa beat_track https://librosa.org/doc/0.11.0/generated/librosa.beat.beat_track.html
21. aubio tempo (Davies & Plumbley) — https://github.com/aubio/aubio/blob/master/src/tempo/beattracking.h , https://aubio.org/manpages/latest/aubiotrack.1.html
22. Böck, Krebs, Widmer 2015, tempo via comb filters — https://archives.ismir.net/ismir2015/paper/000196.pdf
23. Foscarin, Schlüter, Widmer 2024, "Beat This!" — https://arxiv.org/abs/2407.21658 , https://github.com/CPJKU/beat_this , https://github.com/mosynthkey/beat_this_cpp
24. Chroma — https://en.wikipedia.org/wiki/Chroma_feature , https://en.wikipedia.org/wiki/Harmonic_pitch_class_profiles , Müller & Ewert 2011 https://www.audiolabs-erlangen.de/content/resources/MIR/chromatoolbox/2011_MuellerEwert_ChromaToolbox_ISMIR.pdf
25. de Cheveigné & Kawahara 2002, YIN — http://audition.ens.fr/adc/pdf/2002_JASA_YIN.pdf , https://pubs.aip.org/asa/jasa/article/111/4/1917/547221 ; pYIN https://webspace.eecs.qmul.ac.uk/s.e.dixon/pub/2014/MauchDixon-PYIN-ICASSP2014.pdf
26. Envelope follower coefficients — https://www.kvraudio.com/forum/viewtopic.php?t=278170 , https://www.elementary.audio/docs/tutorials/envelope-generators (secondary to Zölzer, DAFX 2nd ed.)
27. Loudness — https://en.wikipedia.org/wiki/LKFS , ITU-R BS.1770-5 https://www.itu.int/dms_pubrec/itu-r/rec/bs/R-REC-BS.1770-5-202311-I!!PDF-E.pdf , https://www.forasoft.com/learn/audio-for-video/glossary/terms-audio/itu-r-bs1770 , libebur128 source https://raw.githubusercontent.com/jiixyj/libebur128/master/ebur128/ebur128.c
28. A-weighting — https://en.wikipedia.org/wiki/A-weighting , https://www.mathworks.com/help/audio/ug/audio-weighting-filters.html
29. Band presets (not fetched this session) — p5.sound `p5.FFT.getEnergy` https://p5js.org/reference/p5.sound/p5.FFT/

Libraries
30. KissFFT — https://github.com/mborgerding/kissfft , https://github.com/mborgerding/kissfft/releases
31. pffft (marton78) — https://github.com/marton78/pffft , https://github.com/marton78/pffft/blob/master/README.md
32. PocketFFT — https://github.com/mreineck/pocketfft , https://github.com/mreineck/pocketfft/blob/cpp/LICENSE.md
33. FFTW license — https://www.fftw.org/doc/License-and-Copyright.html
34. Apple vDSP — https://developer.apple.com/documentation/accelerate/vdsp/fft , https://developer.apple.com/documentation/accelerate/vdsp_fft_zip
35. muFFT — https://github.com/Themaister/muFFT ; meow_fft — https://github.com/JodiTheTigger/meow_fft
36. KFR — https://github.com/kfrlib/kfr/releases , https://www.kfrlib.com/purchase/
37. Intel IPP — https://www.intel.com/content/www/us/en/developer/articles/release-notes/ipp/2026.html , https://www.intel.com/content/www/us/en/docs/oneapi/installation-guide-macos/2024-0/overview.html , https://melatonin.dev/blog/using-intel-performance-primitives-ipp-with-juce-and-cmake/
38. aubio — https://aubio.org/ , https://github.com/aubio/aubio , https://github.com/aubio/aubio/releases
39. Essentia — https://github.com/MTG/essentia , https://pypi.org/project/essentia/
40. Gist — https://github.com/adamstark/Gist , https://github.com/adamstark/Gist/blob/master/LICENSE.txt
41. libebur128 — https://github.com/jiixyj/libebur128 , https://github.com/jiixyj/libebur128/releases
42. Q (cycfi) — https://github.com/cycfi/q , https://github.com/cycfi/q/blob/master/LICENSE , https://cycfi.github.io/q/q/v1.5-dev/index.html
43. chromaprint — https://github.com/acoustid/chromaprint , https://github.com/acoustid/chromaprint/blob/master/LICENSE.md , https://github.com/acoustid/chromaprint/releases
