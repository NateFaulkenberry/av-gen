---
id: reference/keyboard-shortcuts
title: Keyboard Shortcuts
category: Reference
summary: Every key AV Gen actually binds today, and an honest note about the ones it does not.
order: 90
audience: expert
status: partial
tags: shortcuts, keys, keyboard, reference
keywords: keyboard shortcuts; what keys are there; hotkeys; key bindings; is there a shortcut for
related: reference/menus, audio/input, gaps/world-editor
features: subsystem.shortcuts
shortcuts: transport.play, file.open-audio, file.open-scene, file.open-environment, transport.seek-back, transport.seek-forward, edit.undo, edit.redo, edit.duplicate, edit.group, edit.copy, edit.paste, edit.select-all, editor.mode-select, editor.mode-place, editor.gizmo-move, editor.gizmo-scale, editor.toggle-space, edit.delete, edit.delete-back, edit.nudge-forward, edit.nudge-back, editor.frame-selection
---

# Keyboard Shortcuts

## General

| Keys | Does |
|---|---|
| `Space` | play or pause |
| `Left` | seek back five seconds |
| `Right` | seek forward five seconds |
| `O` | open audio |
| `S` | open a glTF scene |
| `E` | open an HDR environment |

These are global, they take no modifier, and they are suppressed whenever a text field or another
Dear ImGui widget wants the keyboard — so typing an `s` into the sequence name field does not open a
file dialog.

Two of them are shared with the world editor below, and the editor only takes them when taking them
means something: `E` opens an HDR unless something is selected, and `Left`/`Right` seek unless the
editor consumed them to nudge a selection.

None of them is configurable.

## World Editor

These arrived with the world editor (ADR-092). They apply while the Edit panel is in use; the ones
that act on a selection do nothing, and stay available to everything else, when nothing is selected.

`Cmd` below is `Ctrl` on a platform without one — the handler accepts either.

| Keys | Does |
|---|---|
| `Cmd+Z` | undo |
| `Cmd+Shift+Z`, `Cmd+Y` | redo |
| `Cmd+C`, `Cmd+V` | copy, paste |
| `Cmd+D` | duplicate in place |
| `Cmd+A` | select every object |
| `Cmd+G`, `Cmd+Shift+G` | group, ungroup |
| `Q` | Select mode |
| `B` | Place mode (the brush) |
| `W`, `E`, `R` | move, rotate, scale gizmo |
| `X` | toggle world / local axes |
| `Delete`, `Backspace` | delete the selection |
| `↑` `↓` `←` `→` | nudge by the snap step, or 0.1 m with no grid |
| `Shift` + arrows | nudge ten times as far |
| `F` | frame the selection |

`E` is also "open an HDR environment" above. The editor claims it only while something is selected,
which is when it means *rotate*; with an empty selection the key falls through and still opens a
file dialog. The arrow keys work the same way against the transport's seek.

See [The world editor](help://gaps/world-editor).

## Sequencer

**No keyboard shortcuts.** The Sequence panel has no key handling at all; every interaction is a
mouse gesture on the strip or a control in the inspector. See
[The sequencer](help://sequencer/overview) for the gestures.

## Rendering and panels

**No keyboard shortcuts.** Panels are toggled from the **View** menu.

> [!WARNING]
> **The File menu shows `Cmd+S` beside "Save Project". Nothing binds it.** There is no
> modifier-aware key handling in AV Gen, so `Cmd+S` reaches the plain `S` handler and opens the
> **Open Scene** dialog instead. Use **File ▸ Save Project**. This discrepancy is flagged by the
> documentation validator as `advertised-unbound`.

## Why this page is short

AV Gen's keyboard surface is genuinely six keys. There is no command registry, no key-binding
system and no command palette; the six are a single block of key comparisons in the application's
event handler.

When the world-editor pass lands its selection, gizmo and brush work, this page will grow and the
validator will report every new binding that is not on it.
