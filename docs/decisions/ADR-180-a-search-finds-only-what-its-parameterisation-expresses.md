# ADR-180: A search can only find what its parameterisation can express

**Status:** Accepted
**Date:** 2026-09-14

## Problem

The Tree of Life is built on a candidate search: a parameter space, a population, banded aesthetic
scoring, diversity selection, a winner. Over several sessions the search was run repeatedly, the
bands were audited for variance, two components were re-banded from the measured distribution, and
one was demoted to a validity guard. The evaluator improved every time.

And every candidate in every population was a rounded ball on a straight trunk.

The natural reading — the one I proposed — was that the scoring was at fault: `balance` bands away
from lopsided, `boxFill` rewards a filled bounding box, `silhouetteComplexity` tolerates a smooth
outline, and the three *together* describe a ball while each is individually defensible.

## The experiment

Widening all three, and re-rendering the population, **surfaced nothing new**. The ranking reordered.
Not one lopsided, lobed or reaching tree appeared in twelve cells.

## The finding

The crown envelope was **a surface of revolution**. Space colonization fills the envelope it is
given, so the crown's outline *is* the envelope's outline. An envelope whose radius is identical in
every compass direction cannot produce an asymmetric crown — and **no search inside it can find one,
at any population size, for any scoring function, because asymmetry was never in the
parameterisation at all.**

Nineteen parameters, thousands of candidates, a carefully audited evaluator, and the one property the
work most needed was not among the things the space could vary.

## Decision

**State the general rule, and check for it before blaming a score.**

> A search can only find what its parameterisation can express. A band that rejects nothing is
> indistinguishable from a space that contains nothing, and the two have different fixes.

Operationally:

1. When a population is uniform in some respect, **first ask whether the space can vary that respect
   at all**, before re-banding anything. This is usually answerable by reading the parameterisation,
   and always answerable by widening the relevant bands and looking — which is one render.
2. A parameter named for a property must be **able to affect that property at its authored range**.
   `lumpiness` existed, was authored, was searched, and was named for exactly the asymmetry that was
   missing — and it thins marker density at an amplitude and scale that never breaks the outline. A
   parameter that cannot affect the thing it is named for is worse than a missing one, because it
   answers the question "is this expressible?" wrongly and confidently.
3. The fix belongs in the generator, not the evaluator. Two angular harmonics at seeded phases and a
   lateral offset of the crown's centre from the trunk's axis, both searched, and the population
   immediately contained lobed crowns with real voids, leaning crowns, broad flat-topped ones and
   ragged open ones.

## Why a candidate search invites this specifically

This failure is not an accident of one project. **A search looks like it is exploring.** It produces
hundreds of distinct parameter vectors, a spread of scores, a ranked list and a diverse selection,
and every one of those signals is real — the search genuinely did explore, thoroughly, a space that
did not contain the answer. The apparatus that makes generate-and-select worth building is the same
apparatus that hides the absence.

The diversity selection makes it worse rather than better: it reports the *most different* candidates
it found, which reads as a survey of what is possible. It is a survey of what is *reachable*.

Nothing in a search can detect this from the inside. The only instrument that found it was a rendered
contact sheet and a person looking at twelve cells and noticing they were all the same shape, which
is the standing conclusion of ADR-178 arriving from a different direction.

## Consequences

- `TreeParams::crownLobes`, `crownLobeAmount` and `crownOffset` exist, and the first is derived from
  the seed rather than given a Sobol dimension, because a discrete choice among three would waste one
  of the well-stratified low dimensions on a coin toss.
- The band widening was **reverted**. It was a hypothesis, the measurement did not support it, and a
  change that survives because it is already typed is how a codebase accumulates mechanisms nobody
  can account for.
- The conjunction hypothesis — several individually defensible bands whose combination selects
  something none of them asked for — is a real and distinct failure mode and is **not** what happened
  here. It is recorded as unconfirmed rather than quietly dropped or bent toward the evidence.

## Verified vs assumed

**Verified:** that widening the three suspect bands changed the ranking and produced no new
architecture; that adding two angular harmonics and a crown offset produced visibly different
architectures in the same population size at the same camera.

**Assumed:** that the envelope is now expressive *enough*. It is less obviously deficient, not proven
sufficient — the same argument that found this one applies to whatever the next uniform property
turns out to be, and the next one will not announce itself either.
