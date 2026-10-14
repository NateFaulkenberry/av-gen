# ADR-1340: Examples are Live or Offline, and nothing is hidden

- Status: Accepted (2026-10-14), repository cleanup
- Supersedes: the top-level File > Live Projects menu (2026-10-07, `78166818`); `"live": true` keeps its meaning.
- Code: `src/app/examples.{hpp,cpp}` (`exampleStatus`, `exampleLabel`, `exampleTooltip`, the index's `generate`),
  `src/app/project_paths.hpp` (`resolveFrom`, shared with the project loader), `src/ui/control_panel.cpp` (the menu).
- Tests: `tests/unit/test_example_menu.cpp`, `tests/integration/test_project_system.cpp` ("Every example in the index
  exists and loads", "Examples > Live lists exactly the sonic.live projects").

## Context

The owner's cleanup brief asked for every example scene to be reviewable from the application, under exactly two
top-level categories, Offline and Live, decided by intended use. Two problems stood in the way:

- **Missing scenes.** Of the 81 distinct scenes the audit found (`docs/audits/repository-cleanup/scene-inventory.md`),
  12 had no index entry. Among them were finished pieces: the offline Astral Forge and Digital Mosh films, Liminal's
  All You Got, the Chorus Field god and the Biovalley macro.
- **Silent failures.** Some entries could not open on a clean checkout and nothing said so. THE RIFT's 14 MB scene is
  generated and gitignored. Opening it fell back to the default orb, and the index test passed it as "loaded".

## Decision

- **One menu, two submenus.** File > Examples has exactly two submenus, **Live** (`"live": true`, a `sonic.live`
  project made to be played) and **Offline** (everything made to be rendered). Each submenu is headed by family, as
  before. Each scene appears once; a scene with both a live and an offline project (THE RIFT, Astral Forge, Digital
  Mosh) has one entry in each submenu, under different families, because the live switcher's sets are categories.
- **Every entry is checked when the menu is drawn.** The check compares the files the entry needs (the project, its
  scene, its song) against the disk. `resolveFrom` resolves the paths exactly as the project loader does, and the
  result needs no restart to update.
- **Missing files are shown, not hidden.**
  - An entry whose project or scene is missing stays listed, greyed. Its label says "(needs generating)" when the
    index names a `generate` command, or "(missing files)" when it does not. The tooltip names the missing files and
    the command.
  - An entry whose song is missing opens, labelled "(no audio)".
- **The index test fails what the menu would show broken.**
  - A missing entry must carry `generate`; it is reported by name (WARN) and not counted as a load.
  - A loaded project must also pass `requireProjectScene()` (ADR-1288), so a scene that fell back to the orb fails.
  - A live entry must always exist in a checkout.

## Consequences

- The owner's Live Projects menu moved one level down: File > Examples > Live.
- **Examples added to the index:**
  - Astral Forge, Trench (offline)
  - Digital Mosh, Feline Footwear and Trench (offline)
  - Chorus Field, Trench God
  - Liminal, All You Got
  - Biovalley, Macro (generated)
  - The Tide
  - Glowmere Valley 2, Song
  - Procedural Space, Showcase
  - the Bioluminescent Valley recipe
  - Open Vista
- The Sonic Garden timbre variants stay fixtures of the Sonic Garden scene and have no entries.
- A new generated example needs a `generate` command in its index entry, or the index test fails on a clean checkout.
