# ADR-1980: The Live panel's audio device rescan runs off the frame

- Status: Accepted (2026-11-09), eng/app-stalls.
- Code:
  - `ControlPanel::drawLive`, in `src/ui/live_panel.cpp`;
  - `ControlPanel::liveAudioScan_`, in `src/ui/control_panel.hpp`;
  - the diagnostic `AVGEN_SLOW_FRAME_MS`, in `src/app/application.cpp`.

## Context

Live, a `ui.build` spike of 5–9 ms arrived every ~90 frames. It landed on top of a slow frame often enough to drop
it. The Live panel rescans its device lists every 3 s:
- the audio capture devices take 3–9 ms, because `listCaptureDevices` initialises a miniaudio/CoreAudio context each
  time;
- MIDI inputs take under 0.1 ms;
- displays take under 0.1 ms.

## Decision

The audio rescan runs in a `std::async` task. Its result is swapped in on the first frame after it lands, and the
previous list is shown until then. `forEachCaptureDevice` builds and tears down its own context, so the call is safe
off the main thread. The MIDI and display scans stay on the frame because they cost nothing. A new scan starts only
when the last one has finished.

## Consequences

Empty-scene control (live, 30 fps, 30 s, frames over 36 ms logged by `AVGEN_SLOW_FRAME_MS`):

| | Slow frames | Slow frames with `ui.build` ≥ 3 ms | Frames on time |
|---|---|---|---|
| Before | 19 | 7 | 99.7% |
| After (with ADR-1981) | 2 | 1 | 99.9% |

The first rescan's list arrives a frame or two after the panel first draws.
