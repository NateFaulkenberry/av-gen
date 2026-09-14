# ADR-199: Two bugs from one assumption — that a thing is centred on its origin

**Status:** Accepted
**Date:** 2026-09-14

Two user reports, filed separately, with the same shape underneath: a number that describes a thing
was computed as though the thing were centred on something it is not centred on.

## The selection box, reported twice

> "these yellow boxes in the world editor are not drawing properly still. they are not around the
> actual object nor are they the right size - still often too big"
> "look how low the yellow box is compared to the elder cap"

`sourceHalfExtent` returns `max(|vertex|)` — the distance from the source's **origin** to its
furthest vertex. That is a half-extent only when the geometry is centred on that origin. A mushroom
cap authored fifteen metres up its own stem is not: the number comes back as roughly the full height,
so the box was **twice as tall as the cap and centred on the ground**. Both symptoms, one cause, and
the reason the earlier fix (ADR-188, which swapped the cull sphere for a tight extent) did not help:
it fixed the *extent* and inherited the bad *centre*.

`primitiveBox` / `sourceBox` return the real box — centre offset and true half-extent — and the tight
bounds carry the centre through the instance rotation alongside the extent. The conservative
symmetric version stays for culling, where being too big is safe and a centre nobody reads would be
waste.

Tested on a tube whose curve runs ten metres above its origin: the box comes out **10.00 .. 10.20**,
where the broken version gave roughly ±10.2.

## The characters stuck on rocks

> "they're often getting stuck on objects and playing a walking animation whilst stuck... I see it
> happening with rocks in particular"

`NavGrid::build` already inflates every solid by a body radius before deciding which cells are
blocked. It used `nav.settings().bodyRadius` — **the world's default of 0.45 m, a person.** Glowmere
Valley 2's inhabitants are six metres tall with a 2.4 m radius. So every path was planned through
gaps they do not fit in, and the per-frame penetration resolve then fought the walk: the body enters
the cell, is pushed out, re-enters, and the gait — which reads speed, not progress — keeps saying
Walk.

This is the one-grid-per-world limitation recorded in ADR-195 and ADR-196 and accepted as survivable.
It was not survivable; it was simply not yet measured.

Measured, over 180 simulated seconds, as *frames in Walk or Run during which the body moved less than
a centimetre*:

| | before | | after | |
|---|---:|---:|---:|---:|
| | still | longest stall | still | longest stall |
| rook | 56.7% | 0.3 s | **36.9%** | 0.3 s |
| tide | 11.2% | 0.1 s | **9.8%** | 0.1 s |
| **sage** | **92.3%** | **62.3 s** | **31.7%** | **0.1 s** |
| ember | 53.4% | 0.1 s | **45.8%** | 0.1 s |

`sage` spent **one unbroken minute** playing a walk cycle and going nowhere. It now travels further
(484 → 599 m) and its longest stall is a tenth of a second. No stall anywhere exceeds 0.3 s.

The fix is a scene key, `navBodyRadius`, set to the widest body that has to route — scene-level
because there is still one grid per world. Blocked cells go 527 → 2,479 on Glowmere Valley 2, which
is the graph finally describing the world these particular creatures live in.

## What the residual percentage is

The "still" figures that remain are the arrival slow-down and turning on the spot, measured against a
deliberately strict threshold (0.6 m/s for bodies that cruise at 3–6 m/s). They are not stalls: the
longest consecutive run is three tenths of a second. A body that never dipped below 0.6 m/s would be
one that never arrived anywhere.

## The lesson worth keeping

Both of these were *visible* and neither was caught by a test, because both tests asserted the thing
was better than it had been rather than that it was right. ADR-188's bounds test checked the box was
tighter than the cull sphere — true, and still wrong by a factor of two. The navigation tests checked
characters reached destinations — true, and `sage` reached them either side of a 62-second wedge.

**A measurement of "improved" is not a measurement of "correct."** The probe that found the second
one asks a question neither previous test did: not "does it get there" but "is it moving while it
claims to be walking".
