---
id: audio/signals
title: Signal Reference
category: Audio
summary: Every signal a modulation route can use, with its range and what publishes it.
order: 23
audience: expert
tags: signals, reference, audio, beat, music, lfo, envelope
keywords: what signals are there; list of signals; what can i modulate from; signal names; signal reference
related: audio/analysis, modulation/routes, modulation/sources
features: subsystem.modulation
---

# Signal Reference

A **signal** is a named value on the signal bus with a declared range. Signal names use dots;
parameter paths use slashes. Anything on this list can be the source of a modulation route.

Some signals are **events** — momentary pulses carrying a strength rather than a level. Route them
through the `envelope` stage to give them a shape.

## Audio analysis

| Signal | Range | Event | Meaning |
|---|---|---|---|
| `audio.rms` | 0 – 1 | | level of the window |
| `audio.peak` | 0 – 1 | | largest sample in the window |
| `audio.bass` | 0 – 1 | | 20 – 150 Hz, auto-gained |
| `audio.lowMid` | 0 – 1 | | 150 – 400 Hz, auto-gained |
| `audio.mid` | 0 – 1 | | 400 – 2000 Hz, auto-gained |
| `audio.highMid` | 0 – 1 | | 2000 – 6000 Hz, auto-gained |
| `audio.treble` | 0 – 1 | | 6000 – 16000 Hz, auto-gained |
| `audio.spectralCentroid` | 0 – 1 | | brightness, log-scaled from 20 Hz to Nyquist |
| `audio.spectralFlux` | 0 – 1 | | raw change since the last frame |
| `audio.onsetStrength` | **0 – 4** | | flux over an adaptive threshold; 1 is at threshold |
| `audio.onset` | 0 – 1 | yes | a picked onset; **carries velocity** |
| `audio.tempo` | 0 – 300 | | BPM, or 0 for unknown |
| `audio.tempoConfidence` | 0 – 1 | | peak prominence, not a probability |
| `audio.beat` | 0 – 1 | yes | the predicted beat grid; strength is always 1 |
| `audio.beatPhase` | 0 – 1 | | position within the beat, stepped at analysis rate |
| `audio.beatCount` | 0 – 100000 | | beats since the start |

The un-normalised band levels and the centroid in Hz exist inside an analysis frame but are **not**
published as signals.

## The beat clock

Smoothly interpolated at render rate and resynchronised to the analyser.

| Signal | Range | Event |
|---|---|---|
| `beat.phase` | 0 – 1 | |
| `beat.pulse` | 0 – 1 | yes |
| `beat.count` | 0 – 100000 | |
| `beat.bpm` | 0 – 300 | |
| `beat.bar` | 0 – 1 | |
| `beat.phrase` | 0 – 1 | |
| `beat.phraseCount` | | |
| `beat.phrasePulse` | 0 – 1 | yes |
| `beat.section` | 0 – 1 | |
| `beat.sectionCount` | | |

## Time and state

`time.seconds` (0 – 3600), `time.progress`, `time.playing`, `state.progress`, `state.index`
(0 – 64).

## Musical events

All events, all 0 to 1: `music.beat`, `music.downbeat`, `music.bar`, `music.phrase`,
`music.section`, `music.energyRise`, `music.energyDrop`, `music.build`, `music.break`,
`music.drop`, `music.impact`. See [Beats and structure](help://audio/beats).

## Sources you add

Each source you create in **Modulation ▸ Sources** publishes under its own name:

| Kind | Publishes |
|---|---|
| `lfo` | `lfo.<name>` (0 – 1) and `lfo.<name>.bipolar` (−1 – 1) |
| `envelope` | `env.<name>` |
| `noise` | `noise.<name>` |
| `random` | `random.<name>` |
| `timeline` | `timeline.<name>` |
| `macro` | `macro.<knob>` |
| `control` | `control.<channel>` — the MIDI and OSC channels |

See [Modulation sources](help://modulation/sources).
