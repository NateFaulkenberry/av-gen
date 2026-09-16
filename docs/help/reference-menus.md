---
id: reference/menus
title: Menu Reference
category: Reference
summary: Every entry in the menu bar, what it does, and when it is available.
order: 91
audience: expert
tags: menus, commands, reference, file, camera, view
keywords: what does this menu do; menu reference; file menu; camera menu; view menu; command list
related: reference/keyboard-shortcuts, start/projects, sequencer/camera-direction, start/interface
features: subsystem.commands
---

# Menu Reference

Three menus. Everything else in AV Gen is a panel control.

## File

| Entry | Shortcut | Does |
|---|---|---|
| Open Audio... | `O` | load an audio file and cut the camera to it |
| Open Scene (glTF)... | `S` | load a `.gltf` or `.glb` |
| Open Environment (HDR)... | `E` | load an equirectangular Radiance map |
| Add Background Shader... | | a user shader behind the scene |
| Add Post Shader... | | a user shader as a post effect |
| Built-in Orb Scene | | the default scene, with its routes already wired |
| Save Scene As... | | write the current scene |
| New Project | | |
| Open Project... | | |
| Examples ▸ | | the shipped projects, grouped by category; disabled when none are found |
| Open Recent ▸ | | up to ten; disabled when empty |
| Save Project | *(shows `Cmd+S`, which is not bound)* | save to the current path; disabled until the project has one |
| Save Project As... | | |
| Export Bundle... | | a self-contained folder — see [Projects and files](help://start/projects) |

## Camera

| Entry | Does | Disabled when |
|---|---|---|
| Enable Auto-director | fold the track into sections and cut the camera between the world's heroes | there is no analyzed audio |
| Disable Auto-director | remove the camera's automation | the camera is not automated |

Both are explained in [Directing the camera](help://sequencer/camera-direction). Each shows its
reason in a tooltip while disabled, rather than being hidden — a menu item that is absent looks like
a feature that does not exist, and one that fails on click looks like a bug.

## View

The top of the menu is generated from the panel registry: one checkable entry per panel, grouped by
dock region, each with its description as a tooltip. Then:

| Entry | Does |
|---|---|
| Restore Default Layout | rebuild the dock tree and reopen the panels the editor ships with |
| Save Layout Now | write the dock tree and the open-panel set immediately |

## What is not here

There is **no command palette** in AV Gen, and no command registry behind these entries — each is an
inline menu item wired to a callback. A palette would need one, and this reference is the beginning
of the inventory it would read.

There is no Edit menu, and no global undo or redo.
