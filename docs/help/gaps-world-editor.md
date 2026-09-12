---
id: gaps/world-editor
title: The World Editor
category: Not Yet Documented
summary: Selection, gizmos, the brush, grouping and undo are being rewritten; this area is deliberately not documented yet.
order: 200
status: not-yet-documented
tags: world editor, selection, gizmo, brush, undo, placement, transform
keywords: how do i select an object; how do i move an object; gizmo; duplicate an object; undo; place an asset; group objects
related: reference/keyboard-shortcuts, reference/panels, start/interface
---

# The World Editor

**This area is not documented yet, and this page will not guess.**

Selection, the transform gizmos, the placement brush, grouping and undo/redo are being rewritten
right now. Documenting them from the current code would produce instructions that are wrong the
week they ship, which is the one thing this Help system is not allowed to do.

## What is missing

- Camera navigation: orbit, pan, zoom, fly, focus, frame selected, speed and modifiers
- Selection: single, multiple, box, by type, hierarchy
- Transform: translate, rotate and scale gizmos; local versus world; snapping; numeric entry
- Object manipulation: create, duplicate, delete, parent, hide, isolate
- Grouping
- Undo and redo
- Asset placement and the brush
- The inspector's editing behaviour

## What is safe to say today

- There are **no keyboard shortcuts** in the world editor. See
  [Keyboard shortcuts](help://reference/keyboard-shortcuts).
- Undo, redo, cut, copy, paste, duplicate, delete and select all **are** wired up, application-wide:
  the Edit menu, the keyboard and the Edit panel's buttons all go through one history (ADR-101), and
  the panel's history list is clickable. The wording of each item and what it does in each panel is
  part of what this page does not describe yet.
- Objects can be **hidden** (undoable, saved) and **locked** out of the pointer (not undoable, saved)
  from the Edit panel's object list.
- Clicking in the viewport picks an object, and the status bar names the selection.
- A drag that begins on the canvas keeps the mouse until the button is released, so a gesture is not
  stolen halfway through by a panel the cursor passed over.
- Asset placement failures report themselves: *"place: no asset library is loaded"*,
  *"place: '<name>' is not in the library"*, *"place: '<name>' has no file"*.

## When this will be written

When the world-authoring pass lands. The documentation validator reports this page as a known gap
on every run, and will report every new panel, command and key binding that pass introduces, so the
gap cannot be forgotten.
