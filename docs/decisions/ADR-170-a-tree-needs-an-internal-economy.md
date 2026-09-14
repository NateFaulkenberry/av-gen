# ADR-170: Space colonization decides where a tree grows; apical control decides how much

**Status:** Accepted
**Date:** 2026-09-14

*Numbered from 170 because two other agents are working concurrently and the index ends at 158.
Expect to renumber on merge.*

## Problem

The Tree of Life brief asks for a hero tree whose structure is generated rather than imported, with
a readable TRUNK → PRIMARY → SECONDARY → TERTIARY hierarchy, a controllable global silhouette,
controlled asymmetry, and per-branch metadata suitable for hierarchical animation. It names Space
Colonization as the leading candidate and explicitly warns against committing to it prematurely.

ADR-043 left this exact seam open: "Branching in particular belongs a level up: a branch is a tube
whose curve starts on another tube's curve, which is a generator concern rather than a primitive
one." `makeTube` and `spatial::Spline` supply the primitive; nothing supplies the generator.

## Decision

**The self-organising model of Pałubicki et al. (SIGGRAPH 2009), with space colonization as its
environmental term, an apical control that decays over simulated age, and the pipe model with shed
memory.** Runions for *where*; Borchert-Honda for *how much*.

Implemented in `src/scene/tree.{hpp,cpp}`; full comparison and citations in
[docs/research/procedural-trees.md](../research/procedural-trees.md).

## Rationale

Space colonization on its own cannot produce this tree, and the reason is structural rather than a
matter of tuning. Its defining property is that **every node is equal**: a node either has attraction
points in range or it does not. There is no quantity in the algorithm that says "this limb matters
more than that twig", so nothing makes a trunk and the branch statistics come out uniform, which is
the coral/noodle look the brief rules out. Every reference implementation admits this by prepending
a straight trunk before colonization begins -- `jakobrichert/space-colonization` has a literal
`Trunk Height: 6.0` parameter and `dsforza96/tree-gen` does the same.

Pałubicki et al. keep the attraction cloud and add the internal signalling it lacks. Light flows
basipetally, a resource flows back acropetally, and at every fork it splits by

```
v_m = v · λQ_m / (λQ_m + (1−λ)Q_l)      v_l = v · (1−λ)Q_l / (λQ_m + (1−λ)Q_l)
```

λ > 0.5 favours the main axis (excurrent: one dominant leader), λ < 0.5 favours the laterals
(decurrent: spreading). Decaying λ over the run reproduces the paper's own account of how a
temperate tree ages -- excurrent when young, decurrent when old -- which is precisely the Tree of
Life outline: a massive single bole under a broad spreading crown.

Two further properties decided it against the alternatives. **Branch length is an output, not a
parameter**: a bud holding resource `v` makes `n = ⌊v⌋` metamers of length `v/n`, so a vigorous bud
makes a long many-segment shoot and a starved one makes nothing. And **every parameter is a real
number**, which is what makes the candidate search's perturbation stage meaningful — a property
L-systems do not have at any price, because grammars do not interpolate.

## Alternatives considered

**Plain space colonization (Runions et al. 2007).** Rejected as the primary generator for the reason
above; **adopted for the environmental term**, which is the half it is good at. The attraction cloud
*is* the silhouette, which is the most direct control over the outline the system has.

**L-systems.** Rejected. Self-similarity is visible, the envelope has to be bolted on separately
(topiary pruning volumes exist for exactly this reason), and the control surface is a grammar, which
cannot be searched by perturbation.

**Recursive / fractal branching (Weber & Penn).** Rejected as the primary generator: it reads as a
mathematical fractal, which the brief forbids, and branches interpenetrate because nothing knows
about anything else. **Adopted for the roots**, at depth 2-3 with under a dozen axes, where nobody
perceives self-similarity and the control it gives is exactly the control wanted.

**Shadow propagation** (the other environment in the same paper). Rejected: it gives emergent form
but no direct silhouette control, and the brief asks for a controllable global silhouette.

**Growing the roots with the same simulation, downward.** Rejected: it produces a mirrored crown,
which is a different bad look rather than a good one, and it has no notion of a ground surface.

## Departures from the paper, and why

**Q is graded, not binary.** The paper's space-colonization Q is 0 or 1. With a binary Q the BH split
collapses -- `λ·1/(λ·1 + (1−λ)·1)` is just λ regardless of what the environment said -- and the
paper itself notes binary Q makes Takenaka shedding unusable. Q here is the claimed-marker count
normalised by a perception capacity. The cost is that the paper's tuning numbers become starting
points rather than answers.

**A bud that has never found space still receives a trickle.** Without it the seedling, sitting below
a crown envelope that holds no markers within reach, receives nothing and the simulation deadlocks at
one node. It is conditional on *never having fed*: left unconditional, the terminal bud sails out
through the top of the envelope and keeps going, because apical control keeps handing it the lion's
share of a resource it can always generate. The tree came out nine units taller than the volume it
was asked to fill, which is the envelope ceasing to be the silhouette control.

**The crown envelope carries a narrow trunk corridor to the ground, reserved for order-0 buds.** This
is what makes the bole a generated structure rather than a prepended stick: the trunk's height,
taper and slight lean are outputs of the same competition as everything else. Reserving it matters --
without the reservation, a lateral bud low on the bole sees straight up the column, colonises it, and
survives as a branch a metre and a half off the ground. Measured across six seeds, the bole height
tracks the envelope's crown base to within half a metre, which is the evidence that it works.

**Shedding is judged against the crown's own mean, not a fixed bar.** This one cost two wrong
answers. Takenaka's rule compares a branch's light against its size; applied against a fixed bar it
works while the cloud is rich and then destroys the tree, because depletion is a property of the
crown rather than of any branch, so every branch fails within an iteration or two of each other.
Normalising by age made it worse: a branch that has stopped growing then looks worse every iteration
forever, so the first shed starts a death spiral -- the crown went from 4,078 nodes at 16 iterations
to 35 at 20. A relative threshold has no drift and cannot remove everything, because something is
always above the mean.

**Width is not reduced when a branch is shed.** This is the paper's, not ours, and it is the single
sentence the trunk design rests on: the model requires a memory of past leaves and branches. A tree
that counts only its surviving tips has a sapling's trunk.

## Consequences

- `spatial::PointGrid` is new. Nothing in the repository answered "which of these 40,000 points lie
  within r of p" in three dimensions; `ObstacleField` is XZ-only and cylinder-shaped. Its layout is a
  counting sort over an open-addressed table, so no hash iteration order is observable -- which is
  what lets a generator built on it be reproducible.
- Generation is 138 ms at showcase parameters, which is 69x the editor's 2 ms interactive rebuild
  budget. It must go through `advanceRebuildDeferral` rather than regenerating on a slider drag.
  That function is already free-standing, so this needs no new machinery.
- The node granularity in the editor is forced by the pick granularity: one `CompositionNode` per
  tier. See the research document §3.4.

## Verified vs assumed

**Verified:** determinism, by generating twice and comparing every node and a content hash, and its
complement, that a different seed differs. Topology, acyclicity and the pipe model's
no-child-thicker-than-its-parent invariant. Bounds against the envelope plus a stated overshoot
margin. JSON round-trip, proven by regenerating an identical tree rather than by comparing fields.
That mean animation weight increases monotonically through the four tiers. The structural figures in
this record, over six seeds.

**Assumed:** that the hierarchy is *visually* readable. Everything above is a structural measurement;
no geometry has been built and nothing has been rendered. That is Phase 5, and it is the point at
which any of this could still turn out to look wrong.
