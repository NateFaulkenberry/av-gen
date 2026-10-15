# ADR-1392: The decoded-asset cache follows the open project

**Status:** Accepted (the owner's slow quit after several projects, 2026-10-15).
**Date:** 2026-10-15

## Context

`assets::AssetRegistry` cached every glTF and image a composition loaded and was never emptied, so an editor that had
opened twenty projects held all of their decoded assets until it quit, and freed them all at once on the way out.

## Decision

A scene load brackets the composition with `beginUse()` / `releaseUnused()`: the cache keeps what the new scene
loaded and drops the rest. An asset still referenced elsewhere lives on through its own `shared_ptr`.
`AVGEN_KEEP_ASSET_CACHE=1` restores the old behaviour for measurement. Two instruments came with it:
`--open-sequence` / `--open-every` (open projects in turn through the menus' handlers, logging `phys_footprint` and
resident memory) and a `shutdown: N ms` line from `main`.

## Consequences

Re-opening a project decodes its assets again (they are not kept for a project that is not open). Measurements are in
`docs/development/project-switch-state-2026-10.md`. `tests/integration/test_project_switch_resources.cpp` holds A -> B -> A
to A's own cache.
