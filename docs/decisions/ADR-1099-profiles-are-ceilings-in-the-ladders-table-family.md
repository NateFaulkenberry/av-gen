# ADR-1099: QUALITY / BALANCED / PERFORMANCE are ceilings in the ladder's table family

**Status:** Accepted (live optimizer, Stage 2). **Date:** 2026-10-03

## Decision

- A profile is a `LiveQualityRung` of ceilings (`qualityProfileCeiling`), the same row type as the ADR-1083 ladder,
  applied to the tier BEFORE the ladder (`applyQualityProfile`). It is the ceiling the controller works under: Ultra
  under PERFORMANCE is the PERFORMANCE picture, and the five levels go down from there. No second ladder exists.
- QUALITY = the tier. BALANCED = the background levers (8 px caster floor, emitters past 120 m, LOD bias 1.25) and
  fog at no more than half resolution. PERFORMANCE = 85% scale, quarter-resolution fog, two cascades in a 1024 atlas
  with plain filtering, half the post taps, 70% particles, emitters past 60 m, 24 px caster floor, LOD bias 2, 80% draw
  distance.
- The ladder's two lowest levels also take the background levers (Low: 12 px, 80 m, LOD 1.5; Emergency: 24 px, 50 m,
  LOD 2). Ultra, High and Medium are unchanged, so the level a 60 target settles on is unchanged (the owner's open
  tuning question is not touched).
- `LiveQualityRung` gained the Stage 2 fields; `applyLiveRung` applies them as ceilings over the base
  (`applyStageTwoCeilings`), and `effectChanges` counts them.
