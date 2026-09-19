# ADR-376: The tree conducts, and the canopy is never quite still

- Status: Accepted (2026-09-19)
- Phases 5 and 6 of the brief. Extends ADR-360 (the wind body, whose frame this reuses).

## Decision

Two emissive terms on the mesh path, both keyed on **where a point is in the body** — height above
the root, distance from the axis — which is exactly the frame `windOrigin`/`windShape` already
establish for the wind. They are reused rather than duplicated: a second origin for the same tree is
two things that can disagree, and ADR-370 already made that argument for the leaf emitter.

**Energy** is a travelling band that leaves the roots and climbs to the canopy, with separate shares
for root, trunk, branch and canopy as the brief asks, over the same radial/height proxy the wind
uses rather than a topology the GLB does not carry. The tier weights overlap rather than partition,
because a hard partition draws a visible line across the trunk where two shares meet. Colour travels
with height — near colour at the roots, far at the crown — which is what makes it read as a current
*arriving* rather than as a region brightening.

**Shimmer** is deliberately not a brightness pulse. The brief is explicit that the canopy must not
simply flash, so it is a low-frequency field advected across the crown, **centred on zero** so it
takes light away as often as it adds it. A one-sided shimmer is a brightening, which is the thing
§6 says not to do.

Both are pure functions of time (ADR-091). The owner's relaxation covers particles; it does not
cover a hero asset's own light, and a tree at a different brightness after a scrub than after
playing to the same second would be noticed immediately.

Added to **both** the radiance and the emission target, because an emissive term that reaches the
frame but not the AOV is invisible to bloom, and bloom is most of what makes a conducted pulse read.

Shipped at intensity 1.2 and shimmer 0.35. At 3.0 the pulse lights the whole crown at once and the
canopy goes flat — which undoes exactly the form `emissiveBoost` 0.5 was lowered to buy, and is
§17's "evenly illuminated tree" arriving by a different road.

## What the failing test was actually telling me

The GPU test's shimmer arm rendered **byte-identically to no shimmer at all**, and the pulse arm
rendered identically at two seconds 3.5 s apart. I spent an hour on the wrong hypotheses — fixture
too small, emission saturating, the shader not seeing the clock (a probe returning
`fract(time * 0.1)` from inside the same function proved it did) — before the real one.

**`shimmerScale` and `noiseScale` are in world units, and the shipped defaults are tuned for a 138 m
tree.** At 0.035 the 4 m test body falls entirely inside **one** noise cell, so the "wave" is a
single constant across the whole object; and because the wave is centred on zero, a negative
constant clamps the whole term to zero and renders as nothing. Setting scales that suit the fixture
turned 58/59 into 68/68 with no engine change.

That is worth stating as a property rather than as a fixture quirk: **a spatial scale has to suit
the body it is applied to**, and this is the third time in this branch that a number correct for a
138 m tree has been wrong somewhere else — after ADR-360's wind amplitude shredding the canopy and
ADR-371's per-metre density written as a unit-scale amplitude.

Two of those hypotheses were also worth keeping. Emission at 2.0 against an unlit black fixture
really does saturate, so two different pulse positions render alike and the tone map eats the
evidence; the test uses 0.35 for that reason. And the shader-clock probe is the reason I could stop
suspecting the uniform path.

## The measurement

`tests/rendering/test_tree_energy_gpu.cpp`, 2 cases, 68 assertions:

- Both intensities default to 0 and `active()` is false, so an entity that has not asked is
  byte-identical at two separate seconds — with both arms moved together, because moving only one
  conflates time with the effect (the mistake `test_mesh_wind_gpu` caught earlier).
- Energy alone changes the frame; shimmer alone changes the frame. Two controls, either of which
  could have come out the other way, and one of which did.
- The pulse travels: two seconds apart are two different pictures.
- The same second twice, from a fresh renderer, is the same frame — ADR-091, for both terms.

In the shipped project, four renders 1.5 s apart differ from the first by means of 10.7, 20.9 and
27.9 luminance levels, and at the pulse's peak the canopy is lit while the trunk stays dark.

## Consequences

- The tier shares are a proxy, not topology. A branch that hangs low and near the trunk conducts
  like a trunk, exactly as it bends like one (ADR-360).
- `noiseScale` and `shimmerScale` need re-tuning for any body of a different size. They are
  parameters, so that is a slider and not a rebuild, but nothing warns you.
- Tree particles (Phase 7) are not built.
