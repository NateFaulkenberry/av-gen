---
id: start/how-it-works
title: How AV Gen Works
category: Getting Started
summary: The path from a waveform to a frame, first in plain terms and then in detail.
order: 10
tags: pipeline, architecture, concepts, signals, parameters
keywords: how does av gen work; what is the pipeline; how does audio become graphics; how does the music control the scene
related: audio/analysis, audio/signals, modulation/overview, modulation/parameters, rendering/overview
features: subsystem.analysis, subsystem.modulation
---

# How AV Gen Works

Every frame, AV Gen runs the same chain:

```
audio -> analysis -> signals -> modulation -> parameters -> scene -> renderer
```

## In plain terms

**Audio** is either a file you loaded or a live input device.

**Analysis** measures it. Several times a second AV Gen takes a short slice of the sound and works
out how loud it is, how much energy is in each frequency band, how bright it sounds, whether
something just hit, and what the tempo is.

**Signals** are those measurements given names and a range, so the rest of the application can use
them without knowing anything about audio. `audio.bass` is a number between 0 and 1. So is
`beat.phase`. So is the output of an LFO you added yourself.

**Modulation** is the wiring. A **route** says "take this signal, shape it, and apply it to this
parameter by this amount". You can have as many as you like.

**Parameters** are the properties of the scene — a colour, an intensity, a position, a radius.
Each frame a parameter's *final* value is recomputed from its *base* value (what you set) plus
whatever the timeline and the routes did to it.

**The scene** is the world: geometry, materials, lights, particles, the camera.

**The renderer** turns the scene into a frame on the GPU.

The important consequence: **you never connect audio to graphics directly.** You connect a signal
to a parameter. That indirection is what lets a single analysis serve a hundred different effects,
and what lets an LFO or a MIDI knob stand in for the music without anything downstream changing.

## In detail

### Each frame, in order

1. **Analysis frames are consumed.** Live playback reads whatever the analysis thread has produced
   since the last frame. An offline render consumes *every* analysis frame whose centre has passed,
   so the result does not depend on frame rate.
2. **Signals are published** to the signal bus: the `audio.*` measurements, the `beat.*` clock, the
   `music.*` structural events, and the outputs of every source you have added.
3. **Parameter finals are reset** to their base values.
4. **The timeline is applied.** Keyframed automation writes finals first, so it is the layer
   everything else sits on top of.
5. **Modulation routes are applied**, grouped by operation: every `replace` first, then `multiply`,
   then `add`, then `min` and `max`. Within a group, routes run in the order they were created.
6. **Every write is clamped** to the parameter's hard range.
7. **The scene updates** from the new parameter values, and is drawn.

### Why the operation order is what it is

`replace` establishes a value, `multiply` scales it, `add` offsets it, and `min`/`max` bound the
result. Grouping by operation rather than by creation order means two routes on the same parameter
compose predictably however you happened to add them.

### Base and final

A parameter keeps two values. **Base** is what you authored — the slider position, the value in the
project file, what a preset recalled. **Final** is what the scene reads this frame. Modulation and
automation write finals and never touch bases, which is why a slider does not drift while a route
is driving it, and why saving a project saves what you set rather than whatever the music happened
to be doing.

> [!TIP]
> If a slider seems to do nothing, the parameter is probably being written by a route or by the
> timeline. The Parameters panel marks an automated parameter with an orange **[A]**.
