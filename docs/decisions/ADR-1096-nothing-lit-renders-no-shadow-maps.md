# ADR-1096: When nothing visible is lit, no shadow map is rendered

**Status:** Accepted (live optimizer, Stage 2; the Sonic Abstract art pass's finding). **Date:** 2026-10-03

## Context

A scene with no authored light gets `scene::defaultKeyLight()`, which casts. In an all-unlit scene its cascades cost
1-3 ms and no pixel samples them.

## Decision

After the frame's draw lists are built, the shadow views are skipped (and `ShadowStats::skippedNothingLit` set) when
every camera-visible entity, every procedural and every SDF material is `unlit`, there is no water, and the scene has no volume (`VolumeRenderer::enabled`, or a march last frame): the march reads
the key light's shadow for its shafts. The light keeps its slot; the next
frame with anything lit renders the views again. This is not a quality setting: it is correct at every tier,
including Offline, because nothing reads the maps.
