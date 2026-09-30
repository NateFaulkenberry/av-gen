# ADR-1000: Procedural space is SDF data on the existing ray marcher

- Status: Accepted (2026-09-29), for the `proto/procedural-space` POC
- Full reasoning: `docs/prototypes/procedural-space/ARCHITECTURE.md`

## Context

The owner's brief (`docs/prototypes/procedural-space/00-brief.md`) asks for a live, audio-reactive
procedural "impossible architecture". It asks for the least invasive host, no first-class
"Euclidean Space" feature, no parallel scene format and no new dependency. ADR-027 already built an
SDF data tree, a GPU interpreter, a depth-composited ray-march pass inside `SceneRenderer`, and a
composition node kind `"sdf"` with per-node parameters. No example uses any of it.

## Alternatives considered

1. A user shader layer (ISF, milestone 0.4). Rejected: a full-screen image with no scene camera,
   lights, depth or composition with meshes.
2. A world effect (ADR-500 registry). Rejected: a first-class Add-button kind, which §12 of the brief
   forbids.
3. `ProceduralGeometry` (ADR-023). Rejected: instanced meshes with fixed topology and per-vertex
   deformers, so no booleans or domain folds.
4. A new render pass or scene type. Rejected: it duplicates `SdfRenderer` and creates a parallel
   format.
5. **Composition `"sdf"` nodes in an example project, with the SDF vocabulary and the pass extended
   generically (chosen).**

## Decision

The space is data: `examples/space/`. The engine changes are generic SDF features in the files that
own them: fold, recursion and morph nodes; `count` as a parameter; named nodes in parameter paths;
march and look parameters; sampled step statistics. The existing routes, scene states and timeline
drive it. If the interpreter proves too slow, the tree is compiled to WGSL on a structural change. A
second marcher is not written.

## Consequences

- Scene loading, presets, routes, states, the timeline, headless and offline render, and depth
  composition with meshes all work unchanged. The brief's §32 hybrid is the existing architecture.
- Performance is bounded by the interpreter and by the depth prepass's second march; both are
  measured in `PROGRESS.md`.
- Removal is deleting `examples/space/`. Each engine addition is a self-contained node kind or field
  (ADR-441).

## Revisit triggers

- The interpreter cannot hold an interactive frame at render scale 0.5. Then compile the tree.
- The owner promotes the look. Then name it as a procedural-environment preset library.
