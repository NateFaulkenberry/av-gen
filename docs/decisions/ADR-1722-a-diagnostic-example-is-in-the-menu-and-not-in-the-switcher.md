# ADR-1722: A diagnostic example is in the menu and not in the live scene switcher

- Status: Accepted (eng/audio-response-lab)
- Amends ADR-1341 (the switcher lists every live example) and ADR-1343 (`hidden`).
- Code: `src/app/examples.{hpp,cpp}` (`ExampleInfo::diagnostic`, parsed and type-checked); `src/app/live_scenes.cpp`
  (`liveSceneList`); `examples/index.json` (the Audio Response Lab under **Diagnostics**).
- Tests: `tests/unit/test_example_menu.cpp` `[adr1720]`:
  - a diagnostic entry is in its own Live menu section and not in `liveSceneList`;
  - a non-boolean value is rejected;
  - the real index keeps the Lab in Live under Diagnostics, and the switcher holds every other live entry.

## Problem

A live project must be `"live": true` in the index (test-enforced, beside `sonic.live`), and every live entry was in the
performer's scene switcher: the Live panel's list, PageUp/PageDown and MIDI program change. An engineering instrument
does not belong in a set. `hidden` would also drop it from the menu.

## Decision

`"diagnostic": true` keeps an entry in File > Examples > Live under its category heading, and out of `liveSceneList`.

## Consequences

- The Audio Response Lab opens from the menu like any live scene. A set never steps onto it, and MIDI program numbers
  for the creative scenes are unchanged.
- The MIDI rig's live-set checks (ADR-1705) iterate the switcher's list, so they do not include the Lab.
