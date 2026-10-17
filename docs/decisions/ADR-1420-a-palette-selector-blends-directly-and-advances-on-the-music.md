# ADR-1420: A palette selector blends directly to a chosen state and advances on the music

- Status: Accepted (2026-10-16), proto/resonant-silk (Resonant Silk's palette brief,
  `docs/prototypes/resonant-silk/03-brief-palettes.md`); shared with Cymatic World's palette system.
- Code: `PaletteSelector`, `PaletteSelectorState`, `Palette::step`, `resetSelector`, `selectorColor`,
  `selectorScalar` (`src/params/palette.*`). In `Engine::update`, `step` is called before `apply`. In
  `Engine::seekSeconds`, the selector is replayed.
- Tests:
  - `tests/unit/test_palette_selector.cpp` (`[palette][adr1420]`): compatibility, direct blend, retarget
    continuity, lock, advance with hold and order, continuous edges, jump restart, JSON.
  - `tests/integration/test_palette_selector_seek.cpp`: onsets advance it in the engine, and a seek lands where a
    play does, with a control arm.

## Context

ADR-1043's palette blends along its ordered states by `palette/position`. Going from state 4 to state 2 therefore
passes through state 3. With eight or ten curated palettes, that is a rainbow sweep, which the briefs forbid.

The palette also has no notion of a chosen palette, a lock, or an automatic change on a phrase or an energy change.
Routes cannot count, wrap or remember a target.

Two scenes need all of this: Resonant Silk (8 palettes) and Cymatic World (10).

## Decision

An optional `selector` block on the project palette:

```json
"selector": { "select": 0, "transition": 4.0, "lock": false, "hold": 16.0,
              "advance": { "signal": "beat.phrasePulse", "threshold": 0.5, "order": [0, 3, 1, 2] } }
```

- **Parameters.** These are registered only when a selector exists:
  - `palette/select`: the target state, rounded;
  - `palette/transition`: seconds;
  - `palette/lock`: > 0.5 stops the automatic advance;
  - `palette/hold`: the least time between two changes.

  Routes, MIDI and the timeline move them like any parameter.
- **The blend.** A change of target starts a blend from wherever the palette stands now: the current blended
  colours and scalars, captured per role. It ends at the target state, along a straight line in OKLab, eased with
  a smoothstep over `transition` seconds. It never passes through the states in between. A retarget mid-blend
  starts from the mid-blend colours, so nothing jumps. `palette/saturation` and `palette/value` apply as before.
  `palette/position` is ignored while a selector exists.
- **Manual select.** A change of `palette/select` retargets at once and restarts the hold. The lock does not stop
  it.
- **Advance.** `advance.signal` is any bus signal. It fires when:
  - an event signal fires with a strength at or above `threshold`; or
  - a continuous signal crosses `threshold` upward.

  When it fires, unless locked and only if `hold` seconds have passed since the last change, the target moves to
  the next state of `order` (default `0..n-1`), wrapping. An energy-matched choice was considered and not built: an
  authored order is composed, and as cheap as it gets.
- **Roles and bindings are ADR-1043's, unchanged.** A colour role into a vec3 or vec4 target writes rgb (min(n, 3)
  components) and leaves w alone, so a material op's `constant`/`constant2` (vec4) can be bound. The vec4 case is
  tested.

## Determinism and seek

The selector carries state between frames: the blend's start, the hold, and the advance signal's last level. For a
given sequence of frames and signals it is deterministic.

On a seek:
- **Offline, with an analysed track, and an advance signal that the seek's signal replay rebuilds** (`audio.*`,
  `time.*`, `beat.*`, `music.*`, `section.*`, `sonic.*`; ADR-870's deterministic prefix): the selector is replayed
  from zero on the render's frame grid (`render.fps`, default 60), beside ADR-1168's control replay. A render
  started mid-track therefore lands on the palette and blend that a play from zero shows. This is tested bit for
  bit against a play at the same grid.
- **Otherwise** (live input, no track, or an advance signal outside that prefix such as `response.*`, `control.*`
  or a stateful source): the selector restarts at `palette/select`'s value on the landing frame, with no blend.
  A manual selection made by a live route (MIDI) is not replayed either, as for any live-input route (ADR-901).
- During play, a step back in time, or a step forward of more than one second, is treated as a seek and restarts
  the selector the same way.

## Consequences

- A project palette with no `selector` behaves exactly as before. No new parameters exist, `step` is a no-op and
  `apply` reads `position`, as the compatibility test asserts.
- A scene picks palettes by index (MIDI CC to `palette/select`), locks them (a note to `palette/lock`), and lets
  phrases change them with a hold so that they never flicker.
- The replay costs one signal-pipeline pass from zero per seek, only for a selector with a replayable advance
  signal. It is the same order of cost as ADR-1168's control replay.
