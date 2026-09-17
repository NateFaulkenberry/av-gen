# ADR-256: A material id is not a class, and a list of integers would go stale in silence

**Status:** Accepted — recommendation settled, implementation not started
**Date:** 2026-09-16
**Context:** The Quality Lab's `vegetationResidual` reports `unavailable: no material-id to class
mapping exists`. Asked how to fill it, the answer was "go with your best recommendation."
**Follows:** ADR-242 (a target's meaning is not its layout), ADR-225 (a setting the application does
not keep is not a setting), ADR-243 (the artifact the reviewer objected to was grass), ADR-250 (the
instrument is not the engine)

## Context

`masks.hpp` ships `materialClassMask(id, materialIds)` — tested against a synthetic identifier plane,
so it is not dead code — and the metric that would use it is unavailable because nobody has written
down which material ids are vegetation.

This matters more than a missing row in a report. **ADR-243's reviewer objected to grass.** The
artifact that started the whole Quality Lab was wind-animated sub-pixel vegetation, and
`vegetationResidual` is the metric aimed squarely at it. Of the seven unavailable dimensions it is
the one with a named human complaint behind it.

## Why the obvious answer is wrong

The obvious answer is a config file:

```json
{ "vegetation": [12, 37, 38, 41] }
```

A material id in this engine is an **index into the material array the scene build produced**. It is
assigned by construction order. Add a rock to Glowmere, regenerate with a different seed, or reorder
a scene file by hand, and every id after the insertion point shifts.

A list of integers would then be *wrong* — and wrong in the way this repository has an ADR about. It
would not fail. `materialClassMask` would return a perfectly well-formed mask over whatever material
now holds id 37, `MotionResidual::over` would compute a perfectly correct residual of it, and the
report would print a confident number about the wrong surfaces, with its coverage percentage
looking entirely plausible. That is ADR-242's normal pass and ADR-251's corner-of-the-frame EXR in a
third target: *a file that passes every test except the one that asks whether it contains the thing
it is named after*.

**So the mapping cannot be authored beside the run. It has to be emitted by the run that produced the
AOVs.** Anything else is a number that decays silently.

## Recommendation

**Give `scene::Material` a surface class, and have the AOV export write a manifest.**

1. **`scene::Material` gains one field.** A small enum — `Unclassified` (the default),
   `Vegetation`, `Terrain`, `Water`, `Rock`, `Architecture`, `Character`, `Effect` — defaulted so
   that every existing material, every glTF import and every hand-written scene file is unchanged
   and every serialisation round-trips byte-identically until somebody sets one.

2. **The generators set it where the knowledge already is.** `world/ecology.cpp` knows a grass blade
   is grass; the terrain generator knows the ground is ground. Nothing downstream does, and nothing
   downstream can recover it. This is the whole argument for the field's existence: the class is
   *authored*, not inferred, and it is authored at the only point in the pipeline where it is known
   for free.

3. **`--aov id` writes `materials.json` beside the frames**: `{ "12": "vegetation", "13": "terrain" }`
   for this render, from this scene, at this seed. The Quality Lab reads that, not a file somebody
   maintains. A mapping that ships with the frames cannot disagree with them.

4. **The metric reports coverage, as every masked metric here already does.** A vegetation residual
   over 0.4% of the frame is a statement about 0.4% of the frame, and `vegetationResidual` is
   meaningless without the number beside it.

### What makes this the recommendation rather than the only option

Three alternatives were considered and rejected for stated reasons:

| option | why not |
|---|---|
| **a hand-authored id list** | silently wrong the moment the scene changes — the failure above |
| **match on material or node names** | `scene::Material` has no name field at all, and a material is shared across nodes, so there is nothing to match on |
| **reuse an existing "is vegetation" flag** | there isn't one. Wind is applied procedurally and reaches no per-material flag; `Material::program` is a procedural-material program name, not a taxonomy. Checked before proposing a new field |

### The ADR-250 objection, and the answer to it

Adding a field to `scene::Material` for a tool's benefit is exactly what "the instrument is not the
engine" warns against, and the objection is fair. The answer is that a **surface class is not a
Quality Lab concept**. It is a fact about the world that several existing systems approximate
separately — wind chooses what sways, ecology chooses what scatters where, the LOD policy chooses
what may flatten. Naming it once, on the material, is engine work that the Lab happens to be the
first consumer of. If that turns out to be wrong — if nothing else ever reads it — the field is a
default-valued enum and removing it costs one commit.

## Consequences

**`vegetationResidual` becomes available for Glowmere and stays unavailable elsewhere, honestly.** A
scene whose materials nobody classified reports `no classified materials in this render` rather than
measuring the whole frame and calling it vegetation. That is the same degradation contract every
other optional dimension in the Lab already keeps.

**The manifest is worth more than the metric.** Once `--aov id` ships a material table, any future
per-class measurement — specular over architecture, shading residual over characters — is a
`materialClassMask` call and no new machinery at all. The mapping was the missing piece, not the
masks.

**What this does not say.** It does not say material ids are badly designed. An index is the right
thing for a renderer to put in a G-buffer and exactly what `packPickId` needs. It says an index is
not a name, and a measurement that wants a name must be handed one by whoever knew it.
