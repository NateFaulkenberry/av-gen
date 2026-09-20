# ADR-482: The scrub is a polyline scan, and the navigation grid was not the answer

**Status:** Accepted
**Date:** 2026-09-20
**Builds on:** ADR-273 (ninety seconds is not an amount of work), ADR-295 (the navigation grid and
what it is allowed to answer for), ADR-182, ADR-170

## Problem

A timeline click on `examples/world/glowmere-valley-2-multicam.json` is acknowledged as a command
in 3.1 ms and answered on screen in about 2.8 seconds. Measured again here, in the live editor,
with `--ui-script scrub --profile-cpu`, so it is this build and not a remembered figure:

    engine.seek ms       median 2800.7      (min 2766.9, max 2844.0)
      entity re-sim ms   median 2800.7
    # seeks/frame        1.000
    # resim ksteps       118.801

**All of it is `EntityWorld::seek`, and all of that is the entity re-simulation.**

**What that arm does and does not measure, because it took a second look to see it.**
`--ui-script scrub` is one line -- `engine.seekSeconds(...)`, called directly. It does not go
through `SequencePanel`, so it creates no interaction record, and it does not go through
`Engine::requestSeek(t, gestureHeld)`, which is where the deferral and the coalescing live. So the
figures above are **the cost of one seek**, measured on the same path before and after, and that is
what this ADR's numbers are about. They are *not* the latency of a held drag, which coalesces:
`# seeks/frame 1.000` beside a frame time equal to the seek is the symptom of the bypass and not a
finding about dragging. The arm that drives the real path is `drag`, and `--ui-ab drag:drag+seeknow`
is the comparison that prices the deferral. ADR-273 bounded
the work in the right currency and said plainly that it did not make Glowmere's own click faster;
this is the part it left, and it named where to look: *"the per-step cost is dominated by
`Navigator::sample`."*

## What the profile actually says

`sample(1)` over the authored seek, leaf attribution, 10 s:

| leaf | samples |
|---|---:|
| **`closestOnPath`** | **6,461** |
| `noise::valueNoise` | 2,640 |
| `WorldMap::height` | 456 |
| `powf` | 209 |
| everything else | < 120 each |

The call chain is `Explore::update -> Navigator::steer -> pathClear -> Navigator::sample ->
TerrainQuery::at -> WorldMap::sample -> WorldMap::height -> closestOnPath`. So it is one level below
where ADR-273 stopped: **62% of an entire timeline scrub is a linear scan over a river's polyline**,
run once per terrain feature per height sample, per walker, per step of a 5,400-step replay.

`explore` is the expensive behaviour and it is not close — 153 µs per body-step against `ground`'s
8.5, `wander`'s 13.6 and everything else under 0.3 (`tools/seek_probe.cpp`, per-kind arm).

## Decision 1: the navigation grid is not the answer, and here is the number

ADR-295 built the mechanism that was supposed to make this cheap: let the baked `NavGrid` settle the
terrain half of `pathClear` where it can prove it, at 0.013 µs against 5.6. On this world it is
switched off, and the editor says so in its own log on every run:

```
nav grid: the grid will NOT answer for this world's terrain -- it disagreed with
the world after 261 sampled walk(s); every walkability query stays analytic
```

The refusal is correct. `tools/charai_probe`'s agree arm says raising the margin does not cure it:
**17 unsafe segments at trust 2 and 17 at trust 3** over 20,000 six-metre segments, and 54 against
52 at forty metres — more margin buys a lower hit rate and the same unsoundness. Most of the
residual is the step test, not the slope or the headroom.

So the question worth answering before anyone spends weeks on soundness is what it would be worth if
it were sound. `tools/seek_probe.cpp` grew a `g` arm and `AVGEN_NAV_FORCE_TRUST` to answer it:

| arm | seek | worst body vs analytic |
|---|---:|---:|
| analytic (`gridTrustMetres = 0`) | 3053.7 ms | 0.000 m (reference) |
| grid answers the terrain (forced) | **2371.5 ms** | **0.359 m** |
| *control:* the same two arms with the grid refusing | 3055.1 / 3083.0 ms | 0.000 m |

**A 22% saving, and it moves a body by a third of a metre.** That is the whole prize, and it is not
worth the soundness project: a scrub of 2.4 s is as unusable as one of 3.0 s, and ADR-182's rule is
that a faster seek which draws a different frame is not a faster seek. The control row is what makes
the measurement mean anything — without the environment variable both arms are the same code, and
they agree to 0.000 m and to within the noise.

**Recorded so that nobody prices this from the ADR-295 headline again.** The grid removes the
terrain half of `pathClear`; `Navigator::sample` is reached from more than `pathClear`, and the
half it does remove is a fifth of the replay.

## Decision 2: the block accelerator gets a cutoff, and it is exact

`closestOnPath` already has an accelerator — a bounding box per run of eight segments, skipped when
the box is further away than the best distance so far. It only bites *after* `best` has shrunk, so
the first block is always walked in full and, for a query point far from the head of the path, so
are many more.

**The cutoff.** Find the block whose box is nearest `p` — boxes only, no segment arithmetic — and
walk just that one to get a distance some segment actually achieves. Blocks whose box is further
than that cannot hold the answer. The box distances are computed once and kept, because the main
loop needs the same numbers and recomputing them was the larger half of the first attempt's cost.

**Measured** (`tools/seek_probe.cpp`, authored Glowmere, seek to t = 90 s, minimum of 2):

    before   3045.2 ms      after   2439.8 ms      -19.9%

and the cap column beside it — 120.694 m, 160.611 m, 212.773 m, 215.206 m at the four budgets — is
byte-identical before and after, which is a second, independent statement that the trajectory did
not move.

### The part that was wrong, and the test that caught it

The argument for exactness is that `cutoff` is a distance a real segment achieves, so the block
holding the true minimum has a box distance below it and is never skipped. That argument is correct
and the first implementation was still wrong, because `boxDistance` and the segment distance are
**different expressions**: when the nearest point on a segment *is* the corner of its own block's
box the two are mathematically equal and numerically need not be. The box came out a few ULPs
larger, the strict `>` fired on the block holding the answer, every other block was already further,
and the function returned `FLT_MAX`.

Measured, before the fix: heights differing from the unaccelerated walk by up to **8.0 m** on four
of the terrain styles. The comparison runs with a one-sided slack now, which can only ever skip
*fewer* blocks and degrades in the worst case to the behaviour it replaced.

**`Feature::blocks` has been documented as "purely an accelerator... passing an empty span walks
every segment and gives the identical result" since it was written, and nothing checked it.** The
determinism captures cannot: they compare two runs of one build, and both runs use the accelerator.
`the path block accelerator returns the identical height, not merely a close one` compares the
accelerated and unaccelerated answers **bit for bit** over 4,225 heights per terrain style, and its
control collapses every box to a point outside the world and requires the heights to move.

## Decision 3: §6 and §7 are already done, and the number says do not build cancellation

The brief asks for obsolete work and latest-request-wins, and warns that they are hypotheses rather
than a plan. They are also already implemented -- `Engine::requestSeek(t, gestureHeld)` defers, and
`InteractionLog::noteSuperseded` folds a new request into the open one. What nobody had was the
ratio. `--ui-script drag`, 600 frames, the real pointer path through `SequencePanel`:

| interaction | begun | done | abandoned | input->ack | input->first visual | input->final visual | seeks | **coalesced** |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| timeline-click | 10 | 10 | 0 | 2.6 ms | 6.7 ms | 1334.9 ms | 10 | 0 |
| timeline-drag | 9 | 9 | 0 | 2.9 ms | 7.6 ms | 2857.0 ms | 9 | **343** |

**352 requests became 9 evaluations. 97.4% of what a held drag asks for is discarded before any
work is started for it.** Not cancelled -- never begun, which is the strictly better thing and the
one a cancellation architecture cannot improve on. Without it a forty-frame drag would issue 352
seeks at about 2.4 s each, which is a quarter of an hour for one gesture.

So the honest reading of §6/§7 on this application is: **the mechanism exists, it is measured, it
works at 39:1, and there is nothing to build.** What is left is not obsolete work. It is that the
one evaluation which does survive costs 2.4 seconds, which is Decision 2 and what remains after it.

The other half of that table is worth keeping beside it: `input->first visual` is **7.6 ms**. The
playhead moves, the picture updates, and the editor is responsive throughout the gesture -- what
takes 2.9 s is the *settled* frame. That is the ratio ADR-084's deferral was built to produce and
this is the first time it has been quoted from the real path.

## Consequences

* A scrub on the owner's film goes from about 3.0 s to about 2.4 s. **It is still far too slow**,
  and this ADR should not be read as having fixed it. What it does is move the remaining cost
  somewhere measurable and rule out the mechanism everyone would have reached for next.
* The two diagnostics stay: `tools/seek_probe.cpp`'s `g` arm and `AVGEN_NAV_FORCE_TRUST`, which is
  an arm and never a setting, in the shape of `AVGEN_LEGACY_PROCGEN`.
* **`valueNoise` is now the second 25% and nothing here touches it.** The remaining structural
  lever, and the one this pass did not take, is that `WorldMap::height` is a pure function of
  (x, z) evaluated thousands of times per step over a walker's small neighbourhood. That is a cache
  with a real hit rate, and it is a bigger change than a cutoff.
* **Two `--ui-script` arms are named after interactions they do not drive.** `camera` pushes a
  plain left drag, which `ui::viewportIntent` maps to `EditorPointer` -- only `left && alt` is a
  camera gesture -- so for its whole life it measured the selection and gizmo path under the
  camera's name. `scrub` calls `engine.seekSeconds` directly, so it reaches neither the interaction
  log nor `requestSeek`'s deferral, and a figure taken from it is the cost of one seek rather than
  the latency of a drag. Both were found by instrumenting the interaction and noticing the arm
  produced no records. In this harness **an arm's name is a hypothesis**, and it is also why two
  interaction kinds could sit in the log with no call site and nobody notice.
* One thing found on the way past and not fixed: the editor also reports that **the walkable ground
  of this world is in four disconnected pieces, and 9,788 of 20,180 walkable cells (49%) cannot be
  reached from the largest.** That is a world-authoring defect, not a performance one, and it is
  recorded here because a walker that cannot reach half the valley is the kind of thing a
  performance investigation notices and nobody else does.
