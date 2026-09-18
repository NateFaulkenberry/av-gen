# ADR-285: An impostor is the size of the thing it stands in for

**Status:** Accepted
**Date:** 2026-09-18
**Context:** ADR-265 "Recorded because it cost something to learn"; docs/shadow-lab/README.md §3.3.
Found by the Shadow Lab, routed to the LOD Lab after the LOD Lab had already landed, and therefore
owned by nobody until now.

## Context

`makeLodMesh(spec, level, impostorSize)` sized the rung-2 billboard as
`2 * impostorSize * sourceBoundingRadius(spec)` -- from the **raw asset** -- and the billboard branch
of `shaders/procedural.wgsl` scales the quad by `inst.scale` alone and never applies
`proc.sourceMatrix`. Rungs 0 and 1 do go through `sourceMatrix`.

The transform chain has three steps and an impostor can only pick up two of them for itself. Its
corners are offsets in the camera's basis; that is what makes it an impostor, and it is also what
takes it out of the path the source transform travels. So the one step that could not reach it was
step 1 -- and step 1 is exactly where every terrain scatter layer puts its normalisation:

> The layer says how tall the thing should be; the asset says how tall it is. The normalisation goes
> on `sourceTransform`. -- `Composition::flatten`

Reproduced by the Shadow Lab with a grid of `CommonTree_1` shrunk to 0.5 m: at rung 2 they drew as a
hedge of 7.3 m trees, fourteen times too tall, in the camera pass as well as in the shadows.

## Decision

`makeLodMesh` takes the source transform's scale and sizes the quad through it. The size is the only
place the scale can reach an impostor, so that is where it goes -- not in the shader, where the whole
point of the billboard path is that it does not travel `sourceMatrix`.

`sourceBoundingRadius` gains an overload that takes the scale **inside** the length rather than
outside it, so a non-uniform source transform gives the sphere about the scaled box and not the
unscaled box's diagonal multiplied by whichever axis a caller happened to pick. It is the same
product `ProceduralGeometry::rebuild` already takes for its instance bounds, which is what makes the
mesh an impostor is built at and the sphere the cull tests it with descriptions of one object.

`ProceduralRenderer::Impl::ensureLodMesh` mixes the source scale into the LOD mesh cache key. Two
scatter layers over the same asset differ in exactly that -- a 0.45 m fern layer and a 14 m canopy
layer share a `meshHash` -- and before this they would have shared a quad.

## What it changes, measured

**Nothing on Glowmere, and that is the correct answer rather than a disappointing one.** Glowmere
multicam, frame 60 at 1280x720, `--tier realtime`: the frame is **byte-identical** across the fix
(`ed6cf9ed41f4...`), and every submission counter is unchanged to the triangle.

The reason is ADR-263's other half. `Composition::flatten` builds every scatter layer as
`PrimitiveKind::Mesh` with an `assetMesh`, and `lodLevelIsImpostor` is false for all four rungs of
one of those -- since ADR-085 those rungs are simplified meshes in the source's own space, which do
travel `sourceMatrix`. So no instance in Glowmere reaches the billboard path at all, and an
identical frame is positive evidence that this change touches the impostor path and nothing else.

The defect is still real and still reachable: it fires for any procedural object with a generated
primitive source, a multi-rung ladder, and a non-identity `sourceTransform.scale`, which is what a
scatter layer whose asset failed to load is, and what a hand-authored ladder over a primitive is.

## Consequences

`tests/unit/test_shadow_lab.cpp` "a LOD impostor is the size of the object it stands in for" loses
its `[!shouldfail]` tag and becomes an ordinary test with five sections: a scaled layer, a
non-uniform scale in both directions, a thin source, rung 3's eighth, and the invariant behind the
arithmetic -- that the world size the quad is built at is the world size rung 0 draws at, measured
off the two meshes rather than off a formula.

The control is the section that keeps `sourceTransform.scale` at 1 and asserts the quad is the size
it always was, because the wrong rule and the right one agree on an unscaled source and a test made
only of those would pass either way (ADR-182).
