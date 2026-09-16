# Audio

Decisions: ADR-003 (engine), ADR-004 (analysis). Research: `docs/research/audio-analysis.md`.

## Purpose and responsibilities

- `audio::AudioFile`: decode a file (WAV, FLAC, MP3 via miniaudio; OGG later) to float32 PCM held
  in memory, plus a mono downmix. Also writes 32-bit float WAV (test fixtures, later export).
- `audio::AudioPlayer`: plays an `AudioFile` through the default output device. The device runs
  at the file's sample rate (miniaudio resamples to the hardware rate) so the play-head is always
  in file frames. Volume, play/pause/stop, seek, end-of-file detection.
- `audio::AnalysisStream`: lock-free SPSC channel from the audio callback to the analysis thread:
  mono samples plus discontinuity markers (`markDiscontinuity(frameIndex)`) so the consumer can
  stamp every sample with its exact PCM frame index across seeks and restarts.
- `analysis::Analyzer`: streaming STFT feature extractor (below).
- `analysis::AnalysisRunner`: thread that turns the stream into `AnalysisFrame`s and publishes
  the newest through a triple buffer; keeps a 512-frame history for plots.
- `analysis::AnalysisTrack`: the same analyzer run over a whole file for offline evaluation,
  addressable by time.

## Interfaces

```cpp
Result<AudioFile> AudioFile::load(path);           // or fromInterleaved(...) for tests
Result<void>      AudioPlayer::setSource(shared_ptr<const AudioFile>);
Result<void>      AudioPlayer::play(); pause(); stop(); seekSeconds(s);
uint64_t          AudioPlayer::positionFrames();   // atomic play-head, no wall clock
AnalysisStream&   AudioPlayer::analysisStream();
void   Analyzer::push(span<const float> mono);  bool Analyzer::pop(AnalysisFrame&);
void   Analyzer::reset(uint64_t startFrameIndex);
```

## Analysis pipeline (defaults: 48 kHz, window 2048, hop 512, periodic Hann)

Per hop the analyzer emits an `AnalysisFrame` stamped with the PCM index of the window centre:

| Feature | Definition | Range |
|---|---|---|
| `rms`, `peak` | over the unwindowed window | 0..1 |
| `magnitude[k]` | `|X[k]| * 2 / (N * coherentGain)` so a full-scale sine reads ~1 at its bin | 0..~1 |
| `spectrum[k]` | `clamp((20 log10(mag + 1e-6) + 60) / 60, 0, 1)` for display | 0..1 |
| `bandsRaw[b]` | `min(1, sqrt(sum mag^2 over bins in band))`; bands: bass 20-150, lowMid 150-400, mid 400-2000, highMid 2000-6000, treble 6000-16000 Hz | 0..1 |
| `bands[b]` | `bandsRaw / runningPeak`, running peak decays with a 4 s time constant (floor 1e-4) | 0..1 |
| `centroidHz`, `centroidNorm` | spectral centroid; log-position between 20 Hz and Nyquist | Hz, 0..1 |
| `flux` | `min(1, sum max(0, mag - prevMag))` | 0..1 |
| `onsetStrength`, `onset` | flux over an adaptive threshold (median of the last 11 flux values × 1.5 + 0.02); onset when ≥ 1, rising, and ≥ 60 ms since the last | ratio, event |

Properties verified by tests: chunking invariance (1-, 100-, 4096-sample pushes give
bit-identical frames), run-to-run determinism, correct band dominance for 60 Hz and 440 Hz tones,
RMS 0.707 for a full-scale sine, onsets on impulse trains and a 120 BPM click track.

Known characteristics: onsets are stamped at the window centre of the first frame that sees the
transient, so they lead the physical impulse by up to half a window (~10-20 ms) and are visually
early rather than late. Per-band running-max normalisation makes any steady tone tend towards 1
in its band, which is intended for visual use (auto-gain); use `bandsRaw` for absolute levels.

## Beat and tempo (milestone 0.3, `analysis::BeatTracker`)

Two paths share the analyzer's onset strength at hop rate:

- Live (`BeatTracker`, run by `AnalysisRunner`): an autocorrelation tempogram over a 6 s window
  with the Ellis log-Gaussian prior (centre 120 BPM), re-evaluated every 0.5 s; a phase-locked
  predictor that snaps predicted beats to nearby picked onsets. Requires at least four picked
  onsets before trusting a tempo, free-runs through breaks and forgets the tempo after roughly
  9 s without onsets. Measured: 120 BPM click track locks at 2 s, 119.97 BPM, beats within 20 ms
  (mean 15 ms early, following the analyzer's early onset stamps); a 120→150 BPM jump is followed
  within 3 s.
- Offline (`trackBeatsOffline`, run by `AnalysisTrack`): Ellis 2007 dynamic programming with a
  global tempo from the median of windowed estimates. Measured: 119.94 BPM, beats within 10 ms of
  the clicks.

Frames carry `tempoBpm`, `tempoConfidence`, `beat`, `beatPhase`, `beatCount`. Octave errors are
only mitigated by the prior; tempo changes inside an offline track get one global tempo.

## Signals (`signals::AudioSignals`)

`audio.rms`, `audio.peak`, `audio.bass`, `audio.lowMid`, `audio.mid`, `audio.highMid`,
`audio.treble`, `audio.spectralCentroid`, `audio.spectralFlux`, `audio.onsetStrength`, the event
`audio.onset` (strength = min(1, onsetStrength / 2)), and from 0.3 `audio.tempo`,
`audio.tempoConfidence`, `audio.beat` (event), `audio.beatPhase`, `audio.beatCount`. The engine
derives a per-frame beat clock from these (`beat.*`, see `docs/architecture.md` §4). No smoothing
is applied at this level; each modulation route smooths independently.

## Live input (milestone 1.1, `audio::AudioInput`)

`AudioInput` opens a miniaudio capture device (default, or the first whose name contains a
case-insensitive substring; `listCaptureDevices()` enumerates them and marks the default) at its
native rate or a requested one, and its callback downmixes every block to mono (channel average),
applies a software gain (clamped to 0..16), tracks the block peak for a level meter and writes the
`AnalysisStream` exactly as the player does, so the analyzer, signals and everything downstream see
a microphone or line input as if it were a file. There is no play-head: `framesCaptured()` is the
sample-accurate stream position (a discontinuity at frame 0 is marked on every open) and the
engine clock is the wall clock. The callback never allocates, locks or logs. macOS asks for
microphone permission on first use; a denied device opens but delivers silence.

## Lifecycle and threading

`Engine::loadAudio` decodes the file, installs it in the player (device recreated at the file's
rate) and starts a fresh `AnalysisRunner`. Play/pause/seek set atomics read by the callback; a
seek or resume pushes a discontinuity marker so the analyzer resets and re-stamps. The callback
never allocates, locks, or logs; it copies frames, applies volume, mixes to mono, writes the
stream, and advances the play-head. The analysis stream receives the unscaled mix so live
features equal offline features regardless of monitor volume.

## Performance

Measured on the M2 Max (`AnalysisRunner::averageHopMicros`): ~100 µs per hop in Debug, ~17 µs in
Release, against a 10.7 ms hop period. Whole-file decode of a 24 s stereo 48 kHz WAV: ~150 ms.
`AnalysisTrack` keeps two 1025-float vectors per frame (about 0.8 MB per second of audio); an
opt-out for the display vectors is a planned optimisation for long files.

## Testing

See `docs/testing.md`. Device tests (`[audio][device]`) skip themselves without an output or
capture device.

## Later

Beat tracking and tempo (0.3), loudness via libebur128, pffft/vDSP behind `RealFFT`, streaming
decode for very long files, duplex (monitor the live input) devices.
