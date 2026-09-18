# ADR-330 — A scale is a ratio, and Glowmere had lost both ends of it

**Status:** Accepted
**Date:** 2026-09-18
**Follows:** ADR-213 (the 3.6x), ADR-218 / ADR-262 / ADR-271 (the beam and what it contains),
ADR-182 (a probe that cannot fail), ADR-225 (a setting nobody keeps)

## Context

> "I think in v2 we ended up having to make models etc much larger to fit the scale of the world."

and, asked to choose between two coherent worlds:

> "glowmere scale - flora comes down"

ADR-213 records how the world got here, and every step in the chain was locally reasonable. The four
aliens were scaled up to sit beside a world authored large. Then the farm animals arrived at their
true metre sizes, a 1.57 m cow stood next to a 6 m alien, and the report was *"all the new animals we
added in glowmere need the same upscaling as we did to the 4 aliens, they are looking small compared
to everything"*. So the animals were scaled to the aliens, which had been scaled to the world. Each
link was measured against the previous link and none of them was measured against the ground.

## The measurement that says which end was wrong

Glowmere's assets are authored at correct real-world scale and then multiplied to fit:

| | native | at the applied scale |
|---|---:|---:|
| bull | 1.771 m | **6.376 m** (3.6x) |
| cow | 1.574 m | 5.666 m |
| alien-scout | 1.662 m | **5.999 m** (3.61x) |
| sheep | 0.941 m | 3.388 m |

And the world they stand in, from the valley's own scatter layers -- `height` is the metres one
instance occupies, `maxScale` the largest the layer will place:

```
pines      10.5 - 21.0        fan-plants   2.08 - 5.12
canopy     10.5 - 18.9        boulders     0.80 - 3.52
deadwood    8.4 - 15.6        beacons      1.12 - 2.40
                              ferns        0.91 - 2.10
hero fungi  3.4 - 16.0        bushes       0.77 - 1.54
                              grass        0.42 - 1.12
```

Two facts fall straight out of that table and neither of them is about the fungi.

**The cast was walking over the undergrowth rather than through it.** `fan-plants` is the tallest
thing the terrain scatters that is not a tree, and it is *everywhere*, which a tree is not: 3.2 m of
frond, up to 1.6x, so 5.12 m at the largest instance the layer can place. A 6.38 m bull is taller
than that. Every frame with ground in it showed a body standing over the plants it should have been
wading through -- and that is why the world read as a diorama in the establishing shot, where the
animals are legible and the trees are two hundred metres away.

**And four of the ten hero fungi had become hats.** The gill line -- the underside of the cap,
measured off the vertices by `mushroomAnchors` -- sits at about 0.74 of an organism's height for
every one of the ten. At the sizes the scene carried, seven of the ten had their gills below the
bull's head and four below the alien's: veil's gills at 2.64 m, ember's at 3.07 m, spire's at 3.11 m,
ridge's at 3.83 m, against a 6.38 m bull. The complaint that started ADR-213 -- "they are looking
small compared to everything" -- was true of the *elder*, at 16 m, and the fix was applied to the
cast, which made it false of the other nine in the opposite direction.

So the chain did not drift in one direction. It broke the relationship at both ends at once.

## The target relationship

Three ratios, argued rather than tuned, and each one is a `CHECK` in
`tests/unit/test_glowmere_scale.cpp` rather than a sentence in a document:

1. **The undergrowth is undergrowth.** No body in the cast is taller than the largest instance the
   `fan-plants` layer will place. This is the arm that is visible in *every* frame with ground in
   it, which is why it comes first.
2. **Every hero fungus is a canopy the cast walks under.** Each organism's gill line clears the
   tallest body in the cast by a quarter of that body again. A canopy you have to duck under is a
   canopy you cannot be filmed standing under.
3. **The signature organism is monumental, and it stops there.** The tallest hero fungus stands
   between **four and eight** of the cast's tallest body. Below four it is a big plant and the world
   has no landmark; above eight the eye stops reading it as an object a figure has a relationship
   with, and the figure at its foot becomes a scale marker rather than a character.

The third is the one that says what "flora comes down" means, and it is the only one with a control
on **both** sides. The old world fails it low: 16 m of elder against a 6.38 m bull is 2.5 bodies, a
mushroom the cast could climb. And a world where the cast comes down and the flora does not fails it
high: 16 m against a 1.77 m bull is **9.0 bodies**. That second control is not hypothetical. It is
the arm this change was rendered against before a single flora number was touched.

## Decision

**The cast returns to its authored size and the hero fungi come down to meet it. The trees stay.**

### The cast: 1.0

Every farm animal from 3.6 and every named alien from 3.344-3.61 to **1.0** -- the metres the GLBs
were authored in, which is a number somebody chose rather than a number anybody tuned.

A node scale is never alone. A farm animal's `gait.walkSpeed` is its clip's measured stride speed
*times the node scale* (ADR-204, ADR-226, asserted today in `test_farm_locomotion.cpp`), so every
metres-per-second and metres-per-second-squared beside it moves with the body: `walkSpeed`,
`runSpeed`, `runEnter`, `runExit`, `moveEnter`, `moveExit`, `accel`, `decel`, and the wander
behaviour's `speed` and `runSpeed`. Eight to nine fields per entity, twenty-one entities, four
scenes. The bull's `walkSpeed` lands on 1.6689 against the 1.6688 m/s
`tests/unit/test_farm_locomotion.cpp` measured off the clip -- which is the check, not a
coincidence.

Territories do **not** scale. `homeRadius`, `minRange` and `maxRange` are metres of world and the
world has not changed; a 1.0x animal with an 18 m home range simply has more of it, which is the
direction ADR-213's *"very little movement areas"* was pushing against but is the honest reading of
what those fields mean.

### The hero fungi: a compression, not a multiplier

A uniform multiplier is the wrong shape. Scaling the whole ladder by 0.625 takes the elder to 10 m
and the veil to 2.1 m -- below a beacon mushroom and barely above a big fern, at which point the
smallest of the ten signature organisms has fallen out of the signature and into the scatter. The
ladder has to compress toward its foot, not slide down.

So the heights are a power law anchored at both ends -- the elder to 10 m, the veil held near its
present 3.4 m -- which preserves the ordering the search produced and pulls the top down:

| | before | after | | before | after |
|---|---:|---:|---|---:|---:|
| elder-2 | 16.0 | **10.0** | umbra | 5.5 | 4.5 |
| bloom | 9.0 | 6.5 | ridge | 5.0 | 4.1 |
| cairn | 7.5 | 5.6 | spire | 4.2 | 3.6 |
| lantern | 6.5 | 5.1 | ember | 4.0 | 3.5 |
| scree | 6.2 | 4.9 | veil | 3.4 | 3.1 |

A hero organism is **one number**. `tools/make_glowmere_valley_2.py` derives ten quantities from a
hero's height and they were checked against the shipped scene, exactly, before a byte was edited:
the generator's `sourceTransform.scale` on all four part nodes, the spore emitter's local offset,
its disc radius, spawn rate and pool capacity, and the hero declaration's crown position, `radius`,
`height`, `preferredCameraDistance` and `activationRadius`. Changing the height and leaving any one
of the other ten is the defect `test_mushroom_alignment.cpp` and `test_glowmere_valley_2.cpp`
already catch.

### The trees stay

This is the part of the brief that the evidence contradicted, and it is recorded as such.

### What the beam had to be told

`liftHeight` is a distance from the beam's **mouth** to the **centre of the body it is lifting**
(ADR-262: `to: actor.beam`, `anchor: drawn`, `place: drawn`), so it is a claim about the cast and it
scales with the cast: **-6.6 → -1.83**. At -6.6 a 1.77 m bull would hang nearly six metres below the
emitter disc; at -1.83 its back sits 0.94 m under the mouth, which is where a 6.38 m bull's back sat
relative to its own size. The beam's own radius is untouched: the owner set it to 0.42 m by hand
(ADR-271) and that is an authoring decision about a world with a native-scale cast in it.

`navBodyRadius` 2.4 → 0.67 m and `navWadeDepth` 1.6 → 0.44 m for the same reason. A wade depth of
1.6 m is shin-deep on a 5.67 m cow and drowns a 1.57 m one.

## Consequences

- `examples/world/glowmere-valley-2{,-multicam,-song}.scene.json` and
  `glowmere-atmospherics.scene.json`, and the four projects beside them.
- `tools/make_glowmere_valley_2.py`: `HERO_SITES` carries the new heights, so a regeneration
  reproduces the file rather than reverting it.
- `tests/unit/test_glowmere_scale.cpp`.
- **Numbers that moved and are measurements rather than regressions**: see the report.

## Rejected alternatives

- **Scaling the flora uniformly.** It takes the bottom of the ladder out of the signature; see
  above.
- **Leaving the fungi alone and only shrinking the cast.** Rendered, looked at, and it fails arm 3
  at 9.0 bodies. It is also the control that arm keeps.
- **Bringing the trees down with the fungi.** Rendered. See the report.
- **Re-widening the tractor beam.** The owner narrowed it to 0.42 m by hand for a world that had not
  arrived yet (ADR-271); the change makes their number right rather than wrong.

## Revisit triggers

- **A second Glowmere world with a different cast.** `glowmere-stylized` is not this world -- it has
  one `wanderer` at 0.08x of a different alien and its own procedural architecture -- and it is
  deliberately untouched.
- **A hero organism authored by hand rather than by the generator.** The ten-quantities-from-one-
  number derivation is the generator's, and a hand-placed hero would have to carry its own.
