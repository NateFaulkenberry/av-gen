# ADR-1869: The attractor's water block is saved, every key the parser reads

- Status: Proposed, scene/strange-attractor (2026-10-29). Follows ADR-1868.
- Code: `src/scene/attractor_world.cpp`. `kAttractorWaterFloats` is one table of every float water key, used by both
  the parser and the save; `waterToJson` writes it, along with the three colours.
- Tests: `[adr1869]` in `tests/unit/test_attractor_world.cpp`. They check two things:
  - parse → save → parse gives equal results, including `foam` 0;
  - the canyon scene loaded into a Composition and saved app-style keeps every authored water key.

## Context

ADR-1868 made the block read every water key. Its writer, `attractorWorldToJson`, wrote no water at all, so an app
save of the canyon dropped the river's whole look, including `foam` 0. The engine's default foam line would have come
back.

## Decision

The parser and the writer share one key table, so the two cannot list different keys. The water block is always
written in full.

## Consequences

A saved canyon project reproduces its river. A new water key has to be added to the table once, and that covers both
the parser and the save.
