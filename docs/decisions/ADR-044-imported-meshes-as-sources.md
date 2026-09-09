# ADR-044: Imported meshes as instanced sources

## Status
Accepted, 2026-09-09.

## Context
Procedural geometry had reached its ceiling as *final art*. A generated rock reads as a generated
rock however much noise is applied to it, and the same is true of bark, moss, roots and leaves.
Production environments are built from scanned and hand-authored assets; the procedural system's
job is to decide where they go, how many there are, and how they vary.

The engine could already import glTF/GLB with full PBR (base colour, metallic-roughness, normal,
emissive, occlusion), but only as `Gltf` nodes: one asset, one transform, one draw. A forest needs
hundreds of instances of a handful of assets.

## Decision
`PrimitiveKind::Mesh` makes an imported asset the *source mesh* of a procedural object. Everything
downstream is unchanged: the same instancing, GPU culling, LOD, per-instance variation,
distributions, effectors and deformers that a generated primitive gets.

The Composition resolves the asset during flattening, where the scene's mesh and texture offsets
are known, and reuses the same per-asset offset map the `Gltf` nodes use, so two objects sharing
an asset share its uploaded meshes and textures.

An asset's entities are merged into one mesh, each baked by its own transform, because instancing
wants a single mesh. The material comes from whichever entity carries the most geometry -- for a
scanned asset that is *the* material -- unless the scene file authored a `material` block, in
which case the author wins. "The author wrote nothing" and "the author wrote the defaults" are
deliberately distinguished by `proceduralMaterialAuthored`.

## Consequences
The renderer stops being the artist. `tools/fetch_polyhaven.py` pulls a curated CC0 library into
`assets/`, and `examples/world/grove.json` is built from it: real photogrammetry rocks, roots and
vegetation, placed and varied procedurally, lit by the engine's own rig and atmosphere.

Two things this does not yet do. A multi-material asset collapses to one material, so an asset
whose trunk and leaves differ will take whichever has more vertices. And an asset's natural scale
is not recorded anywhere, which matters more than it sounds: the curated rocks range from 15 cm
stones to 8 m sets, and a scene that scales them by eye gets a field of same-sized boulders. That
is what the metadata in the asset brief is for.

## The bug this surfaced
Every imported asset first rendered magenta. The material was being resolved correctly during
flattening -- and then thrown away, because `applyProceduralParameters()` rebuilds the live object
from the node's *rest* copy every frame, and the rest copy still held the default `Material`,
whose base colour is `(0.75, 0.2, 0.9)`. A scanned albedo multiplied by that default is a magenta
rock.

Worth remembering in two ways. A default that is deliberately garish is excellent for catching an
unset material and terrible for diagnosing one, because it looks like a lighting problem: I
adjusted the sky and rewrote a light rig before checking the material itself. And any per-frame
"rebuild live from rest" pass silently discards anything written only to live.
