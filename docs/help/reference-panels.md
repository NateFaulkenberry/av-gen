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

Waves propagating through the world (ADR-207): a camera beam that travels ahead of a directed
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

### Atmospheric

Below the propagation effects, the same panel holds the sky ones (ADR-230): **comets** that cross it
on a great-circle arc, and an **aurora** whose curtains rise from the horizon and take their shape
from the audio spectrum. They are here rather than in a panel of their own because they are the same
kind of thing with a different geometry — a source, a lifetime, an appearance and a wish to be
modulated — and they share every control that describes one.

**Add comet** makes an event: a window a sequencer can move, so it launches once. Give it a repeat
interval to make it a shower. **Add aurora** makes scenery that breathes: always on, fading up.
**Preset** sets the colours, the shape and the sparkle in one go and leaves every one of them
editable underneath.

A comet's trajectory is authored in sky coordinates — a bearing and a height to launch from, one to
fly to, and a distance — and the two bearings mean what they say, because it flies an arc at that
distance rather than a straight line through the sky. **Anchored to** decides whether it hangs in the
world, where it has real parallax and can leave frame, or on the camera, where it keeps its bearing
however far the camera travels.

**Ground glow** is off, subtle or strong: how much of the phenomenon lands on the valley below it.
It is cinematic illumination rather than lighting — a wash weighted by which way a surface faces,
plus a pool under a low comet — and it casts no shadows.

An aurora's **Audio response** controls are depths on the bands the engine already publishes, not a
second analyser: bass raises the curtain, low-mid drives the waves, mid the folds, highs the
filaments. Its overall shape can also be driven by ordinary modulation routes, and **Beat response**
writes one, exactly as it does above.

Every control here is an ordinary parameter under `atmos/<name>/`, so the Parameters panel shows the
same numbers, the timeline can key them, a sequencer event can set them and a preset can recall them.

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
