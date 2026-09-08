# ADR-019: Project system — one file restores a session, explicit migration, bundles

- Status: Accepted (2026-09-08)
- Research: `docs/research/audiovisual-systems.md` §21 (project format lessons: text formats,
  scene/sequence separation, asset references), `docs/research/assets.md` (asset identity)

## Problem

Until 0.8 a project file held parameters, routes, sources, presets, shader layers and the
timeline, but not *which* audio, scene or environment they were meant for: opening a project
still required the right `--audio`/`--scene` flags. Projects also had no explicit version
migration (the loader tolerated missing keys) and no way to hand a show to another machine.

## Alternatives considered

1. Keep projects as parameter documents and rely on flags/session state for assets.
2. A binary or archive project (zip with assets inside).
3. A JSON project with relative asset references, explicit migration steps, and an optional
   exported bundle folder (chosen).
4. GUID-based asset identity with a content database.

## Decision

- Project format version 4 adds `"assets"` (`audio`, `environment`, `scene` = orb | gltf path |
  composition path | inline composition) and `"app"` ({name, version}). Every path, including
  shader layers, is written relative to the project file (`..` allowed) and resolved against it
  on load, so a project moves with its folder.
- `Engine::loadProject` restores assets first (they define the parameter surface), then the
  rest. A missing or broken asset is a **warning** (`projectWarnings()`), shown in the UI, not a
  failure: the scene that is still loaded keeps running and every parameter that exists is
  applied. Only an invalid document fails.
- `params::migrateProject` upgrades documents one version at a time (1→2 route polarity and
  empty sections, 2→3 shaders, 3→4 assets/app) with a report of the steps; the loader works on
  a migrated copy and never writes old versions.
- `Engine::exportBundle(dir)` copies every referenced file (audio, environment, glTF plus
  sidecars, scene files rewritten recursively, shaders) into `<dir>/assets` and writes
  `<dir>/project.json` with references into the bundle. `--export-bundle <dir>` does it headless.
- `Engine::newProject` resets everything but the audio; recent projects are remembered in the
  user's preferences directory (`RecentFiles`, SDL pref path) and listed under File > Open Recent.
- CLI order: `--project` loads first and explicit `--audio/--scene/--env/--composition` flags
  override it.

## Rationale

Relative references plus a bundle exporter cover the two real workflows (keep working in a
folder; ship a show) without inventing an archive format or a database: bundles are plain
folders that git, rsync and humans understand, and the project stays a diffable text file
(lesson from every system surveyed: TouchDesigner's `.toe` opacity is the cautionary tale).
Warnings-not-failures for assets is what a live tool needs: a missing HDRI at the venue must not
stop the show. Explicit migration turns "tolerant loader" into tested steps so future format
changes are deliberate.

## Consequences

- Positive: `avgen --project show.json --play` is a complete recall; projects and bundles
  relocate; old projects keep loading through recorded steps; missing assets are visible.
- Negative: glTF sidecar detection for bundles is by extension in the same folder (not by
  parsing the glTF); no content hashing, so a renamed asset is "missing"; recent files are
  per-machine only.
- Follow-ups: content hashes in `assets` for relink-by-hash, autosave, an "unsaved changes"
  flag, and 1.0's render settings living in the project.
