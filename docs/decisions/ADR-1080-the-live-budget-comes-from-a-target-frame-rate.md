# ADR-1080: The live budget comes from a target frame rate the performer picks, with 12% headroom, in one function

**Status:** Accepted (live quality, `docs/live-quality/00-brief.md` §3-4).
**Date:** 2026-10-03

## Context

The adaptive controller (`app::InteractiveResolution`, §15-§17) aimed the GPU at `adaptiveCanvasBudgetMs`, a setting
that defaulted to 16.67 ms whatever the display. Measured through the real projection output on the owner's 120 Hz
display (`evidence-live-projection-2026-10-02.md` §C): Sonic's GPU at 0.85 scale was 15.1 ms, under that budget, so
the controller stopped there; Fifo at 120 Hz then turned every frame over 16.67 ms into 25 ms, giving 54.5 fps with
judder rather than a clean 60. The same scene ran at 78 fps at 0.71 and 96 fps at 0.5.

A budget in milliseconds is the wrong thing to ask a performer for, and 16.67 ms is the wrong default: it leaves no
room for the work the GPU timestamps do not cover (the interface, the projector's copy, the compositor).

## Decision

1. **One calculation**, `app::liveBudget(targetFps)` in `src/app/interactive_resolution.hpp`:
   `targetFrameMs = 1000 / targetFps`, `qualityBudgetMs = targetFrameMs * (1 - 0.12)`.
   60 fps gives 16.67 / 14.67 ms, 90 gives 11.11 / 9.78, 120 gives 8.33 / 7.33. The controller's settings carry
   `targetFps` and `budgetMs`; the Live panel and Settings show `targetFps`, `targetFrameMs` and `qualityBudgetMs`.
   Targets outside 24-240 fps are clamped.
2. **The target is a per-machine setting**, `general.liveTargetFps` (default 60), offered as 60, 90 or 120 in
   Settings and in the Live panel. `--live-target <fps>` overrides it for a run.
3. **The display's refresh rate is never read.** It is where the picture is shown; the target is what the show asked
   for. A 120 Hz display running a 60 fps target is a normal configuration.
4. `adaptiveCanvasBudgetMs` and `--adaptive-budget` are removed outright (ADR-441: no compatibility shims). A
   settings file that still has the key loads; the key is ignored.

## Alternatives considered

- **Derive the budget from the display refresh** (the investigation's first sketch: "the vsync multiple under the
  target"). Rejected by the brief's §4: it couples a user preference to an environmental fact.
- **Keep a millisecond slider and add presets.** Two controls for one choice, and the slider is the one nobody can
  reason about.
- **A Custom target in the UI.** The settings file already accepts any value in range; the UI offers the three the
  brief asks for, so the panel stays small.

## Consequences

- At 60 fps on a 120 Hz display the controller now aims at 14.67 ms, below the 16.67 ms Fifo boundary, which is what
  turns judder into a held rate.
- The headroom constant `kLiveBudgetHeadroom` is the single place to change if the margin proves wrong.

## Revisit triggers

- A measured case where the uncovered work (UI, projector copy) is larger than 12% of the frame.
- A performer asking for a custom target often enough to put it in the panel.
