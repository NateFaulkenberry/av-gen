# ADR-1222: The Astral Forge has live and offline tiers, and a MIDI vocabulary

- Status: Accepted (prod/astral-forge)
- Builds on: ADR-1221 (the Astral Forge environment), the live quality ladder (ADR-1083, ADR-1094..1105) and the
  control map (ADR-021).
- Evidence: `docs/prototypes/astral-forge/09-production.md` (the live profile and the offline render).

## Problem

ADR-1221's renderer has one picture and three costs: its particles, its march and its flakes. The engine's live
ladder can already lower the render scale, which shrinks the march and the shading for free, because the
Environment draws at the HDR target's size. It cannot lower the particle count, the god-ray taps or the shards.

The offline tier wants the opposite: every particle, the full-resolution march, and a seek that lands exactly where
play does.

A performer also needs the god's own verbs on a controller: summon, hold, collapse, choose a god.

## Decision

**`QualitySettings::astralTier`** is a floor, where higher is cheaper. It changes sampling only, never the
composition:

| Tier | Where | Particles | March | God-ray taps | Shards | Seek |
|---|---|---|---|---|---|---|
| 0 | Offline | all | full resolution | 20 | yes | exact (re-simulated from the song's start) |
| 1 | Realtime/High, the live ladder's Ultra and High | all | half-resolution pre-pass + full refine | 20 | yes | pre-roll |
| 2 | ladder Medium | 70% | " | 12 | yes | pre-roll |
| 3 | ladder Low | 50% | " | 8 | no | pre-roll |
| 4 | ladder Emergency | 33% | " | 0 | no | pre-roll |

**How each lever works:**
- **Particles.** A tier simulates a prefix of the particle buffer. Roles are hashed per 64-particle block,
  independent of the count, so a prefix is a uniform sample. The density norm scales with the count, so the
  iso-surface means the same thing at every tier. Nothing is reallocated: a rung change is free, and the frozen
  remainder rejoins when the ladder climbs back.
- **Wiring.** `LiveQualityRung::astralTier` is set on the Medium, Low and Emergency rungs of all three ladders. It
  belongs to the `particles` lever group (ADR-1105), round-trips through the Optimize ceiling (`astralTier`) and
  has two named levers (`astralfewer`, `astralhalf`).
- **Diagnostics.** The quality arms `astral0`..`astral4` force a tier. The live profile files the environment's
  passes under particles/simulation (`astral.sim`, `astral.density`, `astral.flakes`, `astral.shards`) and SDF (the
  cache, march, surface and combine).
- **Offline.** `assertOfflineIsUncompromised` requires tier 0.

**The MIDI vocabulary** (`examples/astral-forge/astral-forge-live.json`) maps every control to an `astral/...`
parameter through the ordinary control map. Nothing is MIDI-specific in the engine.

| Control | Parameter | Range |
|---|---|---|
| CC 1 | summon | 0..1 |
| CC 2 | hold | 0..1 |
| CC 3 | intensity | 0..2 |
| CC 4 | zoom | 0.6..1.8 |
| CC 5..8 | palette, light, atmosphere, godRays | 0..1 |
| CC 9 | god | -1..6 |
| CC 10 | exposure | 0.3..1.6 |
| CC 11 | camera (conductor on/off) | 0..1 |
| CC 12 | density | 0.1..1 |
| CC 13 | legibility | 0..1 |
| note 36 | collapse | |
| notes 37..43 | choose gods 0..6 | |
| note 44 | back to the score | |

- **Collapse.** A note binding sets the base to the velocity on press and 0 on release. `astral/collapse` fires on
  the rising edge.
- **God pads.** These set the same value on press and release, so a pad latches its god.

## Consequences

- **A sample, not a different picture.** A lower tier's god is sparser, never differently shaped or composed: the
  anatomy, conductor, camera and look are the same. At 33% the surface forms from fewer particles, so it is thinner
  at the periphery while forming.
- **Render scale stays the ladder's.** It lowers the march and the shading together, as for every other scene.
- **Tests.**
  - `tests/unit/test_astral_forge.cpp` checks: offline is tier 0; the ladder never raises the tier as the level
    falls, ending at 4; Ultra is the live reference; the ceiling round-trips; the live project's bindings all exist,
    and CC 1 summons the god.
  - `tests/rendering/test_astral_forge_gpu.cpp` checks the particles simulated at tiers 1, 3 and 4, and that each
    still draws the god.
