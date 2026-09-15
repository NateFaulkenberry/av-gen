---
id: reference/panels
title: Panel Reference
category: Reference
summary: What each panel holds, and which topic explains it.
order: 93
audience: expert
tags: panels, reference, interface
keywords: what does this panel do; panel list; where is the parameter panel; world builder; graph editor
related: start/interface, modulation/parameters, sequencer/overview, rendering/offline-render
features: panel.world-builder, panel.assets, panel.world, panel.world-effects, panel.parameters, panel.composition, panel.sequence, panel.render, panel.control, panel.analysis, panel.modulation, panel.graph
---

# Panel Reference

Every panel is toggled from the **View** menu. The region is where the default layout docks it.

## World Builder — left

The recipe, **Generate World**, asset placement and the job monitor. Composing a world happens on a
worker thread and is installed on the main thread when it finishes.

## Assets — left

Everything the asset scan found, by kind, with a kind filter and a search box. It scans the
examples directories recursively to four levels. **Rescan** re-runs it. See
[Projects and assets](help://troubleshooting/projects-and-assets).

## World — right

Layers, the inspector, scene states, direction, macros and debug draw. It also carries the
**authoring layer** selector that decides which parameter groups the Parameters panel shows.

The world-editing half of this panel is being rewritten and is not documented yet. See
[The world editor](help://gaps/world-editor).

## World Effects — right

Waves propagating through the world (ADR-204): a camera beam that travels ahead of a directed
camera on its way to the next hero, a ripple that spreads from the hero it lands on, and whatever
else a scene declares. **Add camera beam** and **Add hero pulse** create one of each with defaults
that work; **Style** sets colour, intensity, sparkle and the wave's shape in one go and leaves every
one of them editable underneath.

**Beat response** is a slider that writes an ordinary `beat.pulse` modulation route onto the
effect's intensity. It is not a hidden audio hook: the route it makes appears in the Modulation
panel, can be curved, enveloped, re-pointed or deleted there, and is saved with the project.

**Advanced** holds the rest — what the effect is about (its source, when it activates, which way a
directional wave points), the wave's shape, the sparkle, how hard each kind of surface answers it,
and the timing. Every control in the panel is an ordinary parameter under `worldfx/<name>/`, so the
Parameters panel shows the same numbers, the timeline can key them and a preset can recall them.

Effects gated on the camera need a directed camera to gate against. With none, the panel says so
rather than leaving you to discover it as an effect that does nothing.

## Parameters — right

Every exposed parameter, grouped. Right-click for **Reset to default** and **Key at current time**.
An orange **[A]** means the timeline is writing the parameter and the slider is only its base.
See [Parameters](help://modulation/parameters).

## Composition — right

The 2D layers over the frame: text, shapes, timing and keys. Drag to reorder; right-click a layer
for **Duplicate** and **Delete**. Each animatable property has a key dot beside it. See
[Lyrics and graphics](help://sequencer/overlays).

## Sequence — bottom

The piece in time: shots, scene cuts, character cues, lyrics and markers. See
[The sequencer](help://sequencer/overview).

## Render — right

Offline render settings, progress and the queue. See
[Offline rendering](help://rendering/offline-render).

## Control — bottom

Transport, audio response and performance, plus the Shaders and Control tabs.

- Transport: play, stop, seek, volume, and the live input selector.
- Audio response: **Master gain** and one amount slider per modulation route.
- Shaders: every user shader layer, with its compile errors.
- Control: MIDI and OSC, learn, and the binding list.
- Performance: frame times, per-subsystem GPU pass times, and **Canvas scale**.

## Analysis — bottom

Bands, spectrum, onsets and the waveform. See [Audio analysis](help://audio/analysis).

## Modulation — bottom

Routes, sources, presets, shaders, scene, timeline, control and outputs. This is where routes are
created and where the keyframe timeline lives. See [Modulation](help://modulation/overview).

## Graph — bottom

The procedural graph editor. It is only usable when the current scene is a composition; otherwise
it says so. Not yet documented.

## ImGui Demo — floating

The Dear ImGui widget gallery. A development aid, not part of AV Gen.
