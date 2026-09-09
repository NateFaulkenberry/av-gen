# ADR-035: Auxiliary render targets and a declared frame graph

- Status: Accepted (2026-09-09)
- Research: `docs/research/render-passes-and-post.md`

## Decision
- The frame writes, alongside HDR colour and depth: **normal + roughness** (octahedral normal in
  RG16F plus roughness and a material flag byte), **velocity** (RG16F, per-pixel screen motion),
  **emission** (RGB in RGBA16F with a bloom weight in alpha), and **identifiers** (R32U packing
  object id and material id). All are written from the existing shading passes, so no extra geometry
  pass is needed; SDF raymarching writes them from its hit point.
- Targets are declared and allocated by a small frame-graph layer over the existing transient pool:
  each pass declares reads and writes, the pool aliases what it can, and a debug view can display
  any target. This replaces the current implicit ordering in `SceneRenderer::render` without
  changing the pass list itself.
- **Previous-frame state**: the previous view-projection and the previous object transforms are
  kept so velocity is correct for camera motion, object motion, instance motion and deformation.
  Particles write velocity from their simulated previous position.
- **Quality tiers** (preview, realtime, high, offline) scale sample counts, resolutions and history
  lengths only; the scene, its parameters and its determinism are identical across tiers.

## Consequences
- Positive: unlocks AO, contact shadows, reflections, object motion blur, temporal reprojection,
  selective post and a much better debug view, all from data the shading passes already have.
- Negative: bandwidth (four extra targets at full resolution: roughly 24 MB at 1080p), and every
  shading path must write them consistently or effects disagree at silhouettes.
