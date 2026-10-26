# MIDI Performance Mapping: audit and plan

Branch `eng/midi-mapping` from main 52d7bd5f. One agent, by the owner's standing rule: the brief's two parallel
investigations were run in sequence by this agent (first MIDI and parameters, then UI and persistence). ADR range
1700-1719.

## 1. What exists (audit)

### 1.1 MIDI input

| Question | Answer in the code |
|---|---|
| Backend | CoreMIDI (`src/control/midi_coremidi.cpp`), one process-wide `MIDIClientRef` on a dedicated run-loop thread; other platforms are a stub reporting no devices (`midi_stub.cpp`). No third-party MIDI library. |
| Enumerate | `listMidiInputs()` returns name, id (`kMIDIPropertyUniqueID`, else the name) and a virtual flag. The Live panel rescans it every 3 s. `--list-midi` prints it. |
| Open / select | `MidiInput::open(filter)`: every source whose name contains `filter` (case-insensitive), or all with `*`. With `*`, sources that appear later connect on arrival (`kMIDIMsgObjectAdded`). The filter is `control.midi.filter` in the project, overridden by the machine's `live.midiInput` (settings) when live input turns on. The Live panel's combo picks "All MIDI inputs" or one device by writing that filter. |
| Several devices at once | Yes: one input port, many sources connected to it. |
| Disconnect | `kMIDIMsgObjectRemoved` disconnects the source. Nothing downstream is told, so a held note stays held. |
| Reconnect | Re-added sources reconnect with the wildcard filter. A non-wildcard filter does not reconnect. |
| Duplicate names | Not handled: a message carries only `std::string source` (the endpoint name), so two devices with the same name cannot be told apart. The UniqueID is read for `listMidiInputs` and then thrown away. |
| Message path | CoreMIDI receive thread → UMP-to-bytes → `parseMidiBytes` → `MidiInbox::push` (mutex, `std::deque`, limit 4096; **beyond it the OLDEST is evicted**, which can drop a note-off and leave a gate stuck) → `ControlHub::update` drains on the engine thread once a frame. |
| Timestamps | Host time in ns per packet (`timestampNs`); used only by the MIDI clock tracker. |
| Allocation per event | `MidiMessage` holds a `std::string` (heap for names over the SSO limit); the deque allocates blocks; `inject` builds a vector per call. |
| Types parsed | NoteOn/Off (velocity 0 → NoteOff), PolyPressure, ControlChange, ProgramChange, ChannelPressure, PitchBend (14-bit), Clock/Start/Continue/Stop. Sysex is dropped; system common and active sensing arrive as `Other`. |
| Types bindable (`MidiBindKind`) | `cc`, `note` (velocity gate or toggle), `noteEvent` (pulse), `pitchBend`, `pressure` (channel), `program`. **No poly pressure, no NRPN/RPN** (CC 98-101/6/38 arrive as plain CCs; nothing decodes them). |
| Defect found | `matchMidi` reads channel pressure from `data1`, but the parser stores it in `data2`. **Every `pressure` binding has always produced 0**: a silent no-op. Fixed in this branch (ADR-1703). |
| Clock / transport | `ControlHub::applyMidi` feeds Clock/Start/Continue/Stop to `MidiClockTracker` before anything else; they are never bound, learned or counted unmatched. With `tempoSource: midi` the clock owns `beat.*`. Left as is. |
| Other consumers | Live Sonic input (ADR-1025, `Engine::liveSonicMidi`) sees every message first and publishes `notes.*`; program change goes to the live scene switcher (ADR-1063). ADR-1667 loads a MIDI *file* into `notes.*` (no live input involved). All left as is. |

### 1.2 Parameters and modulation

- **Registry:** `params::ParameterSet` (`find(path)`, `ordered()`). Each `IParameter` has a stable path (`orb/scale`,
  `grid/prop/waveSpeed`, `macros/sensitivity`), label, group (first segment), kind (float/int/bool/vec/colour),
  hard and soft ranges, default, and flags `exposed / modulatable / serialized`. `ui::parameterSubGroup` already
  derives the middle of a path as a heading (the Parameters panel uses it).
- **Two ways MIDI reaches a parameter today** (`ControlHub::applyTarget`):
  1. a **control channel**: `ControlSource::set/pulse("<name>")` publishes `control.<name>` on the signal bus;
     ordinary **routes** (`params::ModRoute`: source → chain → amount → op Add/Multiply/Replace/Min/Max → depth →
     parameter) consume it. This is the modulation system;
  2. a **parameter base write**: `setBaseComponent(min + (max-min)·v)`, a hand on the slider. Routes then modulate
     on top of that base.
- **Rests and pickup:** ADR-1612 declares a channel's rest (`control.channels`), and ADR-1606 adds soft takeover per
  binding or for all (`pickup`), against the parameter's base or the channel's value.
- **Frame order** (`Engine::update`): `controlHub_.update` (drain, apply) → `sources_.update` (publishes
  `control.*`) → states → `resetFinals` → timeline → fields → `modulator_.applyRoutes` → palette → scene. So a
  message drained this frame is in this frame's parameter evaluation. **There is no one-frame delay to remove**;
  the existing integration test already injects and reads the result after one `update`.
- **Stale pointers:** the hub resolves a parameter with `params().find(path)` per write, never a cached pointer, so
  a scene load (which clears the set) cannot leave a dangling reference. Kept.
- **Pickers:** the Control tab's combo lists every modulatable path flat. The Parameters panel's right-click menu
  (Reset to default, Key at current time) is the natural place for "MIDI Learn".

### 1.3 Persistence and UI

- **A project is one JSON document** (`docs/help/start-projects.md`). It references its **scene** (`assets.scene`:
  a composition file, a glTF, `inline` or `orb`). Swapping the scene keeps the project. Every example the live
  scene switcher steps through is itself a project file.
- **Scene defaults today** are the project's authored `control.midi.bindings` (30 examples ship them; 28 use
  `source: "*"`, channel any).
- **No user layer exists.** The only editor for bindings is the Control tab's list, which edits the authored
  bindings in place, so a performer's change *is* a change to the shipped defaults.
- **Save / dirty:** `Engine::saveProject` writes `projectDocument()`. "Unsaved changes" compares serialisations
  (ADR-440), so anything `ControlMap::toJson` writes is covered by the existing prompt and title marker.
- **Machine settings** (`app::AppSettings`, settings.json) already hold the rig: the live audio input, its
  per-device channel maps, and `live.midiInput`.
- **Export:** File ▸ Export Bundle copies a project with its assets; there is no per-user configuration export.
- **Panels:** `ui::EditorLayout` registry (`kPanels`, region, default open), drawn by `ControlPanel::draw` through one
  `panel(id, size, body)` helper; sub-panels are classes (`CompositionPanel`, `SettingsPanel`). Captures:
  `--capture-ui`, `--capture-ui-panel`, `--capture-ui-frame`; scripted interaction: `--ui-script`.
- **Tests:** Catch2. `avgen_tests` (unit + integration, CPU) and `avgen_render_tests` (GPU).
  `tests/unit/test_midi.cpp` already sends through a real CoreMIDI virtual source.

### 1.4 Hardware on this machine

`--list-midi`: `SE49 MIDI1`, `SE49 MIDI2` (a Nektar SE49 keyboard, physically connected but with nobody to play it)
and `Logic Pro Virtual Out`. Everything automated goes through a CoreMIDI virtual source created by the tests.

## 2. Mapping data model

**Extend `control.midi`; no second system.** A mapping is a `MidiBinding`, the struct the engine already applies.
It gains:

| Field | Meaning |
|---|---|
| `id` | stable mapping id. User mappings get a random 64-bit hex id when created. A default with no authored `id` gets `d-<hash of its source and target>`, stable while the author does not change that binding. |
| `enabled` | off = kept, shown, not applied |
| `device`, `deviceName` | stable device id (CoreMIDI UniqueID) and its name at learn time. Empty = the existing `source` substring (`*` = any). |
| `numberHigh` | note range upper bound (`note`, `noteEvent`, `polyPressure`) |
| `velocity` | note gate/trigger level follows velocity (default true, as now) or is full |
| `invert`, `curve`, `scale`, `offset`, `smoothMs` | the response (curve: linear, exponential, logarithmic, s-curve) |
| `encoder` | `cc` only: absolute (default) or relative: two's complement, binary offset, sign-magnitude. Chosen by the user, never inferred. |
| new kinds | `polyPressure` (note identity kept), `nrpn`, `rpn` (14-bit parameter number, 7- or 14-bit value) |

Behaviours stay as the existing kinds express them: **gate** = `note`, **toggle** = `note` + `toggle`, **trigger** =
`noteEvent` (with or without velocity). A destination is the existing `BindingTarget`: a control channel, or a
parameter path + component + min/max.

`control.midi` in a project:

```json
"midi": {
  "enabled": true, "filter": "*",
  "bindings": [ ... ],              // the scene's authored defaults (unchanged schema, new fields optional)
  "mappings": [ {"id": "…", "scope": "project" | "scene", "scene": "<scene key>", "overrides": "<id>", ...} ],
  "disabledDefaults": ["d-…"],      // tombstones
  "combine": {"control.glow": "max"}// per-destination combination, written only when not "replace"
}
```

**Scene key:** the file name of the project's scene (`aurora-borealis.scene.json`, `tree.glb`), else `inline` or
`orb`. File name, not path, so it survives a move to another computer.

**Combination** (one destination, several mappings): `replace` (default and current behaviour: the latest message
wins), `max`, `add` (sum, clamped to the destination's range). Folded with `params::applyModOp`, the route
combiner. Event pulses on one channel already take the strongest (`ControlSource::pulse`).

## 3. Precedence (exact rules)

Scopes: **default** (authored `bindings`) < **project** < **scene** (only while that scene is the project's scene).

1. A default listed in `disabledDefaults` is off.
2. A user mapping with `overrides: X` replaces mapping X while it exists. Editing a default creates exactly that:
   a scene mapping copied from it with `overrides` set. The default is never written by the panel.
3. A lower-scope mapping whose source **overlaps** a higher-scope active mapping's source (same kind, overlapping
   numbers, channel and device; any overlaps all) **and** that drives the same destination is **shadowed** (shown,
   not applied). So one physical control never double-drives one destination by accident.
4. Different destinations from one source, in any scopes, all apply: that is one source to many destinations.
5. Several sources on one destination combine by that destination's policy (§2).
6. Mappings are applied in the order default → project → scene, so on a tie the higher scope writes last.
7. **Restore default** removes its tombstone and any override of it. **Reset scene mappings** removes this scene's
   user mappings and the tombstones.

## 4. Persistence

- Through the project document, so the existing Save, Save As and unsaved-changes prompt cover it. Nothing is
  written on a MIDI event.
- Defaults are written back exactly as loaded. The panel only adds to `mappings`, `disabledDefaults`, `combine`.
- Serialisation is in list order with only non-default fields: deterministic.
- A missing device keeps the mapping (status "device not connected"; if a connected device has the saved
  `deviceName` it is used and the status says "matched by name"). A missing parameter keeps the mapping (status
  "destination not in this scene"). An unreadable user record is kept verbatim, reported and re-written on save,
  never deleted. A bad *default* still fails the load as today (an author's error stays loud).
- **Portability:** a mapping with `device` empty is device-independent; the schema already allows a future preset
  file to be a bare `mappings` array bound to a device on the other machine.
- **Owner decision flagged, not blocking:** "project scope" here is the open project document. A performer who
  steps through live scenes is opening a different project each time, so a layout that should follow them across
  all live scenes would need a machine-level (settings) scope. Not built; easy to add on the same model.

## 5. Threading and real-time

- The audio callback is not involved: MIDI never touches it.
- **Inbox:** a fixed ring of POD messages (no strings; the device is a 16-bit slot into a device table), allocated
  once, under a short mutex on the CoreMIDI thread. **Full policy:** continuous messages (CC, pitch bend, pressure)
  stop queueing at 3/4 full and go to a 64-entry coalescing table keyed by device/channel/kind/number (latest value
  wins), so a stall keeps every control's final position; notes, program and transport use the remaining quarter.
  If a note message is ever dropped the hub releases every gate (no stuck notes) and counts it.
- **Device table:** slots are never reused; the same UniqueID reconnecting gets its old slot back, so mappings
  restore. Removal enqueues a `DeviceRemoved` marker in order, and the hub releases that device's held notes.
- **Engine thread:** decode (NRPN/RPN), monitor record, learn, mapping. Mappings are found through an index rebuilt
  only when the mapping set or the scene changes: a per-(kind, number) table, plus a short list for "any number".
  Per-mapping runtime state (held notes as a fixed array, pickup, toggle, smoothing, relative position) lives in a
  parallel vector, no allocation per event.
- The UI runs on the same thread between frames, so a panel edit is visible from the next frame; it marks the index
  for rebuild.
- Scene change or a mapping disabled: held gates released first, smoothing and pickup reset; nothing writes until
  MIDI arrives, so a switch causes no jump.

## 6. Panel

A dockable **MIDI** panel (bottom region, beside Modulation; View ▸ MIDI; closed by default):

- **Learn bar** (always visible): destination + scope + "Learn" (amber "LISTENING" while armed, Cancel / Esc). On
  capture: device, channel, type, number, live value, whether the source is already assigned and to what; then
  **Assign** / **Add destination** / **Replace existing** / **Cancel**. Nothing is created before Assign.
- **Assignments** tab: searchable, sortable table (enabled, source device, channel, type, number, destination,
  range/behaviour, scope, status, live activity), stable-id selection; Edit, Duplicate, Enable, Remove, Restore
  default, Make scene default; and a context-sensitive editor below for the selected mapping.
- **Monitor** tab: bounded live stream (time, device, channel, type, number, raw, normalised, meaning), pause,
  clear, device/channel/type filters, coalescing of repeated continuous updates, activity light, select a row to
  learn from it.
- **Devices** tab: every input with connected / enabled / receiving, per-channel activity, enable toggles, rescan.
- **Destination picker:** searchable tree from the parameter registry (exposed + modulatable), grouped by group and
  sub-group, then control channels and macros; shows current value and range.
- Parameters panel right-click gains **MIDI Learn…** (select destination first).
- The Control tab's MIDI learn and binding list are removed (its OSC half stays).

## 7. Sequence

1. Input layer: POD messages, device slots, ring inbox and its full policy, removal marker, pressure fix.
2. NRPN/RPN decoder; mapping model, JSON, scope resolution.
3. Mapper (index, transforms, gates, combination) driven by the hub; Learn and Monitor state machines.
4. Panel, Parameters right-click, Control-tab cleanup, settings for device enable.
5. Tests per §8; CC flood and same-frame measurements; suites; captures; help and ADRs.

## 8. Tests and acceptance

Unit: parser and decoding (CC, PB centre/ends, notes, velocity 0, poly/channel pressure, NRPN/RPN incl.
interrupted/null/7- vs 14-bit, relative encoders), matching (device/channel/range, duplicate names, reconnect
slot), transforms (range, invert, scale/offset, curves, smoothing, pickup), gates (range, held set, stuck-note
release on disconnect/disable/scene change), combination, scopes/precedence/tombstones/overrides, JSON round trip
and determinism, invalid records, Learn (capture, cancel, collision, NRPN priority, note → gate/trigger), monitor
ring bound. Integration through `Engine`: same-frame application, scene switch, save/load, missing device/param.
CoreMIDI virtual source end to end. Performance: CC flood (several devices) through the hub with timing and queue
bound; lookup cost with 1,000 mappings; one-update latency.

## 9. Risks

- The NRPN decoder cannot know a device's resolution in advance: a parameter is treated as 7-bit until a
  value-LSB (CC 38) arrives for it.
- Hardware: only a virtual source can be driven here; real-device behaviour is unverified.
- `pickup` reads the parameter base; with combination `add`/`max` it compares against the combined value.
