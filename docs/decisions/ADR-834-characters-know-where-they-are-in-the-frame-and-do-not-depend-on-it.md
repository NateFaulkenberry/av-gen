# ADR-834: Characters know where they are in the frame, and do not depend on it

**Status:** Accepted
**Date:** 2026-09-25
**Answers:** Phase D §36 (camera/cinematic awareness: `isHero`, `isInShot`, `distanceToCamera`,
`cameraVisibility`, `screenImportance`, "optional", and "should not determine the character's
fundamental world state")
**Related:** ADR-091 (two-tier determinism), ADR-349 (camera clearance), ADR-870 (the replayed bus)
**Implemented by:**
- `Composition::publishCinematicSignals` and `Composition::cinematicSignals`;
- the call in `Engine::update`, after the camera and its lens are final.

**Tests:** `tests/unit/test_cinematic_signals.cpp` (`[adr834]`).

## Decision

Every frame, after the camera is placed, each character gets five numbers. They go on the bus as
`character.<name>.<signal>`:

| signal | meaning |
| --- | --- |
| `isHero` | the active camera rig follows or aims at this character's node |
| `inShot` | the body's centre is inside the frame (the viewport's aspect, the lens's field of view) |
| `distanceToCamera` | metres from the eye to the body's base |
| `visibility` | for a body in shot, the fraction of `heroSightline`'s rays the ground and heroes let through. These are the same exact obstructions the camera's own sightline correction uses |
| `screenImportance` | the fraction of frame height the body fills, weighted from 0.5 at a corner to 1 at the centre. It is 0 out of shot |

The body's height is measured once, from its node's meshes.

**They are inputs a scene may route, never inputs the step reads.** No behaviour, considerer or
locomotion code reads them. A route or reaction can use them for animation quality, attention or
emphasis. The bus is read from the next frame, like every signal. They are not in ADR-870's
replayed bus, because they are derived from the camera and not from the analysis. So a route that
drives a *simulation* parameter from them is live-tier (ADR-091) and not scrub-exact, exactly like
a route from a live input.

## Measured

- With the camera welded to Rook, Rook is the hero, in shot at ~7.4 m, with importance > 0.2 and
  visible. Nobody else is the hero. Every out-of-shot body has importance 0.
- **§36's invariant.** The film run for 20 s with its own cameras, and again with one camera welded
  to Rook, leaves all 19 entities at bit-identical positions (entity LOD lifted, as ADR-800 does).
  The multicam 60 s entity trace hash is `5f6071349066d4eb` before and after this change. The
  invariant test has teeth. With a planted mutation, where the hero's camera becomes an interest
  point every 5 s, the two runs part and it fails.

## Not done

Routing them into anything in a shipped scene. Letting a signal move a character is an authoring
decision, and none has been made.
