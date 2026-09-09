# Vegetation: plants from curves and a grammar

## The finding

A plant generator needed no new engine code. A species is a **grammar** (how it branches), a
**tube** (what an organ is), and **variation** (how individuals differ). All three already existed;
the tube was the only missing piece, and it is a primitive rather than a plant system.

```
grammar          recursive branching -> a placement per organ
  x
tube             a curve swept with taper and twist -> the organ's geometry
  x
variation        per-instance scale/rotation jitter from a seed
  =
a species, of which a seed selects one individual
```

`examples/organic/plants.json` is three individuals of one species: same grammar, same organ,
seeds 7, 23 and 41. They read as related rather than identical, which is the variation test the
brief asks for.

## The species grammar

```
shoot  = conditional(depthLimit) -> grow, else cap
grow   = repeat(segments, step = up + twist + shrink) -> segment, split
segment= place
split  = branch -> armA, armB
armA   = branch(pre = +splay)  -> shoot
armB   = branch(pre = -splay)  -> shoot
cap    = place(scale 0.55)
```

Recursion is `armA -> shoot`, and the `conditional` is what stops it.

**Depth counts every rule expansion, not just recursions.** One generation of this species costs
four levels (shoot, grow, split, armA), so a four-generation plant needs a depth budget near
twenty, not four. Set it to four and the plant renders as a single vertical stack with no branches
at all -- which looks like the grammar is broken rather than like the budget is spent.

## Species parameters

The morphological identity of a species lives in six numbers:

| Parameter | What it changes |
|---|---|
| `splay` | branch angle -- the difference between a column and a canopy |
| `twist` | roll per segment; low values make flat, fan-like plants |
| `shrink` | how fast a branch thins; the taper of the whole silhouette |
| `segments` | length between forks |
| `depthLimit` | generations, so overall size and twig density |
| curve `noiseAmount` | how much each organ wanders from straight |

Change the seed and you get another individual of the same species. Change these and you get
another species.

## What this does not do yet

- **Leaves.** The canopy is twigs. A leaf wants a flat profile, which the tube does not have --
  ribbons are the missing profile shape.
- **Materials.** The bark in the example is a plain dielectric and reads as bone. Bark wants the
  layered treatment: directional roughness, cracks that follow the branch, wetness in the crevices.
- **Tropism.** Real branches bend toward light and away from gravity. The grammar's step transform
  is fixed per rule, so every branch at a given depth bends identically; tropism would need the
  step to depend on the accumulated transform.
- **Placement.** These three stand in a row on a flat plate. Ecosystem scattering -- slope,
  moisture, light, competition -- is a later phase and is what turns plants into a forest.
