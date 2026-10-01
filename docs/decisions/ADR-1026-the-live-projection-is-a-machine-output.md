# ADR-1026: The live projection is an output that belongs to the machine, started from the Live panel

**Status:** Accepted (Sonic Garden, the owner's request of 2026-10-01).
**Date:** 2026-10-01
**Context documents:** `docs/prototypes/sonic-garden/LIVE-QUICKSTART.md` ("Projecting to a second screen"),
ADR-1025, ADR-022 (output windows), ADR-391 (what the viewport looks through).

## Context

The owner asked for "a button at the top of the live panel to open live demo, something like 'start projection'
which opens a new window with the live workspace there ... a clean window to send to a projector - video out".

The engine already had output windows (milestone 1.2, ADR-022):
- `app::OutputManager` owns a set of `OutputDesc`s, each an SDL window with its own swapchain on a chosen display,
  optionally borderless fullscreen.
- `rendering::OutputMapper` draws the final frame through an `OutputMapping` (crop, corner warp, edge blend).
- They come from a project's `outputs` block, the `--output <display>[:fullscreen|:WxH]` flag, or the Outputs tab.
- Each frame, `presentAll` hands every open output `finalTexture_`: the canvas's own render target, after post
  and the live antialiasing. ImGui draws only into the main window's swapchain, so an output never shows UI.
- ADR-391 already makes the canvas show the film's camera while any output is open, so the canvas and the
  projection agree.

What was missing was a one-press path from the Live panel, and per-machine choices. An output in a project's
`outputs` is the project's, so a projector plugged into this Mac would travel with the project file.

## Decision

1. **The projection is an ordinary `OutputManager` output, flagged as the machine's.**
   - It is named "Live projection", and `Output::projection` is true.
   - `toJson` leaves it out, so it is never saved into a project.
   - `fromJson` (a project load) replaces only the project's outputs and keeps it, window and all.
   - A project output with the same name is refused.
   - `closeProjectOutputs` replaces `closeAll` on a project load.
   - Esc in its window closes it, like the close button. `FrameEvents::escape` is routed by window id. The key
     still reaches ImGui as before.
   - No second render, no parallel window system: it presents the same `finalTexture_` the canvas shows.
2. **The decisions are GPU-free** (`src/app/projection.{hpp,cpp}`), so the CPU suite checks them.
   - **Display:** the remembered display, by name (indices shift when a display comes or goes). Else the first
     non-primary display (the projector). Else the primary. A remembered display that is missing is reported as a
     fallback, and the setting is kept for when it returns.
   - **Fullscreen:** as remembered. Otherwise on exactly when the display is not the primary: a fullscreen window
     on the primary would cover the editor.
   - **Window:** borderless only when fullscreen. A windowed projection keeps its title bar so it can be moved
     and closed with the mouse. The Automatic size is the display's size, or half of it on the primary.
   - **Scaling:** Fit (letterbox, the default), Fill (centre crop) or Stretch (the outputs' old identity). It is
     written into the output's `OutputMapping` every frame from the canvas and window pixel sizes. Shapes within a
     pixel of each other give the identity, which is the mapper's plain-copy path.
3. **A state machine, `Projection`: Idle -> AwaitingProject -> Running -> Idle.**
   - Start on a live project (`sonic.live`) opens the window at once.
   - Start on any other project opens the Sonic Live demo through `loadAny`, so the unsaved-changes prompt
     (ADR-440) applies, and waits until no load or prompt is pending. If the project is then live, the window
     opens. If not (Cancel, or the load failed), the projection goes back to Idle with a message.
   - Opening the window turns live input on (`Engine::setLiveSonic(true)`) when it is off.
   - Running ends on Stop; on the window closing (close button or Esc), which `pumpEvents` already turned into a
     closed, disabled output; or on the display's name leaving the display list, polled once a second. On
     unplug, macOS would move a fullscreen window onto the remaining screen, over the editor, so the projection
     stops instead. An empty display list (no video subsystem) never counts as an unplug.
   - Opening another project while it runs does not stop it. The window shows whatever renders.
4. **The choices are this machine's.** They are stored in `settings.json` under `projection`: `display`,
   `fullscreen` (absent means automatic), `windowWidth` and `windowHeight` (0 means automatic), and `scaling`. They
   are read leniently, like `live`: a projector preference is never worth refusing the settings file over.
   Changing one while the projection runs reopens the window with it.
5. **The Live panel** puts the projection first:
   - a full-width Start projection button, which turns red and reads Stop projection while one runs;
   - Display (Automatic, each connected display, and the remembered one if it is missing), Fullscreen, Size and
     Fit/Fill/Stretch;
   - a status line saying where it projects and at what size, or why it stopped.
   `--start-projection` presses the button on the first frame.
6. **"Open live demo" now goes through `loadAny`.** It called `beginOpen` directly and skipped the unsaved-changes
   prompt.

## Measured

The real app on this Mac (M2 Max, one display), the Sonic Live demo with no input (the waiting world), 1200 frames
per arm, interleaved off/on/off/on in one GPU-lock hold. The canvas is 1684x1326 px. The projection is windowed at
1728x1116 px with Fit (letterbox).

| arm | 1200 frames, wall | gpu.frame p50 | gpu.acquire WAIT mean | outputs+share p50 / p99 |
|---|---|---|---|---|
| off 1 | 30.7 s | 23.8 ms | 24.8 ms | 0.001 / 0.005 ms |
| on 1 | 28.1 s | 22.6 ms | 22.2 ms | 0.071 / 4.6 ms |
| off 2 | 29.5 s | 22.7 ms | 23.7 ms | 0.001 / 0.004 ms |
| on 2 | 28.3 s | 22.5 ms | 22.3 ms | 0.071 / 4.4 ms |

- The frame is GPU-bound (about 23 ms, about 40 fps). The second window's cost is below this run's noise: the "on"
  arms were not slower.
- The CPU cost is 0.07 ms a frame at the median. The 4.5 ms p99 is the second swapchain's acquire occasionally
  waiting.
- The mapper's draw is one full-screen triangle sampling an already-rendered texture. `gpu.frame` times only the
  scene renderer's timeline, so that draw is not in that column. The wall times are what include it.
- A fullscreen 4K projector would make that draw about 4x larger in pixels. It is still a copy, not a render.

## Consequences

- The picture on the projector is the canvas's render target: its pixel size and shape follow the canvas.
  - For a 16:9 projector the canvas can be put in Output Frame mode, which renders at the project's 16:9 output
    shape, so Fit gives no bars.
  - A projector render at its own resolution, independent of the canvas, would be a second render per frame.
    ADR-391 already names that as real GPU cost and separate work. It was not built.
- The mouse cursor is not hidden over a fullscreen projection. SDL's cursor visibility is global, so hiding it would
  hide it over the editor too. The cursor appears there only when moved onto that screen.
- Mirrored displays show the editor on the projector too. The quickstart says to extend rather than mirror.
- Tests: `tests/unit/test_projection.cpp`, `[projection]`. They cover the display choice and its fallback, the
  fullscreen and size defaults, the three scalings, every transition of the state machine (start on a live and a
  non-live project, a cancelled prompt, a missing demo, a window that fails to open, closed, Esc, unplugged, Stop
  while loading), the settings round trip and leniency, and the output never entering the project while
  surviving its load.
