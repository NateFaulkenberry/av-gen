# ADR-2125: a Logic Pro bounce's "Tempo: N" cue label is embedded tempo

- Status: Accepted (2026-11-23), avagent/neon-dreams (Neon Dreams, iteration 15; owner msg 232: "the beat pulse is
  firing twice as fast as the beat. The original BPM is 83 BPM with a 3 % varispeed -- that info should be available in
  the stem data")
- Code: `riffLogicTempoLabel` and the WAV chunk walk in `readEmbeddedTempo(path)` (`src/audio/tempo_metadata.cpp`).
- Tests: `tests/unit/test_tempo_metadata.cpp` (`[adr2125]`: the label read, the looser texts refused; the path reader
  finds a label that follows 3 MiB of audio).

## Context

The Sabertooth stems are Logic Pro 11 bounces. Each WAV carries, after its audio: `LGWV` (Logic's waveform overview),
`ResU` (zlib), a `cue ` list with two points at sample 0, a `LIST`/`adtl` naming them `Tempo: 83.0` and `Marker 1`,
and a `bext` whose originator is "Logic Pro". No `acid`, `smpl`, `iXML` or ID3 chunk; the varispeed is not written
anywhere. `BASS.mid` beside them has a 722,892 us quarter (83.000 BPM) in 4/4.

ADR-394 reads tempo only from ID3 TBPM and FLAC Vorbis comments and read only the first 1 MiB of a file, so the engine
logged "embedded tempo: none found in 'Sabertooth.wav'". It left the WAV `acid` chunk out because there was no real
file to validate a parser against. Here there are ten.

The scene's double tempo (170.95 BPM) was not the engine's: its file grid on the kick stem already read 85.47 BPM with
its bars on the true downbeats. It was the scene script's own hand fit of a half-beat grid, named a beat (nd_audio.py
it13). The engine change is for the next song whose analysis is ambiguous between octaves.

## Decision

- A WAV whose `LIST`/`adtl` holds a `labl` text beginning exactly `Tempo: ` followed by a number `parseBpmString`
  accepts is embedded tempo, provenance EmbeddedMetadata, format "Logic Pro cue label (RIFF LIST/adtl)", after a real
  ID3 TBPM. "Tempo: fast", "Old tempo: 83", "Tempo: 83 (half time)", "tempo: 83" are not tempos.
- `readEmbeddedTempo(path)` walks a WAV's chunk headers with seeks past the head probe and gathers every chunk but
  `data` (each, and the total, at most the probe size) into a compact RIFF the byte reader walks. No sample is read.
- What it buys is what ADR-394's seed buys: the beat trackers' prior is centred on the session tempo (0.15 octaves
  wide), which settles an octave. A varispeed bounce plays faster than its label (83 x 1.03 = 85.49 BPM, 0.04 octave
  off): the prior is 0.96 there, so the analysis still measures the grid; the label is not the grid.

## Consequences

- Sabertooth: "embedded tempo: ... Logic Pro cue label ... bpm=83"; the kick-stem grid stays 85.47 BPM (measured, the
  same as unseeded) and a 171 BPM candidate now carries a prior of ~0.
- A Logic session with tempo changes still writes one label (the tempo at the bounce start); the reader takes it as a
  single tempo, like any embedded BPM.
