# ADR-1784: no source names an instrument; the drum classifier is cut

- Status: Accepted (eng/beat-clock; owner rule ADR-441: no compat shims, no aliases)
- Supersedes: the drums-role classifier of ADR-1060 and ADR-1067 (the kick/snare/hat classes and
  `analysis::DrumClassifier`), ADR-1630's "only drums names a hit", and ADR-1726 (the real one-shot drum fixture).
- Code removed: `src/analysis/drum_classifier.{hpp,cpp}`, `tests/unit/test_drum_classifier.cpp`,
  `tools/audio_lab/make_drum_fixture.py`, `StemFrame::kick/snare/hat`, `StemAnalysisConfig::classes`,
  `ChannelAnalysis::Source::classes`, `HistoryFrom::classes`, `audio::classifiesHits`, `StemSource::hits()`,
  `InputSource::hits()`, `--input-sources <n>h[k]`.

## Decision

- `stem.<key>.kick/snare/hat` and `role.drums.kick/snare/hat` are gone, with their strengths. The drums role publishes
  what every role does: onsets, levels and the onset register. `role.<role>.*` is unchanged otherwise.
- The hits tier (the causal onsets) runs on a source a response feed names (ADR-1580), whatever its role; a drums role
  no longer runs it by itself.
- A source's audio-history onset rows are its onsets by register, whatever its role (ADR-1631).
- The Audio Response Lab drops the drums column's kick/snare/hat lamps, the class confusion and the real-drum fixture.
  The gitignored `assets/audio/drum-samples/` copies stay on disk; nothing references them.

## Why

The lab found every real closed hat also fired the snare class (ADR-1726), and a single drums bus cannot be split into
instruments reliably in real time. Scenes that need an instrument use its own stem (Aurora Borealis's Sabertooth Kick,
Snare and Toms stems are separate stems, read by key: unaffected).
