# ADR-1547: A gltf node can stay out of the shadow maps

- Status: Accepted (art/rainbow-world)
- Amends the scene file's gltf node, and is the gltf twin of ADR-1545 (procedural nodes). This change is additive. A
  file without the key casts shadows exactly as before, and a casting node writes no key, so every existing save is
  byte-identical.
- Code:
  - `src/scene/composition.hpp`: `CompositionNode::castsShadow`.
  - `src/scene/composition.cpp`:
    - reads the key with the node's other booleans, and refuses a non-boolean;
    - writes it only when false;
    - during the per-frame node application, clears each of the node's entities' `castsShadow`. It never sets it,
      so an asset whose own entity does not cast is left alone.
- Tests: `tests/unit/test_node_casts_shadow.cpp` (`[adr1547]`).
  - Two nodes on the same triangle glTF, one with the key. After attach and three frames of per-frame application,
    only the keyed node's entity is out of the shadow maps.
  - The save writes the key on that node only.
  - A non-boolean value is refused.

## Problem

Rainbow World's rainbow arches are glTF tubes: an alpha-blended, front-faced mesh with a spectral emissive texture
(07 §2). The shadow pass draws translucent entities as casters. Each arch threw a solid dark arc across the meadow
beneath it. This was confirmed by rendering the same frame with and without the arches
(`~/Desktop/av-gen-review/74-rainbow-world/stage6/`).

A rainbow is light. It cannot shadow anything. The engine already carries `Entity::castsShadow` and honours it, but
a scene file had no way to set it on a gltf node.

## Decision

A gltf node may carry `"castsShadow": false`:

```json
{"name": "rainbow_0", "kind": "gltf", "asset": "rainbow_arch.glb", "castsShadow": false}
```

- It is optional and defaults to true. It is written only when false.
- It is not a parameter. Like ADR-1545, it decides which passes draw the node, not what the node is.

## Consequences

- Rainbow World's arches no longer shadow the meadow.
- No shipped scene sets the key, so no shipped frame changes.
