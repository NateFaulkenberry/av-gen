# ADR-1362: Paced to the target, the live quality level is decided by missed frames, not the GPU time

**Status:** Accepted (live performance follow-ups; the owner's "fix it" on the 30 fps report's first risk).
**Date:** 2026-10-15

## Context

THE RIFT LIVE's 30 fps work (ADR-1344, `docs/prototypes/bioluminescent/live-30fps-report.md` §6) found the live
controller settling a level lower than the hardware holds at a 30 fps target. The suspected cause was the GPU clock:
with ADR-1107's frame cap on, the GPU idles part of every frame and Apple silicon lowers its clock, so the GPU
timestamps the controller reads come out longer.

Measured here (`docs/development/live-controller-30fps.md`, per-frame traces from the new `--live-profile --trace`):

- **The span tracks the period, not the level.** THE RIFT LIVE at 30 fps read 24-26 ms at High, 24-28 at Medium,
  20-26 at Low and **29-30 ms at Emergency** (half the pixels, every effect off), with every frame on time in each.
  The clock governor slows the GPU until the busy time fills most of the period.
- **So the law walked down on noise.** ADR-1085's law drops a level when the 20-frame median span exceeds the budget
  (29.3 ms at 30 fps). Readings hovering at 85-95% of the period crossed it every few seconds. Main's controller left
  High at 10 s with no frame missed, and in four of six minute-long runs it went on to Low or Emergency, again with no
  frame missed. Its climbs (ADR-1107's 90% margin) were rare and undone by the next spike.
- **Two GPU-based fixes were tried and did not help.** One raised the margin to 100% of the budget while frames were
  on time. The other probed up when the window was clean, but still dropped on the span. In the same interleaved runs
  both ended at Low like main, with 5-10 level changes a minute. A clock-correction factor cannot be learned from a
  reading that does not depend on the load. Neither Metal nor Dawn reports the GPU clock; IOKit's
  `PerformanceStatistics` gives utilisation only, which is the span again.

What the governor cannot inflate is a missed deadline: a frame that took a display refresh longer than the target
allows.

## Decision

While the live loop is paced (`InteractiveResolution::setPacing`, set each frame by `Application::runLive` from
`liveFrameCapPeriodMs` and the display's refresh), `note()` takes its decision in `decidePaced`:

1. **A miss.** A frame is late when its interval is longer than the period by a quarter of the period and at least
   half a refresh. That is a frame that landed on a later vsync. It is not the vsync count alone: the interval is
   taken on the main thread, and the cap's wake-up jitters by several milliseconds. With the editor on a 120 Hz
   screen, a half-refresh rule read 37.5-39 ms intervals as misses and walked a light scene to Emergency.
2. **The GPU's miss.** A late frame counts only when a GPU span within a few frames of it reached the period.
   The span arrives two or three frames after its frame, so the frame waits up to five frames, and the frame before
   counts too. The measurements separate cleanly. Every late frame on THE RIFT LIVE, at High, Medium and Low, had
   a span of 1.0-1.4x the period near it. The late frames of Neon Vector at 60 fps, which came from the cap's wake-up
   and the main thread, had spans of 0.80-0.85x, because the governor holds a level with room just under the period.
   A GPU-share test on the window's median cannot tell these apart, for the same reason. The main thread's late
   frames never move the level (§8). They are counted in `stats().lateNotGpu`. One span over twice the period, while
   the window's median fits, is a stall (a compile, an upload, another process) and does not count either. Neon Vector
   World at 60 fps dropped from High four times in a minute on single 40 ms spans with a 14 ms median. A level that is
   far over every frame keeps its median over the period, and its late frames still count.
3. **Down.** A level goes down one step when at least `missesToDrop` (3) of its last `missWindowFrames` (300) frames
   were the GPU's misses. At 30 fps that is 1% over ten seconds: a stray hitch is not enough, but a level that loses
   a frame every few seconds is left. Only the level's own frames count, not the first three after a move.
4. **Up.** One level, after the usual dwell and `raiseHoldFrames`, with no miss in the level's recent window. The span
   is not consulted.
5. **A raise that misses is undone at once.** A raise stays on probation (ADR-1104) until it has held as long as it
   waited, and during that time its first miss reverts it, on that frame rather than at the next dwell.
6. **Backoff.** Every level left on its misses waits twice as long before it is tried again, up to 16 times the hold
   (about a minute at 30 fps; ADR-1104's 8x is unchanged when not paced). One case is different. While frames are
   missing, the GPU is busy all the time, so the span is the level's cost at full clock. A level whose span read more
   than 10% over the period is not on the edge, and it waits the full 16x at once.
7. **No learned ratios while paced.** A ratio taken from governor-made readings would steer the unpaced climb with
   noise, so steps are not measured while paced.

Not paced (the cap off, a target above the refresh, the headless runner): unchanged. The budget function, the ladder,
the dwell, the hold and the CPU-bound hold are the same.

Panel: capped, the GPU figure is shown but no longer turns orange over the budget, and a "missed N" count follows it.
`LIVE TARGET UNSUSTAINABLE` and "At the lowest quality..." say "still missing frames" instead of quoting a GPU time.
The "Cap at target" tooltip says Auto goes by missed frames.

Tooling: `--live-profile --mode live --trace <csv>` writes one row per frame, warm-up included: the interval, the
span, the level, the scale, the controller's reading and the cap's period.

## Consequences

- At 30 fps, THE RIFT LIVE settles where the frames hold rather than where the governor's reading happens to sit.
  The before/after table, over interleaved 75 s runs per scene and target, is in
  `docs/development/live-controller-30fps.md`.
- Probing is the only way to learn that a level above has room, so a level on the edge is visited now and then, and
  each failed visit costs a missed frame or two. The backoff makes these visits a minute apart at most.
- Unit tests (`tests/unit/test_interactive_resolution.cpp`, tag `[adr1362]`) close the loop on a model of the
  governor: a span inflated to the budget with every frame on time never moves the ladder, and the unpaced law, the
  paired arm, does move it. A level that holds is climbed to, slowly. Real misses descend, and CPU misses do not. A
  steady RIFT-shaped load settles on the highest holding level with at most six raises in three minutes. 60 on
  120 Hz behaves the same. The existing tests are unchanged.
- The live profile's `deadline misses` and this controller now count the same kind of event.

## Revisit triggers

- A platform that reports the GPU clock or a present timestamp. Present timing would replace the main-thread
  interval as the miss signal.
- A machine whose governor does not fill the period. Nothing breaks, but the span would be worth consulting again.
