# ADR-1025: Live Sonic input feeds the same character and context: a timbre tap on the analysis thread, a live note track, and a Live panel

**Status:** Accepted (Sonic Garden, second brief PARTS 9-14).
**Date:** 2026-09-30
**Context documents:** `docs/prototypes/sonic-garden/01-brief-live.md` (PARTS 9-14, 19),
`docs/prototypes/sonic-garden/LIVE-QUICKSTART.md`, ADR-1020.

## Context

ADR-1020 built the Sonic system from files: a timbre post-pass over the whole track's stored spectra at audio
load, a `SonicRuntime` in `SignalClock` walking those frames on the hop clock, and a note track read from a
Standard MIDI File. The owner's goal is to play it: "launch the main AV Gen application, connect my MIDI keyboard
and synthesizer, play notes, change the synth's sound, and watch AV Gen generate live-reactive visuals".

The application already had the pieces of a live path: CoreMIDI input through `ControlHub` (every source by
default, with hot-plug), `audio::AudioInput` feeding an `AnalysisStream`, and an `AnalysisRunner` thread
producing analysis frames into a triple buffer. None of it reached the Sonic runtime.

## Decision

1. **Timbre is a tap on the analysis thread.** `analysis::FrameTap` is a new hook on `AnalysisRunner`, called with
   every frame in order, after the beat fields and before publishing. `sonic::LiveTimbre` implements it.
   - It runs `TimbreAnalyzer::analyze`, the function the load-time pass runs, on that thread.
   - It pushes a `TimbreSnapshot` (the features, the hop and a host timestamp, about 100 bytes) into a fixed
     512-entry SPSC queue. The queue takes no lock and allocates nothing, and drops the newest when full.
   - The audio callback is unchanged: it still only downmixes and writes the existing ring.
   - Every live runner gets a tap. It is dormant (one atomic load per frame) unless live input is on.
2. **The character steps on the render thread from the queue.** `sonic::LiveSonic::frame` drains the queue and
   calls the same `SonicRuntime::step` once per analysis frame, with dt set to the hop, which is ADR-1020's hop
   clock.
   - A render thread that skips frames still steps every analysis frame.
   - The render thread's cost is about 1 us per frame. The timbre's cost (measured at about 0.1-0.25 ms per
     analysis frame) stays on the analysis thread.
   - The live runtime is a separate `SonicRuntime` owned by the session, not `SignalClock.sonic`, so a transport
     loop or seek replay cannot clobber live history. While live input is on, `advanceClock` skips the file walk
     and the session publishes instead.
3. **Notes go into a `NoteTrack` on the live clock.** `sonic::LiveNotes` takes note on and off, velocity, channel,
   the sustain pedal (CC 64) and all-notes-off (CC 120 and 123).
   - A held note's duration is kept at "held so far plus 1 s", so the same `contextAt` and `eventsBetween` read it.
     A note-off sets the real duration, never past the release instant.
   - Old notes are pruned beyond twice the longest context window.
   - The live clock is host time (`mach_absolute_time`, the clock CoreMIDI stamps messages with), counted from
     enabling. A message is timed by its own timestamp, clamped into the interval the frame publishes, so
     inter-onset timing (rhythm, regularity) keeps sub-frame accuracy.
   - `ControlHub::applyMidi` hands note messages to the session when it is on. Bindings still see every message.
   - The note keeps a float velocity and pitch and the channel and key, so MIDI 2.0 velocity and per-note pitch
     (MPE) fit later. Pitch bend is not used yet.
4. **The entry point (PART 9) is the smallest one that makes it the application's own:**
   - `Engine::setLiveSonic(bool)`, live mode only (an error in an offline engine, so a render never sees it).
   - A project whose `sonic` block has `"live": true` turns live input on when the live editor opens it, and every
     other project turns it off. So existing projects behave exactly as before.
   - A **Sonic Live** example (`examples/sonic-garden/sonic-live.json`, written from the garden master by
     `tools/sonic_live_project.py`) is in the Examples menu. The **Live** panel (View > Live) has an "Open live
     demo" button.
   - `--live`, `--midi <filter>` and the existing `--input <device>`.
5. **The audio source is whatever the live analysis runner hears.** That is the chosen input device, or else the
   playing audio file. The device and MIDI choices and the smoothing are this machine's (`settings.json`, `live.*`),
   not the project's. Sensitivity is the existing `audio/inputGain` parameter.
6. **The Live panel (PART 13)** has:
   - audio and MIDI pickers;
   - "receiving" lights (a message, or a frame above -60 dBFS, in the last 0.5 s) with a level meter and the
     last note;
   - Enabled;
   - Anti-aliasing (ADR-1024's `general.liveAntialias`, the same setting as Settings);
   - Sensitivity (input gain in dB) and Smoothing (a multiplier on the character's time constants, 1 = the
     project's tuning);
   - the measured MIDI-to-frame and analysis-to-frame latencies, the timbre cost and dropped snapshots.
7. **The capture period** of `AudioInput` is 128 frames rather than miniaudio's default 10 ms (480). A/B on a
   BlackHole loopback, 48 notes per arm: audio to bus p50 27.4 to 23.8 ms, p90 35.8 to 33.2 ms.
8. **No latency compensation.**
   - Measured on this machine (the probe below, 48 notes): MIDI to bus p50 13 ms (p90 23), and to the frame's
     present p50 18 ms. Audio to bus p50 24 ms (p90 33), and to present p50 29 ms. The frame's GPU work and one
     display refresh follow present.
   - The note gesture therefore leads the timbre response by about one frame, so the note is seen first and its
     sound's character follows it: the order a player expects.
   - Delaying MIDI to align the two would slow the part that makes the instrument feel immediate. The analyzer's
     centred 2048-sample window (21 ms of group delay) is most of the audio path, and shortening it for live would
     break live/file equivalence.

## Consequences

- Live and file differ only in where frames and notes come from.
  - A test streams audio through a real `AnalysisRunner` thread, the tap and the queue, and reaches the file path's
    character to 1e-6 on every dimension and both tiers.
  - Live notes give the same `MusicalContext` as the same notes read as a file, at every 1/60 s.
- An unused live mode does nothing: the tap is dormant, the file path is unchanged, and no project turns it on
  except one that says `"live": true`.
- The garden's art needs no change for live input, because it reads only bus signals. Its tuning for live play
  (the register split, `mass`, the family sharpness, the echo) is the art agent's (PARTS 15-16).
- The distortion test shows `sonic.roughness` rising 0.01 to 0.28 while the families stay organic-led, and the art
  agent owns that mapping.
- `tools/avgen_sonic_probe` provides the test equipment:
  - a CoreMIDI virtual source plus a small synth played into BlackHole;
  - `--sonic-live-log` (one CSV row per frame, host times plus every sonic, timbre, notes and visual signal);
  - `--live-capture` (a small re-render per frame for review clips; it halves the frame rate);
  - `tools/sonic_live_latency.py`, `sonic_live_curves.py` and `sonic_live_clip.py`.
- Tests: `tests/unit/test_sonic_live.cpp`, `[sonic][adr1025]`. The CoreMIDI case is also tagged `[device]`.
