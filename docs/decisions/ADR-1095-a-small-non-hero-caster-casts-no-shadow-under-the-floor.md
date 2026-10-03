# ADR-1095: Under the caster floor, a small non-hero object casts no shadow

**Status:** Accepted (live optimizer, Stage 2). **Date:** 2026-10-03

`QualitySettings::shadowCasterMinPixels` (0 = off). An entity whose bounding sphere projects to fewer pixels than the
floor (times its importance weight; a hero never) on the camera's view is not offered to the shadow views, in both
the visible and the off-screen caster passes. Counted in `ShadowStats::castersBelowFloor`. Procedural instances are
not affected (their casters are culled per cascade on the GPU). Arm: `castercull` (24 px). Used by the two lowest
live levels (12 / 24 px) and the BALANCED / PERFORMANCE profiles (8 / 24 px).
