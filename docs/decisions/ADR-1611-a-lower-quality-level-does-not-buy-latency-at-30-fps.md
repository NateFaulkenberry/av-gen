# ADR-1611: A lower quality level does not buy latency at 30 fps, so there is no per-project latency budget

- Status: Accepted (eng/latency-levers; ADR-1605's third lever, **rejected on measurement**). Nothing is built.
- Evidence: `~/Desktop/av-gen-review/85-latency-levers/L2/` (`rw-ll-on*`, `rw-medium`, `rw-low`).

## Problem

ADR-1605 proposed a per-project latency budget (`live.latencyBudgetMs`, or a ladder priority that prefers lower GPU
time) on the assumption that a lower level shortens the GPU span, estimated at 6-12 ms on Rainbow World.

## Measurement (Rainbow World live, 1080p30, AC, ADR-1609 and ADR-1610 on; one level pinned with `--quality`)

| level | GPU span p50 / p95 per hit | `response.onset` light p50 / p95 |
|---|---|---|
| Auto (Ultra 93%, High 7%) | 24.6 / 30.1, 23.4 / 30.0 (2 runs) | 59.1 / 73.9, 60.2 / 74.0 |
| Medium (pinned) | **28.5 / 30.7** | 66.0 / 79.4 |
| Low (pinned) | **28.4 / 31.3** | 60.1 / 81.2 |

The span does not fall with the level: it rises. Paced at 30 fps, Apple's GPU clock governor slows the GPU until the
busy time fills most of the period, whatever the work (ADR-1362 measured THE RIFT at 29-30 ms at Emergency). A budget
that lowered the level would change a scene's look and give no latency.

## Decision

No latency budget. The knob would be a control that does nothing (the silent no-op family). What would shorten the
span is a GPU kept at a higher clock -- a shorter period (60 fps, where the governor's fill is ~14 ms, not available
under the owner's 30 fps live target) or work that keeps the GPU busy, which is waste -- and neither is a per-project
setting. Recorded so the lever is not proposed again without a new measurement.
