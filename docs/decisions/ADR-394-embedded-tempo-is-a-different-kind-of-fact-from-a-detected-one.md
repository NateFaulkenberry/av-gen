# ADR-394: Embedded tempo is a different kind of fact from a detected one

## Status

Accepted (2026-09-20).

## Context / Problem

An audio file can carry the tempo its producer wrote down. AV Gen ignored it entirely and
re-derived a number it already had.

There was no metadata layer of any kind. miniaudio decodes audio; it does not parse ID3 frames,
MP4 atoms or Vorbis comments, and a grep for `ID3`, `TBPM`, `VorbisComment`, `tmpo` or `APEv2`
across `src/` and `tests/` returned nothing at all. So the question was not how to extend a layer
but whether to build one, and how far.

Two prior things constrained the answer and one of them is a trap.

**Tempo estimation already exists.** `analysis::BeatTracker` (causal, tempogram + phase-locked
predictor) and `analysis::trackBeatsOffline` (Ellis 2007 dynamic programming) both produce a
`tempoBpm`, and `AnalysisTrack::analyze` runs the offline one over every loaded file. Anything new
that also produces a number called "tempo" is the second thing with that name.

**`app::TempoSource{Analysis, MidiClock}` already exists** (`src/app/engine.hpp`), is serialized as
`control.tempoSource`, and has `tempoSourceName` / `tempoSourceFromName` beside it.

The trap is the brief's own pair of true statements: *do not spend an expensive analysis pass
rediscovering a number the file already gave you*, and *an embedded BPM gives you no beat phase,
downbeat, bars, time signature, swing or tempo change*. Both hold. Acting on the first by skipping
analysis when a tag is present destroys the beat grid that `BeatTracker`, the Auto-director, the
sequencer and every beat-driven modulation route consume — and every test this feature would
plausibly be given still passes, because none of them look at the grid.

## What the engine can actually open, measured

The first decision was which containers to write readers for, and it was settled by measurement
rather than by the dependency's documentation. A probe compiled against this build's pinned
miniaudio (0.11.25) and fed real files produced by ffmpeg:

| Container | `ma_decoder_init_file` |
|---|---|
| WAV | opens |
| AIFF | opens (`ma_dr_wav_container_aiff`) |
| FLAC | opens |
| MP3 | opens |
| M4A / MP4 (AAC) | **Invalid file** |
| Ogg Vorbis | **Invalid file** |
| Opus | **Invalid file** |

`MA_HAS_WAV`, `MA_HAS_FLAC` and `MA_HAS_MP3` are defined; `MA_HAS_VORBIS` is not, because it is
gated on `STB_VORBIS_INCLUDE_STB_VORBIS_H` and `src/audio/miniaudio_impl.cpp` does not include
stb_vorbis. The AudioToolbox references in miniaudio are all in the Core Audio *device* backend —
playback and capture — and give no decoding of Apple formats. The Ogg result was re-confirmed
against a genuine 10 KB Vorbis file after the first attempt produced no file at all.

This matters because this project has four documented cases in one session of subsystems built,
tested, measured and unreachable, every one of which passed its whole suite. An MP4 `tmpo` reader
would join them: correct, tested, and impossible to feed, because no `.m4a` can reach the importer
at all.

## Decision

**Provenance is a first-class property of a tempo, and metadata and analysis never share one.**

`audio::TempoProvenance{None, EmbeddedMetadata, Detected, ExternalClock, UserOverride}` and
`audio::AudioTempo{available, bpm, source, metadataKey, metadataFormat, confidence,
integerSemantics}` in `src/audio/tempo_metadata.hpp`.

It is called **Provenance**, not Source, because `app::TempoSource` exists and answers a different
question — which live clock drives the beat, not how a number came to be known. Two enums named
`TempoSource`, one selecting a clock and one recording an origin, would be this codebase's two
unrelated "shot" types again, which is its single most common source of wrong answers about itself.

`ExternalClock` is in the enum although the brief did not ask for it: a running MIDI clock already
drives the transport tempo, and a UI reading "Source: Embedded Metadata" while a MIDI clock
supplied the number would be a lie. Honest provenance is the whole feature.

**One extraction layer, for the four containers that open.** `readEmbeddedTempo` in
`src/audio/tempo_metadata.cpp` is the only place in the engine that parses a tag. One ID3v2 reader
serves three of the four containers:

| Container | Carrier | Field |
|---|---|---|
| MP3 | ID3v2 tag at the head | `TBPM` (`TBP` in v2.2) |
| WAV | RIFF `id3 ` / `ID3 ` chunk | `TBPM` |
| AIFF | `ID3 ` chunk | `TBPM` |
| FLAC | `VORBIS_COMMENT` block | `BPM` or `TEMPO`, either, case-insensitive |

It handles ID3v2.2/2.3/2.4, 3- vs 4-character frame IDs, synchsafe versus plain frame sizes,
extended headers, and tag- and frame-level unsynchronisation. No file is decoded and at most 1 MiB
of its head is read.

**Hand-written extraction, not TagLib.** See *Rejected alternatives*.

**Precedence, resolved once.** `Engine::tempo()` returns `UserOverride` > `ExternalClock` >
`EmbeddedMetadata` > `Detected` > `None`, and **both** the transport readout and the beat clock in
`updateTimeSignals` call it, so the number an artist reads cannot disagree with the number driving
the picture. Each arm returns immediately, which is what makes "an import never silently
overwrites a typed tempo" a structural property rather than a rule someone remembers.

**Analysis is seeded, never skipped.** `AnalysisTrack::analyze` always runs. When an embedded BPM
is present the tempogram's log-Gaussian prior is re-centred on it and narrowed from 1.0 to 0.15
octaves, using `BeatTrackerConfig::preferredBpm` and `priorWidthOctaves`, which already existed.

**A tempo map shape, not a tempo map.** `TempoEvent{timeSeconds, bpm}` and `TempoMap{events}`
exist, with `bpmAt`, `secondsPerBeatAt` and `isConstant`; a single embedded BPM is one event at
0.0. Nothing produces more than one yet.

**Only the override is persisted**, in the project's `transport` block, with its provenance token.

## Rationale

### The analysis tension, resolved by reading the code

The brief's reading — that a known BPM should seed or constrain the estimate because it is
"cheaper and more accurate than either alone" — is right about accuracy and wrong about cost, and
the code says why.

Both tempo paths take **onset strength at hop rate** as their input. `estimateTempo` is an
autocorrelation over a 6-second window of that curve; `trackBeatsOffline` is a dynamic program
over it. The expensive part is producing the curve — the STFT pass — and that pass is needed for
onset detection, spectral flux, the bands and every other audio-reactive signal the picture is
driven by, tag or no tag. The whole-track analysis costs about 130 ms for ninety seconds and the
tempo estimate is a by-product of it.

**So there is no expensive pass to skip.** Skipping "tempo analysis" would save an autocorrelation
over an envelope that has already been computed, and would cost the entire beat grid. The brief's
"do not run expensive analysis to rediscover a value already in the file" is satisfied by never
running a *second* pass — which was never proposed — not by suppressing the first.

What the tag actually buys is accuracy, and the amount is large. The estimator's characteristic
failure is the octave error: a pulse at 86 BPM and one at 172 share most of their autocorrelation
structure, and the default prior centred on 120 is what breaks the tie when nothing else can — 86
sits far closer to 120 than 172 does. On a 172 BPM onset envelope with a half-time accent:

| Prior | Estimate | Confidence |
|---|---|---|
| Default (centre 120, width 1.0 oct) | **86.0 BPM** | 0.914 |
| Seeded from `TBPM=172` (width 0.15 oct) | **171.8 BPM** | — |
| Mis-seeded from a wrong `TBPM=300` | 195.2 BPM | — |

The unseeded answer is not merely wrong, it is *confidently* wrong at 0.914 — exactly half the
true tempo. The third row is the reason the prior is narrowed rather than the value simply taken:
a tag that is wrong constrains the search but does not dictate its answer, and the audio still
wins. 0.15 octaves gives an octave error a weight of e^-22 while leaving a 5% tag error 90% of its
weight.

The division of labour that falls out is the honest one: **metadata supplies the scalar, analysis
supplies the grid, and neither substitutes for the other.** An embedded BPM outranks the analyzer
for the tempo *number* only. Beat phase, the beat count and the grid still come from analysis,
because a BPM tag does not contain them.

### Why extraction lives in `ClipSources::sync`

`Engine::installAudio` — the obvious place — receives a **mixdown**: an `AudioFile` built by
`fromInterleaved` with no path and no container. `ClipSources::sync` is the only place in the
engine that opens a real audio file on disk (`AudioFile::load` has exactly one production caller).
A tempo not captured there is gone, and `installAudio` could not recover it however carefully it
tried.

An arrangement is several files that can disagree. The embedded tempo of the arrangement is the
one belonging to the earliest-starting enabled clip that has one; disagreements are logged, not
merged, because averaging two producers' BPMs invents a third number nobody wrote.

### Why only the override is saved

A detected tempo is a measurement; re-analysis produces it again, and persisting it would make a
measurement look like a decision and then outrank a re-analysis of replaced audio. An embedded
tempo lives in the audio file, and a stale copy in the project would outlive the file being
swapped. The one thing nothing can recompute is that a person chose a number — so that is what the
project keeps, and it keeps its provenance token with it.

## Consequences

- WAV, AIFF, FLAC and MP3 imports gain an authoritative tempo when one is embedded. The two WAVs
  shipped in `assets/audio` are bare `fmt`+`data` and correctly report none.
- Beat tracking gets materially more accurate on tagged material, and unchanged on untagged.
- The transport bar's read-only `%.1f bpm` becomes a draggable tempo with a one-word origin beside
  it (`file` / `analysis` / `midi` / `set`) and format-and-field on hover; right-click clears an
  override. No new panel: a tempo is not a reusable engine concept, and a "Tempo" panel would be
  the kind of feature-specific surface this project has deleted before.
- The Sequencer's audio-clip view gains the full diagnostic line, beside the mix report.
- Missing BPM is never an error, and an unusable one (`banana`, `0`, `-128`, `10000`) is a warning
  that leaves the import succeeding.
- A second thing in the codebase is now called something tempo-ish. `app::TempoSource` and
  `audio::TempoProvenance` are deliberately different words for deliberately different questions,
  and both headers say so.

### Named gaps, deliberately left

- **MP4/M4A `tmpo`** — not written, because the engine cannot decode M4A. A test asserts both that
  no tempo is extracted from a well-formed `tmpo` atom *and* that `AudioFile::load` rejects the
  file, so the gap closes loudly if a decoder is ever added.
- **Ogg/Opus Vorbis comments** — same reason. The Vorbis comment *parser* exists and is used for
  FLAC, so this gap is a container walker, not a parser.
- **APE tags** — not written. APEv2 can be appended to an MP3, which is reachable, but it is rare
  and no fixture from a real tagger was available to validate against.
- **ID3 `SYTC`** — investigated and not implemented. It is genuinely a synchronised *tempo map*:
  MPEG-frame or millisecond timestamps paired with tempo values 0–510 BPM with an escape byte
  above that. It is also written by essentially no tool. It is recorded here because it is the
  clearest evidence that `TempoMap{events}` is the right shape — the tempo map is not
  hypothetical, it is a format that already exists — and it is the natural first producer of more
  than one event.
- **The WAV `acid` chunk** — considered and rejected, reluctantly. ACIDized WAV carries a real
  float tempo and is honoured by Ableton, Reaper and Cakewalk, so it is not a proprietary
  invention. But its layout is reverse-engineered rather than published, and no real ACIDized file
  was available to validate against. A parser whose only test is a fixture generated in the same
  layout the parser reads is a probe that cannot fail.

## Alternatives considered

**TagLib.** Complete, correct, and it would have handled every format including the exotic ID3
corners. Rejected on a specific ground rather than a general dislike of dependencies: **TagLib's
marginal value over hand-written extraction is concentrated almost entirely in the containers this
build cannot open.** MP4 atoms, Ogg and Opus streams, APE, WMA, Musepack — all of it is
unreachable here, and paying a full C++ tagging library's build, its own string and encoding
machinery, and a pinned version to maintain in order to read `TBPM` and one Vorbis comment is a
bad trade. The reachable surface is one ID3v2 reader and one Vorbis comment reader, both bounded
and both fully specified. Revisit if the engine gains an MP4 or Ogg decoder — at that point
TagLib's value stops being theoretical.

**Attaching the tempo to `AudioFile`.** Natural-looking and wrong: the mixdown is an `AudioFile`
too, and it would carry a field that can never be populated.

**A `Tempo` panel.** Rejected against the owner's standing rule that first-class UI represents
reusable engine concepts.

**Extending `app::TempoSource` with metadata values.** Rejected: it selects a live clock, and
adding origins to it would make one enum answer two questions.

**Taking the embedded BPM as the tempo and disabling the beat search.** The trap this ADR exists
to name. Rejected: it destroys the beat grid, and it does so silently.

## Revisit triggers

- miniaudio gains an MP4/AAC, Ogg Vorbis or Opus decoder, or a decoding backend is registered for
  one. The capability test will fail, on purpose.
- A second `TempoEvent` producer appears — a DAW export, a `SYTC` frame, a drawn tempo map. At
  that point `TempoMap::isConstant()` stops being universally true and every caller that reads
  `events[0]` needs auditing.
- A real ACIDized WAV or APEv2-tagged MP3 becomes available to validate against.
- Anyone proposes to skip the analysis pass on the strength of a tag. Read the measurement above
  first.
