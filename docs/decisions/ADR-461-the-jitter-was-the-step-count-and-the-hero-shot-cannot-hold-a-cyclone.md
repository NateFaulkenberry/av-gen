# ADR-461: The step count was buying the jitter back, and the hero shot cannot hold a cyclone

- Status: Accepted (2026-09-20)
- Extends ADR-460 (most of the grain is the march's own jitter). Corrects ADR-389's reading of its
  own step ladder. Related: ADR-374 (the camera must be outside the mouth), ADR-143 (temporal
  reprojection rejected), ADR-035 (what a tier may scale), ADR-264 (a project's parameters over
  its scene).

## 1. Turning the jitter off at 32 steps is worth eight times the steps, for nothing

ADR-460 found that a field with **no noise in it at all** was still speckled at 32 steps and clean
at 256, and attributed that to the march's per-pixel start jitter. This is the direct test.

`Environment::volumeJitter` scales the offset. ADR-389's grain metric, hero camera, t=6:

| arm | grain |
|---|---|
| 32 steps, jitter 1.0 (**as shipped**) | 0.7312 |
| 32 steps, jitter 0 | **0.5172** |
| 256 steps, jitter 0 | 0.5196 |

**Thirty-two steps with the jitter off equals two hundred and fifty-six steps.** The ladder ADR-389
measured -- 1.139 at 32 converging to 0.520 at 384 -- was very largely the *jitter's* variance
falling as the step it is scaled by got shorter, not the noise becoming resolvable. The step count
was buying back an artifact the march was creating.

The amplitude ladder is monotone on every arm tried:

| arm | 1.0 | 0.5 | 0.25 | 0 |
|---|---|---|---|---|
| shipped vortex, hero | 1.457 | 1.357 | 1.066 | **0.436** |
| the 3x field, hero | 0.731 | 0.671 | 0.597 | 0.517 |
| the 3x field, looking down | 1.458 | 1.227 | 0.943 | **0.488** |

### The arrangement is not the lever; the amount is

The first attempt replaced the white offset with interleaved gradient noise at the same amplitude,
on the reasoning that an ordered pattern makes a neighbourhood's samples cover the step evenly.
Measured: **-11% where the field is smooth and +8% on the shipped frame.** It made the shipped
picture worse. Where the field is itself aliased noise, a structured sample pattern *exposes* error
that an independent one averages away -- which is ADR-389's weight-based band-limit finding exactly,
reached through a different mechanism and by the same mistake. It is reverted, and recorded so the
next person does not spend the afternoon on it.

### Where the control lives, and why the default does not move

`Environment::volumeJitter`, beside `volumeSteps`, **not** in `QualitySettings`. ADR-035 lets a tier
scale "sample counts, resolutions and history lengths" and this is none of those. It is a property
of the medium -- of how much that medium changes across one step -- and the scene that chose the
step count is the thing that knows the answer. At 4000 m over 32 steps a step is 125 metres and the
funnel changes completely across one; ordinary fog does not.

**The default stays at 1.0 and no existing frame moves.** Verified rather than asserted: Glowmere
Valley 2, which has real fog at only 12 steps, renders **byte-identical** through the new code path.
The expression is written centred (`0.5 + amount * (u - 0.5)`) so that lowering it converges on the
middle of the step rather than its start -- the mean sample position must not move, or every
per-metre coefficient calibrated against the medium's integrated density is wrong, which is
ADR-374/379/381/389's family again.

Two controls say lowering it is safe where it helps. The **Volumetric Lab**'s transmittance ladders
-- flat slabs at right angles to the view, which is the case banding is worst in -- pass unchanged
at amplitude 0, 33 532 assertions. And Glowmere at 12 steps moves by a mean of **0.024 luminance
levels** with 44 of 921 600 pixels past two levels and no change in its row structure.

## 2. The field is geometrically similar in its radius, and that is a free 60%

Every macro parameter added in ADR-460 is a **fraction of the radius or an angle**: `innerVoid`,
`eyeWallWidth`, `eyeWallGain`, `bandDepth`, `bandHarmonic` are fractions, `bandArms` is a count and
`bandPitchDegrees` is an angle. Only `thickness` and `funnelDepth` are in metres. So scaling
`radius`, `thickness` and `funnelDepth` together gives a **geometrically identical field, N times
larger** -- and `periodBase = radius / effScale0` scales with it, so the noise's world period scales
too.

Against an unchanged 125-metre march step, that is a free N-fold increase in the effective sample
rate. Measured at N = 3, with the camera scaled to match so the angular composition is identical:

| | grain |
|---|---|
| 1x field, 1x camera | 3.6295 |
| **3x field, 3x camera** | **1.4576** |

**A 60% fall at identical composition and identical cost.** ADR-374 measured that bringing the mouth
*closer* made the picture worse, and that is still true -- it put the camera inside the funnel. This
is the opposite move, bigger and further away, and nobody had tried it.

This corrects the standing advice that `radius` must not be changed because the structure is
expressed against it. The structure is expressed against it in *fractions*, which is precisely what
makes changing it safe, as long as `thickness` and `funnelDepth` go with it.

## 3. The hero shot cannot hold a cyclone, and moving the vortex does not fix it

The coordinator's decision was to move the vortex rather than the camera: put it beyond the tree
along the view direction and lower, so the tree is silhouetted against it. The reasoning is right --
the vortex being *directly beneath* the island is what made the two mutually exclusive -- and the
placement was worked from the **scene file's** camera at `(158, 6, 152)`. ADR-264: the project
overrides that camera, and against the one that renders, `(-380, -150, -365)` is **15.2 degrees off
the view axis** in a frame whose mouth is only +/-15.3 wide. Corrected onto the real axis and
measured, five placements:

| placement | what it does |
|---|---|
| D 746 on the axis (the decision, corrected) | mouth subtends 30.0 deg -- **exactly the island's own width**, so the island covers the eye again |
| D 600, D 500 | wider than the island, but at 14 deg depression the plane is compressed 4:1 and reads as vertical curtains |
| D 620 at 24 deg depression | the ellipse lands 7.7 to 22.7 deg below frame centre -- behind the island and off the bottom |
| the 3x field at D 1400 | clean, luminous, and still a backdrop |

The constraint is not the vortex's position. Measured off the shipped render, **the island and tree
occupy +/-15.0 degrees horizontally and -18.0 to +15.7 vertically of a 36 x 60 degree frame: the
hero *is* the frame.** A cyclone's readable features live in a horizontal plane and are compressed
by `sin(depression)`; the frame spans depressions of -9.2 to +26.8, so the steepest view available
is 26.8 degrees and even there the compression is 0.45 -- and everything at a useful depression is
behind the island. Pitching the camera down does not help: the island is 232 m away and some 340 m
tall, so a steeper pitch puts *more* island in frame, not less.

**So the shipped hero shot can hold a luminous volumetric backdrop, and it cannot hold a cyclone
seen from above.** Both are legitimate pictures. Which one the film wants is not an engineering
decision and this ADR does not make it.

From a shot that does contain it, the answer to the brief's §5 is **yes**: at 32 steps, jitter off,
shipping cost, the eye wall reads as a bright ring, the arms curl out of it, the funnel descends,
and the island is a speck at the centre.

## Consequences

- **ADR-389's step ladder should not be quoted as a measure of aliasing.** It measured aliasing and
  jitter together and the jitter was the larger part. The `2.7 ms + 0.196 ms per step` cost model
  from ADR-460 stands; what changes is that buying steps to fix grain is now the wrong purchase.
- **`volumeStepScale` per tier drops down the queue.** It was next; it is now worth much less,
  because the thing it was going to buy is available for nothing.
- `AtmosphericFrame` grew from 1612 to 1640 bytes and its `static_assert` fired, which is what it is
  for. Checked: `frameDiffers` memcmps the whole of `Vortex`, so the new members are read.

## Revisit when

- A scene appears whose medium has high optical depth *per step* -- the case jitter exists for and
  the one case this branch could not construct. If lowering `volumeJitter` bands there, the default
  is right where it is and the control is how it should have been reached.
- The camera question is decided, at which point the detail stages have a frame to be judged in.
