# ADR-175: A branch's tier is its substance, not its depth in the graph

**Status:** Accepted
**Date:** 2026-09-14

## Problem

The tree generator produces axes with an **order**: the trunk is order 0, a lateral off the trunk is
order 1, a lateral off that is order 2. Order is a real and necessary quantity — it decides
parentage, phyllotaxis and which axis continues which — and mapping it directly onto the semantic
tier (`Trunk`, `Primary`, `Secondary`, `Tertiary`) is the obvious thing to do. That is what this
project did, and the tier is load-bearing twice over: the evaluator scores `primaryCount`,
`secondaryPerPrimary` and `tertiaryPerSecondary`, and the animation hierarchy is built from it.

## The finding

A candidate whose crown plainly showed a dozen radial limbs reported

```
primaryCount = 2          tertiaryPerSecondary = 193
```

Both numbers were **correct about the orders** and both were meaningless as descriptions of what the
image shows. That tree's trunk forks early, so almost everything a viewer reads as a primary limb is
an order-2 or order-3 axis. Ranked on those numbers, the evaluator placed **the best-looking tree in
the population eleventh of twelve** and preferred compact trees whose orders happened to line up
with their appearance.

It was caught because a contact sheet disagreed with the eye and the per-candidate metric breakdown
said which components were responsible. No structural check could have found it: every number was
internally consistent, the graph was valid, and the metric was computing exactly what it said.

## Decision

**Order decides parentage; thickness decides what something is.** `TreeAxis::tier` is assigned after
the pipe-model pass, from the axis's base radius as a share of the trunk's:

```
trunk:      axis 0
primary:    base radius >= 0.20 x trunk base radius
secondary:  base radius >= 0.055 x trunk base radius
tertiary:   everything else
```

## Rationale

What the eye ranks a limb by is how substantial it is relative to the trunk. It has no access to the
graph and no way to know how many forks separate a limb from the root; a thick limb is a primary limb
whether it left the trunk directly or three generations down.

The same argument holds for the animation hierarchy, which is the other consumer: a thick limb should
move like a thick limb whatever its depth. Deriving the tier from thickness makes both uses agree
with each other and with the picture, which is a stronger property than either one alone.

The thresholds are a judgement and are stated as one. They are not a fit to anything.

## Consequences

- That tree moved from 0.723 to 0.846 and now ranks with the other good ones.
- `measureTree` no longer applies its own "is this thick enough to read as a limb" threshold on top
  of the tier, because that is now the tier's own definition and applying it twice raised the bar
  arbitrarily.
- `tertiaryPerSecondary` had to be re-banded: with tiers by substance the measured population runs
  from 0 to 42, and the old 4-to-12 ideal put four candidates in five at or near zero.
- `validateTopology` still checks **order**, not tier. Order is what the graph invariants are about.

## The general shape, which is the part worth keeping

A metric can be correct and measure the wrong thing, and nothing inside the metric can tell you
which. This is the third time in this work that a rendered frame caught something the structural
loop could not — after an evaluator scoring 0.951 on an unusable image, and after two metrics that
were really reporting artefacts of how they were taken (`depthSpread`'s r² weighting measuring trunk
thickness, `structureVisible`'s pixel count measuring the rasteriser's minimum-disc clamp). The
standing conclusion is that **the rendered loop belongs in front of the structural one**, not beside
it.

## Verified vs assumed

**Verified:** the reclassification, on a rendered contact sheet where the reordering is visible; that
the four tiers remain non-empty and ordered by mean animation weight.

**Assumed:** that 0.20 and 0.055 are the right thresholds. They were chosen to make one population's
classification agree with one person's reading of one contact sheet.
