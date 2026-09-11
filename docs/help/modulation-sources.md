---
id: modulation/sources
title: Modulation Sources
category: Modulation
summary: LFOs, envelopes, noise, random, timeline and macro sources — signals you make yourself.
order: 34
tags: lfo, envelope, noise, random, macro, sources
keywords: how do i add an lfo; make something move without music; sine wave; random value; macro knob
related: modulation/routes, audio/signals, modulation/presets-and-macros
features: panel.modulation
---

# Modulation Sources

Not everything has to come from the music. A **source** publishes a signal of its own, which routes
then use exactly like an audio signal.

Add one in **Modulation ▸ Sources**. The kinds offered are `lfo`, `envelope`, `noise`, `random`,
`timeline` and `macro`.

## The kinds

| Kind | Publishes | Its own parameters |
|---|---|---|
| `lfo` | `lfo.<name>` (0 – 1) and `lfo.<name>.bipolar` (−1 – 1) | `rate`, `phase`, `pulseWidth`, `beatSync`, `beatsPerCycle` |
| `envelope` | `env.<name>` | `attackMs`, `decayMs`, `sustain`, `holdMs`, `releaseMs` |
| `noise` | `noise.<name>` | `rate`, `smoothness` |
| `random` | `random.<name>` | `slewMs` |
| `timeline` | `timeline.<name>` | `offset`, `scale` |
| `macro` | `macro.<knob>` | one knob per macro, `macros/<knob>` |

A source's settings are registered as ordinary parameters under `sources/<name>/`. That has a
useful consequence: **a source can be modulated by a route like anything else.** Route
`audio.rms → sources/wobble/rate` and the LFO speeds up with the music.

## LFO shapes

`sine`, `triangle`, `saw`, `square` and `sample&hold`. With `beatSync` on, the LFO's period is
`beatsPerCycle` beats rather than `rate` hertz, so it stays locked to the tempo.

Use `lfo.<name>` when you want 0 to 1 and `lfo.<name>.bipolar` when you want a value centred on
zero to add to something.

## A seventh kind you do not create

`control` is created automatically by the engine. It publishes `control.<channel>` for every MIDI
or OSC binding that targets a signal rather than a parameter. See
[MIDI and OSC](help://modulation/external-control).

## One frame of latency

A source modulating another source sees the previous frame's value, because sources are evaluated
before routes are applied. For an LFO rate this is invisible. It is worth knowing if you ever build
a feedback chain.
