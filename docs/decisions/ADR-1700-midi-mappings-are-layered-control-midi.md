# ADR-1700: MIDI mappings are layers of control.midi: authored defaults, scene and project mappings, tombstones

- Status: Accepted (eng/midi-mapping, 2026-10-25)
- Code:
  - `src/control/control_map.{hpp,cpp}`:
    - `MidiBinding` gains id, scope, scene, overrides, enabled, device/deviceName, numberHigh, velocity, invert,
      curve, scale, offset, smoothMs and encoder;
    - `ControlMap::midiUser`, `disabledDefaults`, `combine` and `warnings`;
    - `resolveMidiMappings`, `midiSourceValue`, `midiSourcesOverlap` and `midiDeviceMatches`.
  - `src/control/midi_mapper.{hpp,cpp}`: `MidiMapper` and `MidiTargets`.
  - `src/control/midi_learn.{hpp,cpp}`: Learn, collisions, commit, edit/duplicate/remove/restore/reset/promote,
    and `MidiMonitor`.
  - `src/app/control_hub.{hpp,cpp}`: the hub drives them. `Engine::midiSceneKey`.
- Tests: `tests/unit/test_midi_mapping.cpp`, `tests/unit/test_midi_learn.cpp`,
  `tests/integration/test_midi_mapping_engine.cpp`.

## Problem

The owner's brief asks for MIDI Learn, scene defaults and user mappings kept at scene and project scope, with a
predictable precedence.

AV Gen already had MIDI bindings (`control.midi.bindings`). They write a control channel, which routes consume, or a
parameter's base value. But there was one list. The only editor, the Control tab, wrote performer edits into the
authored list, so "the scene's defaults" and "my mappings" could not be told apart.

## Decision

**One mapping type, three layers, inside `control.midi`:**

| Key | Holds |
|---|---|
| `bindings` | the scene author's defaults. Written back exactly as read. |
| `mappings` | user mappings, each `{"id", "scope": "project" or "scene", "scene": <scene key>, "overrides"?}` plus the binding fields |
| `disabledDefaults` | ids of defaults the performer switched off (tombstones) |
| `combine` | per-destination policy for several mappings: `replace` (default), `max`, `add` |

Each key is written only when it holds something, so a project without a user layer saves byte-for-byte as before.

**Ids.** A user mapping's id (`m-<12 hex>`) is minted when it is made. A default without an authored id is named by
its content: `d-<fnv1a of kind, numbers, channel, device, target>`. It is the same on every load, so tombstones and
overrides keep finding it.

**The scene key** is the file name of the project's scene (composition or glTF), else `inline` or `orb`. The engine
recomputes it when `sceneGeneration` or `projectGeneration` changes.

**Precedence** (`resolveMidiMappings`): default < project < scene.

1. A tombstoned default is off.
2. A user mapping with `overrides: X` replaces X.
3. A lower-scope mapping whose source overlaps an active higher-scope mapping's source *and* that drives the same
   destination is shadowed. Sources overlap on kind, numbers, channel and device; "any" overlaps all.
4. Different destinations from one source all apply.
5. Mappings are applied in the order default, project, scene.

**Editing a default** creates (or reuses) a scene mapping that overrides it. The default is never written.

**Restore default** removes its tombstone and its overrides. **Reset scene mappings** removes the scene's mappings and
every tombstone.

**Make it a scene default** moves a user mapping into `bindings`. It is the author's way to ship one.

**The mapper** applies the active set. It indexes mappings by (kind, number), with an "any number" list per kind,
rebuilt on an edit or a scene change and never per message. Per mapping, the value is:

1. invert;
2. curve (linear, exponential, logarithmic, s-curve);
3. scale and offset, clamped;
4. the min..max range;
5. optional one-pole smoothing (written in `advance`, the same frame).

For the other cases:

- **Relative encoders**: the person chooses two's complement, binary offset or sign-magnitude. The mapping steps from
  the destination's current value.
- **Note gates**: a fixed set of 16 held notes, keyed by device, channel and note, over a note range.
- **Toggles and triggers**: unchanged in meaning (`note` + `toggle`, `noteEvent`).
- **ADR-1606 pickup**: kept, for the continuous kinds under `replace`.
- **Several mappings on one destination**: folded by the destination's policy, with `applyModOp`'s arithmetic:
  - `replace`: the latest message;
  - `max`: the largest current output;
  - `add`: the sum, clamped by the destination.

**Unreadable user records** are kept verbatim, reported (`ControlMap::warnings`, then the project-load warnings), never
applied, and written back on save. A bad *default* still fails the load, as before. A missing parameter or device
keeps its mapping and is shown as unresolved.

**Gates are released** when:
- a device is removed (ADR-1701);
- a mapping leaves the active set (an edit);
- the map or scene changes (a fresh rebuild).

## Rationale

The engine already applies a binding. Extending that one struct gave Learn, scopes and the response options without
a second path to the parameters. Routes stay the modulation system, and the base-write stays a hand on the slider.

Explicit ids and an `overrides` link mean an edit never mutates the shipped data. A shipped update to a default does
not erase a user's mapping. An orphaned override is just a scene mapping.

Shadowing by overlap and destination answers "the same control in a default and in my mappings" without the person
having to switch the default off by hand.

## Consequences

- Several authored bindings on one CC and one destination now coexist, as before: a scope can only shadow a *lower*
  one.
- A parameter base write is saved with the project, as a slider move is. That was already true of bindings.
- "Project" is the open project document. The live scene switcher opens one project per live scene, so a project
  mapping does not cross live scenes.
- The Control tab's MIDI learn and MIDI binding list are removed: the MIDI panel (ADR-1704) replaces them. OSC's stay.

## Rejected alternatives

- **Generating routes from `control.midi.mapping.<id>` channels for every parameter mapping**, so the routes'
  ops would combine. Rejected because a Replace route forces its target from the first frame, before any knob has
  moved. The knob would jump the scene on load, which the brief forbids. It would also change what every existing
  parameter binding does.
- **User mappings in the app settings rather than the project.** Rejected because the brief says project and scene
  mappings survive project save and load, through the existing save and unsaved-changes path.
- **Merging defaults and user mappings by display name.** Rejected because names collide and change.

## Revisit triggers

- The owner wants a rig layer that follows them across every live scene. It would be a fourth scope kept in the app
  settings, resolved below project.
- Mapping counts beyond about 10,000 make the O(n²) shadowing pass on rebuild noticeable.
