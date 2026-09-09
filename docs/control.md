# Live control (milestone 1.1, ADR-021)

## Control signals

The `control` source (always present, `sources` group) publishes `control.<channel>` signals
(0..1, or event pulses) that modulation routes consume like `audio.bass` or an LFO. Channels come
from the control map's bindings, from the direct OSC scheme, or from the UI. Route them with
smoothing, curves, polarity and remap exactly like audio.

## OSC

The receiver listens on `control.osc.port` (default 9000, UDP, IPv4; bind address configurable).

Direct scheme (no binding needed; prefix `/avgen` by default):

| Address | Arguments | Effect |
|---|---|---|
| `/avgen/param/<path>` | one number per component (or one for all) | sets the parameter's base value |
| `/avgen/signal/<channel>` | `f` | sets a control signal (0..1) |
| `/avgen/pulse/<channel>` | optional `f` strength | fires an event channel for one frame |
| `/avgen/preset/recall` | `s` name | recalls a preset |
| `/avgen/preset/morph` | `s` a, `s` b, `f` t | morphs between two presets |
| `/avgen/transport/play`, `pause`, `stop`, `toggle` | | transport |
| `/avgen/transport/seek` | `f` seconds | seek |

Bindings (`control.osc.bindings`) map any address or pattern (`/fader/*`) and argument index to
a control channel and/or a parameter with an input range (`inMin`/`inMax`, e.g. 0..127) and a
parameter range (`min`/`max`). `event: true` fires a pulse instead of setting a value.

## MIDI

All connected sources are opened (`control.midi.filter` narrows by name; new devices connect
automatically). Bindings match `kind` (`cc`, `note`, `noteEvent`, `pitchBend`, `pressure`,
`program`), `channel` (-1 = any), `number` (-1 = any) and `source` (substring); `toggle` makes a
note flip 0/1. Values are normalised to 0..1, then mapped to the target's `min..max` when the
target is a parameter. The Control tab shows the last message received and can bind it to a
channel or parameter ("learn").

## Project JSON

```json
"control": {
  "osc": { "enabled": true, "port": 9000, "bind": "0.0.0.0", "prefix": "/avgen", "direct": true,
           "bindings": [ { "address": "/fader/1", "signal": "fader1" },
                         { "address": "/xy", "argIndex": 1, "parameter": "orb/scale", "min": 0.5, "max": 3.0 } ] },
  "midi": { "enabled": true, "filter": "*",
            "bindings": [ { "kind": "cc", "channel": -1, "number": 1, "signal": "mod" },
                          { "kind": "noteEvent", "number": 36, "signal": "kick" },
                          { "kind": "note", "number": 40, "toggle": true, "parameter": "post/bloom/enabled" } ] }
}
```

## Live audio input

`--input [name]` (or the transport's input selector) opens a capture device instead of a file;
`--list-audio-devices` and `--list-midi` enumerate. Analysis, signals and the beat clock run
unchanged; the render clock is the wall clock and there is no transport. The input gain is the
`audio/inputGain` parameter.

## Testing

Everything above is testable without hardware: OSC through loopback UDP, MIDI through
`MidiInput::inject` (bytes) or a CoreMIDI virtual source on macOS, control maps as pure
functions, and the engine integration through `ControlHub::injectMidi/injectOsc`.
