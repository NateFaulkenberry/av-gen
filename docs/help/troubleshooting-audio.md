---
id: troubleshooting/audio-and-control
title: Audio and Control
category: Troubleshooting
summary: Nothing reacts, the tempo is wrong, the controller does nothing.
order: 83
tags: troubleshooting, audio, midi, osc, reactive, tempo
keywords: nothing reacts to the music; no midi devices; bpm is wrong; osc not working; audio is silent; the scene does not move
related: audio/input, audio/analysis, audio/beats, modulation/external-control, modulation/routes
---

# Audio and Control

## Nothing reacts to the music

Work down this list:

1. **Is it playing?** The transport is in the **Control** panel. `Space` toggles it.
2. **Is the analyser seeing anything?** Open the **Analysis** panel. If the bands are flat, the
   problem is upstream of modulation.
3. **Does a route exist?** **Modulation ▸ Routes**. The row shows the route's last output, which
   tells you whether the signal is flat, the amount is zero, or something in the chain is eating it.
4. **Did the route bind?** A route naming a parameter the current scene does not have is kept and
   skipped. It will resolve when a scene providing it loads.
5. **Is something else writing the same parameter?** Timeline automation and any route in
   `replace` mode overwrite whatever came before them in the frame.

## The band values sit at the top and barely move

Band values are auto-gained against a running peak that decays over about four seconds. A sustained
bass line holds `audio.bass` near 1. Use `audio.onset` for the transient instead, or accept that
the band is telling you about *relative* energy. See [Audio analysis](help://audio/analysis).

## `audio.tempo` reads 0

The tempo is unknown. Confidence is below its floor. Offline, that also means no beats were
produced at all. Live, it means the tracker has fewer than four picked onsets in its six-second
window, or has not locked yet.

## Everything froze at its last value when I paused

Expected. Once an analysis frame has been seen, a render frame with no *new* analysis leaves the
continuous values where they were rather than zeroing them. Only the onset event is cleared. Press
**Stop** to reset.

## The waveform is blank

The waveform plot draws the decoded file. There is no decoded buffer for a live input, so it is
blank by design.

## No MIDI devices

On macOS, check the status line beside the **MIDI** checkbox: it shows the source count, or the
error, or `closed`. `--list-midi` enumerates and exits.

On any other platform the status reads *"no MIDI backend on this platform"* — there is no
implementation, only a stub.

Remember that the `filter` field is a substring match. `*` or empty opens everything.

## MIDI messages arrive but nothing happens

The **Learn: last received** line shows the last message, with the channel **1-based**. If it is
moving, MIDI is fine and the binding is the problem. Note that `Clock`, `Start`, `Continue` and
`Stop` never bind — they feed the tempo tracker.

The binding header counts applied and unmatched messages, which is the fastest way to see whether a
binding is matching at all.

## OSC does nothing

OSC is on by default on port 9000 with the prefix `/avgen`, bound to `0.0.0.0`, **IPv4 only**.

A parameter's address includes the prefix and the `param` segment:
`/avgen/param/post/bloom/intensity`. Sending to `/post/bloom/intensity` will not work.

Errors are logged with an `osc:` prefix. The Control tab shows the packet and message counts.

## The audio device opens but delivers silence

Check `audio/inputGain`, and check the `peak` meter beside the input combo. On macOS a denied
microphone permission opens the device and delivers silence; grant it in the system's privacy
settings.
