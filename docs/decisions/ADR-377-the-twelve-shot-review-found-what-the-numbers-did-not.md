# ADR-377: The twelve-shot review found what the numbers did not

- Status: Accepted (2026-09-19)
- Phase 26 of the brief. Bounds ADR-360's mesh wind.

## What the review was for, and what it caught

Twelve controlled shots of the shipped scene at one second: hero, lower, higher, wide, close, a
quiet state, a peak state, wind direction, wind speed, leaves alone, vortex alone, full composite.
Eleven were fine. One was not, and **no metric in this branch would have found it.**

At `scene/windSpeed = 3.4` the canopy is **smeared into streaks** — every leaf card stretched along
the deformation axis. That is ADR-360's shredding, returning at high wind. The shipped value is
1.319 and is clean; a sweep puts the onset at about 1.8.

## Diagnosis, including the part I got wrong

The obvious suspect was the flutter. ADR-360 established that `WindSample::phase` turns a full cycle
every `flutterScale` metres, so the flutter's amplitude shows up as a difference between one end of
a leaf card and the other — that *was* the cause the first time. Capping the flutter at 2.2 here
changed the picture **almost not at all**.

It is the **lean**. Clamping the crown's travel to 0.10 of body height removed the shredding
entirely. The lean varies only through `pow(h, k)` and a smoothstep over radius — gentle gradients —
but at a large enough amplitude even a gentle gradient amounts to metres across a single leaf.
ADR-360 concluded "the lean shreds nothing however large it gets", and that conclusion was wrong: it
was true at the amplitudes measured then and false at four times them.

`wHere.strength` is the authored speed multiplied by the regional variation, so the hard maximum of
4.0 admitted an effective strength near 4.9.

## Decision

Three changes, and **all three leave the shipped frame byte-identical** (0 of 6 220 800 channels).

1. **A soft ceiling on crown travel**, which ADR-055 gives procedural plants (`bendLimit`) and the
   mesh path never had. With a **knee**: exactly 1 below 0.7 of the limit, saturating above, joined
   by a smoothstep. ADR-055's plain `off * L/(len+L)` is smooth but scales *everything*, including
   leans that were never in danger — measured, it moved 839 162 channels of the shipped hero.
2. **The flutter saturates** at 2.2. It is not the dominant cause here, but it is unbounded and it
   was the cause once.
3. **The hard maximum of `scene/windSpeed` goes from 4.0 to 1.6.** This is the change that actually
   protects the asset. The soft range already stopped the slider at 1.5, so a person dragging it was
   never at risk; a **modulation route clamps to the hard range**, and 4.0 let one drive the canopy
   apart. 1.6 sits above the shipped 1.319 and above the soft maximum, so nothing reachable today
   moves.

The limit is a property of **leaf size**, not of taste. Raising it needs a per-leaf frame — rigid
per-card transforms, or a leaf-local axis — which this GLB does not carry. A bigger number here
would only move the smearing.

This is the fourth time in this branch that a value correct for a 138 m tree has been wrong
elsewhere, after the wind amplitude that shredded the canopy, the per-metre vortex density written
as a unit-scale amplitude, and the shimmer scale that put a 4 m fixture inside one noise cell.

## Not mine, and reported rather than fixed

`tests/rendering/test_wind_gpu.cpp:245` — "the shader really is running the same arithmetic as
core/wind.cpp" — **fails on pristine main**, 82 of 83 assertions, measured tip travel 0.3057 against
a 0.94 × 0.3333 = 0.3133 bound, so 91.7% where 94% is required. Verified with main's own test binary
in main's own worktree, not only on this branch.

The likely cause is ADR-372's AgX inverse fix: that test measures tip travel from the *silhouette of
lit pixels*, and a tone curve that moved by up to 9 code levels moves where the silhouette
threshold falls. The 6% tolerance was calibrated against the old curve. It belongs to whoever owns
ADR-372, and the fix is probably to re-derive the tolerance rather than to change the wind.

## The other eleven shots

Worth recording, because two of them change what I would recommend next:

- **The higher and wide shots read as a vortex far better than the hero does.** The funnel's spiral
  structure is legible as concentric arcs from above, and at the hero camera it is a band of
  atmospheric depth. ADR-374 predicted this from the geometry; the review confirms it from the
  picture. If the film wants the vortex to read *as a vortex*, it needs a shot from above or wide.
- **The lower shot is weaker than expected.** §20 says dropping the camera should reveal more depth,
  and it does show more swirl, but the framing cuts the tree. A lower shot needs its own framing,
  not just a lower eye.
- Wind direction visibly redirects the canopy; the quiet and peak states read as different moments;
  leaves and vortex each read alone.
