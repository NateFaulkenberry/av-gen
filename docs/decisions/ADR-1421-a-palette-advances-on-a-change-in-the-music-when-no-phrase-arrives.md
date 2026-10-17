# ADR-1421: A palette advances on a change in the music when no phrase arrives

- Status: Accepted (2026-10-17), proto/cymatic-split (Cymatic World: the owner wants palettes to keep shifting live
  with no tempo); extends ADR-1420's selector, shared with Resonant Silk.
- Code: `PaletteChange`, `PaletteSelector::change`, the change trigger in `Palette::step`, `advanceNext`,
  `changeSignals` (`src/params/palette.*`); the seek replay's prefix check (`Engine::seekSeconds`,
  `src/app/engine.cpp`).
- Tests: `tests/unit/test_palette_selector.cpp` (`[palette][adr1421]`):
  - no primary pulse + an energy change advances, and the hold and the lock still apply;
  - two changes inside the hold advance once;
  - while the primary pulses, changes are not heard;
  - maxHold;
  - a selector without a change block is bit-identical (JSON and a scripted run);
  - JSON round trip and validation.

## Context

ADR-1420's advance listens to one bus signal. Cymatic World uses `beat.phrasePulse`, which exists only with a running
beat clock. A live scene playing whatever comes in, with no tempo, never sees a phrase, so its palette never changes
on its own. The owner wants it to keep shifting live: on phrases where they exist, otherwise on a large, sustained
change in the music (a build arriving, a drop, a breakdown), and never in rapid switches.

A scene-only answer, routes into a macro and the macro as the advance signal, cannot take an absolute value
(a build and a drop are changes of opposite sign). It would also put the advance outside ADR-870's replay prefix, so
a render started mid-track would no longer land where a play does.

## Decision

An optional `advance.change` block, a second trigger:

```json
"advance": { "signal": "beat.phrasePulse", "threshold": 0.5, "order": [ ... ],
             "change": { "signals": ["sonic.energy", "sonic.brightness", "sonic.density"],
                         "fast": 3.0, "slow": 24.0, "threshold": 0.08, "rearm": 0.04,
                         "primaryTimeout": 30.0, "maxHold": 75.0 } }
```

- **The detector.** For each signal, a fast and a slow exponential average (time constants in seconds, stepped with
  each frame's dt). The change is the sum over the signals of |fast - slow|: a slow-versus-fast novelty over several
  descriptors, not a level threshold.
- **Hysteresis.** It fires when the change reaches `threshold` and re-arms only after it has fallen below `rearm`,
  so a sustained new level fires once.
- **The primary wins.** The change trigger is heard only while the primary advance signal has not fired for
  `primaryTimeout` seconds. With a tempo, phrases arrive every few seconds and the trigger stays silent.
- **maxHold** (> 0) also advances after that many seconds with no change at all while the primary is quiet, so a long
  uniform passage still evolves.
- **The rest is ADR-1420's.** `hold`, the OKLab blend, `palette/lock` (which stops both triggers), `palette/select`
  (which wins at once), and the authored `order`.
- **Seek.** A seek replays the selector, as ADR-1420 does, when the advance signal and every change signal are in
  the replay prefix (`sonic.*` is). Otherwise it restarts at the selected state.
- **Compatibility.** Without the block, nothing changes: the JSON writes no `change` key and the primary path is the
  same code. A test runs a scripted sequence with and without an inert change block and compares the colours bit for
  bit.

## Consequences

- Live scenes with no tempo keep moving through their palettes at musically meaningful moments, at most once per
  hold.
- The detector is a few multiply-adds per signal per frame, with no allocation after the first frame.
- The thresholds depend on the descriptors' ranges. Cymatic World's were chosen from Feline Footwear, Trench and
  Hyperborea traces (11-report).
