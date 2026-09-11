---
id: audio/analysis
title: Audio Analysis
category: Audio
summary: Everything AV Gen measures in the sound — the exact quantities, their ranges, and which one to reach for.
order: 21
tags: analysis, fft, spectrum, bands, bass, treble, centroid, flux, onset
keywords: how do i make something react to the kick; which signal is the bass; what does the analyser measure; how do i react to a drum hit; make it pulse with the music
related: audio/signals, audio/beats, audio/input, modulation/recipes
features: panel.analysis, subsystem.analysis
---

# Audio Analysis

AV Gen runs a short-time Fourier transform over the sound and turns each window into one
**analysis frame**. Everything the rest of the application knows about the music comes from these.

## The geometry

| | |
|---|---|
| Window | 2048 samples |
| Hop | 512 samples (75% overlap) |
| Window function | periodic Hann |
| Bins | 1025 |
| Frame rate | about 94 frames a second at 48 kHz |
| Window length | about 43 ms at 48 kHz |

These are fixed. The sample rate follows the file or the device; nothing in the interface, the
project format or the command line changes the window, the hop or the band edges.

## What is measured

### Level

**`rms`** is the root mean square of the window, 0 to 1. A full-scale sine reads about 0.71.
**`peak`** is the largest absolute sample in the window.

### Bands

Five bands, with fixed edges:

| Signal | Range |
|---|---|
| `bass` | 20 – 150 Hz |
| `lowMid` | 150 – 400 Hz |
| `mid` | 400 – 2000 Hz |
| `highMid` | 2000 – 6000 Hz |
| `treble` | 6000 – 16000 Hz |

> [!WARNING]
> **Band values are auto-gained, not absolute.** Each band is divided by a running peak that decays
> over about four seconds. A sustained bass line drives `audio.bass` towards 1.0 and holds it
> there. The signal answers *"how loud is the low end relative to the recent loudest low end"*, not
> *"how loud is the low end"*. The absolute level is computed internally but is not published as a
> signal, so there is no way to get one through a route.

### Brightness

**`spectralCentroid`** is the magnitude-weighted mean frequency, published as a position on a
logarithmic scale from 20 Hz to the Nyquist frequency, 0 to 1. It maps straight onto a parameter
without further shaping. A 440 Hz sine sits at roughly 0.43.

### Change

**`spectralFlux`** is the half-wave-rectified sum of how much every bin grew since the previous
frame, clamped to 1. It is raw and unsmoothed.

**`onsetStrength`** is that flux divided by an adaptive threshold — the median of the last eleven
flux values, scaled and offset. A value of 1 means "at the threshold". Its declared range is
**0 to 4**, not 0 to 1; a route that assumes 0 to 1 will over-drive.

**`onset`** is a boolean event, true when the strength is over threshold, the flux is rising, and
at least 60 ms have passed since the last onset.

## Choosing a signal

**For the transient of a hit, use `audio.onset`.** It is peak-picked, it has a 60 ms refractory
period, and it carries velocity. Shape it with the route's `envelope` set to `peak hold` or
`linear fall` and a `fall/s` value, which is exactly what that stage is for.

**For the weight of the low end, use `audio.bass`** — with the auto-gain caveat above in mind. A
short `attack ms` and a longer `decay ms` gives it a percussive shape.

**For "kick only", combine them.** Flux is summed across every bin, so a hi-hat fires `audio.onset`
as readily as a kick does; there is no per-band onset in AV Gen. The honest recipe is to route
`audio.onset` for the timing and gate it against `audio.bass`, or to use `audio.bass` alone.

**For brightness, use `audio.spectralCentroid`.** It is already normalised.

**For "something changed", prefer `audio.onsetStrength` to `audio.spectralFlux`.** The threshold
division self-normalises across loud and quiet passages.

**For structure rather than texture**, use the `music.*` events — `music.drop`, `music.build`,
`music.impact`. They exist so that nobody has to hand-roll thresholds on `audio.rms`.

> [!NOTE]
> Onsets are stamped at the centre of the window that first sees the transient, so they lead the
> physical hit by up to half a window — around 21 ms at 48 kHz. Visually early, never late.

## The Analysis panel

Top to bottom:

1. A numeric line: `rms`, `peak`, `centroid` in **Hz**, `flux`, `onset`.
2. A swatch that flashes on an onset and decays.
3. **Bands** — five bars of the normalised band values, 0 to 1.
4. A numeric line of the same five.
5. **Band history** — the last 240 frames of all five.
6. **Spectrum** — the log-compressed display spectrum on a logarithmic frequency axis from 20 Hz
   to Nyquist.
7. **Waveform** — 200 ms of the decoded file centred on the play-head. **Blank for live input**,
   because there is no decoded buffer to draw.

The panel does not show BPM, beat phase or tempo confidence. Analysis cost is reported in the
Control panel's performance line as microseconds per hop.
