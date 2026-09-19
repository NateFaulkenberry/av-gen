# ADR-349: The diagonal was the bank, and the bokeh was doing the lying

Status: accepted
Date: 2026-09-19

## The report

> part of the water in the river does not get rendered. you can see a diagonal line of the water
> here, and then the rest of the water is cut off — cant tell if its all of the water or just part
> of a surface effect, but it seems to be wide angle / zoomed out shots

Wide shots of `glowmere-valley-2-multicam`. Two leads came with it: terrain LOD, and camera culling
of the per-chunk water meshes.

## What it is

**The line is the waterline.** Nothing is missing.

Past the diagonal the ground is 5–8 m *above* the river's water level and 17–37 m from its
centreline. It is the west bank of the valley, seen almost along its length, and a long straight
bank viewed end-on projects to a straight line. Three screen columns through the cut, from the depth
AOV, all the same shape — the terrain tracks the waterline to within tens of centimetres, crosses
it, and climbs away:

| screen column | at the line | 100 rows past it |
|---|---|---|
| x = 700  | bed 0.32 m under the surface, 0.5 m off centre | bed 6.4 m over it, 20.3 m off centre |
| x = 900  | bed 0.62 m under, 1.4 m off centre | bed 7.5 m over, 21.8 m off centre |
| x = 1100 | bed 1.10 m under, 3.5 m off centre | bed 6.9 m over, 21.4 m off centre |

Coverage agrees without using the geometry at all: of the ground inside the river feature that sits
below the waterline, **96–99.7% has water drawn on it out to 160 m**. There is nothing missing in
the near field, and the near field is where the report was.

## Why it reads as a clip

The bank past the waterline carries the same bioluminescent scatter as the channel. At this
aperture it is thrown far out of focus into a field of bright bokeh that runs *continuously* across
the waterline, so the eye joins the sparkle on the water to the bokeh on the bank into one ribbon —
and the point where the water stops looks like the ribbon being sliced. The solid-red arm shows this
directly: the red stops, the bokeh does not.

This is a real product problem. It is a *lighting and depth-of-field* problem, not a water one.

## The chain, and what each arm cost

Both leads are refuted as rendering-time decisions, by controls rather than by reading the shader:

| arm | result |
|---|---|
| `terrainCull` off | **MAD 0.00000** — pixel-identical to the baseline |
| `terrainLod` off | MAD 0.00051, the diagonal completely unchanged |
| both off | identical to lod-off |
| solid opaque red water | red stops at the same line: not a surface effect, no water fragment there |
| same camera, fov 44 → 18 | **identical in all 2,073,600 pixels** (see below) |
| same camera + target + lens, moved 50 m east | **the line is gone**; water runs off the bottom of frame |

That last one is what a missing chunk cannot survive. A chunk is fixed in the world; move the camera
and the hole moves with the world, it does not close.

`terrainCull` being *bit*-identical is itself worth keeping: water follows its ground chunk exactly
(`composition.cpp`, one water entity per chunk, same `setCameraCulled`), and the chunk view distance
is 640 m against a 288 m frame. Neither has any reach here.

## Two results I nearly filed as defects

**`cameras/valleywide/fov` is a dead parameter.** Driving it 44 → 18 renders a frame identical in
every one of 2,073,600 pixels; the shot uses the lens `focalLength`. ADR-225 again, one commit after
ADR-350 found the same disease in the day/night block. It also cost me the control I built it for —
the arm meant to separate "the wide-shot path" from "a specific chunk" proved only that the knob is
not wired.

**Water does not stop at 288 m.** That is what a colour threshold of 40 says. Relax the threshold to
12 and the cutoff moves to 340 m; to 2 and it moves to 375 m. *A cutoff that moves with the
threshold is aerial perspective.* The far reach is fogged, not empty. A fixed threshold would have
produced a confident, specific, wrong bug report with a number attached to it.

## The instrument

`tools/water_coverage.py`. Colour cannot answer "is there water here" — a water fragment 200 m out
is dimmed until it reads as terrain — so the tool crosses two independent questions:

- *Is there water?* Render twice with the water painted two flat colours and diff. A pixel a water
  fragment touched changes; one it did not is bit-identical, however dim. Bloom spreads the
  difference, so a positive means "water touched this pixel", not "water covers it".
- *Should there be?* Unproject the depth AOV and compare against the authored river — inside the
  feature width, below the interpolated path level.

The camera has to be reconstructed by hand, because a render writes no camera beside its frames.
`--calibrate` is what makes that honest: it reports the height of every water pixel relative to the
waterline, which a correct model puts at zero. It chose the convention — horizontal FOV from
`sensorWidth`/`focalLength`, depth as view-space z, median **+0.34 m** — over the vertical-FOV and
ray-length readings, which were metres out. Without that check the whole map is a guess with colours
on it.

At a grazing angle a metre of depth error becomes many metres of height error, so the near field is
the trustworthy half and the far field is reported but not believed. That limit is why this ADR
claims nothing about the fogged reach beyond ~200 m.

## What I got wrong first

My previous commit said the water geometry was **missing**. The solid-red arm had shown there is no
water fragment past the line, and I read "absent" as "missing" — skipping the question of whether
any belonged there. It did not. The arm was sound; the inference on top of it was not, and it is
exactly the failure this project keeps writing down: a measurement that is correct and a conclusion
that runs past it.

## Consequences

- No engine change. The water system is behaving correctly in this shot.
- The owner's complaint is still real and now has an address: the out-of-focus bank scatter reads as
  water. That belongs to the lighting/DOF pass, not the water pass.
- `cameras/*/fov` needs wiring or removing. A knob that renders an identical frame is worse than no
  knob.
- The eight diagnostic arms stay in `examples/world/_diag-water-*.json` so the chain regenerates.
