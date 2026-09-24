---
id: reference/panels
title: Panel Reference
category: Reference
summary: What each panel holds, and which topic explains it.
order: 93
audience: expert
tags: panels, reference, interface
keywords: what does this panel do; panel list; where is the parameter panel; world builder; graph editor; where are the effects; add effect; world effects
related: start/interface, modulation/parameters, sequencer/overview, rendering/offline-render
features: panel.world-builder, panel.assets, panel.world, panel.parameters, panel.composition, panel.sequence, panel.render, panel.control, panel.analysis, panel.modulation, panel.graph
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

### Effects

There is no separate effects panel. An effect is attached to something — the World, an object (a
hero is an object), the camera or a light — and its controls are wherever that thing is edited:

- **World → Inspector**, with **Atmosphere → environment** selected: the World's effects (an aurora,
  a comet, a fog bank, a travel beam).
- **World → Inspector**, with an object selected: that object's effects (a Ground Pulse on a hero).
- **World → Inspector**, with **Camera & lighting → camera** selected: the camera's effects.
- **Lights**, with a light selected: that light's effects.

Every one of those shows the same **Effects** section. **+ Add Effect** lists the effect types that
can be attached to that kind of thing, grouped by category; hover an entry for what it does. When no
type can be attached, the button is disabled and the section says so.

Each effect is a card, top of the stack first. The header carries its name, a status, an on/off box
and a **...** menu (**Move up**, **Move down**, **Duplicate**, **Reset parameters**, **Remove**); drag a
card's header onto another card in the same stack to reorder. The status says what happened on the
last frame: *drawn*, *dormant* (outside its activation window), *off*, and two warnings — **not
drawn**, when its render stage's GPU capacity was full and effects above it took every slot, and
**orphaned**, when the thing it is attached to is not in the scene. Orphaned effects are also listed
under the World's section, where they can be removed.

Inside a card: **Preset** sets the look in one go and leaves every value editable; the main
controls; **Ground glow** (off, subtle or strong) for the types that light the ground below them;
**Beat response**, a slider that writes an ordinary `beat.pulse` modulation route onto the effect —
visible, editable and deletable in the Modulation panel, and saved with the project; **Source** and
**Target** pickers for the types that start from, or point at, a thing in the scene. **Advanced** holds
the rest, including **Follows** (the spatial field the effect's motion answers to) and **Anchored
to** for sky effects. **Timing** holds the activation and the delay, fades, lifetime and repeat.

Every control is an ordinary parameter under `fx/<effect id>/`, so the Parameters panel shows the
same numbers, the timeline can key them, a preset can recall them and a route can drive them.
Right-click a row for **Reset to default** and **Key at current time**. Structural edits — add,
remove, move, duplicate, preset, reset, activation, source — are one undo step each, and so is each
slider gesture.

Effects gated on the camera (*while the camera travels*, *while a hero is in focus*) need a directed
camera to gate against. With none, the section says so rather than leaving you to discover it as an
effect that does nothing.

### Frame echo

With **Atmosphere → environment** selected, below the World's effects: the scene-level temporal
pass that reaches back over previous frames so a moving object leaves a trail. It is a setting of
the scene's image, not an effect attached to anything. It describes opaque surfaces only, and the
line under **Enabled** says whether its frame history is still settling after a seek.

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
