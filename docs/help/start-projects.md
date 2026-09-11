---
id: start/projects
title: Projects and Files
category: Getting Started
summary: What a project holds, what it does not, and every file type AV Gen can open.
order: 14
tags: project, save, load, files, assets, bundle, examples
keywords: how do i save my work; what is in a project file; where did my file go; can i move a project to another machine; what files can i open
related: start/interface, rendering/offline-render, troubleshooting/projects-and-assets
features: command.file.save-project, command.file.export-bundle
---

# Projects and Files

A **project** is a single JSON document holding everything you authored: parameter values,
modulation routes, sources, presets, the timeline, the sequence, composition layers, render
settings, MIDI and OSC bindings, and references to the files it uses.

Format `avgen-project`, currently version 4. A project written by an older version is upgraded in
memory when it is opened; the file on disk is only rewritten when you save.

## What a project saves

| Saved | Not saved |
|---|---|
| Every parameter marked serialisable, as its **base** value | Final values — they are recomputed each frame |
| Modulation routes you created | Routes a procedural graph or an entity creates for itself |
| Sources, presets, world macros, scene states | The audio you were playing at the time |
| Timeline tracks and cues you authored | Timeline tracks the sequencer baked from your shots |
| The sequence itself | Composition layers the sequencer made from overlay cues |
| Composition layers you made by hand | |
| References to the audio, scene, environment and shader files | The contents of those files |

The two "not saved" rows about baked output are deliberate. A sequence *is* a set of timeline
tracks; saving both means the next load reads the saved ones and then bakes another set, and two
tracks writing `camera/position` is not a blend — it is whichever ran last. The file holds what you
wrote, and the bake is recreated from the shots it came from.

## Asset references and relinking

Every file reference is stored as a path relative to the project, plus the file's size and a
SHA-256 of its contents.

If a referenced file has moved, AV Gen searches the project's folder for it: files with the same
name, those of matching size first, and — when a hash was stored — only a candidate whose contents
match. A successful relink is reported as a warning, naming both paths. A file that cannot be found
is reported as missing and **the rest of the project still loads**.

## Export Bundle

**File ▸ Export Bundle...** produces a self-contained folder:

1. `project.json` at the top.
2. `assets/` holding a copy of every referenced file. Name collisions between different sources are
   suffixed `_2`, `_3` and so on.
3. For a `.gltf` scene, plausible sidecars from the same directory are copied too — `.bin`, `.png`,
   `.jpg`, `.jpeg`, `.ktx2`, `.webp`.
4. Scene files are rewritten so their own references point inside the bundle, recursing through
   nested scenes.
5. A composition that had never been saved to a file is written out as `assets/composition.json`.

This is what to hand to someone else, or to archive.

## What AV Gen can open

Dropping a file on the window, or using **File ▸ Open...**, dispatches on the extension:

| Extension | What happens |
|---|---|
| `.gltf`, `.glb` | loaded as a scene |
| `.hdr` | loaded as an equirectangular environment map |
| `.json` | a `avgen-scene` document loads as a composition; anything else is tried as a project |
| `.wgsl`, `.isf` | added as a background shader layer |
| anything else | tried as audio |

The file dialogs filter as follows. Audio: `wav`, `flac`, `mp3`, `ogg`, `aif`, `aiff`. Scenes:
`glb`, `gltf`. Environments: `hdr`. Shaders: `wgsl`, `isf`. Every dialog also offers **All files**.

> [!WARNING]
> The environment loader accepts Radiance `.hdr` only. An `.exr` file is recognised by the asset
> browser as an environment but will be refused by the loader with *"is not an HDR (Radiance .hdr)
> image"*.

## Examples

**File ▸ Examples** lists the projects that ship with AV Gen, grouped by category — Showcase, Look
development, Performance, Lab and Benchmark. Hovering an entry shows its description. The four
**Glowmere Density** entries under Performance are a deliberate ladder from a light scene to a
heavy one, with their measured frame times in the descriptions; they are the quickest way to see
what your machine does with a known workload.

## Where the editor keeps its own files

In SDL's preferences directory for AV Gen:

| File | Holds |
|---|---|
| `recent.json` | the recent-projects list, up to ten entries |
| `editor-layout.ini` | the dock tree and window geometry (Dear ImGui's own format) |
| `editor-layout.json` | which panels are open |

A corrupt file in any of these is reported as a warning and ignored; the editor still starts, with
the layout it ships with.
