# ADR-1520: Analysis stems are their own list, analysed at their own rate, and published as `stem.<key>.*`

- Status: Accepted (eng/stems-multichannel)
- Brief: `docs/prototypes/stems-multichannel/00-brief.md` §5-6. Research and plan: `02-research-report.md`,
  `03-implementation-plan.md`.
- Code:
  - `src/analysis/stem_analysis.*`: `StemAnalyzer`, `StemTrack`, the stem onset;
  - `src/audio/stem_source.*`: keys and the file and mix checks;
  - `src/signals/stem_signals.*`: the vocabulary and `publishTrack`;
  - `src/audio/audio_file.*`: `AudioFile::decode`;
  - `src/app/engine.*`: `setStems`, `assets.stems`, the replay;
  - `src/ui/sequence_panel.cpp`: Stems in the Audio clips popup;
  - `tools/make_stem_fixture.py`.
- Tests:
  - `tests/unit/test_stem_fixture.cpp`, `test_stem_analysis.cpp`, `test_stem_signals.cpp`;
  - `tests/integration/test_stems_offline.cpp`.

## Context

A project plays one stereo mix. The owner wants stems (drums, bass, synth) supplied beside it, so that the visuals can
follow each part. The stems are to be analysed, never played.

ADR-103's arrangement names "a stem set" as a use of clips, but a clip is mixed into playback, which is the one thing a
stem must not be. One `AnalysisTrack` per stem was measured at 48.7 MB per minute of frames, plus about 120 MB of
decoded PCM, per 3.5-minute file.

## Decision

**Stems are a separate list, `assets.stems`, of `{key, label, file, offset?, hits?}`.** The mix stays the only thing the
device plays; a test compares the installed playback buffer sample for sample with and without stems.

**Keys:**
- The key is the signal identity, `[a-z0-9_]+`.
- It is made from the label when the stem is added: "Synth Lead" becomes `synth_lead`, and a repeat becomes `_2`.
- It is stored and never re-derived, so relabelling moves no route.
- An invalid or duplicate key in a file is remade from the label, and the project warnings say so.

**Alignment and rate:**
- `offset` is seconds on the timeline. It is explicit only; nothing is guessed from correlation.
- Every stem is analysed at its own sample rate. Frame times are seconds and bands are Hz, so a 44.1 kHz stem beside a
  48 kHz mix needs no resampling. It gets an info note; it is not rejected.
- The existing linear resampler (ADR-103) is not used.

**Analysis:**
- `StemAnalyzer` wraps the main mix's `Analyzer`, plus ADR-1060's causal classifier on the opt-in `hits` tier, and
  keeps a 72-byte `StemFrame` per hop.
- `StemTrack::analyzeFile` analyses while decoding (`AudioFile::decode`), so no stem's PCM is ever held. Stems are
  analysed in parallel, one thread each, at load.

**Vocabulary, `stem.<key>.*`:**
- `active`, `rms`, `peak`, `level`, `onset` (event), `onsetStrength`;
- `bass`, `lowMid`, `mid`, `highMid`, `treble`, `centroid`, `width`;
- `kick`, `snare`, `hat` (events, hits tier).

**Two features are not the main mix's:**
- `onset` is **log-spectral** flux. Measured on the fixture, the Analyzer's linear flux fired every ~75 ms through a
  sustained 55 Hz bass note: the Hann window's negative-frequency image beats against a tone 2.3 bins from DC. Log
  flux stays under 0.0007 there and reads 0.007 to 0.12 on real attacks.
- `level` is read the ADR-1510 way: followed in dB, in `response.level`'s window, below the source's own adaptive
  loudness reference, using `sonic::ResponseSettings` defaults and `sonic::conditionLevel`. A fixed -60..0 dBFS scale
  was the first version and was dropped for ADR-1510's reason.

**Publishing:**
- `StemSignals::publishTrack` is a pure function of (previous instant, instant): the newest frame at or before the
  instant, with events OR-ed since the previous instant.
- It is inactive (every value 0, no event) outside `[offset, offset + duration)`, and for a stem that failed or is
  missing. It never clamps to the first or last frame the way `AnalysisTrack::at` does.

**Seek equals play:** a seek's replay publishes the stems on its engine-shaped scratch bus, and their ids are marked
samplable (the ADR-930 staging precedent). A route on a stem with a stateful chain therefore lands where a play does.
`stemsRevision_` keys the replay's checkpoints.

**Not driven by stems:** the beat clock, `music.*`, `sonic.*`, `response.*`, `AudioHistory` and the reactivity
proposer. They stay on the mix.

## Consequences

- **Measured:**
  - 3 stems of 3.5 minutes analyse in 0.46-0.70 s, and 8 in 2.3-3.6 s (2 of them with hits).
  - Holding every stem's PCM at once had raised peak RSS by about 1.2 GB for 8 stems. Analysing while decoding
    removes that (re-measured in `05-benchmarks.md`).
- A file that fails or is silent is a project warning naming it. The mix plays whatever the stems do.
- **Not implemented:**
  - automatic alignment;
  - per-stem beat tracking;
  - stem playback or soloing;
  - formats beyond miniaudio's decoders (WAV, FLAC, MP3).
