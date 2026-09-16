---
id: sequencer/overview
title: The Sequencer
category: Sequencer
summary: What the Sequence panel holds, how time works in it, and what a bake actually produces.
order: 40
tags: sequencer, timeline, shots, bake, snapping, zoom, audio, waveform, beats
keywords: how do i make a music video; what is the sequence panel; how do i cut between scenes; timeline; shots; where is the waveform; why can i not see the audio on the timeline; how do i cut on the beat; audio lane; see the song
related: sequencer/shots, sequencer/actors, sequencer/overlays, sequencer/timeline-automation, sequencer/camera-direction
features: panel.sequence
---

# The Sequencer

The **Sequence** panel holds the piece in time: the song's waveform, shots, scene cuts, character
cues, lyrics and markers.

A sequence is a **value**. Editing it produces new keyframes on the ordinary parameter timeline —
that step is called the **bake** — and between bakes a sequence costs nothing at all per frame,
because it *is* timeline tracks. A baked track and a hand-authored one are the same thing.

## What a sequence contains

| | |
|---|---|
| **Scene slots** | a named node in the scene that a shot can cut to |
| **Shots** | a span of time with a camera, a scene slot and transitions |
| **Actors** | a node driven by position keys, a spline path, and animation cues |
| **Overlays** | text and shape cues over the frame — lyrics, titles, borders |
| **Markers** | section, cue and beat labels along the strip |
| **Tracks** | piece-level parameter automation |

## Time

**The sequencer stores seconds.** Every time field is in seconds; there is no beat-valued field
anywhere in it. Tempo conversion happens in the layer below, and every track the sequencer bakes is
seconds-based.

The play-head follows the audio position when a file is loaded, and render time otherwise.

Tempo itself is not authored and is not a map: it comes from the analyzer each frame, or from MIDI
clock when that is selected as the tempo source. A bar is four beats.

## Snapping

The `snap` combo offers **Off**, **Frames**, **Beats** and **Markers**. The default is **Beats**.

- **Frames** rounds to the render settings' frame rate.
- **Beats** snaps to the analyzed track's beat times, and does nothing if there is no analyzed
  track.
- **Markers** snaps to every marker that is not a beat marker.

## The strip

Rows, top to bottom: a ruler, a marker row, the audio lane, the shot lane, one lane per actor, then
the overlay lane if there are any overlays.

The **audio lane** shows the loaded song's waveform, with its filename in the corner. It sits
directly above the shots on purpose: what you are usually looking for is whether a cut lands on
something in the music, and that is only readable when the two rows are adjacent. Where the lane
goes dark, the audio has ended and the piece continues past it. It appears only when a song is
loaded.

| Gesture | Does |
|---|---|
| Click on the ruler or marker row | always scrubs — this is the one place guaranteed not to grab a block |
| Click a shot, actor lane or overlay | selects it and opens its inspector |
| Drag a block | moves it, snapped |
| Drag within 5 px of a block's right edge | resizes it |
| Drag on empty strip | scrubs |
| Right-drag | pans |
| Mouse wheel | zooms about the pointer |

A drag is sixty edits a second and a bake rebuilds tracks and layers, so **the bake waits for the
mouse button to come up.**

> [!NOTE]
> The `zoom` slider runs to 8×, but the mouse wheel will zoom to 64×. Past 8× the slider can no
> longer show or restore the value. Press **Fit** to get back.

## The toolbar

| Button | Does |
|---|---|
| **Add Shot** | appends an eight-second shot at the end, inheriting the previous shot's scene |
| **Add Lyric** | a three-second text cue at the play-head |
| **Add Actor** | choose a node to drive; a node with a rig also gets its first animation cue |
| **Add Border** | a full-frame rectangle outline, drawn under everything |
| **Sections** | fold the analyzed track into sections and beats, and label the strip. Needs audio |
| **Import Lyrics...** | LRC, SRT or WebVTT |
| **Rebuild** | bake again — happens automatically after an edit; this is for after a scene change |

The toolbar reports the track, key and layer counts of the last bake. If any baked track names a
parameter the current scene does not have, it says so in red and lists them on hover. That is worth
reading: an unresolved target is silently skipped, and a shot that appears to do nothing is usually
a shot whose target did not resolve.

## The bake replaces, it does not accumulate

An install **owns** every parameter path its bake writes. The next install erases those tracks
before adding its own, so editing a shot replaces its automation rather than layering another copy
on top. This is also why a sequence is not saved as timeline tracks in the project file — see
[Projects and files](help://start/projects).
