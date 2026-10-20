---
id: audio/input
title: Audio Input
category: Audio
summary: Loading a file or listening to a device, and what AV Gen does with the samples either way.
order: 20
tags: audio, input, file, device, microphone, gain
keywords: how do i load music; can i use a microphone; which audio formats work; live input; no sound
related: audio/analysis, audio/beats, troubleshooting/audio-and-control
features: panel.control, command.file.open-audio
shortcuts: transport.play, file.open-audio
parameters: audio/inputGain
---

# Audio Input

There are two sources of sound, and they feed the same analysis, so nothing downstream can tell
them apart.

## A file

**File ▸ Open Audio...**, the `O` key, or dropping a file on the window.

The whole file is decoded into memory at load. Decoding is miniaudio's, at the file's own sample
rate and channel count; WAV, FLAC and MP3 are its built-in decoders. AV Gen then mixes to mono by
averaging the channels. The one stereo measure is `audio.width`, from the first two channels.

Loading a file also runs a **whole-track analysis** in a single pass, taking roughly a tenth of a
second for a few minutes of audio. This happens in live playback as well as for offline renders,
because the musical-structure fold needs to see the whole track at once.

## A live device

Available in the live editor only. The **Control** panel carries an `input` combo whose first entry
is `(audio file)`; below it are the capture devices, rescanned every five seconds. Beside it are a
`peak` meter and the device's name and sample rate.

Devices are matched by a case-insensitive substring of their name, first match wins; an empty name
means the system default. From the command line, `--input` alone opens the default device and
`--input <name>` matches a substring. `--list-audio-devices` prints what is available and exits.

A live input is analyzed at the device's native rate, mixed to mono in the capture callback. There
is **no whole-track analysis** for a live input, because there is no finite track — which is why
beat tracking behaves differently. See [Beats and tempo](help://audio/beats).

> [!NOTE]
> Only capture devices are enumerated. AV Gen does not list output devices and cannot listen to
> system audio without a loopback device installed at the operating-system level.

## Stems: analysing the parts of a mix

A project plays one mix. Beside it you can add **stems** -- the drum, bass or synth bounce of the same song -- which
AV Gen analyses but never plays: open **Audio...** on the Sequence strip and use **Stems (analysis only)**. Each stem
publishes `stem.<key>.*` (level, onset, the bands, and with **Hits** its kicks, snares and hats), so a route can follow
the bass alone while the picture still plays the whole song. See the [Signal Reference](help://audio/signals).

- **The key** (`drums`, `synth_lead`) is the signal name. It is made from the label once and kept: rename the label
  freely; change the key only if you mean to move every route on it.
- **Offset** is where the stem's first sample sits on the timeline, in seconds. Stems bounced from one session start
  together and need 0. AV Gen never guesses an offset from the audio; matching lengths are not proof of alignment.
- **Formats** are the decoder's: WAV (16/24/32-bit, float), FLAC, MP3. A stem may be at another sample rate than the
  mix; it is analysed at its own (nothing is resampled).
- **Problems** are listed on the stem and in the project warnings, naming the file: a file that will not decode, a
  silent file, damaged samples, a stem shorter or longer than the mix. Outside a stem's span its signals read 0.

## Several sources from one interface

On an interface with more than two inputs, the **Live** panel lists every input channel under **Audio input** with a
meter. Tick a channel to analyse it on its own as `stem.<key>.*`, beside the main mix; label it (Drum bus, Bass DI);
tick **Pair** to treat it and the next channel as one stereo source; tick **Hits** on a drum channel. The map is
remembered for that interface on this machine.

AV Gen cannot know what is plugged in: the drum bus is only on input 1 if your mixer sends it there. Give each
part its own physical output on the mixer (a group or aux bus, pre-fader if the main fader should not change it) and
cable it to its own interface input. A stem key used offline (`drums`) works live when an input has the same key.

When the interface is unplugged the panel says **Disconnected**, its sources read 0, and nothing else is opened in its
place; it is reopened when it comes back. `avgen --list-audio-devices` prints every device's channels per stream;
`avgen --input-meter <device>` prints each input's level once a second -- move a signal from input to input to see
which channel answers.

> [!NOTE]
> With no channels ticked, an input is one mix exactly as before. Channels are only captured separately once one is
> ticked.

**Gain** (the last column) trims one source before it is analysed, -24 to +24 dB, on top of Input gain. Use it to lift
a quiet channel over the gate and into the kick, snare, hat and beat detectors. It does not make that source's levels
bigger: those read against the source's own loudness. A stem has the same **gain** in the Stems section (a change
analyses the stem again).

## Feeds: the beat and the response from a source

By default the beat clock (`beat.*`, bars, `music.*`) and `response.*` follow the main mix. The **Feeds** row, under
the Inputs table and under the Stems section, lets a source drive them instead:

- **Beat**: the beat clock from this source. A clean kick or drum bus gives a steadier grid than a full mix. A running
  MIDI clock still wins.
- **Hits**: `response.kick`, `low`, `snare`, `hat` and `onset` (and their envelopes) from this source. The bass line in
  the mix no longer fires `response.low`.
- **Levels**: `response.bass`, `level`, `levelSlow`, `intensity`, `sustain`, `transient` and `flux` from this source,
  read against its own loudness. Use a sustained source (a bass, a pad, the mix): a drum source makes the levels pulse
  with every hit.

Every route keeps working: a route on `response.kick` now follows the drum stem's kicks. A source a feed uses gets the
kick/snare/hat analysis automatically. A fed source that goes quiet, or a stem outside its span, feeds nothing. It is
not swapped for the mix. The same key works offline (a stem) and live (an input source with that key). The choice is
saved in the project as `"feeds"` (see `docs/audio.md`).

## Input gain

The parameter `audio/inputGain` scales the incoming signal. Default 1.0; the slider runs 0 to 4 and
the hard limit is 8. Each input source also has its own **Gain** (above).

## The transport

In the **Control** panel: **Play**/**Pause**, **Stop**, a seek slider and a volume slider. The
whole block is disabled when there is no audio.

| Key | Does |
|---|---|
| `Space` | play or pause |
| `Left` | seek back five seconds |
| `Right` | seek forward five seconds |

There is no loop or play-range feature in the transport.

## With no audio at all

Every `audio.*` signal reads 0, and the Analysis panel shows *"no audio loaded (drop a file on the
window)"*.

One subtlety worth knowing: once at least one analysis frame has been seen, a render frame that
brings no *new* analysis leaves the continuous values where they were rather than zeroing them.
Pausing a track therefore freezes `audio.bass` at its last value instead of dropping it to silence.
Only the onset event is cleared.
