# ADR-102: The transport owns the timeline position; audio follows it

**Status:** accepted
**Date:** 2026-09-12
**Context:** The global transport brief; completes the `Transport` that ADR-012 specified and nobody built
**Audit:** `docs/architecture/transport-audit.md`

## The problem

ADR-012 wrote this class down in the time model and then it was never written:

> **Timeline transport.** `Transport` owns play/pause/seek and the mapping between `renderTime` and
> audio position.

`Engine` absorbed the responsibility instead, and delegated the state to `AudioPlayer`. So for three
milestones:

```cpp
bool Engine::isPlaying() const { return player_ && player_->isPlaying(); }
double Engine::durationSeconds() const { return audioFile_ ? audioFile_->durationSeconds() : 0.0; }
timelineClock_.seconds = audioFile_ ? positionSeconds() : time.renderTime;
```

**"Playing" meant "the audio device is running."** Everything followed from that:

- A project with **no audio could not be played, paused, seeked or stopped at all**. `play()`
  returned "no audio loaded"; the timeline followed the free-running render clock, so a seek was
  overwritten on the very next frame. It ran from process start, forever.
- A project **with** audio was bounded by the wav: a 45-second sequence over 30 seconds of audio
  stopped at 30.
- There was no loop, no playback rate, no project frame rate, no frame or beat stepping, no marker
  navigation and no timecode.

## The decision

**`app::Transport`, owned by `Engine`, owns the position and the playing state. Audio follows it.**

That inversion is the whole ADR. Everything else is consequence.

It is not a new service beside the engine. Every caller — the panels, the keyboard, the OSC/MIDI
control map, the AI tools, the UI script driver, the render job — already went through
`Engine::play/pause/stop/seekSeconds/isPlaying/positionSeconds/durationSeconds`. Those signatures are
unchanged, so every call site migrated for free and there is exactly one place that can answer "am I
playing".

It is also **not a clock**. ADR-012's rule stands: no engine code outside `RealtimeClock` reads a
system clock. The transport is *told* how much time passed and decides what that means for the
position, which is what makes the state machine testable without a device, a window or a thread.

## The two times, named at last

The engine has always had two and nothing said so. Now something does:

| | drives | while parked |
|---|---|---|
| `FrameTime::renderTime` | `deltaTime` integration, ambient motion, wind, water, shader layers | keeps running |
| the transport position | `Timeline::apply`, cues, baked sequence animation, overlays, events | frozen |

They are equal while the transport governs time — playing, or offline. A subsystem *timed by the
piece* reads the transport; one *animated continuously* reads `renderTime`. A world keeps breathing
in the editor with nothing playing, and that is deliberate rather than accidental.

## Audio is the master clock, and only at 1x

While the device is running the transport **reads the play-head** rather than integrating a delta, so
a device that jitters or stalls cannot make the picture drift away from the sound. That is ADR-012's
rule, unchanged. With no audio — the case that did not exist before — it integrates `deltaTime *
rate`.

`AudioPlayer` has no rate control. So at any rate but 1x the device is **paused and the picture runs
alone**, and the bar says so. A device left running at 1x under a 2x transport drifts a second out
every second, which is worse than silence and much harder to notice. Faking it was explicitly
refused.

**One audio source, for now.** `seq::Sequence` has no audio model and `Engine` holds one
`audioFile_`; multi-clip audio is a feature that does not exist (audit §3), not a transport bug. The
transport is written so it does not assume one file: it owns the position and audio is a follower
told where to be, through one narrow call. Adding clips later means a second follower, not re-opening
this class.

## Time is a double, and a frame rate is a rational

The position is **double-precision seconds**. Three hours at 59.94 is 647,000 frames; a float would
have lost a whole frame by the twenty-minute mark, and the audio player is already sample-indexed
underneath, which is the representation that actually survives seeking.

A frame rate is `{numerator, denominator}`. **29.97 is 30000/1001 and not 29.97** — a
double-precision 29.97 drifts a frame over a six-minute piece, which is the length of the pieces this
engine renders. `FrameRate::fromFps` snaps to the standard rationals so a hand-typed 29.97, a
29.97002997 read from a file and 30000/1001 are one rate rather than three.

Conversion, in one place: **frame 0 is time 0; frame N starts at N × denominator / numerator.**
`frameOf` floors, so a position inside a frame is that frame and stepping forward from halfway
through frame 7 lands on 8. Timecode is counted in frames throughout rather than seconds-then-frames,
because at 29.97 the second boundary and the frame boundary are different places. **Non-drop**: NTSC
rates label the frame correctly and let the label drift from wall time. This engine renders picture
and has no broadcast timecode to match; drop-frame done wrong is worse than drop-frame absent.

**The project's frame rate is `RenderSettings::fps`.** Not a second number: the frames a person steps
through have to be the frames the project exports, and two numbers that are nearly always equal are
two numbers that will one day not be.

## Stop, loop and the end of the piece

**Duration is the project's** — the longest of the audio, the baked sequence and the timeline — so
the transport has an end to stop at and a project with no wav still has a length.
`audioDurationSeconds()` is there for the places that genuinely mean the file. A duration of **zero**
means "nothing has said how long this is" and the transport is then unbounded, which is what the
engine did before it had one.

**Stop parks at the start of the play range** (the loop start with a loop on, else 0), which is what
`AudioPlayer::stop` — pause, then seek 0 — already did. `stopInPlace` is the other one people mean.
**Play from the end starts again** rather than playing nothing.

**A loop wrap is a crossing, not a comparison.** It fires when a step takes the playhead over the
loop end from at or before it, so a playhead parked past the loop plays on to the end of the piece
instead of being yanked backwards into a range it was never in. The wrap is a **modulo**, not a jump
to the start: one step can overshoot a short loop by more than its length, and discarding the
overshoot loses time on every lap.

A wrap is a discontinuity like any seek, and goes through `Engine::seekSeconds`, which is what
already resynchronises the modulator, the sources, the music classifier, the beat clock, the cue
state, the entity world (ADR-093) and the event scheduler (ADR-098). The second lap is the first lap
because it is the same code path a scrub takes.

## Offline is untouched, and now says so

Offline, the `FixedStepClock` is the authority and the transport records what it said: **no clamp, no
loop, no end rule, no rate.** A render of 0..120 s against 30 s of audio renders 120 seconds, and a
loop somebody set while previewing cannot become part of an export. `Engine::update` is what records
it, not `Engine::tick`, because an offline caller may build its own `FrameTime` — the headless
benchmark and several tests do — and a timeline that only advanced for callers who used the right
entry point would be a trap.

Seeking already worked by pure re-evaluation (ADR-089 bakes sequences to timeline tracks precisely so
that 10s → 45s → 3s → 30s is four pure evaluations), so nothing here needed a reconstruction path.

## Transport state is not an edit

Play, pause, stop, seek, loop and rate do **not** enter the undo history (ADR-101). They are how you
are working, not what the piece is. What *is* persistent is the **loop range**, saved with the
project under an additive `"transport"` key — a project that never set one gains no key and
round-trips byte for byte. Loading a project **clears** the loop rather than inheriting one from
whatever was open before, and parks the playhead at the new piece's start.

## Consequences

**Good.** One place answers "am I playing" and "where are we". A project with no audio is a project
like any other. A sequence longer than its audio plays to its end. Loop, rate, frame stepping, beat
stepping and timecode exist at all, and every one of them arrives through the same calls the OSC map
and the AI tools already make, so a new surface gets them for nothing.

**Bad.** `Engine::tick` now has a branch nobody had to think about before — following audio versus
integrating — and the cost of getting it wrong is a drift that is invisible for the first minute. It
is one function with one comment explaining the rule, and the integration tests measure both sides.

**Watch for.** `refreshTransport()` is called from `tick`, from `play`, and from the three places
that change the project's length. A fourth such place that forgets it will be a frame late rather
than wrong, which is a bug that hides. The moment a second thing can change the duration on a
schedule of its own, this wants to be a notification rather than a poll.

**Not built.** Multi-clip audio (§8 of the brief) — the model does not exist. Marker navigation past
what `seq::Sequence::markers` already holds. Real-time pitch-correct rate change. Drop-frame
timecode. Each is recorded in the audit or the docs with what it would take.
