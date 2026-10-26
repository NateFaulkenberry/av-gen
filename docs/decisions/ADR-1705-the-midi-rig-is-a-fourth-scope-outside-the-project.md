# ADR-1705: The MIDI rig is a fourth scope, kept outside every project: default < rig < project < scene

- Status: Accepted (eng/midi-rig, 2026-10-26)
- Code:
  - `src/control/control_map.{hpp,cpp}`:
    - `MidiScope::Rig`, between `Default` and `Project`;
    - `ControlMap::midiRig`, never written by `toJson` and never read by `fromJson`;
    - `MidiAction` and `BindingTarget::action` (the app actions);
    - `midiBindingToJson` / `midiBindingFromJson` made public, with a Rig reading mode;
    - `resolveMidiMappings` lists the rig between the defaults and the user mappings.
  - `src/control/midi_rig.{hpp,cpp}` (new): `MidiRig` (the file), `midiRigFingerprint`, `midiRigCommonParameter`,
    `promoteMidiMappingToRig`, `demoteMidiRigMappingToScene`, `overrideMidiRigMappingInScene`.
  - `src/control/midi_learn.cpp`: Learn, edit, duplicate, remove and enable reach the rig. A scene or project Learn
    that replaces a rig mapping switches it off only there.
  - `src/control/midi_mapper.{hpp,cpp}`:
    - the scope order default, rig, project, scene;
    - `MidiMappingStatus::inactiveForScene`;
    - actions fired on a rising edge through `MidiTargets::action`;
    - a message's candidates merged in order instead of sorted.
  - `src/app/control_hub.{hpp,cpp}`: `setMap` keeps the rig; `setRig`, `rigRevision`, `takeLiveSceneSteps`, and the
    transport actions performed after the frame's messages.
  - `src/app/application.{hpp,cpp}`: loads `<preferences>/midi-rig.json` at start-up, saves it when the hub's rig
    revision moves, and performs mapped live scene steps under Page Up/Down's rule.
  - `src/ui/midi_panel.{hpp,cpp}`, `src/ui/control_panel.cpp`: the Rig scope in Learn and the editor, the scope label,
    the App actions in the destination picker, the move/copy/override menu items.
- Tests: `tests/unit/test_midi_rig.cpp`, `tests/integration/test_midi_rig_engine.cpp`.

## Problem

ADR-1700 keeps a performer's mappings in the open project, at project or scene scope. The live scene switcher opens
one project per live scene (ADR-1063), so a mapping made in one live scene is gone in the next. With about 46 live
scenes, a performer would map the same controller 46 times. ADR-1700 named this as a revisit trigger.

## Decision

**A fourth scope, Rig, kept outside every project.**

- **Where it lives.** `<preferences>/midi-rig.json`, beside `settings.json`, `recent.json` and the editor layout. Its
  own file, not a key in `settings.json`, because:
  - it is a list a performer may want to copy to another machine or back up on its own;
  - an unreadable rig must not cost the person their other settings, and the reverse;
  - `AppSettings` is rewritten by many unrelated panels, and the rig is saved only when it changes.

  The file is `{"format": "avgen-midi-rig", "version": 1, "mappings": [...]}`. Each record is a `control.midi.mappings`
  record with `"scope": "rig"`, without `scene` or `overrides`. An unreadable record is handled as ADR-1700 handles a
  project's: kept verbatim, reported in the log, never applied, written back. A file that is unreadable as a whole is
  left untouched on disk for the session, and nothing saves over it.

- **In memory.** The rig rides in `ControlMap::midiRig`. The resolver, the mapper and the panel's editing helpers then
  see one list, and none of them needed a second code path. Three rules keep it out of projects:
  - `ControlMap::toJson` never writes `midiRig`, and `fromJson` never reads it;
  - `ControlHub::setMap` replaces the incoming map's rig with the hub's own, so a project load, a live scene switch, a
    new document or `--osc-port` never drops it;
  - a project record with `"scope": "rig"` is refused and set aside (kept, reported, never applied).

- **Saving.** On every mapping rebuild (an edit or a scene change, never per frame) the hub compares the rig's
  serialised form with the last one. A change bumps `rigRevision`, and the app writes the file from `serviceLiveScenes`,
  at most once a second (a slider dragged in a rig mapping's editor must not write a file every frame), and once more
  as it quits. A project load is not a change. A headless run has no rig file and never writes one.

**Precedence: default < rig < project < scene.** This is the proposal, kept. The reasons:

- The rig beats the scene's defaults because it is the performer's deliberate choice for their controller, and a
  default is the scene author's guess about a controller they have never seen.
- A project or scene mapping beats the rig because it is more specific: a performer who maps a knob differently for
  one scene means it for that scene.
- The alternative, rig above everything, was considered and rejected. Then a scene could never take a knob away from
  the rig, and "this scene needs CC 74 for its own thing" would have no answer except editing the rig, which changes
  every other scene too.

ADR-1700's rules apply unchanged, with the rig as one more rank:

1. Tombstones (`disabledDefaults`) switch off defaults only. They never reach the rig.
2. A user mapping with `overrides` replaces a lower-ranked mapping, so a project or scene mapping can override a rig
   mapping. **Override in this scene** makes such a copy: the rig mapping keeps applying in every other scene. A
   *disabled* override is how a scene switches a rig mapping off for itself.
3. Shadowing is by overlapping source and shared destination, as before. A rig mapping shadows a default; a project or
   scene mapping shadows a rig mapping.
4. Different destinations from one source all apply.
5. Applied in the order default, rig, project, scene. The higher scope writes last.

A rig mapping cannot itself override anything: the id it would name belongs to one project.

**Combine** stays a property of the destination in the open project (`control.midi.combine`). The rig has no policy of
its own: a rig mapping to a destination combines by the project's policy, `replace` unless the project says otherwise.

**Destinations that exist everywhere.**

- **Control channels.** A rig channel is declared in every scene (`ControlMap::channels` includes the rig), so it
  always exists. Whether a scene's routes read it is that scene's business.
- **Parameters common to the live scenes.** Measured by loading every live example
  (`test_midi_rig_engine.cpp`, "every live scene has the rig's common parameters"): the parameters every live scene
  has are the ones the engine registers for every project. All 30 live scenes loaded, and they share 179 exposed,
  modulatable parameters:
  - the post chain, `post/...`;
  - the temporal effects, `temporal/...`;
  - the camera's lens, exposure and focus (`camera/lens/...`, `camera/exposure/...`, `camera/focus/...`) and its orbit
    (`camera/distance`, `camera/height`, `camera/orbitSpeed`). The other camera parameters (`camera/shake/...`,
    `camera/breath/...`, the spline and mode) are missing from some live scenes;
  - the response, `sonic/response/...`;
  - `scene/brightness`, `scene/gridIntensity` and `audio/inputGain`.

  `midiRigCommonParameter` names that set, and the test fails if it and the measured set differ in either direction.
  A scene with its own Environment renderer may ignore some of them (the camera's lens, for one). They still exist, so
  a rig mapping to one is active there; it simply has no visible effect in that scene. When the destination picker is choosing for the rig,
  every other parameter is marked *this scene*.
- **App actions.** Nothing in the app was a bindable MIDI action. Program change drives the live scene switcher, and
  OSC's direct scheme drives the transport. The keyboard commands do exist, with ids in `docs/help/features.json`, so
  `BindingTarget::action` names four of them by those ids and invents none:
  - `transport.play` (Space: play/pause);
  - `transport.return-to-start` (Return);
  - `live.next-scene` and `live.previous-scene` (Page Down / Page Up).

  An action fires once per press, on a rising edge: a note-on, a program change, or a continuous value crossing
  half-way upwards. A CC button that sends 127 then 0 is one press. The hub performs the transport actions after the
  frame's messages, as the keyboard does. The live scene steps are summed and taken by the app's
  `serviceLiveScenes`, under Page Up/Down's rule (a live scene is open, or live input is running). An action is
  stored as `"action": "<id>"` and is exclusive of `signal` and `parameter`. OSC bindings refuse it, because OSC's
  direct scheme already has the transport.

**Inactive for this scene.** A rig mapping whose parameter the current scene lacks gets `inactiveForScene`. Its
precedence state is untouched (Active, Shadowed and so on). It is not indexed or applied in this scene, and it is
listed as *inactive for this scene* in a neutral colour. It is not a load warning or an error, and it is kept. A
project or scene mapping whose parameter is missing still says *not in this scene*, in the warning colour, as before:
that is a project referring to something it does not have.

**Lifecycle.**

- A live scene switch is a project load, which is `setMap`, which is a fresh rebuild. Every gate is released, rig gates
  included, and runtime state (pickup latches, toggles, relative positions) starts again, as ADR-1700 does for any map
  change.
- The rebuild runs at the start of the next `ControlHub::update`, before that frame's messages are drained. A rig
  mapping therefore applies in the frame its message arrives, the first frame of a new scene included.
- ADR-1606 pickup holds. After a switch, a pickup rig knob waits for the new scene's value.

**Editing in the panel.**

- The Learn bar and the editor's Scope offer **Scene / Project / Rig**.
- The Assignments table shows *rig* in the accent colour, and the scope filter has Rig.
- Right-click offers:
  - on a project or scene mapping, **Move to the rig**;
  - on a default, **Copy to the rig**. The default stays the scene's, and the rig copy shadows it there;
  - on a rig mapping, **Move into this scene**, **Override in this scene** and **Remove from the rig**.
- **Reset scene mappings** leaves the rig alone.
- A scene or project Learn that chooses **Replace existing** over a rig mapping switches it off only in that scope,
  with a disabled override. Only a rig Learn, or the rig mapping's own checkbox, switches it off everywhere.

**The mapper does no more per-message work.** A message's candidates are one numbered index list and one "any number"
list. Both are built in entry order, and entries are in precedence order, so the lists are now merged into the
reserved scratch buffer. Before, they were appended and sorted. There is no sort and no allocation per message.

## Consequences

- One controller setup works in every live scene and survives switching scenes, opening other projects and
  restarting the app. A project file is byte-for-byte what it would be without a rig. A test compares the two saves.
- A rig parameter mapping writes a base value, as any binding does. That value is saved with the project when the
  project is saved, as a slider move is (ADR-1700, Consequences). The rig *mapping* is never saved there.
- A scene override of a rig mapping lives in that project and names the rig mapping's id. If the rig mapping is
  removed or moved, the override becomes a plain scene mapping (ADR-1700's orphan rule).
- The panel's appearance is unverified by eye. The coordinator cannot see ImGui, and no captures were taken for this
  change.

## Rejected alternatives

- **Rig mappings inside `settings.json`.** Rejected: one file for every per-machine choice means one bad edit or one
  unreadable rig costs them all. See *Where it lives*.
- **A rig layer that bypasses `ControlMap`, resolved by the hub.** Rejected: the resolver, mapper, Learn, collisions
  and panel would each need a second input, and the precedence would live in two places.
- **Copying the rig into each project on load.** Rejected: a save would write it into the project, which the brief
  forbids, and two projects' copies would drift apart.
- **Inventing new actions (presets, states, quality).** Not done: only the keyboard's existing commands are bindable.
  More can be added to `MidiAction` when the owner asks.

## Revisit triggers

- The owner wants a rig-level combine policy, or per-device rigs (one per controller).
- A live scene gains parameters the owner wants in the rig's common set. Every live scene would have to register
  them, and `midiRigCommonParameter` would then name them. The live-set test will fail first if the set drifts.
