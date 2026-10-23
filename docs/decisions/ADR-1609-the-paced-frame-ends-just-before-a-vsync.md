# ADR-1609: The paced frame's GPU ends just before a vsync

- Status: Accepted (eng/latency-levers; ADR-1605's first lever). On by default; `--live-vsync-align off` restores the
  old phase.
- Amends ADR-1107 (the frame cap's grid): only the grid's phase moves. Its period, `livePaceStep` and ADR-1362's
  missed-frame logic are unchanged.
- Code: `src/platform/vsync_clock.{hpp,cpp}` (a CVDisplayLink per paced display: the latest vsync on the host clock and
  the measured period), `src/app/interactive_resolution.{hpp,cpp}` (`liveVsyncShiftMs`, `LiveLeadEstimate`),
  `src/app/application.cpp` (`runLive`: the clock follows the display the pace is taken from; each frame adds its
  start -> submit + the newest GPU span to the lead; the next deadline is shifted by at most 0.5 ms;
  `--live-vsync-align`; `--sonic-live-log` gains `vsyncNs`, `vsyncPeriodNs`, `leadMs`, `vsyncShiftMs`),
  `tools/sound_to_light.py` (the light is now the first vsync after the GPU's end on the logged vsync grid, not
  "+ half a refresh"; `EXTRA_ARGS` in the rig for A/B arms).
- Tests: `tests/unit/test_interactive_resolution.cpp` (`[adr1609]`: the shift's direction and clamp, the grid is the
  same from any vsync, a closed loop converges from five phases with every interval within the step; the lead's
  percentile, margin, warm-up and reset).
- Evidence: `~/Desktop/av-gen-review/85-latency-levers/L1/`.

## Problem

Under Fifo a frame is shown at the first vsync after its GPU finishes. ADR-1107's grid starts frames at a phase that
falls where the first frame fell, so the wait from GPU end to vsync was whatever that phase made it: uniform over a run
of runs (0-16.7 ms at 60 Hz), and fixed within one. ADR-1605 estimated 8 ms to be had.

## Decision

While the loop is paced, a CVDisplayLink on the paced display (the projection's while projecting) gives the vsync on
the host clock. Each frame records its lead -- wake -> submit plus the newest GPU span -- and the lead is the 95th
percentile of the last 60 plus 2 ms (the compositor's deadline, which nothing here can see). The next deadline moves by
`liveVsyncShiftMs`: towards whichever of the nearer vsyncs puts deadline + lead on it, at most 0.5 ms a frame. A frame
interval therefore differs from the period by at most 0.5 ms (ADR-1362's miss test needs a quarter of the period).
CVDisplayLink is deprecated from macOS 15; CADisplayLink delivers on the main thread's run loop, the thread whose
timing is being corrected, so the deprecated API is used on purpose (`#pragma`, one file).

## Evidence (M2 Max, 60 Hz, live projection 1080p30, Auto, AC power; same binary, `--live-vsync-align off|on`)

| | off | on |
|---|---|---|
| per frame, GPU end -> next vsync p10 / p50 / p90 (Rainbow World, all 2,700 frames) | 1.2 / **9.2** / 15.4 ms | 1.0 / **3.1** / 14.1 ms |
| Rainbow World `response.onset` light p50 / p95, 2 runs pooled (160 hits) | 70.2 / 88.6 | 66.6 / 89.0 |
| Rainbow World, per hit vsync wait p50 / p95 | 5.5 / 15.6 | 3.8 / 15.6 |
| Event Horizon light p50 / p95 (1 run) | 62.6 / 83.8 | 54.8 / 77.2 |
| deadline misses per 2,700 frames (RW, RW, EH) | 1, 2, 1 | 1, 2, 1 |

**It saves about 2-4 ms at p50 and nothing at p95, not the 8 estimated.** The typical frame now ends ~3 ms before its
vsync instead of ~9, but the GPU span's own spread (24.6 ms p50, 30.4 p95 on Rainbow World: Apple's clock governor
stretches it to fill the period, ADR-1362) means the 5% of frames past the 95th-percentile lead still wait almost a
whole refresh. Its other value is that the phase is no longer luck: a run used to land anywhere from 1 to 15 ms per
frame for its whole length. Run to run, the sound -> bus part moves by about 4 ms on its own (frame phase against the
analysis hop), so single-run differences below that are noise; the per-frame vsync wait above is the clean measure.
No misses were added.

The light here is still modelled at the GPU end (present + the GPU span: the frame timeline's timestamps are on another
epoch, ADR-1605) and excludes the compositor and scanout; the vsync grid itself is measured.

## Consequences

- Live frames are shown sooner on average and with a stable phase; nothing changes when the loop is not paced
  (no cap, a render job, `--ui-ab`, headless).
- A further cut needs a narrower GPU span (the governor), not a better phase.
