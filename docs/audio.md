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

`AudioInput` opens a miniaudio capture device (default, an exact device id, or the first whose name contains a
case-insensitive substring; `listCaptureDevices()` enumerates them and marks the default) at its
native rate or a requested one, and its callback downmixes every block to mono (channel average),
applies a software gain (clamped to 0..16), tracks the block peak for a level meter and writes the
`AnalysisStream` exactly as the player does, so the analyzer, signals and everything downstream see
a microphone or line input as if it were a file. There is no play-head: `framesCaptured()` is the
sample-accurate stream position (a discontinuity at frame 0 is marked on every open) and the
engine clock is the wall clock. The callback never allocates, locks or logs. macOS asks for
microphone permission on first use; a denied device opens but delivers silence.

## Analysis stems (ADR-1520)

`assets.stems` lists files analysed beside the mix and never played: `{key, label, file, offset?, role?, gainDb?}`.
`Engine::setStems` decodes and analyses each changed stem on its own thread (`StemTrack::analyzeFile`: chunked
`AudioFile::decode` into a `StemAnalyzer`, no PCM kept), checks it (`audio::checkStem`, `audio::mixDiagnostics`) and
declares `stem.<key>.*` (`signals::StemSignals`). Each render frame, and each step of a seek's replay,
`StemSignals::publishTrack` publishes the newest frame at or before the instant with the events since the previous one,
inactive outside `[offset, offset + duration)`. Stems are analysed at their own rate. `stem.<key>.onset` is
log-spectral flux and `stem.<key>.level` is ADR-1510's law per source. The fixture: `tools/make_stem_fixture.py`, run
by the build into `build/<preset>/test-fixtures/stems` (or by the first test that needs it).

**Roles (ADR-1630).** `role` is what the source is: `drums`, `bass`, `synth`, `vocal` or `other` (absent: `other`;
never inferred from the key or the audio). Only `drums` runs the drum classifier (`analysis::DrumClassifier`) and has
`stem.<key>.kick/snare/hat` on the bus, always. Every other role publishes onsets, levels and the onset register --
activity, never an identity. The mix never runs the classifier: it publishes where an attack is (`response.low`,
`response.onset`, `audio.onset` and the onset register), never what made it. A live input source has the same `role` (`live.inputSources`, the
Inputs table's Role column, `--input-sources <key>@<ch>:<role>`). `role.<role>.*` is the same vocabulary for the first
ACTIVE source of each role (live inputs first while an input is open, then stems, each in list order), declared once a
source has the role, the class names on `role.drums` only; seek-replayed like the stems.

**The stem-set check (ADR-1632).** At load, and whenever the mix or a stem changes, `audio::checkStemSet` compares the
stems with the mix from a 4 kHz probe of each (box-averaged mono, kept per stem: 3.4 MB for 210 s): channels (info),
the residual of the mix fitted as a weighted sum of the stems at their offsets (info: a set may be incomplete), and
each stem's lag within +-0.5 s against what the other stems leave of the mix. A lag is a project warning naming the
offset that aligns the stem; nothing is corrected. Rate and length are `mixDiagnostics`' checks, as before.

## Live input sources (ADR-1521, ADR-1522)

`Engine::setInputSources` names channels (or linked pairs) of the live input to analyse on their own. `AudioInput`'s
callback writes each source's `AnalysisStream` beside the main mono stream, whole blocks or none, with a discontinuity
at the true frame after a drop or a device sample-time gap. `analysis::ChannelAnalysis` (one thread) runs a
`StemAnalyzer` per source and publishes through triple buffers; the engine publishes `stem.<key>.*`, inactive for an
absent channel, a disconnect, or 250 ms with no frame. With sources, macOS opens the device through
`audio::CoreAudioCapture` (AUHAL, every channel of every input stream, the HAL sample time, lifecycle listeners);
without, the plain miniaudio path above, unchanged. `audio::SyntheticCapture` (`synthetic:<n>`) is the test and
benchmark device. The map is saved per device id in settings (`live.inputSources`). `avgen --input-meter <device>`
prints each channel's level once a second.

## Feeds: the beat clock and response.* from a source (ADR-1580, ADR-1581)

By default the mix drives everything. A project's top-level `feeds` block lets a stem (offline) or a live input source
(while an input is open) drive the beat clock and parts of `response.*`. No route changes.

```json
{
  "assets": {
    "audio": "feline-footwear.wav",
    "stems": [
      {"key": "drums", "label": "Drums", "file": "stems/feline-footwear (Drums).wav", "role": "drums"},
      {"key": "bass",  "label": "Bass",  "file": "stems/feline-footwear (Bass).wav", "role": "bass", "gainDb": 6}
    ]
  },
  "sonic": {},
  "feeds": {
    "beat":    "stem.drums",
    "hits":    "role.drums",
    "levels":  "stem.bass",
    "history": "role.drums"
  }
}
```

| Key | Routes | Default |
|---|---|---|
| `beat` | the beat clock: `beat.*`, the metre (bar 1, phrases), `music.*`, the timeline's beat markers and snap, the Director's and the camera director's grid, effect triggers | `"audio"` |
| `hits` | every response hit class not named below: `response.low`, `response.onset`, their `*Env` and the onset register (`response.onsetLow/Mid/High`) | `"audio"` |
| `low` `onset` | that one class, over `hits` (`"audio"` puts one class back on the mix) | `hits` |
| `levels` | `response.bass`, `level`, `levelSlow`, `intensity`, `sustain`, `transient`, `flux` (one source: they share one loudness reference, ADR-1510) | `"audio"` |
| `history` | ADR-1631: the audio history spectrum and onset fields read (Resonator, the generative fields, THE ASTRAL FORGE's conductors): its spectrogram and its low/mid/high onset rows | `"audio"` |
| `historySpectrum` `historyOnsets` | one half of the history, over `history` | `history` |

**Values:**
- `"audio"` is the mix.
- `"stem.<key>"` is the stem with that key in `assets.stems`, or, live, the input source with that key (the same key
  works in both).
- `"role.<role>"` (ADR-1630) is the first source of that role: offline the first stem, live the first input source, in
  list order, whatever its key.
- Anything else is a load warning, and then the mix feeds everything.

**Rules:**
- **No instrument classes in the response:** the response hits are the low attack and the onset, from any role. A drum
  source's kick, snare and hat are its own `stem.<key>.kick/snare/hat` and `role.drums.kick/snare/hat`, not part of
  `response.*`; a key `kick`, `snare` or `hat` in `feeds` is not a feed (a load warning, and the mix feeds everything).
- **The history (ADR-1631):** offline, the whole-file history is built from the named stem (its folded spectrum rows,
  stretched over its own percentiles, at its offset and zero outside its span; its onsets: a drums stem's kick / snare /
  hat as the low / mid / high rows, any other stem's onsets on the row of their register), so a seek is exact as
  before. The mix's onset rows are its picked onsets (`audio.onset`), each on the row of its register (low 30-250 Hz,
  mid 250 Hz-4 kHz, high 4-16 kHz). The beat row
  stays the mix's. Live, the named input source's newest row and onsets, on the mix's clock; an inactive source gives
  silent rows. One history is uploaded either way: no GPU cost.
- **Not fed by a source:** `response.melodic`, `pitch` and `note` stay on the mix and MIDI. `audio.*` is always the
  mix's measurement, `audio.beat` and `audio.tempo` included.
- **Needs a `sonic` block:** `response.*` runs only with a `sonic` block (or live Sonic input). `beat` needs none.
- **The hits tier:** a drums source always runs it, with the drum classifier; a stem or input source of another role that
  a response feed names runs it for the feed's onsets and levels, and publishes no class name.
- **Inactive sources:** an inactive source feeds nothing: no hits, and its levels read 0. That covers a stem outside
  `[offset, offset + duration)`, and an absent, disconnected or stalled input channel. Offline the beat clock keeps the
  stem's grid, extrapolated at its tempo. Live it free-runs at the last tempo.
- **Unknown keys:** a key with no stem or input source is a project warning, and that part reads the mix.
- **Seek equals play:** the fed response and the stem's grid are pure functions of the second, like everything a seek
  replays.
- **Guidance:** take hits from a drum source and levels from a sustained one. Levels read from a drum stem pulse with
  each hit: on Feline, `response.level`'s ripple is 0.65 from the drum stem against 0.14 from the mix.

**Trims (ADR-1582):**
- `assets.stems[].gainDb` sets a stem's trim, and an input source's `gainDb` in `live.inputSources` sets a live
  source's (the Inputs table's Gain column). The range is -24 to +24 dB, applied before analysis.
- A trim lifts a quiet source over the gate and into the detectors. It does not make levels bigger.

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
