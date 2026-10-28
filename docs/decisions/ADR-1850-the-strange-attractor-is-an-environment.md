# ADR-1850: The Strange Attractor is an Environment

- Status: **Superseded by ADR-1861** (2026-10-28: the ray-march renderer is removed; the canyon is engine geometry, ADR-1857). Was: Accepted, scene/strange-attractor (Gate 1, 2026-10-27).
- Code:
  - `src/scene/attractor_world.{hpp,cpp}`: the `attractorWorld` block, its generator and its parameters.
  - `src/rendering/attractor_renderer.{hpp,cpp}`, `shaders/attractor/world.wgsl`: the pass.
  - Wiring: `scene::Scene::attractorWorld`, the Composition (read, write, parameters, regeneration, the composed
    camera), and one line in `SceneRenderer`.
- Tests: `tests/unit/test_attractor_world.cpp` (`[attractor-world][adr1850]`).
- Evidence: `docs/prototypes/strange-attractor/01-research.md` §4-5.

## Context

The brief asks for a procedural surrealist world: fractal terrain, recursive landmarks and an attractor-derived
formation, integrated rather than placed side by side. The engine's mesh path (terrain mesh, surface-net SDFs,
tubes) draws hard intersections between those parts. Its cascaded shadows are coarse over 16 km. Its terrain
generator (ADR-090) speaks rivers and ponds, not erosion fronts and strata.

## Decision

The scene is a fourth Environment on ADR-1200's seam. It adds nothing to the interface.

- **Generation (CPU, deterministic).**
  - `generateAttractorWorld(AttractorGen)` bakes a height field and four masks: rock, playa, cavity and gully ridge.
  - The land is canyon country: a plateau dissected by a nested-domain-warped erosion front. The front's zero
    contour, made metric by `F / |grad F|`, sets cliffs of a fixed width over talus aprons.
  - On top of that: a second tier, a bench with a meandering inner gorge, terraces cut along a tilted, warped and
    folded strata coordinate, and erosion gullies.
  - The camera is composed by a deterministic search for an open lip.
  - It is a pure function of the structural knobs. Rows run in parallel and each texel is independent.
- **Rendering (one fullscreen pass).**
  - It marches the height field: bilinear with a smoothstep blend, plus near-field displacement, and bisects on
    overshoot. ADR-1852 replaced the blend with plain bilinear, with B-spline shading normals and a sub-texel band
    cut.
  - Shading:
    - the strata coordinate, whose hash is bit-identical to the generator's;
    - a low sun with a soft height-field shadow, sky ambient, back light and bounce;
    - analytic height fog with a wavelength tilt;
    - an analytic twilight sky.
  - It writes HDR and clip depth for every pixel, sky included, so it runs first among the environments. AV Gen's
    post and tonemap follow.
- **Parameters.** `attractorWorld/...` in the brief's groups:
  - World Generation (structural): seed, terrainScale, roughness, ridgeStrength, domainWarp, terraces, erosion and
    strataFold;
  - Materials and Atmosphere (continuous): palette, sun, ambient, atmosphere, strata and exposure;
  - Quality: detail;
  - Camera: camera.
- **Regeneration.**
  - A structural change regenerates on the next frame, through a last-world cache keyed by the structural hash.
  - It counts as a camera cut (ADR-912).
  - A look change never regenerates and never re-uploads.

## Consequences

- **No shadow casting into the engine's maps, and no engine lights.** ADR-1221 accepts the same. Meshes added to such
  a scene would be drawn over by the pass, which writes every pixel. The scene is the whole picture by design.
- **Generation cost.** 2048² takes about 1.2-1.5 s and 4096² about 6-10 s (M2 Max, 12 threads), run at load and on a
  structural change. A live seed change stalls that long until generation moves to a worker (planned before
  Stage 6).
- **Height-field limits.** Vertical faces are stretched texels near the camera. Overhangs need the SDF landmarks of
  later stages, which this pass will evaluate in the same march.
