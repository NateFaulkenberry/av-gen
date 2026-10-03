# ADR-1097: Importance is inferred, and heroes are exempt from every live lever

**Status:** Accepted (live optimizer, Stage 2). **Date:** 2026-10-03

## Decision

- `scene::Importance` (hero, foreground, normal, background, ambient), with `importanceLeverWeight`: 0, 0.5, 1, 1.5,
  2. It is read ONLY by the live levers (LOD bias, draw distance, caster floor, particle cull).
- A composition node may author `"importance"`; nothing has to. Unset, `Composition::importanceOfNode` infers it:
  Hero when the node is a hero in `Composition::heroes()` (a hero is the node of the same name, ADR-107), else
  Normal. Flatten copies it to every entity, particle system and procedural the node produced. Written back only
  when authored, so every existing scene round-trips unchanged.
- `ImportanceInput::hero` is now set for entity LOD, so `RepresentationPolicy::heroFloor` finally applies.
