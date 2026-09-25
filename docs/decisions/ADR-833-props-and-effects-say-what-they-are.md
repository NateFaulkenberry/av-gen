# ADR-833: Props and effects say what they are

**Status:** Accepted
**Date:** 2026-09-25
**Answers:** Phase D §25 (semantic categories), §66 ("props and effects carry no semantic tags"),
and §69 (world effects are a thing a character can know about)
**Related:** ADR-097 (entity tags), the §25 `SemanticTags` vocabulary, ADR-832
**Implemented by:**
- `CompositionNode::tags` (scene key `"tags"`, additive);
- `scene::semanticTagsOf`;
- `EntityWorld::setLandmarkTags`, read by `refreshInterestPoints`;
- in `glowmere-valley-2-multicam.scene.json`, `tags` on the `visitor` and `visitor-beam` entities.

**Tests:** `tests/unit/test_semantic_landmarks.cpp` (`[adr833]`). Without the landmark tags the
film's world has no word "mushroom" at all, and the test fails. It passes with them.

## Decision

The awareness layer has filtered and weighted percepts by semantic tags since §25. Its words came
from two places: an entity's `tags`, and the five interest kinds. Everything in Glowmere that is
not a character was a bare "landmark": forty mushroom caps, the saucer and its beam. Now:

- **A node has `tags`**, authored in the scene and saved only when present.
- **A node also says what it demonstrably is.** A generated procedural adds its generator's name,
  so every mushroom cap is "mushroom" without anyone writing it. Nothing is inferred from a node's
  *name* (§66). A rock is a rock because someone tagged it, or because a rock generator made it.
- **Landmarks carry their node's words.** Heroes share their node's name, so they carry them too.
  `refreshInterestPoints` ORs the words into each named point's mask. It runs when the landmarks
  change, not per frame.
- **In the film**, the saucer entity is `["ufo", "vehicle"]` and its beam is
  `["world_effect", "beam"]`. Perception reads a moving entity's tags from the entity, not from its
  landmark.

## Behaviour

This is neutral unless someone asks. A tag changes a decision only through a considerer's tag
filter or a perception block's tag tastes, and no character in any shipped scene declares either
for these words. Measured: the multicam film's 19 entities over 60 s at 60 Hz hash identically with and without
this change (`5f6071349066d4eb` both ways). Scrub parity and the ADR-333 route golden are
unchanged.

## Not done

Scene-level `effects` (the travel beam, fog, tornado) are not points in the world and get no
interest points here. Adding them would change what every decider can choose. A world effect as an
*event* (§26's WorldEffectStarted/Ended) is its own piece of work.
