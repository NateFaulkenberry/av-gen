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

### Query and feedback

| Address | Arguments | Reply |
|---|---|---|
| `/avgen/query` (or `/avgen/query/<path>`) | `s` path | `/avgen/param/<path> f…` (base components) |
| `/avgen/query/all` | | one `/avgen/param/<path>` per serialised parameter, bundled in chunks of 50 |
| `/avgen/query/presets` | | `/avgen/presets s…` (preset names) |

Replies go to `control.osc.feedbackHost:feedbackPort` when a host is set, otherwise back to the
sender's address and port (`OscMessage::sender`). With `control.osc.feedback: true` and a host
set, every parameter base value that changed since the previous frame (UI, presets, cues,
MIDI bindings) is pushed as `/avgen/param/<path>` once per frame; changes that arrived through
OSC in that frame are not echoed back, so a controller and the app cannot loop. The push
compares against a per-path cache, re-primed whenever the map is applied.

## MIDI

All connected sources are opened (`control.midi.filter` narrows by name; new devices connect
automatically). Bindings match `kind` (`cc`, `note`, `noteEvent`, `pitchBend`, `pressure`,
`program`), `channel` (-1 = any), `number` (-1 = any) and `source` (substring); `toggle` makes a
note flip 0/1. Values are normalised to 0..1, then mapped to the target's `min..max` when the
target is a parameter. The Control tab shows the last message received and can bind it to a
channel or parameter ("learn").

### MIDI clock as the tempo source

System real-time messages (Clock at 24 ppqn, Start, Continue, Stop) never bind or count as
unmatched; they feed `control::MidiClockTracker`. The tracker reports the tempo (a
median-gated mean over the last 48 tick intervals: an interval far from the window's median is a
dropped or doubled tick and is discarded, a run of four is a tempo change; the mean of the
accepted intervals telescopes to the window span so per-tick jitter cancels — ±1 ms jitter
stays within ±0.5 BPM), the beat phase (extrapolated between ticks from the frame time), the
beat count since Start and a running flag (Stop, or two seconds without a tick, clears it; a
device that sends clocks without a Start free-runs). Messages without a timestamp use the
engine's frame time.

`Engine::setTempoSource(TempoSource::MidiClock)` (project `control.tempoSource: "midi"`) makes
`beat.phase/pulse/count/bpm/bar`, the `SourceContext` tempo fields and the timeline's beat clock
follow the MIDI clock while it is running and has a tempo; otherwise, and with `"analysis"`
(default), the analyzer's beat tracker drives them. The analyzer keeps running either way, so
switching back is instant.

## Project JSON

```json
"control": {
  "tempoSource": "analysis",
  "osc": { "enabled": true, "port": 9000, "bind": "0.0.0.0", "prefix": "/avgen", "direct": true,
           "feedbackHost": "", "feedbackPort": 9001, "feedback": false,
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
functions, and the engine integration through `ControlHub::injectMidi/injectOsc` (an injected
OSC message may carry a `sender` so query replies travel over real UDP to a test receiver; an
injected `0xF8` clock stream at a fixed frame rate gives an exact tempo).
