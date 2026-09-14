# ADR-174: Vegetation is banded on height above the water table, and the cap thins instead of truncating

**Status:** Accepted
**Date:** 2026-09-14
**Scope:** `world::ScatterLayer`, `world::scatter`

## The problem

`ScatterLayer` could constrain placement on slope, altitude, biome weights, proximity to another
layer, and a clearance mask. Every one of those except proximity is a property of a point **on its
own**. None of them can say "near the river", which is the single strongest organiser of vegetation
in a valley and the thing Glowmere Valley 2 is composed around.

Altitude is the closest available substitute and it is the wrong axis. A valley whose river descends
26 m from one end of the map to the other has riverbank at altitude 0.9 upstream and altitude 0.1
downstream; a band on altitude picks out a horizontal slice through the geography, which is a
contour line, not a habitat.

## The decision

**Add a band on height above the water table**, feathered at both edges:

```
minHeightAboveWater / maxHeightAboveWater / heightAboveWaterFeather
```

evaluated as `WorldMap::heightAboveWater(p)` — the ground minus the surface of the *nearest* water
course, extrapolated past its bank — and multiplied into the layer's density as
`smoothstep(min∓f) · (1 − smoothstep(max∓f))`.

Three details, each deliberate.

**The nearest course's surface, extrapolated, not `waterSurface()`.** `waterSurface` answers "is
there water here" and therefore collapses to `seaLevel` outside a feature's own bank. That is right
for deciding what is submerged and useless for deciding what grows, because it cannot order two dry
points. The riparian literature's *height above river* is a groundwater proxy and it is a gradient
over the whole valley: a bench two metres above the water and forty metres from it is wet, and a
shelf two metres above it and ten metres away is wet in the same way. Lateral distance says
otherwise and is wrong.

**Feathered, unlike `minSlope`/`maxSlope`.** A hard edge on this axis draws a visible contour line
across the hillside in plants. Slope and altitude get away with hard edges because their iso-lines
are already broken up by the terrain; this one's are not.

**Defaults admit everything, and the field is evaluated only when a layer constrains it.** It costs
a distance query against every water feature. Most layers do not care, the original Glowmere's
thirteen do not use it at all, and a scene that does not name it round-trips byte-identically — the
serialiser writes the three fields only when `constrainsHeightAboveWater()`.

## The second decision: `maxInstances` thins, it does not truncate

The cap was enforced with a `break` on the placement loop's inner iteration, so a layer that reached
it kept the first N cells in row-major order and dropped the rest — **truncating the population from
−Z rather than thinning it.** On a 640 m map with generous caps this never fired and so never showed.
It is a straight edge across the world waiting for a larger map or a denser layer.

It is now a two-pass thinning: the first pass runs unthinned; if it overflows, `keep` is the measured
overflow ratio and the pass runs again rejecting on a hash channel of its own. Deterministic,
order-independent, and the second pass only ever happens to a layer that actually overflows.

The channel is new rather than reused, which matters: thinning must be independent of whether a cell
was *accepted*, or the survivors are the low-hash half of the population and every other per-instance
property — scale, yaw, hue, emission — skews with them.

## Evidence

**The band works and it is what composes the valley.** Glowmere Valley 2's thirteen layers are banded
into a wet floor (grass, ferns, flowers, fungi, fan-plants), a transitional slope (bushes, canopy,
deadwood) and an upland (pines, 11–95 m). `test_glowmere_valley_2.cpp` asserts the field itself is
finite everywhere, rises away from the channel, and that no dry ground sits below the water table.

**The truncation fix changes nothing that existed.** The original Glowmere's representation baseline
reproduces **273,819 triangles exactly**, unchanged, because none of its layers reaches its cap.

**What the band did *not* buy is the interesting part.** Banding thirteen layers plus a coverage
budget (screen-radius and view-distance floors) removed 6% of the scene's triangles and moved the
frame time by **0.3%** — nothing. The frame only moved when `ScatterClearance` was used to take big
geometry out of the *near field*: 8% fewer triangles for **−33% of the frame and −35% of the scene
pass**.

That is ADR-126 and ADR-151's finding restated from the other direction, and it is the reason this
ADR does not claim a performance benefit. **The band is a composition tool. Coverage is the cost, and
what a habitat band buys is a valley that reads as one.**

## Rejected alternatives

- **Distance to the channel.** Cheaper, and wrong for the reason in the decision: it cannot tell a
  floodplain from a bank.
- **A hard band, matching `minSlope`/`maxSlope`.** Contour lines in plants.
- **Putting HAR on `Sample`.** `Sample` is evaluated for every terrain vertex and every scatter
  candidate in the world; charging all of them for a field that a minority of layers reads would have
  moved the original scene's cost, which is the one thing this work may not do.
- **A full Field-of-Neighborhood competition pass** (`02-research.md` §2.5). Not rejected on merit —
  not built, because the band plus the existing `ScatterProximity` and cluster field delivered the
  structure this scene needed, and an unmeasured second mechanism would have made it impossible to
  say which one did it.

## Revisit when

- A scene has two water courses at different levels close together. "Nearest wins" is a decision, and
  a confluence or a hanging tributary is where it will first look wrong.
- A layer wants the band as a *preference* rather than a constraint — a soft peak with tails, rather
  than a plateau with shoulders.
