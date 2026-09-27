---
id: audio/beats
title: Beats, Tempo and Musical Structure
category: Audio
summary: How AV Gen finds the pulse, why the live and offline answers differ, and what the music events mean.
order: 22
tags: beat, tempo, bpm, structure, drop, build, section, downbeat
keywords: how do i sync to the beat; why is the bpm wrong; what is a drop event; beat phase; tempo confidence
related: audio/analysis, audio/signals, sequencer/camera-direction, modulation/recipes
features: subsystem.analysis
---

# Beats, Tempo and Musical Structure

## Two different beat trackers

AV Gen uses one of two algorithms depending on whether it can see the whole track.

**Offline** — for a loaded file — uses dynamic programming over the entire onset envelope. It picks
one global tempo, the median of overlapping six-second estimates, and then finds the beat sequence
that best explains the onsets under a penalty for irregular spacing. It is accurate and stable, and
it **cannot follow a tempo change**, because there is only one tempo for the whole track.

**Live** — for a capture device — uses a causal tracker: a rolling six-second window,
re-estimating tempo twice a second, with a phase-locked predictor that a picked onset can pull. It
follows tempo changes, needs a few seconds to lock, and free-runs through quiet passages.

Both estimate tempo by autocorrelating the onset envelope, weighted by a preference for tempi near
120 BPM, over a range of 60 to 200 BPM.

## Tempo confidence

`audio.tempoConfidence` is **not a probability.** It measures how much the best autocorrelation
peak stands out from the average. A genuinely periodic envelope gives a high value; a noisy one
gives a low one. Below a floor of 0.15 the tempo is reported as **0, meaning unknown**, and the
offline tracker returns no beats at all.

The live tracker additionally needs at least four picked onsets in its window, and forgets the
tempo after a full window with none.

## The beat clock

`audio.beat` fires on the tracker's own beat, which only moves when a new analysis frame arrives —
about 94 times a second. `beat.pulse` is the engine's clock: with a loaded file, the analysed beat
grid read at the playhead (so a seek lands on the beat a play reaches); with live input,
extrapolated smoothly between analysis frames and resynchronised whenever the tracker reports a
beat.

**For anything continuous, prefer `beat.phase` to `audio.beatPhase`**, and for a pulse at a high
frame rate prefer `beat.pulse` to `audio.beat`.

A bar is four beats. Phrases and sections are counted from bars, with the phrase length and section
length set in **Parameters → music → meter** ("bars per phrase", "phrases per section").

## Which beat is beat 1

The tracker finds beats, not bars. The analysis decides which tracked beat is **beat 1 of bar 1**
from two kinds of evidence: the backbeat (a snare or clap on beats 2 and 4) and where the
arrangement changes (layers enter and leave on bar lines). It also estimates the phrase length --
4, 8 or 16 bars -- when the changes make it clear.

Every bar in AV Gen is counted from that one answer: `beat.bar`, `beat.count`, `music.downbeat`, the
phrase and section counters, beat-synced LFOs, timeline keys in beats, a material's bar input,
effect triggers on beats, the sequencer's bars and bar snap, the Director's "bar 64 beat 3" and the
transport's bars readout.

When the estimate is wrong, pin it in **Parameters → music → meter**. Beside a setting left on
detect, the panel says what the analysis decided ("detected: beat 0 (confidence 1.00)", "detected:
8 bars"):

| Setting | Parameter | Detect | Meaning |
|---|---|---|---|
| bar 1 starts on beat | `music/meter/bar1Beat` | -1 | the tracked beat bar 1 begins on, counted from 0 at the first beat heard. If every bar line is a beat early, add 1 |
| bars per phrase | `music/meter/phraseBars` | 0 | the phrase length; the estimate picks 4, 8 or 16 when the changes make it clear, else 4 |
| phrases per section | `music/meter/sectionPhrases` | — | how many phrases make one counted section (default 4) |

They are saved in the project's `parameters` like any other parameter; a detect value is saved as
detect, so the next analysis decides again. In a project file:

```json
"parameters": { "music/meter/bar1Beat": 0, "music/meter/phraseBars": 8 }
```

An effect trigger on beats counts from the same place: `everyN` 4 and `offset` 0 fire on every
downbeat. The transport's bars readout counts from bar 1 too, so a wrong phase shows as "1.1"
landing a beat away from the first downbeat.

> [!WARNING]
> `audio.beat` is **not a kick drum**. It is the predicted beat grid. It fires with constant
> strength, it exists only once a tempo is locked, and it will happily fire through a break where
> no drum is playing. Use it for metronomic movement; use `audio.onset` to react to a drum.

## Musical events

A separate detector watches the analysis stream and publishes eleven structural events, all
momentary, all 0 to 1:

| Signal | Fires on |
|---|---|
| `music.beat` | a tracked beat |
| `music.downbeat` | beat 1 of a bar |
| `music.bar` | a bar boundary |
| `music.phrase` | a phrase boundary |
| `music.section` | a change of section |
| `music.energyRise` | energy rising past a ratio of its recent level |
| `music.energyDrop` | energy falling past a ratio |
| `music.build` | rising energy with rising brightness, sustained |
| `music.break` | energy falling below a low threshold |
| `music.drop` | a break resolving into an impact |
| `music.impact` | a large onset |

These are what to reach for when you want the *shape* of a piece rather than its texture. They are
also what the Auto-director folds a track into — see
[Directing the camera to music](help://sequencer/camera-direction).

The detector is fed one frame per *analysis* frame, never per render frame, so a 30 fps offline
render and a 120 fps editor session agree about where the drop is.

## Choosing a tempo source

The Control panel's `tempo source` combo selects `analysis` or `MIDI clock`. With MIDI clock
selected, an incoming 24-ppqn clock owns the tempo outright and the analyzer's estimate is ignored.
