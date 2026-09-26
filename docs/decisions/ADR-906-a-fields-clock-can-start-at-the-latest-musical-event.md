# ADR-906: A field's clock can start at the latest musical event

**Status:** Accepted
**Date:** 2026-09-26
**Follows:** ADR-025 (fields), ADR-091 (a seek lands where a play does), the Effect Library's
TRIGGER (`world/effects/effect_trigger.hpp`: `Trigger`, `TriggerClock`)
**Found by:** the GV3 revision's audit (`mushrooms-wind.md` §2): a wave of light could be sent
through the mushrooms on an event only by keying `waveOrigin` at every event by hand, or by an
envelope route on it, which is stateful and lands differently on a seek than on a play.
**Implemented by:** `FieldSpec::trigger`, `triggerAge`, `clock()`, `silent()` (`src/spatial/field.*`),
`scene::resolveFieldTriggers` (`src/scene/field_params.cpp`), `Composition::setTriggerClock`,
`Engine::update` (binds the engine's clock before the flatten), `FieldUniforms::update`.
**Tests:** `tests/rendering/test_emission_lanes_gpu.cpp` "A wave field timed from a marker sends a
ring of light through the instances after it" (and the composition test's ring section);
`tests/unit/test_emission_lanes.cpp` "A field's trigger round-trips, and a proximity is refused by
name", "A triggered field counts from the latest event, is silent before the first, and is pure in
time", "A composition resolves its fields' triggers from the clock it is handed".

## Context

A field's time is the transport clock: a wave field's front is at `waveOrigin + waveSpeed * t`, so a
ring leaves its origin once, at t = 0, and is gone. What a music video wants is a ring *per event* --
a drop, a downbeat, every fourth beat, a section marker -- and the effects already have exactly that
machinery: a `Trigger` names the event and `TriggerClock::lastTriggers` answers "when was the latest
one at or before second t" as a pure function of the piece (the offline beat track, the musical-event
walk, the markers, a schedule). Fields could not use it.

## Decision

- **`FieldSpec` gains an optional `trigger`** -- the effects' own `Trigger`, read and written by the
  effects' own `triggerFromJson`/`triggerToJson`, so "every 4th beat from beat 1" means one thing in
  both places. `beat`, `onset`, `musicEvent`, `marker` and `repeat` are accepted; `proximity` is
  refused by name, because a field is not a body HIST records.
- **The field's clock is the seconds since the latest such event.** `scene::resolveFieldTriggers`
  asks the clock at the frame's transport second and stores `triggerAge`; `packField` (the GPU) and
  the CPU samplers read the field's clock through `FieldSpec::clock()`, so a triggered wave's front
  leaves `waveOrigin` at each event and travels out at `waveSpeed`; `tau` (the noise kinds'
  animation) restarts the same way. A compound's children keep their own clocks on both sides.
- **Before the first event the field is silent** -- it samples and packs exactly as a disabled field
  does -- and so is a triggered field with no clock (a composition no engine drives).
- **Seek-exact by construction:** the age is a pure function of the second and the piece; there is
  no "the wave started when I saw the beat" state to checkpoint or replay.
- **The engine binds its `TriggerClock` for the frame before the flatten** and hands it to the
  composition (`setTriggerClock`), whether or not the project has effects; binding it again later in
  the frame is the same frame (`TriggerClock::setFrame`).
- Hashed and serialised only when present, so every field before this keeps its hash and its file.

## Consequences

- **A wave of light through the mushrooms on a musical event** is now: a `wave` field with a
  `trigger`, named by the scatter layer's `emissiveField` (ADR-905). The GPU test sends one out from
  a marker at 10 m/s and finds its crest 5 m out 0.5 s after the marker and 9 m out 0.9 s after, on
  both sides, nothing before the marker, and the same bytes for a second reached by stepping from zero
  and one asked cold.
- The same field can drive anything else that samples fields -- `volumeColorField` for a glowing
  crest in the mist, an effector, a hero part's `emissiveField`.
- Only the latest event's front exists: a new event restarts the wave. Overlapping rings are what a
  Ground Pulse with a trigger is for (up to 8 live).
