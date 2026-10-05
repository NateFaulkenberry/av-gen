# ADR-1108: Contribution is screen coverage over lever weight, and the caster table is the floor's own test

**Status:** Accepted (live optimizer, Phase 5 stage A). **Date:** 2026-10-04

## Context

The brief's 5.1/5.2 asks for screen-space importance per entity and for contribution-based optimization ("remove
low-contribution shadow casters", "increase LOD bias for objects under 0.5% of the frame"). Stage 5 groundwork already
wrote each entity's projected area, distance and hero flag; the levers already weight by importance and exempt heroes
(ADR-1094..1098). Nothing read the data.

## Decision

- Each entity row of `avgen.liveprofile/1` gains `radiusPx` (the bounding sphere's radius on the render target, computed
  with **the caster floor's own formula**: half-diagonal x largest axis scale x `ViewContext::pixelsPerUnitAt`), the
  lever weight, `onScreen`, a screen box on the output, and `contribution` = coverage / weight (a hero's is "protected").
- A `contribution` section (`analyseContribution`, `app/live_optimize.cpp`): top contributors; **low-contribution
  shadow casters** at 8/16/24/48 px -- the exact set `shadowCasterMinPixels` would remove at that floor (radiusPx <
  floor x weight, never a hero), their count, summed coverage and names, and the hero casters it would have removed but
  keeps; **LOD candidates** (visible non-hero entities under 0.5% of the frame); **particle emitters** with the
  `particlelod` cull's own test (distance - reach > 60 m / weight); the on-screen **hero regions** (for ADR-1110); and
  **suggested heroes** when none is declared -- the largest visible objects under a quarter of the frame (a backdrop
  such as terrain or water is not a hero). Suggested only; nothing is marked.
- Text report: a CONTRIBUTION block. Limits are stated in the record: bounding spheres, not silhouettes; a small caster
  can cast a large shadow; the camera of the last measured frame; procedural instances and SDF objects are not entities.

## Measured (headless, Ultra, M2 Max)

Glowmere Valley 2 from 0 s: 256 entities, 67 on screen, 74 casters, no hero declared. Floors: 8 px 6 casters (0.03% of
the frame), 16 px 11, 24 px (castercull) 17 (0.48%), 48 px 26 (2.13%); 5 LOD candidates; 10 of 13 emitters beyond
60 m. The `castercull` A/B on the same run: -0.07 ms, inside the noise -- the analysis explains why: the casters it
removes are 0.48% of the frame. Sonic has no entities (SDF/procedural): the analysis says so.
