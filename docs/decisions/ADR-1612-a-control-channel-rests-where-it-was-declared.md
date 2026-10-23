# ADR-1612: A control channel rests where it was declared, so a knob can cut as well as boost

- Status: Accepted (eng/latency-levers; for The Living Prism's MIDI intensity knobs)
- Extends ADR-021 (control signals) and ADR-1606 (pickup). Opt-in: a project that declares nothing behaves as before.
- Code: `src/control/control_map.{hpp,cpp}` (`ChannelRest`, `ControlMap::channelRests`, `restFor`, the `channels` key,
  `channels()` declaring them, the direct OSC `<prefix>/control/reset` = `DirectCommand::Kind::ControlReset`),
  `src/signals/source.{hpp,cpp}` (`ControlSource::setRest`, `rest`, `resetToRest`; a channel remembers whether its knob
  moved), `src/app/engine.cpp` (`ensureControlSource` applies the rests; a project load resets to them),
  `src/app/control_hub.{hpp,cpp}` (pickup on a channel with a rest; the reset command); `docs/control.md`.
- Tests: `tests/unit/test_control_map.cpp` (`[adr1612]`: the keys round-trip, are absent when unset, are checked; the
  declared channels; the reset address), `tests/integration/test_control_engine.cpp` (`[adr1612]`: through the engine,
  the rest before any message, a plain knob takes over at once and attenuates to 0, a pickup knob waits for the rest
  and follows, a reset returns every channel and the pickup knob lets go until it comes back).

## Problem

A `control.<ch>` channel read 0 until its knob first moved. So an intensity knob could only add: a route that wanted
"CC 1 at its middle = the look as authored, lower = less, higher = more" read 0 (full cut) at the start of every show
until the performer found the knob, and a knob physically at 0.8 jumped the look the moment it was touched. The Living
Prism's CC 1/2 need exactly that middle. ADR-1606's pickup skipped control channels, because there was nothing to take
over from.

## Decision

The project's control block declares a channel's rest: `"channels": [{"name": "glow", "rest": 0.5, "pickup": true}]`.
- The channel publishes its rest until its knob (or OSC) first sets it, and again after a reset: when a project loads
  and on `<prefix>/control/reset`.
- A route reads the middle as neutral through `"polarity": "bipolar"` (0.5 -> 0), so the knob attenuates below it and
  boosts above it.
- With `pickup` on the channel, on its binding or on `control.midi`, the rest is a value to take over from: the knob is
  held (ADR-1606's `pickupAccepts`) until it reaches the channel's current value, then follows; after a reset the value
  is no longer what the knob last sent, so it lets go until it comes back.
- Undeclared channels rest at 0, exactly as before; nothing is written to a project that declares none.

Live input only changes `control.*` in a performance; an offline render of a project with a declared rest reads the
rest (its neutral look), where it read 0.

## Consequences

- A scene can author "intensity" knobs that start neutral and go both ways, and a performer's first touch does not
  jump the look.
- The Living Prism can declare CC 1/2 with `rest: 0.5`, `pickup: true` and route them bipolar.
