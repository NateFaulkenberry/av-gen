# ADR-103: Several audio files are one arrangement, mixed down to one buffer

**Status:** accepted
**Date:** 2026-09-12
**Context:** The global transport brief §8, the one acceptance criterion ADR-102 could not meet
**Supersedes nothing; extends** ADR-003 (audio decoded in memory) and ADR-102 (the transport)

## The problem

`Engine` held one `audioFile_` and played it through one `AudioPlayer`. A piece made of several
files — a stem set, a song with a spoken outro, two cues with a gap between them — could not be
expressed at all. `docs/cinematic-world-gap-analysis.md` §25 had it as a known hole, and ADR-102
recorded it as the one thing the transport work could not deliver:

> Multi-audio — **Does not exist** — `Engine` holds one `audioFile_`; `seq::Sequence` has no audio

## The decision

**An arrangement of clips is mixed down to one `AudioFile`, and the engine installs that buffer
exactly as it installs a loaded file.**

```
clips + decoded sources ──► mixArrangement ──► AudioFile ──► installAudio ──► player
                                                                           └─► analysis
```

A clip is four numbers and a gain: where it sits on the timeline, how far into the file it starts,
how long it plays, how loud — plus optional fades and an enabled flag. The same four every editor
has.

## Why a mixdown, and not a mixer

The obvious shape is a mixer in the audio callback: keep every clip decoded, walk the active ones per
buffer and sum. That is what a DAW does, and it is the wrong answer *here*, for a reason that is
about blast radius rather than taste.

Everything downstream of `AudioFile` is built on one continuous buffer: `AudioPlayer`'s real-time
callback, `AnalysisStream`, `AnalysisRunner`, `AnalysisTrack::analyze`, `WaveformSummary`, and
ADR-102's "audio is the master clock" rule. A live mixer changes **every one of them**, and several
are real-time code that is correct today and was hard to get that way.

A mixdown changes **none** of them. And it is only safe because of one property, which is tested
first in the test file and is the thing to protect:

> **A one-clip arrangement is bit-identical to the file it names.**

That is what makes it right to route `Engine::loadAudio` — the path every existing project takes —
through the mixer. There is one audio path rather than two that agree most of the time. The output
rate is the *highest* rate among the clips precisely so the common case, every file at one rate,
copies rather than resamples.

ADR-003 already decided audio lives decoded in memory. This is that decision applied once more.

## What it costs, measured

A pass over every sample whenever the arrangement is edited, and memory for the mix on top of the
sources. Measured rather than asserted: **a six-minute 48 kHz stereo arrangement of six clips mixes
in about 50 ms**, and the test that says so fails if it ever becomes quadratic.

50 ms is too long to do per drag frame, so a clip drag edits a copy and applies it on release —
exactly as the sequencer's own bake already waits for the mouse, for the same reason.

Sources are held across edits by `ClipSources`, one decode per distinct file however many clips name
it, released when nothing names it any more. Dragging a clip does not re-decode a hundred megabytes
of wav.

## What it does not do, on purpose

- **No automatic fades.** An automatic one would break the identity property, and a person cutting
  between two takes wants to choose where the crossfade is rather than discover one. Fades default
  to zero and are per clip.
- **Linear resampling, and it says so.** A clip whose rate differs from the arrangement's is
  interpolated linearly and a warning names it. Linear resampling is audible on music; the honest
  fix is to convert the file, and the engine should not quietly degrade it on every mix instead.
- **No pitch or time stretching**, and no per-clip effects. A clip is a placement, not a channel.
- **An absurd arrangement is refused.** A hand-edited start time of 1e9 seconds would otherwise make
  the allocation that follows the last thing the process did.

## Failure behaviour

A missing file is a warning, not a refusal: the rest of the piece still mixes, the clip still draws
on the strip from its own numbers, and it contributes **nothing** to the length — padding silence for
a file that is not there would make a broken project quietly *longer* than a working one.

A single-clip arrangement whose file will not load returns the failure rather than noting it, because
that is exactly the old `loadAudio` failure and the project loader already turns it into one warning
in its own words. Reporting it twice was the first thing this broke.

## The project file

One plain clip is written as `assets.audio`, a single reference, exactly as before — so every project
written before arrangements existed round-trips byte for byte, and that is tested. Anything richer is
written as `assets.audioClips`, and the two are never both present: two places naming the audio is
two places to disagree about it.

Each clip writes only what differs from the default, so a clip that was merely dropped on the
timeline reads as one line rather than eight fields of zero.

## The clips get a lane of their own

Drawn on the waveform first, and reported within the hour: a click meant to scrub the music grabbed
the clip and moved it, so the piece began fourteen seconds in and its name appeared twice — once as
the waveform lane's label and once on the clip.

The panel already had the rule, in a comment directly above the code that broke it:

> The waveform holds no blocks and is deliberately left that way: clicking a moment in the music to
> hear it is worth more than anything a block there could offer.

So clips are a thin lane of their own, under the waveform, and the waveform lane lost its file label
— the clips name themselves, and a lane label naming the song beside a clip naming the same file is
the double that was reported.

The deeper fault was that the strip's height, its drawing and its hit testing were three separate
calculations of where the lanes are, and only two of them were changed. They are now one
(`ui::StripLanes` in `ui_logic.hpp`), which is ImGui-free and tested — the waveform lane holding no
block is an assertion rather than a comment.

## Consequences

**Good.** A piece can be several files. The transport, the analysis, the waveform, the offline
renderer and the audio-reactive chain all work on it with no changes at all, because from their side
nothing happened. `referencedFiles()` reports every clip, so asset collection and relinking see them.

**Bad.** Memory: the sources plus the mix. A six-minute stereo piece is about 140 MB where it was
70 MB. A live mixer would not pay that, and the day a project is big enough for it to matter, this
ADR is the thing to revisit — the clip model would survive that change unaltered, which is the point
of writing it down.

**Watch for.** The mix runs on the UI thread. 50 ms is fine for a release on a drag and invisible on
a load; a much longer piece would want it on a job (`app::JobSystem` already exists) with the old mix
kept until the new one lands.

**Not built.** Per-clip effects, crossfade handles on the strip, packaging a multi-clip project with
`--collect` (it copies `assets.audio` only; the clip list is reported by `referencedFiles()` but not
rewritten), and a waveform drawn per clip rather than of the mixdown.
