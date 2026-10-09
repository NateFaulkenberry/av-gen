# ADR-1285: An empty points list places nothing

- Status: Accepted (2026-10-08), proto/bio-valley (found in the art pass's macro test)
- Code: `src/scene/procedural.cpp` (`ProceduralGeometry::generateCloud`).
- Tests: `tests/unit/test_generator.cpp` (`[adr1285]`).

## Context

A `points` distribution reads its placements straight through (ADR-1118), but only when the list is non-empty. An
empty list fell through to the derived path, whose instance count is at least 1, so it produced one identity
placement: an object at the world origin, at scale 1, that nobody placed. A generator that writes "this species has
no specimens in this shot" as an empty list put a full-size organism in the frame. In the macro test that was a giant
fungus in front of the lens.

## Decision

A `points` distribution with no list, or an empty list, produces an empty cloud and no instances, exactly as a
generator distribution does on the CPU side. Every other distribution is unchanged, and a list of N points is still
N instances.

## Consequences

- Scenes that relied on an empty list drawing one object at the origin, if any, now draw nothing. That object was
  never authored.
