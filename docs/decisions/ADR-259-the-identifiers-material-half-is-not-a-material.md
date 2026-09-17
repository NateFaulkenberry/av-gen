# ADR-259: The identifier's "material" half is not a material, and the manifest had to be keyed on the half that is

**Status:** Accepted — implemented
**Date:** 2026-09-17
**Context:** Implementing ADR-256's surface-class mapping, and finding its premise wrong in a way
that makes its conclusion stronger
**Follows:** ADR-256 (a material id is not a class), ADR-242 (a target's meaning is not its layout),
ADR-243 (the artifact the reviewer objected to was grass), ADR-250 (the instrument is not the
engine), ADR-182 (a probe that cannot fail proves nothing)
**Amends:** ADR-256's description of what the identifier AOV contains

## Context

ADR-256 decided that `vegetationResidual`'s missing mapping could not be a hand-authored list of
integers, because a material id is a scene-build index and a list of indices is silently wrong the
moment the scene changes. The mapping had to be **emitted by the run that produced the AOVs**. That
argument is right and everything built here follows it.

Its premise about the data is not right, and the correction matters because it changes what the
manifest is keyed on.

## The finding: nothing in the identifier AOV is a material

ADR-256 says a material id is *"an index into the material array the scene build produced"*. There is
no material array. Materials are held by value on the objects that use them, and what the renderers
actually write into the identifier target's high 16 bits is this:

```cpp
// scene_renderer.cpp   — entities
obj.ids = glm::vec4(packPickId(PickSpace::Entity, thisEntity), thisEntity + 1, 1.0f, jointCount);
// procedural_renderer.cpp
obj.ids = glm::vec4(packPickId(PickSpace::Procedural, i), i + 1, 1.0f, 0.0f);
// sdf_renderer.cpp
obj.ids = glm::vec4(packPickId(PickSpace::Sdf, objectId), objectId + 1, 1.0f, 0.0f);
```

**It is the object's own ordinal within its pick space, plus one.** Three consequences, all of them
observable in one frame of the Quality Lab's own aliasing scene:

* Two objects sharing one material get **different** numbers there.
* An entity and a procedural with nothing in common get the **same** number: the scene's orb (entity
  0) and its left fence (procedural 0) both report "material 1".
* Nothing anywhere recovers a material identity from it.

So `masks.hpp`'s `materialClassMask`, which keys on that half, cannot select by material. It is a
well-formed mask over a well-defined set of pixels, computed correctly, named after something it is
not — which is ADR-242's own sentence, in the Quality Lab's code rather than in the engine's.

**The low 16 bits are the half that is unique.** They are `packPickId`: a two-bit `PickSpace` tag and
a 14-bit index, distinct across entities, procedurals and SDFs by construction. `materials.json` is
keyed on that, and says so in the file so that a reader is not left to assume:

```json
"key": "objectId: the low 16 bits of the id AOV ... NOT the high 16 bits, which carry the object's
        ordinal within its pick space and are named 'materialId' for historical reasons only"
```

**ADR-256's conclusion survives intact and its reasoning gets stronger.** It argued a hand-authored
list would go stale when the scene gained an object. Keyed on an object ordinal it would go stale
*faster* and in more ways — and it would also have merged classes across pick spaces, which no amount
of maintenance would have caught.

## What shipped

### 1. `scene::SurfaceClass` on `scene::Material`, defaulted, never authored

The enum ADR-256 named — `Unclassified`, `Vegetation`, `Terrain`, `Water`, `Rock`, `Architecture`,
`Character`, `Effect` — with `Unclassified` the default, so every existing material, every glTF
import and every scene file round-trips byte-identically. There is **no authoring path and no
serialisation change**: the class is set by generators and by nothing else. ADR-256 promised that
hand-written scene files would be unchanged; they are, because a scene file cannot say it. Adding a
file-format field for something no scene author has asked to control would be exactly what ADR-250
warns against, and it can be added the day somebody wants it.

### 2. The ADR-250 objection is real and the answer needs amending

ADR-256 anticipated the objection — a field on `scene::Material` for a tool's benefit — and answered
that a surface class is a fact about the world several systems already approximate separately. That
answer holds, but it understated the position: **a taxonomy already exists.**
`assets::AssetCategory` has fourteen values (Flora, Fungi, Rock, Crystal, Creature, Structure,
Terrain, Water, Architectural, Organic, …), and `entity::classifyAsset` already maps it to a
navigation type. ADR-256 did not weigh a second enum against that, and it should have.

Two enums are still the right answer here, for a stated reason: `scene/` sits **under** `assets/` in
the dependency order, and the asset library's categories describe a *file* while a surface class
describes a *surface in a frame*. What makes it safe is that the mapping is **one-way and lives in
the generator**, so the two cannot be independently authored and drift apart. Had the mapping been
duplicated at both ends, this would be the failure `entity::classifyAsset`'s own first comment warns
about — *"two classifications of the same thing that can disagree"*.

### 3. The generators set it where the knowledge is, and the fallback is honest about being keywords

| generator | class | count on Glowmere |
|---|---|---:|
| terrain chunks (`Composition::flatten`) | `Terrain` | 256 |
| terrain water sheets | `Water` | 21 |
| ecology scatter layers | from `assets::AssetCategory`, else the keyword fallback | 11 `Vegetation`, 1 `Rock` |
| everything else — glTF imports, orbs, grids, SDFs, city pieces, particles | `Unclassified` | 7 |

**`entity::classifyScatterLayer` is deliberately not the primary path**, even though it reads the
same field, because it is a *navigation* classifier and its collapse is wrong here: it maps Terrain,
Water, Particle and Atmosphere onto `Vegetation`, which is correct for "can a walker pass through it"
and false about what the surface is. It is used only as the **fallback**, where it is the
filename-and-layer-name keyword table and nothing else exists.

That fallback is where Glowmere lands, because its scene file predates asset categories entirely —
and it is visibly wrong in two places: **`valley_pebbles` and `valley_beacons` are classified
vegetation.** That is not worked around. Adding "pebble" to a second keyword table would create the
duplicate classification this ADR just argued against; the fix is to give the scene categories, which
improves both consumers at once. The misclassification is recorded in `vegetationResidual`'s own
limitations, where somebody reading the number will see it.

### 4. `materials.json`, written by the run, and re-checked at the end of it

`--aov id` writes it beside the frames: every entity, procedural and SDF, with its `objectId`, the
`materialId` the high half will hold, its pick space, its name and its class.

And one thing ADR-256 did not think of. *"A mapping that ships with the frames cannot disagree with
them"* is only true if the frames it shipped with are the frames it described — and ADR-091 live-tier
content, a rebuild on seek, or ambient population can move the object set **during** a render. So
`finish()` re-derives the manifest and compares. If it moved, the file is rewritten with
`"stable": false` and a reason, and the Quality Lab refuses to use it rather than measuring the wrong
surfaces confidently. That is the same failure the ADR is named after, one level further along.

### 5. `vegetationResidual` is a number

On Glowmere at 640×360, 12 frames, against a supersampled reference:

| metric | value | coverage |
|---|---:|---:|
| `temporal.motionCompensatedResidual` (ungated) | 1.114 | the frame |
| **`perClass.vegetationResidual`** | **2.800** | **18.74%** |

Vegetation is **2.5× less stable** than the frame average, over nearly a fifth of it. ADR-243's
reviewer objected to grass; this is the first number that is about the grass rather than about the
frame the grass is in. It is not a validation of anything — nobody has looked at these frames, and
ADR-257 is the receipt for what that distinction is worth — but it is the dimension that was
unavailable, available.

## Consequences

**The manifest is worth more than the metric, as ADR-256 said.** Any future per-class measurement —
specular over architecture, shading residual over characters — is now an `objectClassMask` call and a
line in the report. The masks were never the missing piece.

**One Quality Lab function is now named after something it does not do, and it is kept anyway.**
`materialClassMask` still keys on the high half. It is retained, with its true behaviour documented
and a unit test that *demonstrates* the merge across pick spaces rather than asserting it away,
because a renderer that one day writes a real material identity there will want it and because
deleting the evidence of a mistake is how the next person makes it again.

**What this does not say.** It does not say the identifier layout is wrong — an object ordinal is a
perfectly good thing for a material program to vary on, and `packPickId` is exactly what picking
needs. It says the field is named `materialId` and is not one, that a document written from research
believed the name, and that the cost of believing it would have been a confident number about the
wrong surfaces. It also does not say Glowmere's classification is *correct* — 11 of its 12 scatter
layers are classified by a keyword table over filenames, two of them wrongly, and the number above
inherits that.
