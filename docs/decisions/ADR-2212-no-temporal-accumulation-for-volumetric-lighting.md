# ADR-2212: no temporal accumulation for volumetric lighting (revisitable for live)

- Status: Accepted (2026-11-26; coordinator decision C7, STAGE-A-PLAN §13), eng/115-volumetrics
- Extends: ADR-143 (temporal reprojection of the volume rejected)
- Code: `shaders/volumetric_light.wgsl` `vlOffset` (fixed in-froxel offsets)

## Context

Production froxel systems (Frostbite, Unreal) reproject the grid with a per-frame jitter over 8-20 frames. Unreal's
own documentation records the cost: fast-changing lights leave trails, and the remedy is to drop that light from the
fog. AV Gen's lights flash on the beat; a history weight of 0.85 is about 200 ms at 30 fps, 40% of a beat at 120 BPM.
ADR-143's further reasons hold: a temporal path would make preview and final differ by a reconstruction, and it
reopens history-bookkeeping determinism defects. And the froxel grid's cost does not scale with pixels, which removes
temporal's main motivation (Stage A: 0.1-1.6 ms).

## Decision

No history at any tier. The sample positions inside a froxel are a fixed function of the subsample number alone --
the froxel's centre for one sample, a stratified pattern for several -- never of the froxel or the frame, so a still
frame is a still picture and a frame is a pure function of the scene, the time and the camera. (A per-froxel random
depth offset was tried first and drew a spot's cone edge as froxel-sized blocks: independent noise per froxel is what
temporal accumulation exists to average, and without it the noise stays.) Quality comes from the grid, the shadow
taps and in-frame subsamples (ADR-2213).

## Revisit when (live only)

All of: a lab case shows thin-occluder shaft aliasing the Offline tier's in-frame subsampling cannot remove within
budget; the temporal path is live-only and labelled; and the history weight drops to 0 for any light whose intensity
or pose changes faster than a threshold (Unreal's per-light remedy, made automatic). Offline never takes it.
