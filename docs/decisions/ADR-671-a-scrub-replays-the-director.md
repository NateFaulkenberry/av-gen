# ADR-671: A scrub replays the director -- the Glowmere film lands where the play does, abduction included

**Status:** Accepted -- the owner's ruling, 2026-09-21
**Date:** 2026-09-21
**Replaces:** the seek rule of the director POC -- "reset the director on a seek; a scenario that
autostarts picks up again on the next frame" (`Engine::seekSeconds`, cited in code as ADR-209; the
director's ADR file is **ADR-210**, the number having collided with the selection-box ADR-209 when
both landed on 2026-09-15 and the code references never being renumbered)
**Related:** ADR-210 (the director), ADR-273 (the replay window), ADR-360 as the project cites it --
"a render must be reproducible and a scrubbed frame must equal a played one" (the file numbered 360
is the tree-and-wind ADR: another collision; the rule itself is stated in `EntityWorld::seek`),
ADR-670 (the awareness layer, which is what exposed this)
**Implemented by:** `Composition::seekWithDirector`, `Composition::ReplayPlacement`,
`EntityWorld::SeekHooks`, `EntityWorld::directorBefore/After`, `Engine::seekSeconds`
**Tests:** `tests/unit/test_glowmere_scrub.cpp`

---

## Context

A scrub reset the director and replayed the entities alone. So the abduction restarted from its top
at every scrub, and anything that perceived the craft or the animals diverged from the play. For the
nine farm animals and the craft this had been true since the director landed. The five aliens
happened to land exactly at 30 s and were up to 5.7 m apart at 90 s. With the awareness layer
(ADR-670) they perceive the craft and hear its beam, and the divergence became 30.6 m at 30 s and
101 m at 90 s. Measured with `avgen_behavior_trace --scrub`.

The owner chose, on 2026-09-21, to **replay the director on seek**. A scrub, and a render that
starts mid-film (which is a seek followed by ordinary frames), must land every body where playback
puts it. A scrub into an abduction now shows it mid-cycle instead of restarting it. The owner
accepted that visible change and "some" scrub cost.

## Decision

1. **`Composition::seekWithDirector` replaces the reset-then-entity-seek pair.** It resets the
   director and then replays it with the entities, one fixed step at a time, in the order a play
   frame runs them: parameter finals reset, the director's update, its beats raised as world events,
   the entity step, then the entity offsets written back onto the finals.
2. **`EntityWorld::seek` takes `SeekHooks`** (`before`, `after` each step). With hooks present,
   every body is replayed in full (a craft classified shallow would stand at its anchor on the early
   steps the director plans its approach from).
3. **The Director tier is applied inside the seek step** (`directorBefore`/`directorAfter`, now
   shared with `update`). Before this, seek never applied a `DirectorMotion` at all.
4. **`Anchor::Drawn` during a replay is answered by `ReplayPlacement`.** It is the same arithmetic
   `visualPlacement` does over the last flattening (the root fold, the node's world transform from
   the finals, the particle emitter's local point, the eight corners of every mesh's cached bounds),
   captured after each replayed step, so it is one step old exactly as a play's flattening is. It
   answers false before the first step, as a play's frame zero does.

## Alternatives considered

- **Keep the reset (ADR-210's rule) and document the divergence.** Rejected by the owner.
- **Replay the whole frame loop, flattening and posing included.** Exact by construction, but a
  play of this film costs about 4.3 ms a frame headless, so a 90 s scrub would take 23 s against
  today's 1.07 s. Rejected on cost: the director needs placements for a handful of nodes, not a
  flattened scene.
- **Checkpoints (§63).** Would bound the cost of scrubs past the window below. Not built; recorded.

## Consequences, measured

- **Exact.** Every one of the film's 16 bodies (craft, beam, nine animals, five aliens), drawn
  position, at 30 s (inside the first lift), 45 s (inside the second cycle's beam) and 90 s:
  **0.000000 m**, and the same director beat. That is the bound `test_entity_seek` already holds a
  scrub to. Before: up to 291 m (the craft). A render started at 44 s draws the same frames as one
  from zero for four seconds through the beam's cut-out: bodies 0 m, and every particle system's
  enabled, spawn rate, size and emitter position identical. Both tests fail with the replay removed.
- **Found on the way:** `seek` took ADR-620's "speed last step" *after* the action tier had written
  this step's, so a replayed body started every walk without its acceleration ramp: 1.7 cm off at
  45 s. Now taken where `update` takes it.
- **Cost**, minima of 5, headless, `glowmere-valley-2-multicam` with the aliens aware. The before
  and after were taken on one binary (`avgen_behavior_trace --scrub T [--legacy-scrub]`):

  | | 30 s | 90 s | ratio at 90 s |
  |---|---|---|---|
  | first pass, machine quieter | 341.7 → 361.9 ms | 1066.6 → 1077.2 ms | 1.01x |
  | re-take after the owner's render, load average 16 | 325.5 → 368.3 ms | 981.7 → 1358.9 ms | 1.38x |
  | interleaved pairs, load average 44-67 (other agents' builds) | | 1433.5 → 1603.6; 1246.5 → 1550.6 ms | 1.12x, 1.24x |

  The extra work is structural: every body is replayed in full (86,400 body-steps against 81,001)
  plus one director update and one placement capture per step. Every ratio is under the owner's
  1.5x, but the machine was never quiet during the re-take, so **the ratio should be re-measured
  once it is**.
- **The replay window still bounds exactness.** Past `SeekBudget::maxSeconds` (90 s) the replay
  starts from a reset director mid-film and cannot match. Measured at 150 s: the craft 200 m off.
  The same was already true of every entity, and this ADR does not change the window.
  Exactness at any time needs either a whole-history replay (cost linear, about 12 ms per film
  second here) or §63's checkpoints.
- **Not replayed:** modulation routes and reactions, since a seek has no signal history, which is
  the limit every seek in this engine already has. So an audio-driven offset on the craft is not in
  a scrubbed frame. Headless and silent films are exact.
