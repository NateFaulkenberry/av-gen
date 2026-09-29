# ADR-986: A stem meets its cap

**Status:** Accepted
**Date:** 2026-09-29
**Found by:** the GV3 art pass, revision round 1 (`docs/glowmere-valley-3/art-pass/00-brief.md`): the owner, on the
first final, "check all new hero mushroom stems properly connect to their caps (looks like one or two had gaps)"
**Implemented by:** `organism::buildMushroom` / `sweepStem` (`src/organism/mushroom.cpp`, `StemJoin`); GV3's elder in
`tools/gv3/heroes.py` (`join_parts`)
**Tests:** `tests/unit/test_mushroom_alignment.cpp` (`[adr986]`; the probes `hero stem-to-cap gaps` and
`stem-to-cap gaps over the population`)

## Context

A hero mushroom is four generated parts from one parameter vector: upper cap, underside, stem, gills. The
underside is a lathe with an opening at its centre -- its innermost ring, 0.965 times the stem's top radius -- and
the stem is a tube swept along a curved axis whose rings are perpendicular to the axis' tangent.

At the top of a curved stem that tangent leans by atan(2.1 x curvature) -- up to 43 degrees -- while the opening lies
in the cap's plane, tilted about X by the cap's own tilt (up to 18 degrees). Two circles of one size in two
planes meet at one point at most: on the stem's low side its top edge stood below the underside, and the opening
gaped round it. The existing alignment check compares the two rings' CENTRES (within 1.25 stem radii), so it
passed every one.

Measured with geometry (the new probe: from each vertex of the opening's rim to the stem's surface, and from each
vertex of the stem's top to the underside, on the side the underside faces), in the generator's unit frame and in
metres at the scale each hero stands at in Glowmere Valley 3:

| hero | stem top radius | gap, stem radii | gap, metres |
|---|---|---|---|
| sail (#707) | 0.123 | 0.78 | **0.52** |
| ridge (#755) | 0.108 | 0.97 | 0.39 |
| elder (#131) | 0.054 | 0.45 | 0.27 (with GV2's hand fix, below) |
| bloom (#4) | 0.092 | 0.30 | 0.23 |
| scree (#508) | 0.079 | 0.50 | 0.22 |
| lantern (#776) | 0.057 | 0.54 | 0.18 |
| umbra (#515) | 0.047 | 0.95 | 0.18 |
| cairn (#675) | 0.069 | 0.28 | 0.11 |
| opal (#251) | 0.034 | 0.54 | 0.09 |
| ember (#380) | 0.117 | 0.20 | 0.08 |
| veil (#684) | 0.064 | 0.30 | 0.06 |
| spire (#644) | 0.086 | 0.17 | 0.06 |

All twelve, not just the two new ones: the owner's "one or two" are the two largest in the valley's new east
(the sail's is the largest of all). Over the search's whole population (the 660 candidates its hygiene and
plausibility gates pass) the median was 0.45 stem radii and the worst 1.05.

GV2 had met it once, by hand: its projects stand the elder's stem 0.6034 m above its cap, underside and gills
(`nodes/elder-2-stem/position`). That buried the stem's top inside the cap, but the stem's SIDE still stood 0.27 m
clear of the opening.

It is not the wind. Each part bends in the wind about its own bounds (the procedural renderer's `windBaseY` and
`windExtent`), so the stem's top and the cap's opening do sway by different amounts; but at the heroes' motion
values (tip amplitude 0.01, wind sensitivity 0.16, stiffness 18) the static bend is 9e-5 of a part's height, under a
millimetre at the stem's top. Nor is it any per-instance variation: each part is a `single` distribution at the
hero's one transform, and all four share it (checked on every hero in the scene), the elder's stem excepted.

## Decision

1. **The stem's last three rings turn from the axis' tangent to the cap's axis, and move onto the opening's
   centre** (a smoothstep over the three), so the top ring lies in the opening's plane, round its centre: the pivot
   the cap tilts about, over the stem's end.
2. **One more ring carries the stem on up inside the cap** along the cap's axis, by a quarter of the cap's depth
   over the opening (the upper surface's height there less the underside's) and at most one top radius, so the
   opening's edge meets the stem's side rather than a knife edge. A cap too thin over the opening to hold it
   (a plunge under 5% of the top radius) gets none: the ring would only add zero-area triangles. Its uv runs past
   1, marking the part of the stem the cap hides.
3. **The generator's version stays 1.** The parameters mean the same mushroom; its stem now meets its cap. A bump
   would refuse every scene that names version 1 (GV2's three projects, GV3, the hero record).
4. **GV3's elder loses GV2's hand fix** (`heroes.join_parts`, after seating so the cap keeps its height): its stem
   goes to the place its other three parts are, and its base 0.6 m further under its pool. GV2's own projects are
   left as they are.

## Consequences

- The twelve heroes, the same measurement: 0.04-0.08 stem radii, 0.6-4.0 cm (the underside's 3.5% tuck inside the
  stem and two polygon approximations of a circle), none through the top. In GV3 the elder, its stem joined: 0.05
  radii, 3.2 cm.
- The population: 689 candidates pass the gates (29 more: the plausibility gate rejected some for this very gap),
  median 0.056 stem radii, worst 0.108; stems through the top fell from a worst of 0.61 radii to 0.12 (what is left
  is caps thinner at the opening than the stem's ring is tilted, with a depressed centre, #89).
- GV2's heroes change with it: every stem now meets its cap. GV2's elder keeps its hand-raised stem, which is
  still 0.27 m clear on one side, as it was.
- **The search no longer reproduces the record.** With a different population and the stems' bounds in the
  features, a re-run picks a different twelve from the second pick on (131, 644, 227, 4, 684, 515, 675, 755, 508,
  471, 307, 283). `examples/organisms/glowmere2-heroes.json` is what Glowmere's heroes are, by value, and stays;
  the search test now writes its record beside its sheet unless `MUSHROOM_WRITE_RECORD=1` asks for the canonical
  file, so a run cannot take away the heroes `tools/gv3/heroes.py` builds.

## Rejected alternatives

- **Moving each part by hand**, as GV2 did for the elder: a number per hero that fixed nothing but the one the
  eye was on, and missed the side of the stem.
- **Tilting the cap to the stem's lean**: every hero's cap would change its angle, the most visible thing about it.
- **Reshaping the underside's opening to follow the tilted stem ring**: the gills hang from the underside's
  profile and would stand off the warped opening.
