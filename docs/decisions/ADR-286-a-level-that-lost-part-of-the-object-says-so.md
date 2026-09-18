# ADR-286: A LOD level that lost part of the object says so

**Status:** Accepted
**Date:** 2026-09-18
**Context:** ADR-078 (the chain builder), ADR-085 (the guard against a level larger than its
predecessor), the LOD Lab's `[.analysis][lod]` instrument.

## Context

At 2.9% of its triangles `CommonTree_1` came back from `buildLodChain` with the bottom **31% of its
bounding box gone** -- the trunk -- while reporting a relative error **5.3x smaller** than the
deviation it actually had.

Both halves of that are defects and they are different defects.

The missing trunk is a level that did not simplify the object, it lost a part of it. `mesh_lod.hpp`
already says what that costs: "a level that shrank by destroying the shape ... passes a triangle
count". ADR-085 added a guard for the one case where the arithmetic gives it away -- a level larger
than its predecessor -- and there is no arithmetic that gives this one away, because 183 triangles
is a perfectly good number for a bottom rung.

The understated error is a broken promise in the header:

> it rises monotonically with aggressiveness and **never understates**, so a selector that switches
> levels when the error projects below a pixel is conservative rather than wrong.

It was not conservative and it was wrong, by 5.3x, on the asset the scatter leans on hardest.

## Decisions

### 1. The guard ADR-085 applied to triangle counts, applied to bounds

A candidate level whose bounding box has receded from the source's by more than
`LodChainSettings::boundsTolerance` of the source's diagonal, on any one face, is refused exactly
the way an inverted level is: retried from the level above, and failing that, replaced by it.

Simplification can only ever shrink a bounding box -- a simplified vertex is a weighted position of
vertices that were inside it -- so the test is one-sided by construction and needs no tolerance for
growth.

### 2. `LodLevel::error` is floored by the recession, so the header's claim is true

The recession is not an estimate. The source has a vertex out past the level's box and the level's
surface lies inside its own box, so the gap is a **lower bound on the one-sided Hausdorff distance**.
Taking the larger of it and the simplifier's own number is discarding the smaller of two claims, not
inventing a third. `LodLevel::boundsError` reports it separately as well, because the two answer
different questions: `error` is what a selector should threshold on, and a level where `boundsError`
dominates it is a level that lost a part rather than smoothed a surface.

### 3. Where 0.12 comes from

Measured over every asset the Glowmere scatter layers use, at the ratios `vegetationLodSettings`
asks for: 39 rungs, printed side by side with and without the guard by `[.analysis][lod]` "What each
rung of the chain draws, against the source". 0.12 is the smallest round value that

* admits every rung in this repository that keeps at least 90% of the source's height (the largest
  such recession is `Bush_Common` lod3 at 0.102 of its diagonal), and
* refuses every rung that keeps less than 80% (`Grass_Common_Short` lod3 at 0.174 and 78%,
  `Pebble_Round_2` lod3 at 0.187 and 24%, `CommonTree_1` lod3 at 0.257 and 59%).

The 80..90% band is decided by the diagonal, which is the right measure for a squat object and the
wrong one for a tall thin one. That is the part of this number that is a judgement rather than a
measurement, and it is written down as one.

Tightening it is cheap to try and expensive to ship. At 0.06 the guard replaces 11 of those 39 rungs
instead of 3, and takes rungs 2 and 3 off `Bush_Common` and `Grass_Common_Short` entirely -- layers
whose instances are a few pixels across by the time they reach those rungs, where a tenth of a
diagonal is a fraction of a pixel. A threshold in **projected** units would separate those properly,
and `buildLodChain` knows nothing about the ladder that would need.

## What it costs, measured

Three of the 39 rungs are replaced, and the three that are replaced are the three the brief named:

| asset | rung | before | after |
|---|---|---|---|
| `CommonTree_1` | 3 | 183 tris, 59% of its height, recession 0.257 | **130 tris**, 86%, 0.105 |
| `Grass_Common_Short` | 3 | 8 tris, 78%, 0.174 | 19 tris, 100%, 0.098 |
| `Pebble_Round_2` | 3 | 4 tris, 24%, 0.187 | 8 tris, 94%, 0.080 |

Over every rung below 0, across every Glowmere asset: **14,183 triangles before, 14,145 after, −0.3%**.
`CommonTree_1`'s bottom rung got *cheaper* as well as better -- the retry from the level above found
a 130-triangle level where simplifying from the source had found a 183-triangle one that had thrown
the trunk away. Ten of the 39 unguarded levels deviated further than the error they reported; none
of the guarded ones does.

On the frame, Glowmere multicam frame 60 at 1280x720, `--tier realtime`:

| | before | after |
|---|---|---|
| camera pass | 340,799 tris / 1,314 inst | **372,296 tris** / 1,314 inst (+9.2%) |
| shadow pass, ecology | 558,168 tris / 3,396 inst | **652,659 tris** / 3,396 inst (+16.9%) |
| draws | 266 | 266 |

The instance counts do not move, because nothing about which rung an instance is on changed -- only
what each rung draws. The frame's pixels do change, which is the point.

## Consequences

`LodChainSettings::boundsTolerance = 0` disables the guard and the error floor together and returns
the chain this code built before either existed. That is not a compatibility shim: it is what lets
the regression test have its arm and its control **in one process**, which is the only honest way to
show a guard fired.

`tests/unit/test_mesh_lod.cpp` gains two tests. The first runs a canopy-on-a-thin-trunk fixture at
three aspect ratios through both settings; its two closing assertions are the controls, and they
check that the unguarded chain really did lose geometry and really did understate it, because a
fixture where the wrong answer is unreachable proves nothing (ADR-182). The second runs
`CommonTree_1` itself, because a synthetic fixture can be tuned until it says what you want.
