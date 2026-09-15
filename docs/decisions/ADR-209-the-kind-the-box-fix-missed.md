# ADR-209 — The kind the box fix missed

**Status:** accepted · 2026-09-15
**Follows:** ADR-188 (parenting gets an appearance), ADR-199 (two bugs from one assumption)

## Context

The world editor's selection box was reported wrong three times, with screenshots:

1. "these yellow boxes in the world editor are not drawing properly still. they are not around the
   actual object nor are they the right size - still often too big" — `elder-2-stem`
2. "another example, look how low the yellow box is compared to the elder cap" — `elder-2-cap`
3. Then a formal mandate: *"Do not assume any previous fix was correct."*

ADR-188 replaced the cull sphere with a tight extent and inherited a bad centre. ADR-199 found the
cause — `sourceHalfExtent` returns `max(|vertex|)`, which is a half-extent only for geometry centred
on its own origin — and fixed it by measuring a true box from the geometry.

**ADR-199 fixed `Mesh` and `Tube`. It did not fix `Generated`.** Every hero mushroom in Glowmere is
a `Generated` source. So the second screenshot was of the first fix not applying, and the mandate
was right to refuse to assume otherwise.

## The defect

`primitiveBoxImpl` branches on three kinds and lets everything else keep the pre-ADR-199 pair:

```cpp
centre = glm::vec3(0.0f);
half   = sourceHalfExtent(s);
```

And `sourceHalfExtent` has no `case PrimitiveKind::Generated` at all, so a generated source falls
through `case Torus: default:` and returns

```cpp
{ majorRadius + minorRadius, minorRadius, majorRadius + minorRadius }
```

— **torus dimensions, from fields a mushroom never sets.** With the defaults that is a fixed
`2.5 × 0.5 × 2.5` box at the source origin, *the same for every generated source in the scene
regardless of its real size or shape*, scaled only by the node's transform.

Both reported symptoms fall out of that single missing branch: the wrong size comes from the torus
fields, and "how low the box is compared to the cap" comes from the zero centre — a cap authored
fifteen metres up its own stem gets a box on the ground.

## Reproduction

`"a generated source gets a box around the geometry it generates"`, in `test_composition.cpp`. It
registers a generator returning a slab occupying `y ∈ [10, 12]`, `x, z ∈ [−1, 1]` — so the expected
centre is `(0, 11, 0)` and the expected half-extent `(1, 1, 1)`, **computed from the mesh the test
itself defines rather than from the function under test.** A bounds test that asks the bounds code
what the bounds are cannot fail.

Against the pre-fix implementation, five of its six assertions fail:

| | centre | size | min.y |
|---|---|---|---|
| expected | (0, **11**, 0) | (2, **2**, 2) | 10 |
| measured before | (0, **0**, 0) | (2.5, **0.5**, 2.5) | **−0.25** |
| measured after | (0, 11, 0) | (2, 2, 2) | 10 |

`min.y = −0.25` for geometry that begins ten metres up: the box was below the object by its whole
height, which is exactly what the screenshot showed.

A generator that is *not* a torus, a tube or a mesh was chosen deliberately — the question is what
happens to a kind the box code has no branch for, and using one of the three handled kinds would
have tested nothing.

## Decision

Handle `Generated` the way `Mesh` is handled: generate the geometry and take its real minimum and
maximum.

The mesh is memoised by `generatedMeshForBounds`, keyed on generator, version, part and the whole
parameter vector — because `values` is what a generated source *is* (ADR-175), and two mushrooms
differing in one number are two different shapes. Not keyed on the node's name or transform: those
move the box, they do not change it. A cache rather than a rebuild per call because
`Composition::nodeBounds` runs every frame the editor draws a selection and a mushroom generator is
not free. A generator that refuses is remembered as a refusal, so a broken source is not re-run
every frame; the caller then keeps the symmetric fallback, which is what it had before.

## Visual evidence

`tools/avgen_overlay_shot --scene examples/world/glowmere-valley-2.scene.json --select elder-2-cap`
now draws a broad, shallow box with the gizmo at its centre — the shape a mushroom cap is. Before,
every generated source drew the same flat 2.5-metre slab at its origin whatever it actually was.

## What this does not settle

**`Cylinder` was examined and left alone.** Its branch sets `centre = 0` under a comment claiming
the primitive is "authored from its base", which would make the centre wrong — but
`makeBeveledCylinder` works in `halfHeight = height * 0.5`, i.e. the mesh is centred on its origin,
so the code is right and the comment is misleading. Changing it on the strength of the comment would
have broken a correct case.

**The remaining kinds are still unbranched** — `Box`, `Sphere`, `Point`, `Torus` — and for those the
symmetric fallback is correct, because each of those primitives *is* centred on its own origin.
`Procedural` resolves through the reference. So the set is now complete, but it is complete by
argument rather than by construction: a tenth primitive kind added later will silently inherit the
fallback again, which is how this bug survived ADR-199. The durable fix would be for
`sourceHalfExtent` to have no `default:` case, so a new kind fails to compile rather than falling
through to a torus. That is recorded, not done.
