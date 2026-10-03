# ADR-1107: The live loop is capped at the target, and climbs back at 90% of the budget

**Status:** Accepted (live optimizer; the coordinator's decision on the open 60-target tuning question). **Date:** 2026-10-03

## Context

`~/Desktop/av-gen-review/30-live-quality/REPORT.md` §4 measured the 60 target through a 1080p projection:

- **Uneven pacing.** Uncapped, the loop presents as soon as the GPU is done. Sonic at a 60 target showed a median
  frame interval of 9.2 ms against a mean of 15.6 ms: frames alternating roughly 9 and 25 ms. The display shows
  that as judder; a steady 60 looks better. This happened on a 60 Hz display (LG UltraFine): Fifo through the
  editor window and the projection window does not hold the loop at the refresh (72 fps mean, `A2-*` in
  31-live-optimizer).
- **Settling one level low.** At a 60 target every scene settled one level lower than it needed (72-118 fps): the
  level that would hold 60 sat within 20% of the budget, and the 0.8 raise margin refused to climb back to it. The
  picture was softer than 60 required.

## Decision

1. **A frame cap at the live target** (`general.liveFrameCap`, on by default; the Live panel's "Cap at target" next
   to Target; `--live-frame-cap on|off` for a run). `Application::runLive` waits, at the top of the frame and before
   input is read, for the frame's slot on a fixed grid:
   - The period is a whole number of display refreshes, the most that is still at least the target rate
     (`liveFrameCapPeriodMs`): 60 on 120 Hz is every 2nd vsync; 60 on 60 Hz every vsync; 60 on 144 Hz every 2nd
     (72 fps, not an uneven 2-3 alternation). Unknown refresh: the target's own period. Fifo then lands each
     present on the right vsync. The refresh is the projection's display while projecting, else the editor's.
   - **Equal to the refresh is capped; above it is not.** The display is the lower ceiling above it. At equality
     the cap is needed, because Fifo here does not hold the loop at the refresh (above). This departs from the
     brief's "never when the target is at or above the refresh", on that measurement.
   - The grid advances by whole periods (`livePaceStep`), so an oversleeping wake-up does not lengthen the next
     period; a frame later than half a period restarts the grid instead of bursting to catch up.
   - **Live editor only.** `runHeadless` never paces; neither does the editor while a render job shares the loop,
     nor an `--ui-ab` measurement.
   - **The controller's reading stays a cost.** ADR-1085 caps the GPU span by the mean frame interval. A capped
     interval would read 16.7 ms however light the GPU is, and the controller's CPU-bound check (GPU under 70% of
     the wall clock) would read the idle wait as the CPU. So the controller, and the status line's GPU figure, are
     fed the interval minus the cap's wait. The live profile's frame statistics keep the real present interval and
     count the wait among the waits.
2. **The raise margin goes from 0.8 to 0.9.** The level above must be predicted at no more than 90% of the GPU
   budget for `raiseHoldFrames` before the controller climbs. A raise that then misses is undone at its next
   decision, and the same raise waits twice as long next time (ADR-1104's probation): that is the safety net that
   makes the smaller margin acceptable.

## Consequences

- At a 60 target the loop presents at a steady 60 where the GPU allows it, instead of 72-118 fps with alternating
  intervals, and the settled level can be the one that holds 60.
- A scene that can't hold the target is unaffected by the cap (no frame waits). The cap never touches a render.
- Tests: `the frame cap's period ...`, `the frame cap's steps hold a fixed grid ...`, `the raise margin is 0.9 ...`
  (tests/unit/test_interactive_resolution.cpp) and `the live frame cap round-trips and defaults on`
  (test_app_settings.cpp).
- Measurements: `docs/live-optimizer/PROGRESS.md`, "ADR-1107".
