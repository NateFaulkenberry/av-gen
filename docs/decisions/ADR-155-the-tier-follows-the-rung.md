# ADR-155: The material tier follows the LOD rung, and the ceiling is not reachable

**Status:** Accepted
**Date:** 2026-09-13
**Corrects:** my own correction to ADR-138, which overstated its error

## What shipped

`QualitySettings::flatTierFromRung` — the first LOD rung whose procedural draws shade at the flat
tier, or < 0 for none. Enabled for Realtime and Preview at rung 1; High and Offline never demote.

It needed no new plumbing. The scatter path already issues **one indirect draw per (layer, rung)**
(ADR-154) and each rung already has its own `ProceduralUniforms` slot, so the tier is a float in an
existing struct written in an existing loop. ADR-135 records that `ObjectUniforms` has no free lane
for a per-draw tier; that is true of *entities*, and procedurals have had one all along — which is
fortunate, because procedurals are where 98% of the saving is.

Rung is the right axis rather than a convenient one: it is computed from projected screen radius, so
"demote from rung N down" means exactly "demote what is small on screen".

## The sweep, and the number that matters

Glowmere 1280x800, idle, interleaved, baseline blocks varying 0.00–0.88% GPU:

| arm | saving | % of frame | share of the ceiling |
|---|---|---|---|
| flat from rung 1 | **+1.11 ms** | 7.46% | 19% |
| flat from rung 2 | +0.39 ms | 2.63% | 6.6% |
| every procedural draw (the ceiling) | +5.90 ms | 39.47% | 100% |

**81% of the procedural shading cost is in rung 0** — the near, large instances. Demoting everything
smaller than the foreground reaches 19% of the ceiling, and no threshold reaches more without
demoting what the camera is looking at.

## Where I was wrong

I recorded that ADR-138's ~1.4 ms estimate was "wrong by 4x", on the strength of the frame-global
procedural arm measuring 5.64 ms. That comparison was between two different quantities:

- ADR-138 estimated the **realizable** share — what assignment could take while sparing the
  foreground. It reasoned from coverage, and got **~1.4 ms**.
- I measured the **ceiling** — what demoting *everything* takes, foreground included. That is
  5.90 ms, and it fails §50 because it flattens the scene's bioluminescence.

The realizable share measures **1.11 ms**. ADR-138's estimate was close, and its reasoning — that
fragment cost follows screen coverage — was correct. My "coverage and cost are different quantities"
was too strong: *within* the procedural set, cost follows coverage closely, and the coverage is in
rung 0. What ADR-138 could not know without the arm was the size of the ceiling, not the size of the
prize.

**The prize was never 5.64 ms.** It is 1.11 ms, because the expensive geometry is the geometry that
must keep its quality. That is a property of the content, not a limitation of the mechanism.

## §50

Captured and inspected against the baseline: **3.07% of pixels differ, mean 0.62, max 159**, and the
image is indistinguishable at full size. The foreground ferns keep their gradient, the glow pools are
intact, the purple shrub keeps its bloom, the hillside trees keep their colour variation. Compare the
frame-global arm at 18.58% and an obviously flattened scene.

So this ships and that one does not, for a 5.3x difference in saving. That is the trade §50 exists to
make.

## Consequences

- **Representation-based material assignment is done, and its value is bounded.** A finer mechanism —
  per instance rather than per rung — would not help: the cost is in rung 0 either way, and rung 0 is
  what has to stay Full.
- `MaterialTierSelector` and `ImportanceEvaluator` stay unwired. They would decide the same thing the
  rung already decides, from a projected radius the cull pass has already computed. Wiring them would
  add a second importance notion for no measured gain; ADR-122's warning against exactly that applies.
- The remaining 4.8 ms is not reachable by demoting anything. It is the foreground's real shading
  cost, and reducing it means making the *Full* tier cheaper — which is Phase B's territory, and
  Phase B has already measured its two candidates as slower.

## Verified vs assumed

**Verified:** every row in the sweep, idle machine, interleaved, in-process, baseline spread under
1%; the §50 capture, inspected at full size; that the mirrored flat-tier constant matches the enum
(static_assert); that the arm reaches the shader (the frame-global arm and the rung arms produce
different images).

**Assumed:** that rung 1 is the right default rather than rung 2 — rung 1 saves 2.8x more and shows
no visible damage on this scene's canonical frame, but "no visible damage" was established on one
frame of one scene, not on a camera path.
