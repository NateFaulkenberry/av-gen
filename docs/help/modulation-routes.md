---
id: modulation/routes
title: Routes in Detail
category: Modulation
summary: Every control on a modulation route, what it does, and what the project file can set that the panel cannot.
order: 32
audience: expert
tags: routes, curve, envelope, threshold, remap, attack, decay
keywords: what does amount do; what is bipolar; curve types; peak hold; route reference; envelope fall
related: modulation/overview, modulation/recipes, audio/signals, modulation/parameters
features: panel.modulation
---

# Routes in Detail

## Creating one

**Modulation ▸ Routes ▸ Add route**. Pick a source and a target. The target list contains every
parameter that is modulatable; a route naming a parameter that does not exist is kept but skipped,
and resolves the moment a scene providing it is loaded.

Binding fails, visibly, for: an unknown source signal, an unknown target parameter, a parameter
that is not modulatable, or a component index outside the parameter's width.

## The controls the panel exposes

Each route row is headed `source -> target`.

| Control | Range | Meaning |
|---|---|---|
| `amount` | −8 to 8 | scales the route's output; negative inverts it |
| `op` | add, multiply, replace, min, max | how it combines with the current value |
| `bipolar` | | map the source from 0…1 to −1…1 before the chain |
| `attack ms` | 0 – 2000, logarithmic | time constant for a rising value |
| `decay ms` | 0 – 5000, logarithmic | time constant for a falling value |
| `curve` | linear, power, log, exp, scurve | the shape applied to the value |
| `k` | 0.1 – 5 | how much curve |
| `envelope` | none, peak hold, linear fall | what to do with an event |
| `fall/s` | 0.1 – 20 | how fast the envelope falls, shown when envelope is not none |

Every curve maps 0 to 0 and 1 to 1, and is made odd-symmetric so that a bipolar signal keeps its
sign.

## The envelope stage

This is what turns an event into something you can see. `audio.onset` is a momentary pulse; a
parameter driven by it directly would flicker for a single frame.

- **peak hold** holds the highest recent value and lets it fall at `fall/s` per second.
- **linear fall** falls at a constant rate from wherever the event put it.

A `fall/s` of 2 means the value takes half a second to go from 1 to 0.

## What only the project file can set

The processing chain has more stages than the Routes tab shows. These can be set in a project file
and will be honoured, but there is no widget for them:

| Field | Does |
|---|---|
| `gain`, `offset` | a linear pre-scale and pre-offset, ahead of the curve |
| `clampMin`, `clampMax`, `clampEnabled` | bound the value before the threshold |
| `threshold`, `thresholdMode` | none, gate, binary, subtract |
| `envelopeHoldMs` | how long a peak is held before it begins to fall |
| `remapMin`, `remapMax` | map the final 0…1 to another range; **does not clamp** |

`threshold` with mode `gate` is what you want to gate one signal against another's level.

## Reading a route

The route row shows the route's last output, which is the fastest way to tell whether a route is
doing nothing because the signal is flat, because the amount is zero, or because something further
down the chain is eating it.

## Routes AV Gen creates for you

Some scenes ship with routes already wired. The built-in orb scene, for example, carries
`audio.bass → orb/scale`, `audio.mid → orb/rotationSpeed`, `audio.treble → orb/emissive`,
`audio.rms → scene/brightness` and `audio.onset → orb/impulse`. The engine itself adds
`audio.rms → post/bloom/intensity` and `audio.onset → post/lens/chromaticAberration`.

Routes created by a procedural graph or by an entity are **not** saved in the project, because the
graph or entity recreates them on load. Saving them would grow a duplicate on every save, and leave
a route in the file that you could not delete.
