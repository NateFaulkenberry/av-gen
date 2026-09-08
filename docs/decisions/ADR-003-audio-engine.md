# ADR-003: Audio decoding and playback

- Status: Accepted (2026-09-08)
- Research: `docs/research/audio-analysis.md` Part A

## Problem

Load audio files (WAV, FLAC, MP3, later OGG/Opus), play them through the system output device
with low latency, report a sample-accurate playback position for render synchronisation, support
seeking, and expose decoded PCM so analysis can run against timeline position rather than live
capture.

## Alternatives considered

1. miniaudio (public domain / MIT-0; 0.11.x; single header; device I/O, decoding, resampling,
   ring buffers).
2. PortAudio or RtAudio for device I/O plus dr_libs/libsndfile for decoding.
3. SDL3 audio.
4. JUCE.
5. CoreAudio directly.

## Decision

**miniaudio** for device output and decoding, with the following structure:

- `audio::AudioFile` decodes an entire file to float32 PCM in memory via `ma_decoder` (mono and
  interleaved stereo retained), reporting sample rate, channel count, and duration.
- `audio::AudioPlayer` owns a `ma_device` in playback mode. The real-time callback copies from the
  decoded buffer at a `std::atomic<uint64_t>` play-head, applies volume, and pushes the same
  samples into an SPSC ring buffer for the analyser. No allocation, locks, or logging in the
  callback.
- Playback position is derived from the play-head frame index, not wall time.
- Seeking sets the play-head atomically; analysis state is reset on seek.

## Rationale

- One permissively licensed header covers decoding and output on macOS, Windows, and Linux.
- Whole-file decode to memory is the correct model for a timeline-driven tool: seeking is free,
  offline analysis reads the same buffer, and typical tracks (10 minutes stereo float32 at 48 kHz)
  are ~230 MB, acceptable for a desktop application. Streaming decode is a later optimisation
  behind the same interface.
- The callback design follows the research's real-time constraints (no allocation, no locks).

## Consequences

- Memory use scales with track length; very long files (DJ sets) will need streaming decode in
  a later milestone.
- OGG Vorbis requires wiring stb_vorbis into miniaudio (documented, deferred).
- miniaudio's device callback thread is the only real-time thread; the analyser thread is normal
  priority and tolerates jitter because it is fed by a ring buffer.

## Rejected alternatives

- libsndfile: LGPL; no device I/O.
- PortAudio/RtAudio: device I/O only; adds a second library for decoding for no gain.
- SDL3 audio: no decoders; callback thread guarantees weaker.
- JUCE: AGPL/commercial licensing.
- CoreAudio directly: macOS-only.

## Revisit triggers

- Need for live input (1.x): miniaudio supports duplex devices; same library.
- Memory pressure from very long files: add streaming decode behind `AudioFile`.
