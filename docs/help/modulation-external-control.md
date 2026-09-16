---
id: modulation/external-control
title: MIDI and OSC
category: Modulation
summary: Driving AV Gen from a controller or another application, and what it can send back.
order: 35
audience: expert
tags: midi, osc, control, learn, cc, binding, clock
keywords: how do i use a midi controller; midi learn; osc address; can i control it from touchosc; midi clock sync; no midi devices
related: modulation/routes, audio/beats, troubleshooting/audio-and-control
features: panel.control, subsystem.control
---

# MIDI and OSC

Both live in the **Control** panel's Control tab. Both can either publish a signal (a
`control.<channel>` that routes then use) or write a parameter's base value directly.

## MIDI

macOS uses CoreMIDI. On other platforms there is no backend, and the status reads
*"no MIDI backend on this platform"*.

Tick **MIDI** to open. The `filter` field is a case-insensitive substring of a device name; `*` or
empty opens every source, and with the wildcard, devices that appear later connect on their own.
The status line shows the number of sources and the message count, or the error, or `closed`.

`--list-midi` prints the available inputs and exits.

### Learn

The **Learn: last received** section shows the last message in the form
`MIDI <kind> ch <n> #<cc> = <value>`, with the channel displayed **1-based**. Fill in a `signal`
name, or pick a parameter from the target combo, then press **Bind last MIDI**.

Ticking **event** before binding makes a note produce a momentary pulse rather than a level.

### Binding kinds

| Kind | Matches | Value |
|---|---|---|
| `cc` | control change | 0 – 127 normalised |
| `note` | note on and off | velocity, or a 0/1 toggle |
| `noteEvent` | note on only | an event pulse carrying velocity |
| `pitchBend` | pitch bend | 14-bit normalised, centre 0.5 |
| `pressure` | channel pressure | normalised |
| `program` | program change | an event, or a continuous value |

A binding can specify a source substring, a channel (or any), and a number (or any). A binding to a
parameter also carries a `range` mapping the incoming 0…1 onto the parameter's units.

### Clock

`Clock`, `Start`, `Continue` and `Stop` never bind and never count as unmatched — they feed the
tempo tracker. Set `tempo source` to `MIDI clock` to let an incoming clock own the tempo instead of
the analyzer.

## OSC

OSC 1.0 over UDP, IPv4, implemented in AV Gen itself. Defaults: enabled, port **9000**, bind
`0.0.0.0`, prefix `/avgen`, direct scheme on, feedback off, feedback port 9001.

`--osc-port <n>` overrides the project's port.

### The direct scheme

With **direct scheme** on, these addresses work without any binding:

| Address | Arguments | Does |
|---|---|---|
| `/avgen/param/<path>` | one number per component | set a parameter's base value |
| `/avgen/signal/<channel>` | float | set a control signal |
| `/avgen/pulse/<channel>` | optional float strength | fire a control event |
| `/avgen/state/go` | name, optional instant flag | go to a scene state |
| `/avgen/preset/recall` | name | recall a preset |
| `/avgen/preset/morph` | name, name, t | blend between two presets |
| `/avgen/transport/play` `pause` `stop` `toggle` | | transport |
| `/avgen/transport/seek` | seconds | seek |
| `/avgen/query` or `/avgen/query/<path>` | optional path | ask for a parameter's value |
| `/avgen/query/all` | | every serialisable parameter, in bundles |
| `/avgen/query/presets` | | the preset names |

So a parameter's address is `/avgen/param/orb/scale`, prefix included.

Sending a single number to a multi-component parameter writes it to **every** component.

### Bindings and feedback

A binding matches an address — exactly, or as an OSC pattern with `?`, `*`, `[abc]`, `[a-z]`,
`[!abc]` and `{foo,bar}`, matched per path segment so `/` is never matched by a wildcard. It names
which argument carries the value, an input range to map from (0 to 127, say), and a target.

With **OSC feedback** on and a feedback host set, AV Gen pushes a parameter's value whenever its
base changes. Paths written by OSC in the same frame are excluded, so a motorised controller and
AV Gen cannot drive each other in a loop, and nothing is sent on the priming pass.

Query replies go to the feedback host if one is open, otherwise back to whoever asked.
