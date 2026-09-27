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
| `audio.beatCount` | 0 – 100000 | | tracked beats since the start (the tracker's own count) |

The auto-gained bands above are each divided by their own recent peak, so a passage 5 dB quieter
reads full scale again within seconds. For levels that stay where the music puts them, use:

## Levels and energy that survive a flat master

| Signal | Range | Event | Meaning |
|---|---|---|---|
| `audio.bassLevel` | 0 – 1 | | 20 – 150 Hz, **not** auto-gained: 0 = −60 dB, 1 = 0 dB (a full-scale sine), smoothed over 1 s |
| `audio.lowMidLevel` | 0 – 1 | | 150 – 400 Hz, likewise |
| `audio.midLevel` | 0 – 1 | | 400 – 2000 Hz, likewise |
| `audio.highMidLevel` | 0 – 1 | | 2000 – 6000 Hz, likewise |
| `audio.trebleLevel` | 0 – 1 | | 6000 – 16000 Hz, likewise |
| `audio.energy` | 0 – 1 | | how much is happening, independent of loudness: high-band share, brightness, flux, percussive density and stereo width, smoothed over 1 s |
| `audio.onsetRate` | 0 – 20 | | density: percussive onsets per second over a 2 s window |
| `audio.width` | 0 – 2 | | stereo width, side over mid; 0 for mono |

On these scales 6 dB is 0.1. A heavily limited master barely moves `audio.rms`, but its arrangement
moves `audio.energy` and the band levels.

## Kick, snare and hats

| Signal | Range | Event | Meaning |
|---|---|---|---|
| `audio.onsetLow` | 0 – 1 | yes | the kick: a percussive attack in 100 – 300 Hz, told apart from a bass note |
| `audio.onsetMid` | 0 – 1 | yes | snare or clap: an attack in 2 – 6 kHz |
| `audio.onsetHigh` | 0 – 1 | yes | hats: an attack in 6 – 16 kHz |

Each carries its strength against the strongest of its band in the surrounding second. They come
from the whole-track analysis of a loaded file; live input has none.

The centroid in Hz exists inside an analysis frame but is not published as a signal.

## The beat clock

With a loaded file, the analysed beat grid read at the playhead; with live input, interpolated at
render rate and resynchronised to the analyzer. Bars, phrases and sections are counted from **bar 1
beat 1**, which the analysis finds (see [Beats and structure](help://audio/beats)).

| Signal | Range | Event | Meaning |
|---|---|---|---|
| `beat.phase` | 0 – 1 | | position within the beat |
| `beat.pulse` | 0 – 1 | yes | each beat |
| `beat.count` | | | the beat number: 0 on bar 1 beat 1, negative in a pickup |
| `beat.bpm` | 0 – 300 | | |
| `beat.bar` | 0 – 1 | | position within the bar; 0 on the downbeat |
| `beat.phrase` | 0 – 1 | | position within the phrase |
| `beat.phraseCount` | | | phrases since bar 1 |
| `beat.phrasePulse` | 0 – 1 | yes | each phrase boundary |
| `beat.section` | 0 – 1 | | position within the counted section |
| `beat.sectionCount` | | | counted sections since bar 1 |

## The section timeline

The sections of the Sequence panel's section timeline, as authored.

| Signal | Range | Event | Meaning |
|---|---|---|---|
| `section.index` | −1 – | | which section the playhead is in, from 0; −1 when there is none |
| `section.progress` | 0 – 1 | | how far through it |
| `section.energy` | 0 – 1 | | the energy the section carries |
| `section.change` | 0 – 1 | yes | the playhead crossed into another section |

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
