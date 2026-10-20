# ADR-1572: A gltf node can be drawn through a material program

- Status: Accepted (art/rainbow-world-2)
- Amends the scene file's gltf node, beside ADR-1547 (`castsShadow`). Additive: a node without the key draws exactly as
  before and writes no key.
- Code:
  - `src/scene/composition.hpp`: `CompositionNode::program`;
  - `src/scene/composition.cpp`:
    - reads the key with the node's other fields and refuses a non-string;
    - writes it only when set;
    - copies it in `cloneNodeSpec`, with `castsShadow`, which was also missing there;
    - in the per-frame node application, sets each of the node's entities' `material.program` to it, prefixed for a
      nested scene as every other program name is.
- Tests: `tests/unit/test_node_program.cpp` (`[adr1572]`):
  - the program reaches the entity through attach and three frames, and resolves to a program the scene carries;
  - a node without the key is untouched;
  - it round-trips and is written only when set;
  - a non-string is refused.

## Problem

Rainbow World's arches are glTF tubes: alpha-blended, with a baked spectral emissive texture. The stem-reactivity pass
needs light to travel along them: bands that flow with the synth, and fronts that run from the feet to the crown on
the backbeat. That is a function of the tube's UVs and of routed constants, which is what a material program does.

Two existing routes did not work:

- A procedural node (mesh source) can carry a program, but it draws blended materials opaque
  (`procedural_renderer.cpp`: "Blend materials are drawn opaque in this phase"). The arches came out as solid navy
  tubes.
- A gltf node had no way to name a program. The pbr path entities draw through, including the blended lit variant,
  already evaluates one (ADR-030); only the scene file could not reach it.

## Decision

A gltf node may carry `"program": "<name>"`:

```json
{"name": "rainbow_0p", "kind": "gltf", "asset": "rainbow_primary.glb", "castsShadow": false, "program": "rw_rainbow"}
```

- It names one of the scene's `materialPrograms`.
- Every entity of the node draws through that program in place of whatever its material named.
- The asset keeps its shape, UVs, textures and alpha mode, so a blended asset stays blended, and a program that
  writes opacity sets its coverage.
- It is not a parameter: like ADR-1547 it decides how the node is drawn, not what it is. The program's constants are
  parameters already (`material/<program>/op/<n>/...`), and that is where a route moves it.

## Consequences

- Rainbow World's arches are drawn by `rw_rainbow` / `rw_rainbow2`. These rebuild the bow's spectrum and coverage from
  the UVs and add the moving terms (`docs/prototypes/rainbow-world/16-effect-mapping.md` §4).
- An unknown name renders the asset's own material and is reported by the existing unresolved-program check
  (`scene.hpp`), as for any surface.
