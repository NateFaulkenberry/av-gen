---
id: sequencer/transport
title: The Transport
category: Sequencer
summary: Play, pause, stop, scrub, step and loop — the one playhead the whole application follows.
order: 39
audience: everyone
tags: transport, play, pause, stop, loop, playhead, scrub, timecode, frame, seek, playback
keywords: how do i play; how do i pause; nothing happens when i press play; how do i loop a section; how do i step one frame; what does stop do; why is there no sound when i speed it up; how do i see timecode
related: sequencer/overview, sequencer/timeline-automation, audio/input, reference/keyboard-shortcuts
features: panel.sequence, panel.control
shortcuts: transport.play, transport.step-back, transport.step-forward, transport.return-to-start, transport.go-to-end, transport.toggle-loop
---

# The Transport

One playhead, and everything follows it: the timeline, automation, cues, the camera, baked character
animation, overlays, sequence events and the audio.

The bar sits across the top of the **Sequence** panel, and in a shorter form in **Control**. They are
the same transport — moving one moves the other, because there is only one.

## The controls

| | Does |
|---|---|
| ⏮ | return to the start of the play range |
| ⏪ | back one beat |
| ◀▮ | back one frame |
| ▶ / ⏸ | play or pause |
| ⏹ | stop |
| ▮▶ | forward one frame |
| ⏩ | forward one beat |
| ⏭ | go to the end |
| ⟳ | loop the range over and over |

**Play** resumes from where the playhead is. From the end of the piece it starts again, rather than
playing nothing.

**Pause** freezes the piece exactly where it is. The world keeps breathing — wind, water and anything
animated continuously carry on — because those are not on the timeline. Everything that *is* on the
timeline stops.

**Stop** stops and returns to the start of the play range. With a loop on, that is the loop's start,
because that is where the next Play will begin.

**You do not need audio.** A project with no sound plays, pauses, seeks and loops like any other. Its
length is the longest thing in it: the audio, the sequence, or the timeline.

**You can have more than one audio file.** **Sequence → Audio...** places several: a song and a
spoken outro, two cues with a gap, a stem set. They are mixed into one piece, and the strip outlines
each one so you can see where the takes change. Drag a clip to move it, its right edge to trim it.

## Time display

Click the time to change how it reads:

- `1:23.456` — minutes and seconds
- `00:01:23:12` — timecode, frames after the last colon
- `2506` — frames
- `42.3` — bar and beat, when the tempo is known

The position and the length always read the same way as each other.

A **frame** is the project's frame rate, which is the one in the **Render** panel. There is
deliberately only one number: the frames you step through are the frames the project exports.
29.97 and 59.94 are kept as the exact rationals they are (30000/1001, 60000/1001), so stepping does
not drift a frame out over a long piece. Timecode is non-drop.

## Scrubbing

Drag the playhead on the Sequence panel's strip, or the slider in Control. The whole scene follows as
you drag — the camera, the automation, the characters — because evaluating the piece at a second is a
pure function of that second, not a replay of everything before it.

Seeking never fires a one-shot event that has already happened. Events are *restored* at the new
playhead rather than replayed: a character told to walk somewhere at 0:12 is still walking there when
you drop the playhead at 0:40.

## Looping

Turn the loop on with ⟳ or `L`, and set its range in the two fields beside it. Playback wraps at the
end of the range and carries on, over and over. Stop parks at the loop's start.

A playhead **outside** the range plays on to the end of the piece rather than jumping backwards into
it, so a loop you left set does not capture a click somewhere else in the timeline.

The loop is saved with the project. It has no effect on an offline render — an export renders the
range you gave it, not the one you were previewing with.

## Playback speed

0.25x to 4x. **Audio plays at 1x only.** At any other speed the sound is silent and the picture runs
alone, and the bar says so — the audio engine cannot yet change speed without changing pitch, and a
sound left playing at the wrong speed would drift a second out of sync every second. Offline renders
are not affected by this at all.

## Keyboard

| Keys | Does |
|---|---|
| `Space` | play or pause |
| `Left`, `Right` | step one frame (held: walks frame by frame) |
| `Shift` + `Left`/`Right` | step one beat |
| `Up`, `Down` | jump to the previous or next marker |
| `Home` | return to the start |
| `End` | go to the end |
| `L` | loop on or off |

Markers are the sequence's own -- the sections the **Sections** button labelled, and any cue you
added. The beat markers drawn on the strip are skipped: there are thousands of them, and "the next
marker" means the next *place*.

Beats come from the analysed beat grid when there is one, so stepping lands on the beats the music
actually has rather than on a metronome's idea of them. With no analysis and no tempo, a beat step
does nothing.

The arrow keys nudge the selection instead while something is selected in the world editor; with
nothing selected they step the transport. See
[Keyboard shortcuts](help://reference/keyboard-shortcuts).

## Offline rendering

A render does not use any of this. It evaluates frame N at N ÷ the frame rate, with no loop, no
playback speed and no dependence on how fast your machine draws — which is what makes two renders of
the same range identical. See [Rendering offline](help://rendering/offline-render).
