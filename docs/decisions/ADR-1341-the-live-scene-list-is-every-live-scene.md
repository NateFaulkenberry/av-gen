# ADR-1341: The live scene list is every live scene; a 30 fps target; no "Open live demo"

- Status: Accepted (2026-10-14), the owner's request
- Supersedes: ADR-1074 (live scene *sets* that followed the open project). Amends ADR-1063 and ADR-1080.
- Code: `src/app/live_scenes.{hpp,cpp}` (`liveSceneList`), `src/app/application.cpp` (the list, built once at start-up),
  `src/ui/live_panel.cpp` (the Scene dropdown, family headings; the button removed), `src/app/interactive_resolution.hpp`
  (`kLiveTargetChoices`).
- Tests: `tests/unit/test_live_scenes.cpp`, `tests/unit/test_example_menu.cpp`, `tests/unit/test_interactive_resolution.cpp`.

## Context

The owner, looking at the Live panel: remove the "Open live demo" button, make every available live scene show up in
the Scene dropdown, and add a 30 fps live quality target.

- **The dropdown was incomplete.** It showed only the *set* the open project belonged to (ADR-1074): Sonic Live plus
  the 16 Sonic VFX scenes, or the 9 Sonic Abstract scenes. Three live scenes appeared in no set: Astral Forge Live,
  Digital Mosh Live and THE RIFT LIVE. They could be opened only from File > Examples > Live.
- **The button was redundant.** "Open live demo" opened Sonic Live, which now leads the dropdown.
- **There was no slow target.** The choices were 60, 90 and 120 fps, so on a heavy scene the quality ladder could
  only fall toward Emergency chasing 60.

## Decision

- **One list.** The live scene list is every index entry with `"live": true`, the same entries as File > Examples >
  Live (ADR-1340). Sonic Live comes first, then the index's order. The dropdown, its arrows, PageUp/PageDown and MIDI
  program changes (program n opens scene n) all use this one list. It no longer depends on what is open, so it is
  built once at start-up.
- **Family headings.** The dropdown labels each scene by its name without the family prefix, and shows the family
  (Sonic Garden, Astral Forge, Digital Mosh, Sonic Abstract, Sonic VFX, The Rift) as a heading above each run of
  scenes.
- **No "Open live demo".** The button and its callback are gone; the dropdown opens Sonic Live like any other scene.
- **A 30 fps target.** `kLiveTargetChoices` is {30, 60, 90, 120}. A 30 fps budget is a 33.3 ms frame, with
  29.3 ms for the GPU (the same 12% headroom, ADR-1080). On a display whose refresh is a multiple of 30, the frame
  cap paces it to whole vsyncs, e.g. every 2nd vsync at 60 Hz (ADR-1107).

## Consequences

- **MIDI program numbers moved.** A performer's controller that sent program n for a Sonic VFX scene now reaches a
  different scene. The list is the index order with Sonic Live first, so Sonic Live is still program 0.
- **One continuous cycle.** PageUp/PageDown now cross families, from Sonic VFX into Sonic Abstract and on.
- **A new live example joins automatically.** It reaches the dropdown by being `"live": true` in the index, which
  the index tests already require of every `sonic.live` project.
