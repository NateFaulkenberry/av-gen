# ADR-1087: The Live panel shows the quality in force, and both choices it depends on are in the panel

**Status:** Accepted (live quality, `docs/live-quality/00-brief.md` §12).
**Date:** 2026-10-03

## Context

The performer needs to know what AV Gen is doing to hold the frame rate without opening a profiler. The owner's rule:
anything visible must be controllable, and named in plain words.

## Decision

A compact block under the projection controls in the Live panel (`ControlPanel::drawLive`), fed every frame by
`Application::serviceLiveQuality` through `ControlPanel::LiveQualityView`:

```
LIVE QUALITY
Quality [Auto v]  Target [60 fps v]
Now: High (auto)  scale 0.85  1632x918 into 1920x1080
GPU 13.8 ms of 14.7 ms budget    CPU 7.1 ms
```

- **Quality** (Auto, Ultra, High, Medium, Low, Emergency) and **Target** (60, 90, 120 fps) are the two settings the
  line depends on, written to this machine's settings and saved; the same two are in Settings. When the command line
  set them for the run, they are shown disabled with a tooltip that says so.
- GPU and CPU are smoothed over about a second (an exponential average) so they can be read. GPU is the controller's
  own reading (ADR-1085); CPU is the main thread's work, excluding the swapchain waits.
- **The CPU is diagnosed, never acted on (§8):** when it alone is longer than the target's frame, an orange line says
  that lowering quality cannot reach the target. At Emergency and still over budget, a line says so.
- The project's order (ADR-1084) and the number of quality changes this session are in the tooltip of the "Now" line.

## Alternatives considered

- **A diagnostics window.** The brief says not to build one.
- **The status bar only.** Not where a performer looks during a show, and has no room for the two choices.

## Revisit triggers

- A performer who wants the line on the projection screen (it is deliberately not: the projection shows only the
  picture, ADR-1026).
