# ADR-1289: Sculptural clouds from authored lobes

- Status: Accepted (2026-10-09), proto/bio-valley (spec 07 §5.2, Deliverable C)
- Code: `src/scene/clouds.{hpp,cpp}` (the `clouds` scene block), `src/rendering/cloud_renderer.{hpp,cpp}`,
  `shaders/clouds.wgsl`. Registered in `SceneRenderer` as the first Environment (ADR-1200 seam). Wired through
  `scene::Scene::clouds` and `Composition` (read, write, copy).
- Tests: `tests/unit/test_clouds.cpp` and `tests/rendering/test_clouds_gpu.cpp` (`[adr1289]`).

## Context

The vista's sky needed cloud masses with volume, cool interiors and lit edges (spec 07 §5.2). The engine had no way
to render a shaped cloud:
- **Procedural meshes:** these draw BLEND as opaque, so a cloud mesh is a hard cut-out.
- **The volumetric medium:** this is a single scene-wide slot with no shape (ADR-560).
- **SDFs:** these march hard surfaces.

The asset artist authored the clouds in Blender against the shot. They were exported as lobe skeletons:
- centre, radius, per-axis scale, density and edge softness;
- in the vista's world frame;
- four masses (about 1,500 lobes) that frame the moon.

## Decision

A `clouds` scene block: a lobes file (or inline lobes), an optional mass filter, and shading settings. It is drawn by
`CloudRenderer`, a fullscreen pass in the Environment seam. It runs first, ahead of the ecosystem and the astral
renderers, because clouds are sky.

**Ray setup.** Per pixel:
1. The ray is tested against the set's bounding sphere.
2. Then against every lobe's bounding sphere. Up to 160 hits are kept, with the span they cover.
3. That span is marched (`steps`), cut at the opaque scene's linear depth, so a mountain or the moon in front
   hides the cloud and the cloud hides what is behind it.

**Density.** The union of soft ellipsoids, eroded from the outside in by a domain-warped fbm (`noiseAmount`,
`noiseScale`), so edges break into wisps while interiors stay solid.

**Light.** At each sample:
- the key (`lightDirection`, the moon), through a shadow march over the same lobes, with a two-lobe phase (a
  forward silver lining and some back-scatter) and a powder term that darkens thin edges facing away from it;
- a two-colour ambient, by height in the mass;
- a warm horizon light that reaches only samples low in the sky and toward the glow (`warmDirection`, `warmHeight`).
  It lands on undersides and flanks, never on tops.

**Compositing.** Distance haze fades the cloud into `hazeColor`. The output is premultiplied over HDR before the
medium, bloom and tone mapping.

## Consequences

- A scene with no `clouds` block records nothing, and the frame is unchanged. The GPU test holds a disabled block to
  byte-identity with no block.
- Static by design: there are no parameters beyond the authored ones and no motion. These are still-image clouds.
- Offline cost: about lobes-on-ray × steps × (1 + light steps) density evaluations per sky pixel. It is not
  measured in this phase.
- The shadow march sees only the lobes on the view ray, which is an approximation. A lobe that shadows a sample
  without lying on its view ray is missed.
