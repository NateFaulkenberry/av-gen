---
id: modulation/presets-and-macros
title: Presets and Macros
category: Modulation
summary: Snapshots of a look, and one knob that moves many things at once.
order: 36
tags: preset, macro, morph, blend, snapshot, world macro
keywords: how do i save a look; blend between two looks; what is a macro; one knob many parameters; preset morph
related: modulation/parameters, modulation/sources, sequencer/timeline-automation
features: panel.modulation, panel.world
parameters: macros/energy
---

# Presets and Macros

## Presets

A **preset** is a named snapshot of parameter **base** values. It holds values only — never routes,
never sources. Those belong to the project.

**Modulation ▸ Presets** has a name field and a **Store** button, which captures every parameter at
once. Recalling one applies every value it holds; paths it does not mention are left alone, so a
preset captured from one scene can be applied to another and will simply do less.

Two presets can be **blended**. Component by component, the result is a linear interpolation. A
parameter present in only one of the two is taken from that one at full weight once the blend
favours it, and left unchanged before that.

> [!WARNING]
> A preset recalled by a timeline cue silently outranks whatever the scene file set for the same
> parameter. AV Gen reports this on load — *"N cue preset(s) take over N value(s) the scene sets"* —
> naming the paths. If an edit in a scene file keeps being undone, this is usually why.

## Macros

A **macro** is one knob that drives many parameters.

The knob itself is a parameter, `macros/<name>`, defaulting to 0.5, and it publishes a signal
`macro.<name>`. Because the knob is a parameter, a macro can itself be modulated — route
`audio.rms → macros/energy` and the whole fan-out follows the music.

**World ▸ Macros** is where you author them. Each macro has a label, a default and a list of
targets. A target names a parameter path, a component, a `min` and `max` to map the knob's 0…1
onto, a curve, and an operation — `add` offsets the parameter's base, `replace` sets it.

Under the hood a macro expands into ordinary modulation routes, so nothing about a macro is a
special case downstream.

A **director** is a named set of world macros. **World ▸ Direction** switches between them.
