# ADR-985: Water on a steep course is a cascade

**Status:** Accepted
**Date:** 2026-09-28
**Found by:** the GV3 targeted art pass, item 1 (`docs/glowmere-valley-3/art-pass/00-brief.md`): after ADR-980 put
the falls' edges on their banks and joined them to the river, the first verification stills still showed the hill
water as "a slab": a flat, evenly lit panel with a brick lattice printed on it (the owner's second reference shows
the same lattice)
**Implemented by:** `shaders/water.wgsl` (the `cascade (ADR-985)` blocks: `cascadeTurn`, `cascadeWhite`, and five
places in `fs_water`), `scene::WaterSettings::cascade`, the `nodes/<terrain>/water/cascade` control
**Tests:** `tests/rendering/test_water_cascade_gpu.cpp` (`[cascade]`, 5 cases)
**Data change beside it:** `tools/gv3/look.py` `WATER["cascade"] = 0.35` (Glowmere Valley 3's falls)

## Context

The water shader was written for a sheet that lies flat. Four things in `fs_water` assume it:

1. the ripple normal is built about +Y;
2. which side of the surface the eye is on is `camera.y < point.y`;
3. the depth that sets colour, opacity and the shoreline is `thickness * |v.y|`, straight down;
4. the tears (ADR-916) lie on a lattice in XZ.

GV3's falls run thirty metres down the north head at 30-44 degrees (`avgen_water_probe`: 607 m2 steeper than 30
degrees, 149 m2 steeper than 40, steepest 43.6; the valley's river is 7.8 degrees at its steepest). Seen from the
valley, which is where every camera sees them from, each assumption fails:

- every part of the falls above the camera's own height took the "seen from under the water" branch -- a dark
  ceiling with a fixed opacity -- while the part below it was shaded normally, so a horizontal seam ran across the
  falls at the eye's height;
- the view ray crosses the sheet almost edge-on to the vertical, so the "vertical" depth came out a fraction of the
  real one: the falls read as shore and thinned toward transparent;
- the light was a level pool's: a grazing Fresnel on a surface that actually faces the camera;
- the tears' lattice, stood up on the slope, drew rows of bricks.

Together: a flat panel with a brick wall on it.

## Decision

A surface steeper than 12 degrees is shaded about its own plane, fully past 30 degrees and blended between so
nothing switches at a line.

1. **The face.** Its up comes from the position's screen derivatives (`cross(dpdx, dpdy)`, taken in uniform control
   flow beside the existing footprint derivatives; constant over a triangle, and the water mesh's triangles are
   1.2 m). `steep` is exactly 0 under 12 degrees -- the branch that computes it is not entered -- and every change
   below is inside `if (steep > 0.0)`.
2. **The eye's side** is the plane's: `dot(toEye, up) < 0`, so a camera below a falls sees its upper side.
3. **The depth** is measured across the sheet: `thickness * |dot(v, up)|`.
4. **The ripples**, built about +Y as before, are turned onto the slope (a closed-form rotation of +Y onto `up`).
5. **The tears** fade out with the steepness (their shift, blur and band are scaled by `1 - steep`).
6. **Whitewater**, a new setting `cascade` (0 by default; 0..10): streaks along the flow in the foam's colour,
   composited as foam is, thinning with the shoreline. A streak is COMBED rather than drawn in a frame of its own:
   the mean of twelve samples of the water's value noise stepped 0.2 m back up the local flow (a line integral),
   so it needs no frame -- a frame turned to the local flow would swing the pattern about the world origin wherever
   the flow bends (300 m out, a degree of meander moves it five metres). It travels at three times the water's
   speed under ADR-914's bounded advection, and where a 0.45 m grain is too fine to resolve it fades to its mean
   white, so a distant falls is a pale ribbon rather than a crawl. It rides the free lane of the `shore` uniform:
   the uniform block is unchanged.

Every addition sits between `cascade (ADR-985)` markers, so a test can strip them and compare.

## Consequences

- **Flat water is untouched, byte for byte.** A flat synthetic sheet, two views, tears on and off, two seconds,
  `cascade` 1.5: identical to the shader with the blocks removed (8 of 8), and the same comparison on a 35-degree
  sheet differs (the control). The QA scene's real river: every pixel the code changes is one a third arm (the live
  shader painting every pixel that takes the path) marks as steep -- three pixels on bank slivers steeper than 12
  degrees in one pose, none elsewhere.
- **The eye's height no longer cuts a falls in two.** A level camera looking up a 35-degree sheet: the log ratio of
  the rows just above and just below the eye's height is 0.007, against 1.023 without the code.
- **The tears are gone from a slope past 30 degrees**: a trace of tears against a full set draws the same bytes;
  without the code they changed the sheet by 0.0041 mean luminance.
- **Whitewater reads as streaks along the flow**: `cascade` 1 lifts a steep sheet's mean luminance by 0.50, with
  3.3 times the luminance gradient across the flow as along it.
- GV3 sets `cascade` 0.35 on the valley's water; only the falls carry any.
- Every other scene's steep water, if it has any, is now shaded about its plane too; that is the fix, not a look.
  Its whitewater stays off unless the scene asks for it.
- Cost: nothing on flat water (one cross product and a compare per pixel); on steep pixels, 24 value-noise
  evaluations for the whitewater.

## Rejected alternatives

- **A flow-aligned frame for the streaks** (`dot(p, dir)`, `dot(p, across)`): cheap, and wrong away from the
  origin, as above; the combed line integral costs more and has no lever arm.
- **Hiding the falls' texture** by turning the tears off on the whole surface: the tears are GV3's deliberate look
  on its flat river (ADR-916), and the other three faults would remain.
- **A separate pipeline for steep water**: steep and flat water are one mesh and one material; they cannot be
  drawn apart.
