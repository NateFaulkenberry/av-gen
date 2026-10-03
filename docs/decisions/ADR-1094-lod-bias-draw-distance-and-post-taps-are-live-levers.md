# ADR-1094: LOD bias, draw distance and the post effects' taps are live levers

**Status:** Accepted (live optimizer, Stage 2). **Date:** 2026-10-03

## Decision

New `QualitySettings` fields, neutral by default in every tier (`assertOfflineIsUncompromised` requires them
neutral):

- **`lodBias`** (1 = as authored). Entity mesh LOD: the policy's `maxScreenError` is multiplied by
  `1 + (lodBias - 1) * weight`. Procedural `LodSettings`: screen-size thresholds are multiplied, distance thresholds
  divided, on a per-frame copy (the scene's settings and offline renders are untouched).
- **`drawDistanceScale`** (1 = as authored). Procedural `maxDistance` on the same copy; on the CPU side
  `scene::DetailLimits::distanceScale` scales the entity bands (`cullDistance`, `fullDetailDistance`) and the rig
  rates' distances. The live editor and the profile set it from the quality in force; offline limits leave it at 1.
  Like the bands it scales, it moves the simulation.
- **`postEffectQuality`** (1 = as authored): motion blur's samples and the physical depth of field's tap cap (192 at
  1) are scaled by it. **Not resolution.** Half-resolution depth of field and motion blur need a CoC-aware composite
  with the full-resolution frame, which is a shader algorithm change and out of this stream's scope (02-plan.md);
  rendering either pass into a smaller target would soften the whole image, sharp parts included. The bloom pyramid
  already starts at half resolution.

`weight` is the node's importance (ADR-1097): 0 for a hero (exempt). Each lever has an A/B arm
(`lodbias2`, `drawdist75`, `posttaps`) and the gates of ADR-1083 have theirs (`nomotionblur`, `nodof`).
