---
id: sequencer/timeline-automation
title: Keyframes and Interpolation
category: Sequencer
summary: The parameter timeline underneath everything — tracks, keys, curves and cues.
order: 44
audience: expert
tags: keyframe, interpolation, timeline, automation, curve, bezier, cue
keywords: how do i keyframe a parameter; interpolation types; ease in out; bezier tangents; automation; what is a cue
related: modulation/parameters, sequencer/overview, modulation/presets-and-macros
features: panel.modulation, subsystem.modulation
---

# Keyframes and Interpolation

The timeline is the **first modulation layer**: it runs after parameter finals are reset to their
bases and before any modulation route. Automation therefore sets the value routes add on top of,
and **tracks never write base values.**

## Tracks

A track names a target parameter path, a component (or all of them), a time base, a mode and a list
of keys.

| Mode | Result |
|---|---|
| `replace` | the track's value |
| `add` | current + the track's value |
| `multiply` | current × the track's value |

`timeBase` is `seconds` or `beats`. A `loop` length above zero wraps the track's own time, so a
four-second track with `loop` 4 repeats for as long as the piece runs.

## Interpolation

Interpolation is **per key**, not per track, and the key on the **left** of a span shapes the whole
span.

| Mode | Shape |
|---|---|
| `step` | holds the key's value until the next key |
| `linear` | straight |
| `smooth` | clamped Catmull-Rom through the neighbouring keys |
| `easeIn` | cubic, slow at the start |
| `easeOut` | cubic, slow at the end |
| `easeInOut` | cubic, slow at both |
| `bezier` | Hermite with explicit tangents, in value units per time unit |

Before the first key a track reads that key's value; after the last, the last one's — unless the
track loops.

## Adding keys

Three ways:

1. Right-click a parameter in the **Parameters** panel and choose **Key at current time**. This
   keys the current *base* value, creating the track if needed, with `linear` interpolation on a
   `seconds` time base.
2. The key dots beside each animatable property in the **Composition** panel. A lit dot means the
   property is already automated.
3. **Modulation ▸ Timeline ▸ Add key**, which also gives you the key list and a curve preview with
   draggable points.

## Unbound tracks

A track whose target names no parameter in the current scene is **kept**, not discarded, and is
skipped silently while it is unresolved. Both the Timeline tab and the Composition panel list
unbound targets in red, which is worth checking whenever automation appears to do nothing.

## Cues

A **cue** is a labelled point in time that can also recall a preset, optionally morphing into it
over a number of seconds. A cue with no preset is just a marker.

> [!WARNING]
> A cue's preset outranks whatever a scene file set for the same parameter. AV Gen reports this on
> load, naming the paths it affects.
