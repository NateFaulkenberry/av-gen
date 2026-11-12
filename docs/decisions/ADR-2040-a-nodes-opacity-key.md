# ADR-2040: A node's `opacity` key is read from the scene file

- Status: Accepted (the owner's "go ahead" on the two bugs found while writing the Scene Authoring Reference,
  2026-11-12), eng/scene-key-fixes.
- Code: the node reader, writer and duplicate in `src/scene/composition.cpp`;
  `examples/musical-geography/look.py` (`horizonDensity` 9 -> 8).
- Tests: `[composition][adr2040]` (`tests/unit/test_node_opacity_key.cpp`):
  - the key becomes the base of `nodes/<name>/opacity`;
  - it fades the node's entities and promotes them to blending;
  - it round-trips, and a node at 1 writes no key;
  - values outside 0..1, and anything that isn't a number, are refused.

## Context

Every node has had a routable `nodes/<name>/opacity` parameter (a multiplier on its materials' opacity, which also
switches an opaque asset to blending while it is under 1). But the scene file had no way to set its starting
value: the reader never looked for `opacity`, and the writer never wrote it. So:
- **Strange Attractor's Mars scene** gives each old rover-track segment an `opacity` that falls with distance behind
  the rover. They all drew at full strength.
- **Its Moon scene** starts the Earth halo's glitch copies at `opacity` 0. Their routes `replace` the value every
  frame, so nothing showed it. But the key was dead.
- **An opacity set in the app** was lost on save.

## Decision

- **Read** `opacity` on any node: a number 0..1, refused otherwise (as `temporalSlot` is). It is the parameter's base.
- **Write** it on save only when it is not 1, so every existing scene saves byte for byte as before.
- **Duplicating** a node keeps it.

No compatibility path: the key simply starts working. A file that set it now gets what it asked for.

Alongside: Musical Geography asked for `horizonDensity` 9, above the engine's maximum of 8 (`kHorizonDensityMax`),
to which every reader clamps it. The build now says 8. The picture is unchanged.

## Consequences

- **Strange Attractor's old rover tracks now fade** behind the rover, as their builder intended. That is a visible
  change to the merged demo's Mars section.
- **The Moon halo copies are unchanged**, because their routes own the value.
- **No other example sets the key** on a node (checked with a search of `examples/`).
