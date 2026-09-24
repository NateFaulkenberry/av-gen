# ADR-754: The Director's capabilities are generated from engine data, never listed

**Status:** Accepted
**Date:** 2026-09-24
**Related:** ADR-617 ("documentation that cannot rot"), ADR-194 (airborne), ADR-096 (activities),
ADR-091/098 (determinism tiers), ADR-750 (the module), ADR-702 (effects; not yet in the registry)
**Implemented by:** `directing::CapabilityRegistry`, `CharacterCard`, `CameraCatalog`,
`EventCatalog` (`src/directing/capabilities.*`); `scene::allShotTransitions`; `ai::capabilityDocument`
(`capability.list`)
**Tests:** `tests/unit/test_directing_capabilities.cpp` (`[directing][capabilities]`,
`[directing][boundary]`)

## Context

The Director must refuse a capability that does not exist before it compiles anything. "Have Rook
backflip over Umbra" must come back as CAPABILITY_UNAVAILABLE naming what Rook *can* do (spec §1.5).
That needs a description of what the scene can do. The one thing this codebase has already learned
about such descriptions is that a hand-written one goes stale. `capability.list` still declared the
`entity` and `render` domains unavailable while listing `entity.list` and `render.probe` in them.

## Decision

`directing::CapabilityRegistry::fromComposition` reads everything from the structures that decide
behaviour. Nothing in it is a list somebody maintains:

- **Characters** (`CharacterCard`): the `EntityDesc` supplies the activity-to-clip map, gait speeds,
  tags and Phase D affordances. These are joined to the node's **loaded rig**, which says whether
  each clip exists, its playable length, and whether its state loops. Activities are classified
  ground / action / airborne by a switch over `entity::Activity`, so a new activity is a compile
  warning rather than a silent "custom". The jump envelope comes from the entity's `explore`
  behaviour settings, falling back to `entity::JumpSettings` defaults, and says which (`source`).
  Clips on the rig that no activity maps are listed separately (`unmappedClips`); they are
  information, not vocabulary.
- **Cameras** (`CameraCatalog`): the scene's rigs; presets from `seq::allCameraPresets()`;
  behaviours, shot camera kinds and sequence transitions enumerated from their own name tables (a
  value whose name reads back as itself is real); cut transitions from the new
  `scene::allShotTransitions()`. That function is generated from the table
  `shotTransitionName` indexes. Probing that function out of range read past the table, and that
  is how this was found.
- **Events** (`EventCatalog`): every trigger and action kind, with its determinism tier taken from
  `seq::triggerIsScheduled` / `seq::actionIsBaked`, the predicates the engine itself uses.
- **Semantic names are the interface.** A plan asks for `run`; the clip (`Running`) is carried as
  information (spec §10).
- **No "unavailable" list.** The card never lists what a character cannot do: that would need a
  vocabulary of every verb anyone might ask for, and that list would go stale. Unavailability is
  the validator's finding, relative to the request (Slice 1.3).
- **A card with no loaded rig can do nothing, not anything.** `rigLoaded` is false and no activity
  is `available`, so a missing asset is a refusal rather than a fallback clip.
- **`capability.list`** now derives availability from the tool registry. A domain with tools is
  available; its authored reason becomes `limits`; a tool domain with no prose is listed with a
  generated entry. The transaction text now describes ADR-752's history rather than parameter
  snapshots.

## Consequences

- Measured on the benchmark (Glowmere Valley 2 multicam): Rook can idle, walk, run, jump, fall and
  land. He has no backflip activity and no clip that is one. His airborne set is {fall, jump,
  land}. `Jump_running` is on his rig but unmapped. His jump envelope is the engine default (apex
  1.1 m, g = 18, 4 m), because he has no `explore` behaviour. `run` plays 0.70 s: the feasibility
  report's 0.73 s is the raw last key time, before the 1/30 s start offset.
- Every state the rig builds loops today, and the card says so (`loops: true` on `jump`). One-shot
  semantics are Slice 3's work, and the card will report them when they exist.
- Effects are not in the registry. When ADR-702 merges, the effect catalogue will come from its
  `EffectSchema` type registry, keyed by instance id and owner.
- The boundary test scans `src/directing/` for `#include "ai/` and for AI vendor names as whole
  words ("playable" contains "laya").

## Addendum (2026-09-24, after ADR-702 merged)

The registry now has an **effect catalogue**, read from `world::effectSchemas()`:
- types: key, display name, category, render stage, the owner kinds each may attach to, and
  non-hidden field leaves;
- instances: the composition's one effect list, by id, owner and activation.

On the benchmark: 16 types; `aurora` attaches only to the world; `groundPulse` attaches to
entities; `umbra-cap-hero-pulse` is a `groundPulse` owned by `umbra-cap`, activated on `heroFocus`.
