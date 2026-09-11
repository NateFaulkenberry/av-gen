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
averaging the channels — there is no stereo analysis anywhere.

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

A live input is analysed at the device's native rate, mixed to mono in the capture callback. There
is **no whole-track analysis** for a live input, because there is no finite track — which is why
beat tracking behaves differently. See [Beats and tempo](help://audio/beats).

> [!NOTE]
> Only capture devices are enumerated. AV Gen does not list output devices and cannot listen to
> system audio without a loopback device installed at the operating-system level.

## Input gain

The parameter `audio/inputGain` scales the incoming signal. Default 1.0; the slider runs 0 to 4 and
the hard limit is 8.

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
