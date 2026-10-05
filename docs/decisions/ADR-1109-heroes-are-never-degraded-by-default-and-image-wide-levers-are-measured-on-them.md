# ADR-1109: Heroes are never degraded by default, and an image-wide lever's effect on them is measured

**Status:** Accepted (live optimizer, Phase 5 stage B). **Date:** 2026-10-04

## Context

The brief's 5.3: a hero's geometry, shadow, animation, particles and reactive effects are preserved. ADR-1097 made
heroes exempt from the per-object levers in the renderer, but an audit of every lever against that list found three
leaks:

1. `particleSpawnScale` (the `particlelod` arm, the profiles, the lower live levels) thinned a hero's emitter.
2. The draw-distance lever's CPU half (`DetailLimits::distanceScale`) slowed and culled a hero's **rig** (`updateRigs`).
3. ...and moved a hero **entity's** behaviour and field bands (`EntityWorld::update` / `updateFields`).

## Decision

- **Engine fixes.** A hero emitter keeps its authored spawn rate (`ParticleRenderer`, weight 0 = exempt, as the
  distance cull); a rig any hero entity is skinned by keeps distance scale 1; an entity driving a hero node keeps its
  authored bands (`entity::distanceScaleFor`, fed `Composition::heroNodes()`, sorted, built at each flatten).
- **Every lever is classified** (`heroEffectOfLever`, `candidates[].heroEffect` in the record): `exempt` (castercull,
  lodbias2, drawdist75, particlelod: the engine skips heroes), `image-wide` (resolution, fog, post, shadow atlas, PCSS:
  every pixel changes the same way, the heroes' included), `degrades` (`noprograms`), `unknown` (treated as degrades).
- **Policy** (`--hero-policy`): `protect` (default) never proposes, applies or searches a `degrades`/`unknown` lever,
  and admits `image-wide` ones with their effect inside the hero boxes MEASURED (ADR-1110) and counted again in the
  search's rank key (ADR-1111); `strict` excludes image-wide levers too.

## Tests

`the live spawn scale thins a normal emitter and never a hero's` (GPU), `the draw-distance lever culls a far rig
sooner, never a hero's`, `heroes: an entity driving a hero node keeps its authored bands...`, `heroes: every lever with
a ceiling form has a hero classification...`.
