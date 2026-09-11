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
shortcuts: transport.play, file.open-audio, file.open-scene, file.open-environment, transport.seek-back, transport.seek-forward
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

These are the **complete** set. They are global, they take no modifier, and they are suppressed
whenever a text field or another Dear ImGui widget wants the keyboard — so typing an `s` into the
sequence name field does not open a file dialog.

None of them is configurable.

## World Editor

**No keyboard shortcuts.** The world editor is driven entirely by the mouse and by panel controls.
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
