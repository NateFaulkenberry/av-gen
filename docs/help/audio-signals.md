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
| `audio.onsetMid` | 0 – 1 | yes | snare or clap: an attack in 2 – 6 kHz |
| `audio.onsetHigh` | 0 – 1 | yes | hats: an attack in 6 – 16 kHz |

With a loaded file they come from the whole-track analysis, and each carries its strength against
the strongest of its band in the surrounding second. With live input they come from the causal
snare and hat detector (ADR-1060), the same one behind `response.snare` and `hat`.

**A mix has no kick signal** (ADR-1620). On real music about half of what the mix's kick detector
called a kick was a bass note or a hat's click, so `audio.onsetLow` is gone and `response.kick`
fires only from a drums source (a stem or an input whose role is drums, through the Feeds row). On a
mix, use `beat.pulse`, or the live scenes' `signal.kick`: the drums source's kick (`role.drums.kick`)
when there is one, else the beat clock's pulse while the mix's hat or snare detector says the drums
are playing.

The centroid in Hz exists inside an analysis frame but is not published as a signal.

## Stems and input sources

One analysis source -- an offline stem file, or a channel of a live input -- publishes the same set, under its key
(`stem.drums.level`, `stem.bass_di.onset`). See [Audio Input](help://audio/input) for adding them.

| Signal | Range | Event | Meaning |
|---|---|---|---|
| `stem.<key>.active` | 0 – 1 | | 1 while the source has audio now; 0 outside a stem's span, for a missing file, an absent channel or a disconnected device |
| `stem.<key>.rms` | 0 – 1 | | level of the window, unsmoothed |
| `stem.<key>.peak` | 0 – 1 | | largest sample in the window |
| `stem.<key>.level` | 0 – 1 | | the level against this source's own loudness, as `response.level` reads the mix |
| `stem.<key>.onset` | 0 – 1 | ✓ | any attack in this source (log-spectral flux: a held bass note does not retrigger it) |
| `stem.<key>.onsetStrength` | 0 – 4 | | how far the attack cleared its threshold |
| `stem.<key>.bass` … `.treble` | 0 – 1 | | the five bands, auto-gained, as `audio.<band>` |
| `stem.<key>.centroid` | 0 – 1 | | brightness |
| `stem.<key>.width` | 0 – 2 | | stereo width of a stereo stem or a linked input pair; 0 for mono |
| `stem.<key>.kick` `.snare` `.hat` | 0 – 1 | ✓ | a **drums** source only: kicks, snares and hats found in it (not on the bus for any other role) |
| `stem.<key>.onsetLow` `.onsetMid` `.onsetHigh` | 0 – 1 | | the low, mid and high shares of the latest onset's attack |

Values change once per analysis hop (about 11 ms); a render frame shows the newest, and carries every event since the
last frame, so none is lost at 30 fps. A source with no audio reads 0 and fires nothing -- never a held value.

**By role.** `role.<role>.*` -- `role.drums.*`, `role.bass.*`, `role.synth.*`, `role.vocal.*`, `role.other.*` -- is
the same set for the first active source of that role (a live input first, then the stems), and reads 0 when none is
active. It exists once a source has the role, and only `role.drums` has `.kick`, `.snare` and `.hat`.

| Signal | Range | Event | Meaning |
|---|---|---|---|
| `signal.kick` `signal.kickEnv` | 0 – 1 | ✓ / | `role.drums.kick`, else the beat clock's pulse while the drums are playing (`tools/drum_signals.py`) |
| `signal.bassLevel` | 0 – 1 | | `role.bass.level` while a bass source is active, else `response.bass` |
| `signal.synthLevel` | 0 – 1 | | `role.synth.level` while a synth source is active, else `response.sustain` |

These three are signal sources a project adds (the generators add the ones a scene reads); they need a `sonic` block.

A project's **feeds** can make a source drive the beat clock, `response.*` (the kick from the drum stem, the levels
from the bass) and the history fields hear, without changing any route. See [Audio Input](help://audio/input).

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
