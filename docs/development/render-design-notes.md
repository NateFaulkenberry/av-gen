# Render stream (GV3 wave 2, ADRs 917-919): status, 2026-09-27

Branch `agent/render`, worktree `av-gen-render`. Based on `0b623b88`; main `3f720bfa` merged in
(`ce7afb5e`, at the coordinator's instruction). The ADRs hold the decisions and the measurements;
this file is the handoff.

## Built
- **ADR-917, post sizes follow the frame.** `post/referenceHeight` (default 720). Every pixel-sized
  post value scales by chain height / reference. A frame whole octaves finer than the reference is
  box-filtered down by them (colour and emission) before the bloom and halation pyramids; the plan
  (`scene::planPyramid`) shares any fraction of an octave between bracketing levels. The streak's
  reach and the motion-blur tiles scale; tiles past 40 px are reduced separably (rows, then columns);
  the look stage's low-pass descends an octave instead of truncating. At the reference nothing moves.
- **ADR-918, fog from the sky.** `scene/fogSky`: a 128x32 map of the sky's radiance (background,
  aurora, comets) built each frame the fog asks for it; `applyFog` fades towards it. Environment
  panel, "Sky and fog", "Fog takes the sky's colour".
- **ADR-919, offline floors.** March steps 32, anisotropy 16x, procedural sky cubes 1024 px a face;
  each logged when it raises. Shown under the Render panel's tier row.
- **World edge: report only.** `WorldMap` size is data (validated to 1e6 m; the ocean world ships
  40 km), so a bigger GV3 world is a data change plus re-authoring the valley, river and wall paths
  -- but `WorldMap::prepare`'s 97x97 altitude survey re-ranges `altitude01`, so biomes and scatter in
  the core valley shift when the map grows, and the nav grid and terrain chunk grid scale with it. A
  backdrop ring (a terrain ring builder in `terrain.cpp`, the composition terrain cache, an entity
  drawn after the water entities) is estimated at 1-2 days: over the brief's "hours" bar, so GV3
  closes its valley ends with ridge features.

## How it was verified
- The GPU tests' first run found three code defects (weightless pyramid levels blurred the finer
  frame's halo; the single-pass tile maximum cost 34 ms at 80 px tiles; the sky floor logged every
  rebuild) and four test defects (a ridge behind the far plane, a smear too short to see the tiles,
  a reach measure summing three halos, a sky statistic measuring half-float quantisation). All fixed;
  thresholds are set from measurement with controls that fail on the old behaviour, including two
  throwaway builds (the look stage's truncation; the single-pass tile maximum).
- Four existing GPU tests were re-baselined (ADR-917's and ADR-918's Consequences list them).

## The GV3 evidence
- `build/gv3-eval/` (not tracked): a snapshot of GV3's project and scene (`snap/`, with the
  materials, light rig and entity profile it references), variants written by `variant.py`, the
  batch `render-gv3-batch.sh` (main `3f720bfa` built from `git archive` in
  `build/main-3f720bfa/` as the before arm) and `analyse.py`. Stills are in
  `~/Desktop/av-gen-review/18-glowmere-valley-3/revision/render/`.
- The first batch rendered an empty scene with exit 0: the snapshot lacked `../entities/`,
  `../materials/` and `../lightrigs/`. Every summary line now records what the render loaded.
