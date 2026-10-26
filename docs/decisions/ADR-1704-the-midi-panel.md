# ADR-1704: The MIDI panel: Learn, assignments, monitor and devices in one registry panel

- Status: Accepted (eng/midi-mapping, 2026-10-25)
- Code:
  - `src/ui/midi_panel.{hpp,cpp}`.
  - `src/ui/editor_layout.cpp`: the `MIDI` registry row (bottom, closed by default).
  - `src/ui/control_panel.cpp`:
    - the panel drawn;
    - the Parameters right-click **MIDI Learn...**;
    - the Control tab's MIDI learn and binding list removed.
  - `src/app/settings.{hpp,cpp}`: `live.midiDisabled`.
  - `src/app/application.cpp`: wiring.
  - `docs/help/modulation-midi-mapping.md`.
- Tests:
  - the logic the panel draws: `tests/unit/test_midi_learn.cpp`;
  - the help validator: `tests/unit/test_help.cpp`;
  - the captures in `~/Desktop/av-gen-review/94-midi-mapping/`.

## Problem

The brief asks for a first-class panel with:

- a device status view;
- clear Learn controls with collision handling;
- a searchable, sortable assignment table with a context-sensitive editor;
- a destination picker over the real parameter registry;
- a bounded live monitor.

It must use the existing docking.

## Decision

**One registry panel, `MIDI`**, docked bottom beside Modulation, because the table is wide.

**The Learn bar** is always visible above three tabs. It holds:

- a destination picker;
- the Scene / Project scope;
- **Learn**, which shows an amber pulsing **LISTENING** bar while armed. **Esc** or **Cancel** stops it;
- options: Any device, Any channel, Notes trigger, and a device filter.

On capture, it shows the source, its live value and its collisions. Then **Assign** / **Add destination** /
**Replace existing** / **Learn again** / **Cancel**. Nothing is created before a confirm.

**The tabs:**

- **Assignments**:
  - an ImGui table (sortable, clipped, rows by stable id) with status and live activity;
  - a right-click menu: enable/disable, duplicate, remove, restore default, make it a scene default;
  - Reset scene mappings;
  - an editor below that shows only the controls for the source type.
- **Monitor**: the hub's ring, newest first, with pause (a frozen copy), clear, device/channel/type filters, merge
  repeats, clock, and "Learn from selected".
- **Devices**: connection versus actual receiving, a 16-channel activity strip, counts, a per-machine on/off switch
  (settings `live.midiDisabled`), Rescan, and Listen to all inputs.

**The destination picker** is searchable. It lists control channels first (plus a new one), then every exposed and
modulatable parameter by group and sub-group (`parameterSubGroup`), with its value and range.

**Selecting the destination first:** the Parameters panel's right-click **MIDI Learn...** sets the destination, arms
Learn and raises the panel.

## Rationale

The Live panel keeps its input picker and receiving light. The MIDI panel reuses the hub's device management and does
not open devices itself.

Every edit goes through the `midi_learn` helpers, which never write a default. The editor's state is a copy, applied
to the editable mapping only on a change.

The panel only reads and edits. All behaviour lives in `control/` and is tested there.

## Consequences

- The Control tab's MIDI section shows counts and an **Open MIDI panel** button. Its OSC learn and OSC bindings stay.
- The panel's appearance is verified by captures, which are one frame each. Hover, drag and the second click are not
  covered by them.

## Revisit triggers

- Many assignments per scene make the per-frame `resolveMidiMappings` call in the table visible. It is O(n²) on
  overlap. Cache it then, keyed on an edit counter.
