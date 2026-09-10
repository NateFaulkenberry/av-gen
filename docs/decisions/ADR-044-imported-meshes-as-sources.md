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

An asset's entities are grouped by material and each group merged into one mesh, baked by each
entity's own transform, because instancing wants a single mesh *per draw*. Each group is a *part*,
and each part becomes an ordinary `ProceduralGeometry` of its own: the same cloud, the same seed,
the same variation, culling and LOD, its own mesh and its own material. A scanned rock is one part;
a tree is bark and leaves. Part 0 is whichever carries the most surface area, and it is the one the
node's parameters are registered from. `SourceSpec::assetPart` is runtime-resolved like `assetMesh`
but *is* hashed, because the renderer caches source meshes by that hash and two parts of one asset
would otherwise collide.

A scene file's `material` block still overrides the asset's factors -- for every part, since the
author wrote one material -- while the parts keep their own maps. "The author wrote nothing" and
"the author wrote the defaults" are deliberately distinguished by `proceduralMaterialAuthored`.

## Consequences
The renderer stops being the artist. `tools/fetch_polyhaven.py` pulls a curated CC0 library into
`assets/`, and `examples/world/grove.json` is built from it: real photogrammetry rocks, roots and
vegetation, placed and varied procedurally, lit by the engine's own rig and atmosphere.

One thing this does not do. An asset's natural scale is not recorded anywhere, which matters more
than it sounds: the curated rocks range from 15 cm stones to 8 m sets, and a scene that scales them
by eye gets a field of same-sized boulders. That is what the metadata in the asset brief is for.

## Addendum, 2026-09-10: one draw per material

The first version merged a whole asset into one mesh and picked one material for all of it, so a
tree drew its leaves with the bark's texture. Two ways to fix that, and the difference is worth
recording.

The one that sounds right is sub-ranges: keep one source mesh and give the object a list of
`(indexOffset, indexCount, material)`, one indirect draw per range per LOD, sharing the instance
buffer and the cull results. It is not affordable here. `drawIndexedIndirect` args carry
`indexCount` and `firstIndex`, and `instanceCount` is written by the *GPU*, in `cull.wgsl`, into a
slot addressed by the object's stats index -- so a sub-range needs its own args slot, which means a
new args layout, a new `CullPassUniforms` (its `indexCounts` is one `uvec4`: four levels, one
range), and a change to the cull shader's ABI. Then LOD 2 and 3 are a single camera-facing billboard
for the whole object, which no sub-material owns, and the ADR-045 decimator clusters vertices across
the whole mesh, which would weld ranges together. Three of those are invasive and two have no
correct answer.

The affordable one is a part per material at flatten time. Each part is an *ordinary* object, so
every one of those mechanisms works unchanged and none of them was touched. It costs one more cull
dispatch and one more instance buffer per extra material. Measured on
`examples/world/terrain.scene.json` at 1440x900, where three of eleven scatter layers have two
materials: 21.12 ms minimum frame before, 21.01 ms after -- inside the noise, with the sky-only
calibration unmoved.

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
