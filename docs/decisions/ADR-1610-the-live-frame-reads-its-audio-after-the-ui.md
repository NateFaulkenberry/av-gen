# ADR-1610: With live input, the frame reads its audio after the editor UI is built (late latch)

- Status: Accepted (eng/latency-levers; ADR-1605's second lever). On by default with live input;
  `--live-late-latch off` restores the old order.
- Code: `src/app/application.cpp` (`runLive`: the UI block is `buildUi`; with live Sonic input it runs after the late
  input poll and before the seek service and `Engine::update`, else where it was; `--live-late-latch`).
- Evidence: `~/Desktop/av-gen-review/85-latency-levers/L2/` (both arms with ADR-1609 on).

## Problem

The engine update takes the newest audio analysis, MIDI and signals at its start, and the editor UI (ImGui build and
record: 7.3 ms on Rainbow World, 01 §4.3) was built after it, so every hit waited for the UI before the frame was
recorded and submitted. ADR-1605 measured update -> present at 12.8 ms on Rainbow World.

## Decision

While live Sonic input is on, the frame builds its UI first, then services the seek and runs the update, then records
and submits. The update -- and with it the audio, the routes and the signals -- is as late in the CPU frame as it can
be without splitting it (everything after it depends on its routes). The UI therefore shows the previous frame's engine
state (its meters always lagged a frame) and its edits reach this frame's update rather than the next. Nothing about
the update itself changes, so seek = play and offline determinism are untouched: this is the live editor loop only,
and headless and offline renders never run it. Off, as before, for scripted UI runs (`--ui-script`), `--capture-ui`,
`--ui-ab` and while a render job shares the loop, which measure or depend on the old order.

## Evidence (M2 Max, 60 Hz, live 1080p30, Auto, AC; `--live-late-latch off|on`, ADR-1609 on in both)

| | off | on |
|---|---|---|
| Rainbow World update -> present p50 (per hit) | 11.0, 11.3 ms | **4.8, 4.7 ms** |
| Rainbow World `response.onset` light p50 / p95, 2 runs pooled | 70.4 / 89.3 | **59.7 / 73.9** |
| Event Horizon update -> present p50 | 3.0 | 1.9 |
| Event Horizon light p50 / p95 | 51.2 / 67.5 | 47.5 / 63.6 |
| deadline misses per 2,700 frames (RW, RW, EH) | 1, 3, 1 | 3, 3, 1 |

The saving is the UI's time: about 6 ms on Rainbow World (the p95 gain is larger than the p50's partly because the
off arm's runs landed at worse frame phases), about 1 ms on Event Horizon whose UI is light. The miss counts are within
the baseline's spread (one in 900); the live baseline is re-checked with both levers on (ADR-1608).

## Consequences

- The editor's UI reads one frame late while performing; no panel depends on the same frame's update.
- Together with ADR-1609: Rainbow World impacts 73.1 / 91.3 ms (ADR-1605) -> 59.7 / 73.9; Event Horizon 51.7 / 69.0 ->
  47.5 / 63.6. T10 (45 / 60) is still not met.

## The live baseline with ADR-1609 and ADR-1610 on (2026-10-23)

`tools/ci/live-perf.sh` against the committed baseline **failed** (misses 10-25 per 900 where the baseline has 1-3).
The cause is the machine, not the levers: the 60 Hz LG UltraFine the baseline was taken on had been disconnected, so the
projection ran on the built-in 120 Hz display, where ADR-1600's counter rounds intervals to 8.3 ms vsyncs and counts
every interval over 37.5 ms. The baseline is therefore not comparable and was **not** updated. What was measured
instead is an interleaved A/B on that display (`~/Desktop/av-gen-review/85-latency-levers/ab-120hz/`; `--live-only`,
three reps per arm alternating arm by arm, 60 s gaps for the other agents, AC on every run):

| misses per 900 [reps] / p99 / Medium or better | levers off | levers on |
|---|---|---|
| Rainbow World | 28 [30, 28, 28] / 38.5 / 100% | **23** [23, 25, 20] / 38.7 / 100% |
| THE RIFT LIVE | 11 [5, 11, 15] / 38.0 / 70% [70, 70, 28] | 12 [15, 11, 12] / 39.2 / **39%** [36, 39, 46] |
| Event Horizon | 21 [32, 11, 21] / 39.0 / 100% | **5** [11, 5, 1] / 36.8 / 100% |

Misses are no worse with the levers (fewer on Rainbow World and Event Horizon). THE RIFT's share at Medium or better is
lower in this sample, but both arms follow the same drops (Ultra->High in warm-up, Medium->Low at the song's heavy
section 13-14 s in); the arms differ in whether a later raise held, and the off arm's own reps span 28-70%. Three reps
cannot separate that from noise; it should be re-measured on the 60 Hz display with `live-perf.sh` before the baseline
is moved.
