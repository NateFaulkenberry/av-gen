# ADR-1074: The live scene switcher steps through the set the open project belongs to

- **Status:** Accepted (2026-10-02), proto/sonic-garden. Amends ADR-1063.
- **Code:** `liveSceneSet`, `liveSceneListFor`, `kLiveSceneSets` and `liveSceneLabel` in `src/app/live_scenes.*`;
  `Application::refreshLiveScenes` (`src/app/application.cpp`).
- **Tests:** `tests/unit/test_live_scenes.cpp` (`[live][adr1074]`, a fixture index, not the real one).

## Context

The owner made the eight abstract prototypes a separate project: `examples/sonic-abstract/`, index category
"Sonic Abstract". ADR-1063's switcher had one list (Sonic Live, then every "Sonic VFX" example), so a performer in an
abstract scene would PageDown into the VFX set.

## Decision

A live scene SET is an index category named in `kLiveSceneSets` ("Sonic VFX", "Sonic Abstract"), its examples in
index order. "Sonic VFX" is led by Sonic Live, exactly ADR-1063's list. The switcher (the Live panel's Scenes row,
PageUp/PageDown, MIDI program change) uses the set that lists the open project; a project in no set, or none, gets
the default set, "Sonic VFX" -- today's behaviour. The list is chosen again whenever the open project changes, so the
Scenes row always shows the set being played. Labels drop the set's own "<category> - " prefix.

Adding a set is one entry in `kLiveSceneSets` and a category in the index.

## Consequences

- No stepping across sets: from an abstract scene the switcher never opens a VFX one, and the reverse.
- The response carry and the no-prompt rule between listed scenes (ADR-1063) apply within a set unchanged.
