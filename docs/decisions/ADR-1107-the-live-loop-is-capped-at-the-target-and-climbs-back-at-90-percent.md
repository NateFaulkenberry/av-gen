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
   - **The controller's reading stays a cost.** ADR-1085 reads min(median GPU span, mean frame interval). The
     interval only ever caps the span, so a padded 16.7 ms interval cannot be read as cost; it only stops capping.
     And when the cap waits, the GPU has finished the previous frame before the next is submitted, so the span is
     not inflated by overlapping frames, the case the interval cap exists for. The controller is therefore fed the
     whole interval, as before. **Tried and rejected:** feeding it the interval minus the cap's wait. The GPU works
     *during* the wait, so that read 3-5 ms on a 12 ms GPU frame; the controller climbed and fell back every 4-5
     seconds (Glowmere and Liminal Medium/Low x3, Sonic 11 changes in 15 s; ADR-1104's doubling was all that
     slowed it). The live profile's frame statistics keep the real present interval and count the wait among the
     waits.
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
- **Measured** (M2 Max, LG UltraFine at 60 Hz, 1080p projection window, 60 target, floor 0.38, 15 s, two
  interleaved repeats per arm; `docs/live-optimizer/PROGRESS.md`, "ADR-1107"):

  | scene | before (0.8, no cap) | 0.9, no cap | 0.9 + cap |
  |---|---|---|---|
  | Sonic | Medium, 72 fps, p50 8.8-9.2 / p99 28.6-28.8 ms, misses 12-13% | Medium, 72 fps, misses 12-16% | Medium, 60.1 fps, p50 16.6 / p99 21.7-23.1, misses 0 |
  | Glowmere | Low, 83 fps, p50 8.7-8.8 / p99 25.7-25.9, misses 4.5% | Low, 83 fps, misses 3.6-3.9% | Low, 60.0 fps, p50 16.65 / p99 18.6-19.3, misses 0 |
  | Liminal @60 s | Emergency, 85.5 fps, p50 8.7 / p99 25.8-26.1, misses 4% | Emergency, 85 fps, misses 3.9% | Emergency / Low, 60.1 fps, p50 16.65 / p99 20.9-21.1, misses 0 |

  - The cap does what it is for: a steady 60, with zero deadline misses where every uncapped run missed 4-16%.
  - The margin moved no settled level on this machine at this sitting: Sonic's High measures over the budget and
    the next levels up for Glowmere and Liminal are not predicted within 90% of it. One capped Liminal run settled a
    level higher (Low). The margin stays at 0.9 as decided: it is the cheaper of the two errors, and ADR-1104 bounds it.
  - Capped, the GPU spans read 5-15% longer at the same level (Glowmere Low 12.5-13.4 ms against 11.2): with idle
    time in every frame the GPU clocks down. That errs towards staying lower, never towards hunting.
  - Not measured: a 120 Hz display (none attached), where 60 is every 2nd vsync.
