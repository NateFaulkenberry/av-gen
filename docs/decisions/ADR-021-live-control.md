# ADR-021: Live control — OSC and MIDI as signals and remote control, live audio input

- Status: Accepted (2026-09-09)
- Research: `docs/research/audiovisual-systems.md` §2.9, §6.3, §18 (OSC 1.0, Resolume's
  address scheme, TouchDesigner's channel-name-as-address), §19–20 (smoothing, lesson 4: the
  parameter path is the OSC address), `docs/research/audio-analysis.md` (live input latency)

## Problem

Milestone 1.x needs the engine to be played live: controllers (MIDI), other software (OSC),
and a microphone or line input instead of a file, without a second modulation system and
without new third-party dependencies where the platform already provides the transport.

## Alternatives considered

1. Controllers write parameters directly, only (no signals).
2. Controllers only as bus signals (everything through routes).
3. Both, through one control map, plus a fixed direct OSC scheme (chosen).
4. Third-party OSC/MIDI libraries (liblo, oscpack, RtMidi, libremidi).

## Decision

- `signals::ControlSource` ("control" kind, always present) publishes named channels
  `control.<name>` (continuous 0..1 or event pulses) that routes consume like any other signal.
- `control::ControlMap` (project `"control"` block) binds MIDI (CC, note, note event, pitch
  bend, pressure, program; source/channel/number filters, toggle) and OSC (address patterns,
  argument index, input range) messages to a control channel and/or a parameter base value with
  a range. `matchMidi`/`matchOsc` are pure functions; the UI has "learn" (bind the last message).
- Direct OSC scheme, no binding needed: `/avgen/param/<path> f…`, `/avgen/signal/<ch> f`,
  `/avgen/pulse/<ch> [f]`, `/avgen/preset/recall s`, `/avgen/preset/morph s s f`,
  `/avgen/transport/play|pause|stop|toggle`, `/avgen/transport/seek f`. Prefix configurable.
- Transports: an in-house OSC 1.0 implementation (messages, bundles, patterns, UDP receiver
  thread with a bounded inbox, sender) and MIDI over CoreMIDI on macOS (stub elsewhere), with an
  `inject` path so tests and other transports feed the same parser. Everything is drained and
  applied on the engine thread once per frame (`app::ControlHub`).
- `audio::AudioInput` (miniaudio capture) feeds the same `AnalysisStream` the player does, so
  the analyzer, signals, beat clock and routes run unchanged on live audio; the wall clock is the
  render clock and the capture frame count is the analysis position.

## Rationale

Signals plus direct parameter writes cover both live uses: a fader that *modulates* through a
chain (smoothing, curves, polarity) and a fader that *is* a parameter. One map, saved in the
project, keeps controller setups portable with the show. OSC's address model maps onto the
parameter path space directly (research lesson 4), so the direct scheme costs nothing to learn.
OSC 1.0 is small enough to implement and test in-house (no dependency, no licence); CoreMIDI is
the platform API the third-party wrappers call anyway. Feeding live audio through the existing
stream keeps ADR-012's model: analysis is indexed by sample position, not wall time.

## Consequences

- Positive: any parameter is remote-controllable today; controllers become modulation
  sources; microphone/line input works with the whole pipeline; no new dependencies.
- Negative: IPv4 only; MIDI clock/tempo sync not yet used; no OSC feedback to controllers;
  MIDI on Linux/Windows needs a backend (ALSA/WinMM or a library); live input has no latency
  compensation beyond the analyzer's hop.
- Follow-ups: MIDI clock as a beat source and OSC feedback/query (`/avgen/param/*` replies)
  are done (`docs/control.md`: `control::MidiClockTracker`, `Engine::setTempoSource`, feedback
  host in the control map); multi-output and NDI/Syphon (1.2), display/projection workflows.
