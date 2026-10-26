# ADR-1721: `--sonic-trace` writes the frame's events, and the mix analysis

- Status: Accepted (eng/audio-response-lab)
- Amends ADR-1571 (the trace's stem and envelope columns) and ADR-1602 (`--trace-finals`).
- Code: `src/app/sonic_trace_cli.cpp`; `src/app/application.cpp` (`writeLiveSonicLog`).
- Tests: `tests/integration/test_sonic_trace_stems.cpp` `[adr1720]`:
  - `stem.drums.onset`, `stem.drums.kick`, `response.onset` and `beat.pulse` have rows that fired;
  - the kick column fires about once per fixture kick, not on every frame;
  - `audio.midLevel`, `audio.tempoConfidence` and `audio.onset` are columns.

## Problem

An event lives one frame, and `Engine::update` clears every event (value and flag) at its end. The trace read the bus
after the update, so **every event column read 0 on every row**:
- `response.onset/low/note`;
- `stem.<k>.onset/kick/snare/hat`;
- `beat.pulse`;
- `music.*` events.

ADR-1571 traced events through `env.*` envelope sources instead. Before ADR-1601 those carried a frame of lag and lost
the peak. The trace also left out `audio.*`, so the mix's band levels and tempo confidence could not be traced at all.

## Decision

- The trace turns on the engine's frame-event copy (`setKeepFrameEvents`, ADR-1605's, taken just before the clear).
- An event column writes the strength the event fired with on that row, else 0. An event that fired with strength 0
  reads 0.001, as in ADR-1605.
- Value columns are unchanged.
- `audio.*` and `control.*` join the column set.
- `--sonic-live-log` (ADR-1605) writes `audio.*` whole, where it wrote only `audio.onset*`, so the live log carries the
  tempo confidence the lab scores.

## Consequences

- Any earlier analysis that read event columns from a trace saw nothing. The ones known to have used `env.*` instead
  were correct.
- Traces are wider: about 70 more columns.
