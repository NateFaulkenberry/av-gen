---
id: modulation/midi-mapping
title: MIDI Mapping and Learn
category: Modulation
summary: Playing the scene from a keyboard, synth, pad or fader bank: MIDI Learn, the assignments, the monitor, and how defaults, your rig, project and scene mappings combine.
order: 36
tags: midi, learn, mapping, assignments, monitor, controller, nrpn, pads, faders, synth
keywords: how do i map a midi knob; midi learn; map a drum pad; my synth knob does nothing; midi monitor; which cc is this knob; restore default mapping; turn off a default midi mapping; save my midi mappings; nrpn; midi rig; same mappings in every live scene; map next scene to a pad; midi play pause
related: modulation/external-control, modulation/routes, reference/panels
features: panel.midi
---

# MIDI Mapping and Learn

The **MIDI** panel turns any device that sends MIDI into controls for the scene. That includes a synth, a keyboard,
a drum pad, a fader bank or a DAW. Open it from **View ▸ MIDI**. It docks along the bottom, beside Modulation.

A mapping does one of three things:

- it writes a **control channel** (`control.<name>`). The scene's routes then decide what that channel moves, so the
  modulation system stays in charge;
- it writes a **parameter's base value**, as if you had moved its slider;
- it fires an **app action**: play/pause, return to start, or the next or previous live scene.

It lands in the same frame it arrives in. The engine reads MIDI before it evaluates any parameter.

## Connect and inspect a device

1. Plug the device in. With **All MIDI inputs** chosen (the default, here or in the Live panel), it connects as soon
   as it appears.
2. Open the **Devices** tab. Every input the session has seen is listed with:
   - **Connection**: whether it is plugged in.
   - **Receiving**: whether it is actually sending. A connected keyboard that nobody plays is not receiving.
   - **Channels 1-16**: which channels it has sent on. Each cell lights up as messages arrive.
   - Its message and clock counts.
3. Untick a device to switch it off on this computer. Its messages are still listed in the monitor, marked
   *switched off*, and nothing else uses them. The choice is saved in the app settings, not in the project.
4. **Rescan** reopens MIDI input. **Listen to all inputs** clears a device filter that a project or the Live panel
   set.

## MIDI Learn

The Learn bar at the top of the panel is always visible.

1. Choose a **Destination**. The button opens a searchable list:
   - the control channels;
   - the **App actions** (see below);
   - every parameter you can modulate, grouped as the Parameters panel groups them, with its current value and range.
     While you are choosing for the rig, a parameter that only this scene has is marked *this scene*.

   You can also right-click a slider in the **Parameters** panel and choose **MIDI Learn…**. That picks the
   destination and starts listening in one step.
2. Choose **Scene**, **Project** or **Rig** (see below).
3. Press **Learn**. An amber **LISTENING** bar appears.
4. Move a knob or fader, press a pad, or play a note.

AV Gen then shows what it detected: the device, the channel, the message (for example *CC 74* or *Note C4*) and its
live value. It also shows whether that control is already assigned, and to what.

5. Confirm or cancel:
   - **Assign** when the control is free.
   - **Add destination** keeps the existing assignments and drives this destination as well.
   - **Replace existing** switches the existing assignments off. A scene default is never deleted, only switched
     off.
   - **Learn again** listens for a different control.
   - **Cancel** (or **Esc**) assigns nothing.

Nothing is created until you confirm, so a stray message cannot overwrite anything.

### What Learn captures

The **first** message that qualifies is the source. The rest of that knob's stream only updates the value shown, so
turning a knob makes one mapping, not one per message.

Learn never captures:

- clock or transport messages;
- note-offs;
- bank select (CC 0 and CC 32, which come before a program change);
- channel mode messages (CC 120-127);
- the CCs that carry an NRPN (see below). The decoded NRPN is captured instead.

The "from every device" selector restricts Learn to one device.

### Learn options

- **Any device** is off by default. The mapping then listens only to the device it was learned from, so two
  instruments that send the same CC do not collide. Tick it to accept every device on purpose.
- **Any channel** works the same way for channels.
- **Notes trigger** controls what a learned note becomes:
  - off: a **gate**, high while the key is held;
  - on: a **trigger**, a one-frame pulse for an event channel.

### Learn from the monitor

You can work the other way round. Select a row in the **Monitor** tab and press **Learn from selected**. That message
becomes the detected source, and you then pick its destination.

## The assignments

The **Assignments** tab lists every mapping: the active ones and, with **Inactive** ticked, the rest. For each it
shows:

- whether it is on;
- the device;
- the channel;
- the message type and source;
- the destination;
- its range and behaviour;
- its scope;
- its status;
- a live activity light with the latest value.

A source that drives several destinations is marked **x2**, **x3** and so on. You can search by source, device,
destination or scope, and sort by any column. Rows are identified by a stable mapping id, so sorting and filtering
never edit the wrong one.

Right-click a row to:

- **Enable / Disable** it;
- **Duplicate** it;
- **Remove** it (**Remove from the rig** for a rig mapping, which removes it for every scene);
- **Restore default**;
- **Make it a scene default**;
- **Move to the rig** (a project or scene mapping), or **Copy to the rig** (a default, which stays the scene's);
- **Move into this scene** or **Override in this scene** (a rig mapping; see *Your rig* below).

### The editor

Select a row to edit it below the table. Only the controls that apply to its source are shown.

**Source**
- **Device**: any device, or a specific one.
- **Channel**: any, or 1-16.
- **Type** and **Number**.
- For notes, a note range (**to note**).
- For CCs, the **Encoder** setting (below).

**Continuous sources** (CC, pitch bend, pressure, NRPN)
- **Min / max**, in the destination's units.
- **Invert**.
- **Curve**:
  - linear;
  - exponential, which gives fine control at the bottom;
  - logarithmic, which gives fine control at the top;
  - S-curve, which gives fine control at both ends.
- **Sensitivity** and **Offset**.
- **Smoothing**, in milliseconds.
- **Pickup**: soft takeover. The value does not jump on your first touch; it waits until the knob reaches it.

**Notes**
- **Behaviour**:
  - **Gate**: high while any note of the range is held.
  - **Toggle**: each press flips it.
  - **Trigger**: a one-frame pulse.
- **Velocity sets the level**.
- **Off / on**: the two values the destination moves between.
- **Velocity curve**.

**Combine** says what happens when several mappings drive one destination:

- **Replace**: the latest message wins. This is the default, and what bindings always did.
- **Max**: the largest current value wins. For example, a pad held high over a fader's level.
- **Add**: the sum, clamped to the destination's range.

Combine belongs to the destination, so it applies to every mapping on it.

**Encoder** is for endless encoders that send steps instead of positions. Choose **two's complement**, **binary
offset** or **sign-magnitude**, as the device's manual says. AV Gen never guesses this from the device. A relative
mapping starts from the destination's current value, so the first detent moves it one step.

A trigger mapped to a parameter sets the parameter and leaves it there. For a flash, map the trigger to a control
channel and let a route shape it. The editor says so when you set this up.

**Scope** moves the mapping between **Scene**, **Project** and **Rig**.

### App actions

A pad, button or key can fire one of the keyboard's commands:

| Action | Same as |
|---|---|
| Play / pause | Space |
| Return to start | Return |
| Next live scene | Page Down |
| Previous live scene | Page Up |

An action fires once per press: on a note-on, on a program change, or when a knob or button crosses half-way upwards.
A button that sends 127 and then 0 is one press. The live scene actions work as Page Up and Page Down do: while a live
scene is open, or while live input is running.

## Defaults, your rig, project and scene mappings

There are four kinds of mapping, lowest precedence first.

| Scope | Where it is saved | When it applies |
|---|---|---|
| **Default** | the project's `control.midi.bindings`, written by the scene's author | always, unless you switch it off |
| **Rig** | `midi-rig.json` in AV Gen's preferences folder on this computer, never in a project | in every scene and every project |
| **Project** | `control.midi.mappings`, scope `project` | whatever scene the project shows |
| **Scene** | `control.midi.mappings`, scope `scene` | only while the project shows that scene (its file name, for example `aurora-borealis.scene.json`) |

The precedence is exact:

1. **Switching a default off** records its id in `control.midi.disabledDefaults`. The default itself is untouched.
2. **Editing a default** never writes the default. AV Gen creates a scene mapping that *overrides* it, and the
   default is shown as *replaced by your edit*.
3. When a higher scope (scene over project over rig over default) maps an **overlapping** source to the **same
   destination**, the lower mapping is **shadowed**: it is shown, but not applied. Sources overlap when they are the
   same type with overlapping numbers, channels and devices; "any" overlaps everything. So one physical control
   never drives one destination twice by accident.
4. Different destinations from one source all apply, whatever their scopes.
5. On one message, mappings apply in the order default, rig, project, scene. The higher scope writes last.

To undo:

- **Restore default** removes the default's tombstone and any edit that overrides it.
- **Reset scene mappings** removes this scene's mappings and switches every default back on. Project mappings and the
  rig stay.

A scene's author can also turn a mapping into one of the scene's own defaults with **Make it a scene default**. It is
written to `control.midi.bindings` when the project is saved.

> [!NOTE]
> "Project" means the open project file. The live scene switcher opens a different project for each live scene, so
> a project mapping does not follow you from one live scene to the next. For that, use the rig.

### Your rig

The rig is your controller setup, mapped once for every scene. Choose **Rig** in the Learn bar, or right-click an
existing mapping and choose **Move to the rig**. The rig:

- applies in every live scene and every project you open;
- survives switching scenes, opening other projects and restarting AV Gen;
- is saved on this computer as soon as you change it, in `midi-rig.json` beside AV Gen's settings. It is never
  written into a project, so a project you share or save is exactly what it would be without a rig;
- is shown with a **rig** label in the Assignments table.

A project's own mapping or a scene's own mapping wins over the rig on the same control and destination. To change a
rig mapping for one scene only, right-click it and choose **Override in this scene**. That makes a scene copy which
replaces it in this scene, and the rig keeps applying everywhere else. **Move into this scene** takes it out of the
rig and saves it with this project instead.

If you Learn at Scene or Project scope and choose **Replace existing** over a rig mapping, the rig mapping is switched
off in that scene or project only.

What a rig mapping can drive in every scene:

- **control channels**. They exist in every scene; whether a scene's routes use one is up to that scene;
- **app actions**;
- **the parameters AV Gen itself gives every scene**. Every live scene has these:
  - the post chain (`post/...`: bloom, colour, lens effects and the rest);
  - the temporal effects (`temporal/...`: feedback, echo, mosh, slit-scan);
  - the camera's lens, exposure and focus (`camera/lens/...`, `camera/exposure/...`, `camera/focus/...`) and its orbit
    (`camera/distance`, `camera/height`, `camera/orbitSpeed`);
  - the response (`sonic/response/...`);
  - `scene/brightness`, `scene/gridIntensity` and the live input gain (`audio/inputGain`).

  A scene that draws with its own renderer may ignore some of them, for example the camera's lens settings. The
  parameter is still there, so the mapping stays active.

A rig mapping to a parameter that the current scene does not have is listed as **inactive for this scene**. That is
not an error: it is kept, and it applies again in every scene that has the parameter.

After a scene switch, every held note is released, and a rig knob with **Pickup** waits until it reaches the new
scene's value before it moves it.

How several mappings combine on one destination (**Combine**) belongs to the open project. A rig mapping uses the
project's setting for its destination, **Replace** unless the project says otherwise.

## Saving

Default, project and scene mappings are part of the project. **Save** writes them, and the unsaved-changes prompt and
the dot in the title bar cover them like any other edit. Nothing is written to disk when MIDI arrives.

The rig is saved to its own file as soon as it changes. It does not mark the project as changed. A rig mapping to a
parameter moves that parameter's value, as a slider does, and that value is saved with the project.

A mapping refers to its device by the system's stable id and keeps the device's name:

- If the device is **not connected**, the mapping is kept and marked *device not connected*. It works again when the
  device comes back.
- On **another computer** the id is different. If a connected device has the saved name, that device is used and the
  mapping says *device matched by name*. Otherwise, pick the device again in the editor, or choose **Any device**.
- A mapping whose **parameter is not in this scene** is kept and marked *not in this scene*. For a rig mapping, it
  is *inactive for this scene*.
- A saved mapping that this version **cannot read** is kept exactly as it was, reported when the project loads, and
  written back on save. It is never applied, and never deleted for you.

## The monitor

The **Monitor** tab shows the most recent incoming MIDI. It keeps the last 512 rows. Each row shows:

- the time;
- the device;
- the channel;
- the type;
- the number, with the note name for notes;
- the raw value;
- the value normalised to 0-1;
- what the message means, for example *CC 74 92/127*, *Note On C4 vel 108*, *Pitch bend centre +18%* or
  *NRPN 1234 8192/16383*;
- how many mappings it moved.

The controls:

- **Pause** and **Clear**.
- Filters by device, channel and type.
- **Merge repeats** turns a turning knob into one updating row with a count.
- **Clock** shows clock messages. They are hidden by default; the Devices tab counts them.

The monitor is a diagnostic view. It costs the same whether it is open or not, and mapping never waits for it.

Note names use **middle C = C4** (note 60), as macOS and Ableton Live do. Some devices and DAWs call note 60 "C3".

## Supported messages

| Message | Mappable | Notes |
|---|---|---|
| Control change (CC) | yes | 7-bit (0-127), absolute or relative |
| Note on / off | yes | gate, toggle or trigger; velocity; a single note or a range; velocity 0 counts as note-off |
| Pitch bend | yes | 14-bit; the centre is exactly the middle of the range |
| Channel pressure | yes | |
| Polyphonic key pressure | yes | per note or note range |
| NRPN / RPN | yes | decoded from CC 99/98 (101/100) and 6/38/96/97; 7- or 14-bit |
| Program change | yes | as a trigger (one program) or a value (any program); it also drives the live scene switcher |
| Clock, start, stop, continue | no | they drive the tempo (see [MIDI and OSC](help://modulation/external-control)) |
| System exclusive | no | ignored |

### NRPN and RPN

A synth that sends a knob as NRPN sends four CCs for each change. AV Gen decodes them into one message per change,
carrying the parameter number, and treats them as one control:

- A parameter counts as 7-bit until the synth sends a value LSB (CC 38). A 7-bit synth's maximum is then 1.0, not
  0.992.
- The null RPN (127/127) deselects the parameter.
- Data entry with nothing selected is an ordinary CC 6.

The monitor lists the CCs that carry an NRPN greyed, marked *part of an NRPN/RPN*, next to the decoded message.

AV Gen shows only the NRPN's **number**. Which synth parameter a number means differs from synth to synth, and AV Gen
does not know it.

## When a knob does nothing

AV Gen responds to the messages it **receives**. Many hardware synths accept parameter changes over MIDI without
sending them when you turn the knob. Others send knobs as NRPN rather than CC, and some send nothing until a setting
is changed. Open the Monitor and turn the knob:

- **Nothing appears**: the synth is not sending it. Look in its global or MIDI settings for something like
  *Param Transmit*, *Knob Send*, *CC/NRPN Out* or *Local Control*. Set it to send CC or NRPN, check the MIDI channel,
  and check that MIDI out goes to the USB port or cable you are using. The synth's manual names the exact setting;
  AV Gen has no list of them.
- **Rows appear, but greyed and marked *switched off***: tick the device in the Devices tab.
- **A mapping is marked *shadowed*, *replaced*, *other scene*, *inactive for this scene* or *device not
  connected***: the status column says why it does not apply.

## How it works

- **Input.** CoreMIDI delivers messages on its own thread into a fixed-size queue. Nothing is allocated per
  message. Each message names its device by a slot whose stable id survives an unplug.
- **When the queue is full.** This only happens when the engine stalls, for example while a project loads.
  Continuous controls then keep only their latest position, so every knob still ends where it was left. Notes keep
  the rest of the queue. If a note is ever lost, every gate is released rather than left held.
- **Unplugging.** A device that disappears releases its held notes. So does disabling a mapping, changing scene or
  switching live scene.
- **Processing.** Once a frame, before the routes run, the engine:
  1. reads the queue;
  2. decodes NRPN/RPN;
  3. updates the monitor and Learn;
  4. applies the mappings, found through an index by message type and number.

  On an M2 Max, four devices at 3,000 messages a second each added about 0.2 ms to a frame. Looking up one message
  among a thousand mappings took about 0.1 µs.

The decisions are recorded in ADR-1700 to ADR-1705.

## Device profiles

There are none, and nothing depends on one: an unknown controller works through Learn. A future profile could give
names to a device's numbers, suggest default mappings and set encoder modes. It would do that by producing ordinary
`control.midi` mappings, so the model would not need to change.
