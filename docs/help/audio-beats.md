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

AV Gen uses one of two algorithms depending on whether it can see the whole track. Both read the same
**beat strength**: how fast each band's level rises, weighted towards the bass, where a kick or a bass
note marks the beat.

**Offline** — for a loaded file — first finds the tempo through the file: an estimate every second
from the eight seconds around it, joined into one path that changes tempo only when the music holds a
new one. It then finds the beat sequence that best explains the onsets at that local tempo. So it
**follows a tempo change**, and `beat.bpm` reads the tempo where the playhead is. Where the low
register keeps time, it places the beats on it, so a syncopated hat on the "and" does not pull the grid
half a beat off.

**Live** — for a capture device — makes a first estimate after **2 seconds** of music, refines it as
its history grows to 8 seconds, and holds a lock unless another tempo wins twice running. It follows a
tempo change in a few seconds.

**Silence.** When the input falls below -60 dBFS and the next beat does not come, the live tracker
stops the beat (no `beat.pulse`, `beat.bpm` 0) and its confidence falls under 0.1 within about half a
second. It keeps the phase: if the music comes back on it within 8 seconds, the lock resumes with no
new acquisition.

Both score each candidate tempo on its whole bar of evidence (the autocorrelation at 1, 2, 3 and 4
beats), with a preference for tempi near 120 BPM, over 60 to 200 BPM. That is what stops a syncopated
pattern reading at 4:3 or a backbeat reading at half tempo.

## Tempo confidence

`audio.tempoConfidence` is **not a probability.** It measures how much the best tempo stands out from
the rest. A genuinely periodic envelope gives a high value; a noisy one gives a low one. Below a floor
of 0.15 the tempo is reported as **0, meaning unknown**, and the offline tracker returns no beats at
all. On a file it is one number for the whole file (`beat.source` 2): it cannot show lock.

The live tracker additionally needs at least four picked onsets in its window, and forgets the
tempo after a full window of sound with no rhythm in it.

## Setting the beat by hand

When the analysis gets a file wrong, set the clock yourself on the transport bar: type the **tempo**
in the bpm field, pause on a downbeat and click **Mark beat**. The marked beat is beat 1 of a bar, and
the tempo and the mark together are the file's beat clock: beats every 60 / bpm seconds through it,
`beat.source` 4. The button then shows where the mark is; click it again to mark the playhead instead,
right-click it to clear the mark. Clearing the tempo clears the mark with it.

Which clock `beat.*` follows, highest first:

| `beat.source` | Clock | When |
|---|---|---|
| 1 | MIDI clock | MIDI clock is selected and running |
| 4 | manual | a file, with a tempo and a marked beat |
| 2 | file grid | a file (the beat feed's stem, else the mix) |
| 3 | live tracker | a live input |
| 0 | none | nothing knows the beat |

The manual clock is saved in the project, with the tempo (`transport.tempo`: `bpm`, `anchor` in
seconds). It does not apply to live input, which keeps its tracker.

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
