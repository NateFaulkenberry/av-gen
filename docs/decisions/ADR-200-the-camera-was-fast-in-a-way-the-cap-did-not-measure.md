# ADR-200: The camera was fast in a way the first cap did not measure

**Status:** Accepted
**Date:** 2026-09-14

> "add a parameter to the auto director panel that allows me to set max movement speed, sometimes
> it's just moving about way too fast"

Built as asked: a cap on how fast the camera *travels*. Then:

> "I don't know if max speed is working correctly — even at its smallest value it's still moving
> blazing fast"

## What the measurement said

The cap was working perfectly and was the wrong quantity. On a reference cut:

| | camera travel | aim point | **view rotation** |
|---|---:|---:|---:|
| uncapped | 23.2 m/s | 44.7 m/s | 52.0 °/s |
| cap 20 m/s | 20.3 ✓ | 44.8 | 52.0 |
| cap 5 m/s | 5.1 ✓ | 45.4 | 50.2 |
| cap 1 m/s | **1.0** ✓ | 45.6 | **63.7** |

The camera did exactly what it was told. The **view rotation got worse** — 52.0 to 63.7 — because a
camera that moves less still has to sweep its aim the same distance in the same time.

**What a viewer calls "moving too fast" is almost never metres per second.** It is degrees per
second: how fast the world swings across the frame.

## And where that rotation actually comes from

The obvious guess was the handoff swing — a transition turning from one subject to the next — so the
first attempt widened that swing's window. It lowered the aim point's speed from 44.7 to 28.5 m/s and
left the peak view rate at **exactly 52.0**, unchanged.

Instrumenting which shot held the peak answered it: **`finalBuild-8`, a `Subject` shot.** Its aim is
a fixed point and every degree of rotation comes from the camera orbiting around it. A camera close
to its subject rotates fast while barely moving, and neither a swing window nor a
metres-per-second cap reaches that at all.

So `limitViewRate` does both: widen a handoff's swing where there is one — that costs nothing, the
shot still arrives where it was going at the same moment — and then shrink the move itself for
everything still over, which on a real cut is most of it. 52.0 → 26.3 °/s at a 30 °/s cap.

## Two controls, not one refined

**max speed** (m/s) and **max swing** (°/s) are separate because the measurement says they are
separate problems: one of them moved the other in the wrong direction. The panel says which is
usually wanted.

## What gives way, in both cases

**The distance, never the timing.** Every cut in this director lands on the music — that is the whole
reason it reads a musical structure — so slowing a shot by lengthening it would move every cut after
it off the beat it was built for. A shot that is too fast covers too much ground for the time the
music gave it, and the ground is what shrinks.

The *end* is pulled back toward the start, never the reverse: in a continuous cut a shot begins where
the last one ended (ADR-185), so moving a start would break the chain while moving an end is
something the chain can follow. Both caps walk the shots forward, re-chaining each start before
measuring it — the first version capped everything and re-chained afterwards, which left two of nine
shots faster than the cap again because re-chaining changes the path it had just measured.

## A bug worth recording, because the shape recurs

The first shrink branched on *which fields were set* — `endPosition && startPosition`. A shot with an
authored end and **no** authored start takes `cameraAt`'s point path anyway, so shrinking its polar
fields edited numbers that path never reads: it came out completely untouched, keeping twenty of the
twenty-two metres a second it was meant to lose.

**Branch on the path the code takes, not on the fields that happen to be present.** The same mistake,
one layer down, as ADR-193's control that proved nothing.

## Tested

That the caps hold; that the cuts do not move — every `startSeconds`, `durationSeconds` and subject
identical before and after; that a continuous cut still joins to within a centimetre; that zero is
off rather than a cap of nothing-may-move; and that the swing cap at least halves the rate as well as
meeting it, because a cap met by making every shot static would also be "under the cap".
