# ADR-1802: The route badges' periodic re-check is a slice of rows per frame

- Status: Accepted (eng/qa-pass-2, 2026-10-27)
- Code: `src/ui/route_row_logic.hpp` (`LivenessSweep`), `src/ui/control_panel.cpp` (the route list).
- Tests: `tests/unit/test_route_row_logic.cpp` `[adr1802]`; the live baseline (`tools/live_perf_baseline.py`).

## Problem

Talking Heads missed its 30 fps live deadline 31-32 times in every 900-frame run after the foil merge (`e8af53aa`),
against a baseline of 1, with a frame p99 of 47.5 ms against 37.7. The misses came every 30-31 frames: a 1.00 s
wall-clock period (3.640, 4.671, 5.676 ... 13.734 s), so not the music (every second beat at 118 BPM is 1.017 s). The
headless profile, which draws no UI, had none.

The Control panel's route list re-checked every route's liveness badge (ADR-902) once a second by `ImGui::GetTime()`:
all of `Registry::checkRoute` for every route in one frame. The foil merge took Talking Heads to 1,306 routes, and
that frame's CPU work pushed it past the deadline.

## Decision

A route that changed (its signature) is still re-checked at once, and a load, add or delete still re-checks
everything. The periodic refresh, which exists because a rule also reads the scene, now visits a slice of rows per
frame (`LivenessSweep`: `max(8, ceil(rows / 30))`), so a whole sweep takes about a second at 30 fps and half at 60, and
no frame pays more than its share. The set rules (a later Replace on the same target) are re-judged when a route
changed and once per sweep. What a badge says is unchanged; it can lag the scene by up to one sweep, as before.

## Consequences

Talking Heads live, LG UltraFine 60 Hz, AC power, `live_perf_baseline.py --scenes talking-heads` (3 reps):

| | misses / 900 (median) | frame p99 (median) | frames over 36 ms, gap between them |
|---|---:|---:|---|
| before (`3ea9b678`) | 32 | 47.3 ms | 38-44, every 24-30 frames |
| after | 4 | 36.5 ms | 6-17, no period |

T1-T3 pass again. Event Horizon was already clean (2 misses against a baseline of 3).
