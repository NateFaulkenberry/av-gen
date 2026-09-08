# ADR-017: Scene composition and asset registry

- Status: Accepted (2026-09-08)
- Research: `docs/research/audiovisual-systems.md` §20 (lesson 8: scenes as reusable graphs),
  `docs/research/asset-pipeline.md` (asset identity, caching, reload)

## Problem

Until milestone 0.6 a "scene" was one hard-coded controller (the orb or one glTF file plus a
grid and a dust system). Milestone 0.7 needs scenes that are assembled from parts, saved as
files, reused inside other scenes, and driven by the same parameter and modulation system as
everything else, without loading the same glTF twice and without a separate scene-graph
runtime for the renderer.

## Alternatives considered

1. Keep one controller per asset type and let the project file pick one (status quo).
2. A general scene graph (nodes with parents, per-node components, traversal each frame) that
   the renderer walks.
3. A flat *composition* of nodes that is flattened into the existing `scene::Scene` (chosen).
4. Embedding compositions in the project file only (no standalone scene files).

## Decision

- `assets::AssetRegistry` owns loaded glTF scenes and images, keyed by resolved path, returned as
  `shared_ptr<const …>` with a version counter; `reload(path)` re-reads and bumps the version.
  Paths are resolved against a base directory (the scene file's folder) and relativised on save,
  so scene files move with their assets.
- `scene::Composition` is a `SceneController` made of `CompositionNode`s of kind `gltf`, `orb`,
  `grid`, `particles` or `scene` (another composition file, nested up to four levels, cycles
  refused). Each node has a rest transform, visibility, emissive and roughness overrides and, for
  particle nodes, a `ParticleSystem`. The composition flattens its nodes into one `scene::Scene`
  (meshes and textures shared per asset, entities per instance with pre-multiplied transforms,
  particle systems and lights appended), so the renderer is unchanged.
- Every node exposes `nodes/<name>/position|rotation|scale|visible|emissiveBoost|roughnessScale`
  parameters; nested scenes prefix theirs with `nodes/<name>/`, particle nodes get
  `particles/<name>/…`; the composition itself keeps the camera, environment, root and grid
  parameters the earlier controllers had, plus the same default audio routes.
- Scene files are JSON with `"format": "avgen-scene"`, version 1 (documented in
  `docs/project-format.md`); the engine routes a `.json` by its `format` field, so a project and
  a scene can both be opened with `--project`/`--composition`/drag and drop.

## Rationale

Flattening keeps the renderer, particles and post chain untouched: they still see one `Scene`
with entity, mesh and texture arrays, which is what a GPU-friendly renderer wants anyway. A
traversal-based scene graph would have been a second runtime data model whose only job is to
compute the world matrices the composition computes once per frame. Nesting is a load-time
expansion, so nested scenes cost nothing per frame beyond their entities, and namespacing the
parameters by node makes modulation routes and presets address nested content by stable paths.
The registry's version counter is the hook for asset hot reload (the `FileWatcher` from 0.4 can
drive `reload`), and returning shared pointers makes reload safe while a composition still holds
the previous version. Standalone scene files (not only in-project) are what makes a scene a
reusable asset in another scene or project.

## Consequences

- Positive: reusable and nested scenes, instancing without duplicated GPU data, all node
  attributes modulatable, scene files that are diffable and relocatable.
- Negative: no parenting between nodes inside one composition (a nested scene file is the way to
  group); per-node world matrices are recomputed on the CPU each frame (fine for hundreds of
  nodes, revisit for thousands); glTF animations, skins and cameras are still ignored.
- Follow-ups: asset hot reload through the registry version, per-node material overrides beyond
  emissive/roughness, node parenting if a real need appears, and a scene browser in the UI.
