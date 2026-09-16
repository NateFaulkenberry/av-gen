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
about 94 times a second. `beat.pulse` is the engine's clock, extrapolated smoothly between analysis
frames and resynchronised whenever the tracker reports a beat.

**For anything continuous, prefer `beat.phase` to `audio.beatPhase`**, and for a pulse at a high
frame rate prefer `beat.pulse` to `audio.beat`.

A bar is four beats. Phrases and sections are counted from bars, with the phrase length and section
length configurable in the Control panel's `phraseBars` and `sectionPhrases`.

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
| `music.downbeat` | the first beat of a bar |
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
