---
id: troubleshooting/projects-and-assets
title: Projects and Assets
category: Troubleshooting
summary: Warnings on load, missing files, and edits that keep being undone.
order: 82
tags: troubleshooting, project, assets, relink, missing, layout
keywords: project wont open; missing file; asset not found; my scene edits keep getting overwritten; layout is broken; panel is gone
related: start/projects, modulation/presets-and-macros, start/interface
---

# Projects and Assets

## The project opened with warnings

Load is deliberately partial: a missing asset does not stop the rest of the document.

**`relinked audio: <old> -> <new>`** — the file had moved and AV Gen found it under the project's
folder by name, size and content hash. Saving will record the new path.

**`project: scene: <message>`** — the loader's own error for that asset. The project still loaded;
that one asset did not.

**`project version N is newer than supported version M`** — the file came from a later build. There
is no forward migration; open it with the build that wrote it.

Older files are upgraded in memory when opened, one version at a time, and the upgrade is logged.
The file on disk is only rewritten when you save.

## An edit in a scene file keeps being undone

A timeline **cue's preset** silently outranks whatever the scene file set for the same parameter.
AV Gen reports this on load: *"N cue preset(s) take over N value(s) the scene sets"*, and names the
paths. Either remove the paths from the preset or stop setting them in the scene. See
[Presets and macros](help://modulation/presets-and-macros).

## Export Bundle failed

The messages are specific: *"cannot create '<dir>'"*, *"missing file '<path>'"*, *"cannot copy"*,
*"scene files nest too deeply"*, *"is not a valid scene file"*. The commonest is a referenced file
that no longer exists, which relinking could not resolve.

## A panel has vanished, or the world fills the whole window

Open it from the **View** menu.

If the world has expanded to fill the window with nothing beside it, a saved layout had every panel
in a region closed and the dock node collapsed. AV Gen detects this on the next launch, repairs it
and says so: *"a saved region was empty, so the canvas had taken its space; rebuilding the default
arrangement"*. **View ▸ Restore Default Layout** does the same thing immediately.

## The layout or recent-files list is corrupt

Reported as a warning; the editor starts anyway, arranged the way it ships. Refusing to start over
a preferences file would be a worse answer.

## An asset is not in the Assets panel

The scan recognises files by extension, and by the `format` field for JSON: `.gltf`/`.glb` as
models, `.hdr`/`.exr` as environments, `.wgsl`/`.fs`/`.frag` as shaders,
`.wav`/`.mp3`/`.flac`/`.aiff`/`.m4a` as audio, and `avgen-project`, `avgen-scene`, `avgen-graph`,
`avgen-preset` JSON documents as themselves. Anything else is skipped rather than listed.

It scans recursively to four levels, skipping anything beginning with a dot and anything named
`build`. **It scans the examples directories**, which is also what `AVGEN_EXAMPLES_DIR` points at —
so that variable controls the asset browser too. Press **Rescan** after adding files.

## Text layers do not draw

AV Gen reports this at error level, once per layer, because a render whose type quietly changed is
a render whose determinism claim is false:

- *"asked for font '<name>' and this machine has no usable face; the layer will not draw"*
- *"asked for font '<name>' and got '<other>'; this machine does not have the requested face, so
  this render will not match one made where it is installed"*
