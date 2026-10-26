# MIDI Performance Mapping, Learn Mode & Assignment Panel (owner brief, verbatim, 2026-10-25)

The owner's covering note: "queue for when an agent slot is available"

---

AV Gen — MIDI Performance Mapping, Learn Mode & Assignment Panel

Objective

Implement a complete, production-quality MIDI performance mapping system for AV Gen. The system must allow performers to use a broad range of MIDI hardware and software sources—including synthesizers, drum machines, electronic drum pads, MIDI keyboards, control surfaces, fader banks, generic MIDI controllers, and DAW-generated MIDI—to control AV Gen’s visual scenes in real time.

The intended workflow is simple:

1. A scene ships with useful artist-authored default MIDI mappings.
2. The performer connects their MIDI hardware and optionally opens the MIDI monitor.
3. They select a visual parameter, enable MIDI Learn, and move a physical control or play a note.
4. AV Gen learns the incoming MIDI message and assigns it to the selected destination.
5. They customize the mapping, save it with the scene or project, and use it in future performances without having to configure everything again.

For a musician, the physical controls of their existing instrument should become an expressive visual performance surface. Turning a synthesizer filter knob might shift a scene’s palette. A drum pad might trigger a particle burst. A keyboard’s velocity might control the intensity of a visual event. A fader bank might control scene intensity, effect mix, or other parameters.

These are examples, not hard-coded behavior. The performer must be able to assign supported MIDI sources to compatible visual destinations.

The feature is MIDI Performance Mapping, not a collection of integrations for individual hardware brands. Prioritize a generic, extensible implementation that works with any device transmitting MIDI messages that AV Gen can receive.

Non-negotiable requirements

* Audit the existing implementation before changing code.
* Reuse the existing MIDI input, parameter, modulation, scene, project, serialization, and UI systems wherever appropriate.
* Add a dedicated MIDI Learn / Assignments panel.
* Provide MIDI Learn and a useful live MIDI monitor.
* Support incoming continuous controls, note events, velocity, and other expressive MIDI message types that the existing MIDI infrastructure can reliably expose.
* Support multiple MIDI input devices, with device-specific and channel-specific filtering where the existing input architecture permits it.
* Support artist-authored scene defaults and user-created mappings.
* Save and restore user mappings at scene and project scope.
* Make the precedence between default, scene-specific, and project-specific assignments explicit and predictable.
* Support multiple destinations from one MIDI source and multiple MIDI sources contributing to one destination where meaningful.
* Ensure mappings operate within the existing modulation and parameter architecture rather than creating a parallel visual control system.
* Ensure MIDI mappings can affect the current frame’s parameter evaluation where the existing architecture permits it. Do not introduce avoidable one-frame delays.
* Keep the feature suitable for AV Gen’s live-rendering budget and target of 1080p30 on an M2 Max.
* Preserve existing MIDI behavior unless an audited design decision explicitly requires changing it.
* Do not introduce backward-compatibility shims, duplicate parameter systems, or unnecessary abstraction layers.
* Do not hard-code the Prophet, Moog, or any other manufacturer’s MIDI implementation into the core mapping engine.

⸻

Phase 1 — Audit and implementation plan

Before implementation, investigate the existing codebase thoroughly.

1.1 Existing MIDI infrastructure

Determine:

* How MIDI devices are enumerated, opened, selected, and disconnected.
* How incoming MIDI messages enter the engine.
* Whether multiple input devices can be used simultaneously.
* How MIDI channels and message types are represented.
* Whether CC, note-on, note-off, pitch bend, channel pressure, polyphonic key pressure, program change, and system-exclusive messages are currently received or exposed.
* Whether NRPN or RPN messages are already decoded, or whether they can be decoded from incoming CC sequences.
* Whether MIDI messages are timestamped and how they are delivered to the audio, control, and render/update threads.
* How the existing MIDI keyboard and other MIDI features consume incoming events.
* How device reconnection, unplugging, duplicate device names, and unavailable devices are handled.

Do not assume that the underlying MIDI library’s capabilities are the same as AV Gen’s implemented capabilities. Distinguish what the hardware/library can transmit from what AV Gen currently receives and processes.

1.2 Existing parameter and modulation architecture

Trace the complete path from an incoming MIDI event to a visual parameter.

Identify:

* The canonical parameter registry, if one exists.
* How scene parameters, effect parameters, material parameters, generator parameters, and global controls are exposed.
* Whether parameters have stable IDs, human-readable names, types, minimum/maximum values, and metadata.
* How modulation sources are registered and resolved.
* How normalized modulation values are transformed into parameter values.
* How parameter overrides, macros, automation, and other modulation sources interact.
* When parameter values are evaluated relative to scene updates and rendering.
* Which parameters can safely be changed during a live performance.
* Whether the existing UI already has reusable parameter pickers, search controls, or modulation-assignment components.

The MIDI mapping system must integrate with the actual parameter model. It must not rely on fragile display-name matching or direct pointers into scene objects that may be destroyed when a scene changes.

1.3 Existing persistence and UI

Identify:

* How scene defaults are authored and serialized.
* How projects are saved and loaded.
* How scene-specific user settings are persisted.
* How the application distinguishes scene-authored data from user overrides.
* How panels, docking, tabs, keyboard shortcuts, selection states, and editor themes are implemented.
* How existing tests are structured and how to run the relevant suites.

1.4 Deliver a plan before implementing

Write a concise but technically substantive plan that identifies:

1. The relevant existing files and systems.
2. The proposed mapping data model.
3. The proposed panel structure and interaction model.
4. The persistence and default-override strategy.
5. Threading and real-time safety considerations.
6. The implementation sequence.
7. Test coverage and acceptance criteria.
8. Risks, unknowns, and any design decisions that depend on the audit.

Prefer two independent investigations if the coding-agent environment supports two parallel subagents: one for MIDI/parameter architecture and another for UI/persistence. Have them report findings before implementation, then reconcile the results into one plan.

Do not begin with a large refactor. Once the plan is established, implement the smallest coherent architecture that satisfies the requirements below.

⸻

Phase 2 — MIDI mapping model

2.1 Separate sources, assignments, and destinations

Use the existing architecture where possible, but maintain three clear concepts.

MIDI source: An incoming message identified by its source device or device scope, message type, channel where applicable, and controller/note identifier.

Mapping assignment: A persistent rule that matches an incoming MIDI source, processes its value, and routes the result to one or more destinations.

Destination: A stable, discoverable AV Gen parameter or modulation input that can accept the incoming control.

Do not create a separate hard-coded mapping implementation for every kind of MIDI controller.

2.2 Supported MIDI source types

Support the following where the existing input layer can provide the necessary data.

Continuous and expressive controls

* Control Change (CC), values 0–127.
* Pitch bend, including its native resolution where available.
* Channel pressure / aftertouch.
* Polyphonic key pressure, retaining note identity.
* NRPN and RPN parameters, if they can be decoded reliably.
* Other existing continuous MIDI sources that have a clear mapping representation.

Note and trigger sources

* Note-on, including note number and velocity.
* Note-off, retaining note identity.
* Note-on with zero velocity, handled according to MIDI semantics.
* Note-trigger and note-gate behaviors derived from note events.
* Velocity-derived event intensity.
* Optional note-number or note-range filtering where useful.

Program changes may be exposed in the monitor and considered as discrete triggers if the architecture supports them. Do not interpret arbitrary system-exclusive data as generic continuous controls.

MIDI Clock, transport messages, and other system messages should remain distinct from ordinary CC and note mappings. Do not accidentally consume or alter the existing transport/clock behavior.

Important hardware compatibility constraint

Many hardware synthesizers can receive MIDI parameter changes without transmitting those parameters when a physical knob is moved. Some expose knob movements as CC, others use NRPN, and some require configuration changes or do not transmit particular controls at all.

AV Gen must respond to messages it actually receives. Do not promise that every physical synth control is discoverable on every instrument.

2.3 Source identity and filtering

A mapping should be able to specify:

* Input device: a specific device, all devices, or an explicit device group if supported.
* MIDI channel: a specific channel or any channel.
* Message type.
* Controller number, note number, pitch-bend source, or other appropriate identifier.
* Optional note range or other source-specific filters.
* Optional matching rules for discrete events.

Use stable device identifiers when the MIDI backend exposes them. Device names alone may not be unique. When stable identity is unavailable, implement a reasonable fallback and make the limitation visible.

A device reconnecting under the same identity should restore its mappings automatically where possible.

Avoid matching all devices and all channels by default when doing so could cause collisions between instruments. Global matching should be an intentional choice.

⸻

Phase 3 — Mapping behavior and processing

3.1 Continuous mappings

For continuous sources, normalize the incoming value into a documented internal representation, typically 0–1, while preserving the source’s original resolution where useful.

Each assignment should support the following options where applicable:

* Minimum and maximum output range.
* Invert.
* Sensitivity or scale.
* Offset.
* Response curve, including linear and a small set of useful nonlinear curves.
* Optional smoothing.
* Enable/disable.
* Mapping-level contribution or scale where the destination model supports it.

Do not force a low-resolution 7-bit CC value to appear higher resolution than it is. High-resolution sources such as pitch bend or decoded NRPN should retain their extra resolution.

The mapping should be predictable: moving a control from minimum to maximum should produce the configured range, subject to its response curve and smoothing.

Pickup and relative encoders

Support absolute controllers first. Add explicit support for relative encoders if the source format can be identified reliably.

For absolute controls, consider a pickup/soft-takeover mode when a hardware control may be out of sync with the saved software value. This prevents an abrupt jump when the performer first touches the control.

For relative encoders, decode the supported increment/decrement formats correctly rather than treating their values as absolute positions.

Do not infer relative behavior solely from a controller’s name.

3.2 Discrete and note-driven mappings

Note sources should support useful event semantics rather than being treated as ordinary continuous CC values.

At minimum, define behaviors for:

* Trigger on note-on.
* Trigger intensity from note velocity.
* Gate high while a note is held, low on note-off.
* Optional toggle behavior, if it fits the existing parameter model.

Where practical, allow filtering by individual note or note range and channel.

Note-off must correctly release a gate. Note-on with zero velocity must not leave a gate stuck.

If a device disconnects or a mapping is disabled while notes are held, prevent stuck gates by releasing active note state as appropriate.

Avoid allocating a new object or rebuilding the entire mapping graph for every note event.

3.3 Mapping one source to multiple destinations

Allow a single source to drive multiple destinations.

Example: one physical synth filter knob drives:

* Palette position.
* Crystal illumination.
* Refraction intensity.

Each destination must have its own mapping configuration. The user should be able to disable or remove one destination without destroying the others.

The UI should make it easy to see that the same source is assigned multiple times.

3.4 Multiple sources targeting one destination

Allow multiple MIDI sources to contribute to one destination when meaningful.

Do not simply let the last message received overwrite the destination without defining the behavior.

The system should support or explicitly define a small set of predictable combination policies, such as:

* Replace/override.
* Additive contribution.
* Maximum.
* Other policies already supported by the modulation architecture.

Reuse existing modulation combiners where possible. Only expose combination modes that have well-defined semantics for the destination type.

If a destination only supports one controlling assignment, make that limitation explicit rather than silently creating conflicting assignments.

3.5 Real-time behavior

MIDI input and mapping evaluation must be safe for the existing thread model.

* Do not perform file I/O, expensive allocations, or blocking operations in a real-time-sensitive callback.
* Do not mutate scene objects unsafely from the MIDI input thread.
* Do not introduce locks in the audio callback if they can be avoided.
* Use the existing event queue or an appropriate bounded message-passing mechanism.
* Define what happens if the event queue is full.
* Avoid unbounded queues or unlimited accumulation of obsolete CC messages.
* Preserve ordering where note-on/note-off and gate behavior depends on it.
* Coalesce redundant continuous-control updates only where doing so does not lose required event semantics.
* Avoid per-frame scans of every possible MIDI source if a more efficient indexed mapping lookup is available.
* Do not rebuild or reallocate the entire mapping graph whenever a controller moves.

The design must work with multiple devices sending continuous CC streams while notes and other messages arrive concurrently.

Map changes initiated by the UI must become visible to the engine safely and predictably.

⸻

Phase 4 — Dedicated MIDI Learn / Assignments panel

Implement a new, polished, first-class panel that fits AV Gen’s existing UI framework and visual language.

Use the existing docking/panel infrastructure. Do not create a separate windowing framework or introduce a new UI toolkit.

The panel should be useful to both a first-time performer and an advanced user managing dozens of assignments.

4.1 Panel organization

Use a layout that makes the following areas accessible without overwhelming the user.

A. Input/device status

Show:

* Available MIDI input devices.
* Connected/enabled status.
* Whether a device is receiving messages.
* Channel activity where practical.
* An all-devices option.
* Device-specific input enable/disable.
* Refresh/rescan functionality if supported by the existing MIDI backend.

Do not imply that a device is receiving data merely because it is connected. Distinguish device availability from actual message activity.

Reuse existing device management rather than creating a second independent MIDI device-opening mechanism.

B. MIDI Learn controls

Provide a clear Learn Mode state and a simple workflow.

The user must be able to:

1. Select a destination.
2. Arm Learn for that destination.
3. Move a hardware knob/fader, press a pad, or play a note.
4. See the detected source.
5. Confirm or cancel the assignment.

Where the UI supports it, allow the user to select a destination first and then click Learn.

Also support the reverse workflow if practical: select a detected source in the MIDI monitor and assign it to a destination.

Learn mode must have a clear visual indication when armed.

Provide a cancel action and an obvious way to exit Learn mode.

Learn-mode safety

Do not let incidental MIDI activity silently overwrite an existing assignment.

When a new source is learned:

* Display the device, channel, message type, and controller/note identifier.
* Show whether the source is already assigned.
* If it would collide with an existing mapping, explain the collision and let the user decide whether to add another destination, replace an assignment, or cancel.
* Respect device/channel filters.
* Do not create an assignment until the user has selected a valid destination and the learn operation has succeeded.
* Do not capture unrelated traffic when the selected input filter can exclude it.

Support learning both continuous controls and note-based triggers.

Where NRPN is supported, distinguish a decoded NRPN parameter from its underlying CC messages so a single NRPN interaction does not create several misleading assignments.

If a hardware controller emits a stream of CC messages while Learn is armed, capture the intended source according to a documented policy. Avoid creating a separate mapping for every CC message received during a single physical action.

C. Assignment list

Show all active and inactive mappings in a searchable/filterable list or table.

Each assignment should expose enough information to understand its behavior without opening a separate dialog.

Suggested columns:

* Enabled state.
* Source device or device scope.
* Channel.
* Message type.
* Source identifier.
* Destination.
* Value range/behavior.
* Scope: default, scene, or project.
* Conflict or availability status.

Allow the user to:

* Select an assignment.
* Edit it.
* Duplicate it.
* Enable or disable it.
* Remove it.
* Search/filter by source, device, destination, or scope.
* Sort by source or destination where useful.

Use stable mapping IDs, not row indexes, for selection and editing.

The UI should remain responsive with a substantial number of assignments.

D. Assignment editor

When an assignment is selected, expose its relevant settings.

For continuous sources, show:

* Input device and channel filters.
* Message type and source identifier.
* Destination parameter picker.
* Minimum and maximum.
* Inversion.
* Sensitivity/scale.
* Response curve.
* Smoothing.
* Pickup/soft takeover, when relevant.
* Enable state.
* Applicable combination behavior.

For note-driven mappings, show:

* Input device and channel filters.
* Note or note-range filter.
* Trigger, velocity, gate, or toggle behavior.
* Destination.
* Velocity scaling or other applicable intensity settings.
* Enable state.

Do not show irrelevant controls for the selected source type. The editor should be context-sensitive.

Provide useful validation and an explanation if the chosen source/destination combination is unsupported.

E. Learn target/destination picker

The destination picker must expose meaningful parameters from the current scene and relevant global systems.

Use a searchable hierarchy where possible, for example:

* Global
* Current Scene
    * Scene Parameters
    * Generators
    * Lighting
    * Materials
    * Particles
    * Effects
* Existing modulation inputs or macros
* Other supported parameter groups discovered during the audit

This is an illustrative organization. The actual hierarchy must reflect the parameter registry and scene architecture.

Show human-readable names and enough contextual information to disambiguate duplicate names. Internally, resolve destinations using stable identifiers.

Only list parameters that can actually be driven safely by a MIDI mapping. Do not expose arbitrary internal variables or unsafe engine state.

Where possible, show the parameter’s current value and valid range.

F. Live MIDI monitor

Include a dedicated monitor section or tab in the panel.

The monitor should display a live, readable stream of incoming MIDI activity.

At minimum, show:

* Timestamp or elapsed time.
* Source device.
* Channel.
* Message type.
* Controller/note/parameter identifier.
* Raw value.
* Normalized value when applicable.
* Human-readable interpretation where available.

Examples:

* CC 74 — value 92/127
* Note On — C3 — velocity 108
* Pitch Bend — center + 18%
* NRPN 1234 — value 8192, if NRPN decoding is supported.

Use correct note-name and octave conventions consistently. If the chosen convention differs from another common convention, document it.

The monitor must support:

* Pause/resume display.
* Clear.
* Device filtering.
* Channel filtering.
* Message-type filtering.
* Optional suppression of redundant continuous updates.
* Selection of a message for inspection or assignment.
* A visible indication that incoming traffic is active.

The monitor is a diagnostic tool, not a raw MIDI recording system.

Monitor performance

CC-heavy devices may generate hundreds or thousands of messages per second.

* Use a bounded buffer.
* Limit visible rows.
* Avoid allocating UI objects for every incoming event.
* Batch UI updates at an appropriate frequency.
* Do not render every individual CC event as a separate expensive UI operation.
* Keep monitoring independent from the correctness of MIDI mapping execution.
* Never let a full UI monitor buffer block MIDI processing.

Provide a useful default filter so the monitor is readable during performance, with an option to inspect all supported message types.

⸻

Phase 5 — Defaults, user mappings, and persistence

This is central to the feature. A performer should not need to rebuild mappings every time they open a scene.

5.1 Three logical scopes

Implement a clear model for three types of mapping.

Scene-authored defaults

Mappings intentionally shipped with a scene by its author.

Examples:

* A default MIDI keyboard note mapping for triggering an event.
* An illustrative CC assignment for global scene intensity.
* A documented set of generic performance controls appropriate for that scene.

Defaults must not assume that every user has a specific hardware synthesizer.

Where the scene’s artistic design benefits from a known controller layout, document that layout. Do not require a physical controller to use the scene.

Scene-specific user mappings

Custom assignments that apply to one scene.

Examples:

* This scene’s palette is controlled by a synth filter knob.
* A drum pad triggers a scene-specific visual burst.
* A fader controls one effect’s intensity.

These mappings should follow the scene when the user returns to it.

Project-level user mappings

Mappings or assignments that the user intentionally wants available across the project.

Examples:

* A consistent global intensity control.
* A global performance control for visual reactivity.
* A shared MIDI surface layout used across several scenes.

Project-level mappings must not accidentally depend on a parameter that only exists in one scene unless their scope and activation behavior explicitly support that case.

5.2 Default override and precedence

Define and document the behavior of defaults and user overrides.

Requirements:

* Scene defaults are loaded automatically with their scene.
* User mappings persist independently from the scene’s shipped defaults.
* Editing a user’s mapping must not silently mutate the shipped scene defaults.
* User mappings must not disappear when a scene’s default mapping data is updated.
* Users must be able to disable a default assignment without deleting or modifying the underlying default.
* Provide a way to restore a default mapping or reset the scene’s user mappings to the shipped configuration.
* Define how duplicate sources and destinations behave across scopes.
* Avoid ambiguous double-driving when the same physical control exists in both defaults and user assignments.
* Make the active mapping set visible to the user.

Prefer explicit stable mapping IDs and a clear override/tombstone model over ad hoc merging based on display names.

Do not overcomplicate the system with a general-purpose configuration framework if a simple scoped mapping model suffices.

5.3 Save/load behavior

Persist user mappings through the existing project/scene serialization architecture.

At minimum:

* Project mappings survive project save/load.
* Scene-specific mappings survive project save/load and are restored when the relevant scene is loaded or activated.
* Scene defaults are loaded through the existing scene-default mechanism.
* Device references are handled safely when devices are disconnected or unavailable.
* Missing devices must not cause mappings to be deleted.
* Missing destination parameters must not crash loading.
* Invalid or outdated mapping records must be reported or safely disabled with a useful explanation.
* Saving and loading must be deterministic.
* Do not write mapping data on every incoming MIDI event.
* Respect the existing unsaved-changes and save workflow.

The system must remain robust when a project is loaded on a different computer or with a different set of MIDI devices attached.

5.4 Presets and portability

Investigate whether AV Gen already has an appropriate mechanism for exporting/importing user configurations.

If a suitable mechanism exists, integrate with it.

Otherwise, design the mapping model so a future mapping-preset export/import feature is possible without requiring a schema redesign. Do not expand this task into an unrelated preset-management system unless implementation is trivial and naturally fits the existing architecture.

A future portable mapping preset should be able to describe generic MIDI assignments independently of a specific device connection, while allowing the user to bind those assignments to a device on another system.

⸻

Phase 6 — MIDI source discoverability and hardware independence

AV Gen should support as many MIDI surfaces as its input infrastructure allows, without requiring per-device integrations.

The generic system should work with:

* Hardware synthesizers transmitting CC, NRPN, pitch bend, notes, or other supported MIDI messages.
* MIDI keyboards with mod wheels, expression controls, aftertouch, and assignable knobs.
* Drum machines and electronic drum pads transmitting note events or CC.
* Generic fader banks and knob controllers.
* Grid controllers and pad surfaces.
* DAW-generated MIDI.
* Virtual MIDI ports and software instruments, where supported by the platform’s MIDI backend.

Device profiles are optional, not foundational

A device profile can later provide:

* Human-readable names for known controls.
* Suggested mappings.
* Default channel filters.
* Known encoder modes.
* Manufacturer/model-specific parameter interpretations.

The core engine must not depend on these profiles. An unknown controller should still work through generic MIDI Learn.

Do not add a large device database as a prerequisite for the feature.

NRPN and RPN

Investigate support during the audit.

If practical, implement reliable decoding of NRPN and RPN using the incoming MIDI CC sequence, respecting the MIDI specification and handling incomplete/interrupted sequences safely.

Keep the original CC stream available to the MIDI monitor if useful, but avoid learning the intermediate CC messages when the user intends to learn a decoded NRPN/RPN parameter.

Do not assume every hardware synth uses the same parameter numbering or scale. A decoded NRPN identifier is not automatically a human-readable synthesizer parameter name. Manufacturer-specific interpretation belongs in optional profiles.

If NRPN/RPN support would significantly complicate the initial implementation, isolate it behind a clean extension point and document the limitation. Prioritize the core generic mapping system first.

⸻

Phase 7 — Live performance and scene behavior

7.1 Scene transitions

Define the activation behavior for mappings when switching scenes.

* Project-level mappings remain active according to their declared scope.
* Scene-specific mappings activate only when their target scene is active.
* Scene defaults are combined with user overrides according to the documented precedence rules.
* Mappings targeting parameters that are not present in the current scene are not applied to unrelated parameters.
* Scene transitions must not leave stale parameter references or active note gates behind.
* Switching scenes must not produce a sudden uncontrolled parameter jump unless the mapping semantics intentionally allow it.

Where possible, use stable destination handles resolved against the current scene rather than raw object pointers.

7.2 Mapping feedback

Provide useful feedback that helps the performer understand what is happening.

Examples:

* A source’s activity indicator flashes when MIDI arrives.
* The selected assignment shows its latest incoming value.
* The destination’s current value is visible.
* Disabled, disconnected, conflicting, or unresolved assignments are clearly indicated.
* A destination picker shows whether the parameter is currently available in the active scene.

Feedback must not depend on the MIDI monitor being open.

Do not build an elaborate new visualization system for this feature. Reuse existing parameter inspection and UI mechanisms.

7.3 Scene-authored mapping intent

Scenes should be able to ship a small set of useful default assignments when their design benefits from them.

However:

* Do not create arbitrary default mappings for every possible MIDI control.
* Do not require specific physical hardware.
* Do not bind a scene to a particular brand’s CC layout.
* Prefer generic defaults or documented, well-known controller conventions.
* Keep a scene fully usable without any MIDI device attached.

The authoring model should let scene creators specify intended performance destinations and optional generic default sources.

⸻

Phase 8 — Testing and verification

Add tests using AV Gen’s existing testing conventions.

8.1 Unit tests: MIDI decoding and matching

Test:

* CC decoding and normalized values.
* Pitch-bend decoding, including center and range endpoints.
* Note-on and note-off behavior.
* Note-on with zero velocity.
* Velocity mapping.
* Channel and device filtering.
* Source matching.
* Relative encoder decoding if implemented.
* NRPN/RPN decoding if implemented.
* Handling of incomplete or interrupted NRPN/RPN sequences.
* Invalid and unsupported messages.
* Duplicate device names where applicable.
* Device disconnection and reconnection.
* Held-note cleanup when a device disappears.

8.2 Unit tests: mapping transformations

Test:

* Minimum/maximum scaling.
* Inversion.
* Sensitivity and offset.
* Response curves.
* Smoothing.
* Pickup/soft takeover.
* Continuous and discrete event semantics.
* Multiple destinations from one source.
* Multiple sources contributing to one destination.
* Combination policies.
* Enable/disable behavior.
* Invalid destinations.
* Scene changes and mapping activation.
* Stable mapping IDs and source identity.

8.3 Persistence tests

Test:

* Project-level mapping save/load.
* Scene-specific mapping save/load.
* Scene defaults combined with user overrides.
* Disabling a default mapping without modifying the default.
* Resetting user mappings.
* Missing devices.
* Missing destinations.
* Unknown or invalid mapping fields.
* Loading projects on a system with different MIDI devices.
* Deterministic serialization.

8.4 UI tests or testable interaction logic

Where the existing test infrastructure permits, test:

* Learn mode captures the expected message.
* Learn mode can be cancelled.
* Learning detects an existing assignment.
* Collision handling does not silently destroy mappings.
* Note learning creates the expected trigger/gate behavior.
* The monitor’s bounded-buffer policy.
* Assignment edits update the engine safely.
* Scene/project scope selection behaves as documented.

Do not attempt to test visual appearance solely through unit tests. Use existing UI testing or manual verification where appropriate.

8.5 Real-hardware validation

If hardware is available, test at least:

1. A generic MIDI keyboard with mod wheel and notes.
2. A controller that transmits CC from knobs/faders.
3. A note-based pad or drum source.
4. Two input devices simultaneously, if supported.

For the synthesizer use case, validate with a hardware synth that actually transmits parameter changes when its physical knobs are moved. Confirm the message format and any required transmit settings rather than assuming the parameter exists because it appears in the synth’s MIDI documentation.

If hardware is unavailable, use a virtual MIDI source or the best existing automated input path, and document which parts have not been verified on physical devices.

8.6 Performance verification

Measure or otherwise verify that:

* A stream of continuous CC messages does not block audio processing.
* Multiple devices can send MIDI simultaneously without unbounded queue growth.
* The monitor does not materially affect rendering performance.
* Mapping lookup and evaluation remain inexpensive as the number of assignments grows.
* MIDI updates become visible within the intended frame/update cycle.
* Note events remain correctly ordered and gates do not stick.
* There is no avoidable one-frame delay between receiving a mapping value and using it in scene parameter evaluation.

Use existing profiling tools where appropriate. Do not claim performance targets were met without evidence.

⸻

Phase 9 — Documentation

Update the relevant AV Gen documentation and architecture records as appropriate.

Document:

* How to open the MIDI Learn / Assignments panel.
* How to connect and inspect a MIDI input device.
* How to use MIDI Learn.
* How to map continuous controls and note-based triggers.
* How to edit, disable, duplicate, and remove assignments.
* How default, scene-specific, and project-level mappings interact.
* How mappings are saved and restored.
* Supported MIDI message types.
* Known limitations, including devices that do not transmit their physical knob movements.
* Any required synth-side settings for transmitting parameter changes.
* NRPN/RPN limitations, if any.
* Threading and mapping-evaluation architecture.
* How to add optional device profiles in the future, if the design provides an extension point.

Avoid documenting planned functionality as implemented functionality.

⸻

Implementation boundaries

Do not use this task as an excuse to redesign unrelated systems.

Specifically:

* Do not replace the MIDI backend unless the audit demonstrates a blocking limitation.
* Do not introduce a separate modulation engine.
* Do not hard-code visual destinations for individual synthesizer brands.
* Do not require a hardware-specific profile for generic MIDI Learn.
* Do not add a large preset or device database before the generic mapping workflow works.
* Do not allow UI code to mutate engine parameters unsafely from the MIDI input thread.
* Do not build a complex MIDI routing matrix if a simple source-filtering and assignment model meets the requirements.
* Do not add mappings to scenes that do not benefit from them simply to demonstrate coverage.
* Do not silently drop or alter existing MIDI clock/transport behavior.
* Do not retain dead code or add compatibility layers that the audit demonstrates are unnecessary.

Favor a clean, coherent implementation over the maximum possible feature count. If the audit finds that an existing subsystem already provides part of the required behavior, extend it rather than duplicating it.

⸻

Acceptance criteria

The work is complete when all of the following are true:

1. AV Gen has a dedicated MIDI Learn / Assignments panel.
2. The user can inspect incoming MIDI activity through a live monitor.
3. The user can select a visual parameter, activate Learn, manipulate a hardware or virtual MIDI control, and create a mapping.
4. Both continuous controls and note-driven events are supported.
5. A learned mapping can be edited, disabled, duplicated, removed, and saved.
6. A single source can drive multiple destinations.
7. Multiple sources can contribute to a destination wherever the destination’s combination semantics allow it.
8. Input filtering prevents accidental cross-device or cross-channel collisions.
9. Scenes can ship with default mappings.
10. Users can override or disable defaults without mutating the shipped configuration.
11. Scene-specific and project-level mappings persist across save/load.
12. Missing devices or destinations do not crash the application or silently delete user mappings.
13. Generic MIDI Learn works without requiring device-specific integrations.
14. The existing modulation system remains the authority for how mapped values reach scene parameters.
15. MIDI input and mapping evaluation are safe under the existing thread model.
16. The monitor remains bounded and responsive under CC-heavy traffic.
17. Relevant tests pass, and new tests cover decoding, mapping, persistence, and collision behavior.
18. Documentation accurately describes what is supported and how to use it.
19. The implementation has been reviewed for unnecessary complexity, stale references, unsafe threading, and avoidable frame latency.

Final report

After implementation, provide a concise but substantive report containing:

* The architecture discovered during the initial audit.
* The main implementation decisions and why they fit the existing codebase.
* Files and systems changed.
* MIDI message types actually supported.
* The final panel layout and Learn/monitor workflow.
* The exact precedence and persistence rules for defaults, scene mappings, and project mappings.
* Threading and real-time safety measures.
* Tests added and the results of running them.
* Performance observations or measurements.
* Hardware validation performed, distinguishing real hardware from virtual MIDI testing.
* Known limitations and future work.

Do not claim that a MIDI source, parameter, test, or hardware device is supported unless it was actually implemented and verified to the stated degree.

The desired outcome is a generic, reliable, low-overhead MIDI performance-mapping system that makes AV Gen feel like a playable audiovisual instrument, while remaining straightforward to use during a live show.
