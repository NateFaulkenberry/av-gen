---
id: modulation/parameters
title: Parameters
category: Modulation
summary: What a parameter is, what base and final mean, and how ranges and groups work.
order: 30
tags: parameters, base, final, range, group, path
keywords: what is a parameter; why does my slider do nothing; what does the A badge mean; parameter path; soft range hard range
related: modulation/overview, modulation/routes, start/how-it-works, sequencer/timeline-automation
features: panel.parameters, subsystem.modulation
parameters: audio/inputGain, post/bloom/intensity, scene/brightness
---

# Parameters

**For a beginner:** a parameter is a value you can change — a brightness, a colour, a radius — that
can also be changed over time or controlled by something else.

**For reference:** a parameter is a typed value with a unique path, a default, a hard range, a soft
range, a label and a group, presented to the rest of the application as one to four floating-point
components.

## The path

A path is slash-separated, like `post/bloom/intensity`. By convention the first segment is the
**group** the Parameters panel files it under, and the last is the **label** it shows.

Real paths, to show the shape of the space:

```
audio/inputGain
scene/brightness
post/bloom/intensity
post/tonemap/operator
camera/lens/focalLength
camera/exposure/iso
env/sky/sunIntensity
nodes/beacon-grove/position
material/bushGlow/emissionIntensity
particles/spores/spawnRate
layers/7/opacity
macros/energy
```

The groups you will meet are `audio`, `scene`, `env`, `post`, `camera`, `root`, `macros`, `shader`,
`procedural`, `field`, `spline`, `sdf`, `material`, `particles`, `nodes`, `lightrig`, `parts`,
`sources`, `layers`, `orb`. A full world typically registers over a thousand parameters; most of
them are per-node and per-material and are created when the scene loads.

## Types

| Kind | Widget |
|---|---|
| Float | slider |
| Int | integer slider |
| Bool | checkbox |
| Vec2, Vec3, Vec4 | multi-component slider |
| Color | colour picker (a vec3 or vec4 flagged as a colour) |

## Base and final

Every parameter holds two values.

**Base** is what you authored: the slider position, the value in the project file, what a preset
recalled, what a MIDI binding wrote.

**Final** is what the scene reads this frame. At the top of every frame `final` is reset to `base`;
then the timeline writes finals, then modulation routes write finals.

This is why a slider does not drift while a route is driving the parameter, and why saving a
project saves what you set rather than whatever the music happened to be doing at the moment you
pressed save.

> [!TIP]
> If a slider appears to do nothing, something is overwriting its final. The Parameters panel marks
> an automated parameter with an orange **[A]** and the tooltip *"automated by the timeline; the
> slider is the base value"*. A modulation route in `replace` mode has the same effect without a
> badge — check the Modulation panel's Routes tab.

## Ranges

A parameter has two ranges, and they do different jobs.

The **hard range** clamps. Every write to base *and* to final is clamped to it, so no combination
of automation and modulation can push a value outside it.

The **soft range** is the slider's range and nothing else. It never clamps. A parameter whose hard
range is 0 to 20 may well present a slider running 0 to 3, because that is where the useful values
are; the project file can still hold 12.

## Why a parameter might not be visible

The Parameters panel filters by **authoring layer**, selected in the World panel:

- **Beginner** shows `macros/`, `scene/`, `env/`, `post/`, `camera/`, `root/`, `shader/`
- **Intermediate** adds `procedural/`, `field/`, `spline/`, `sdf/`, `material/`, `particles/`,
  `nodes/`
- **Advanced** shows everything

This is the control that hides parameters in practice. Raise the layer to see more.

## Keying a parameter

Right-click any parameter in the panel:

| Item | Does |
|---|---|
| **Reset to default** | base back to the registered default |
| **Key at current time** | adds a linear keyframe at the play-head with the current base value |

The second creates the timeline track if there is not one already. See
[Timeline automation](help://sequencer/timeline-automation).
