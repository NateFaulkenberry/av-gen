# ADR-1501: A grid field can read a plane grid round the inside of a sphere

- Status: Accepted (art/generative-pass)
- Amends ADR-032 (grid fields) and ADR-1201 (the grid field's channel lane). Additive: a grid field without the
  option reads exactly as before.
- Found by: the Generative pass (`docs/prototypes/generative-pass/00-brief.md`, Part 4). Phase Hall's oscillator
  network (ADR-1450) is a plane: the hall read it as a strip unrolled round four walls through four linear reader
  frames. The hall becomes a sphere, and no linear frame takes a point on a sphere to a place on a plane.

## Problem

A grid field samples its grid at the point in the field's frame (`q = worldToLocal * p`): a linear map. Panels laid
round the inside of a sphere need their network cell by direction (azimuth along the strip, latitude across it), so
the network's islands and fronts travel round the sphere with no seam where the strip's ends meet. Several readers
with linear frames would each cover a facet and meet at seams, and spend field slots (sixteen on the GPU) for nothing.

## Decision

`"projection": "sphere"` on a grid field (`FieldSpec::gridSphere`) unrolls the frame's point about its origin onto the
plane grid before the cell lookup:

```text
x = radius * atan2(q.z, q.x)            (azimuth in (-pi, pi]; atan2(0, 0) reads 0, as the GPU guards)
z = radius * asin(q.y / |q|)            (latitude)
y = 0                                    (a plane grid's middle)
|q| = 0 reads the cell at (0, 0, 0)
```

`radius` is the field's existing `radius`: the grid's x extent once round is `2 pi radius`, so with bounds
`x = +-pi * radius` and wrap, the strip closes on itself with no seam. Every radius along a direction reads the same
cell, so a membrane displaced along its normal keeps its pattern. Validation refuses the option on any other kind or
with a radius of 0. JSON writes it only when set; the hash includes it only when set. On the GPU it is bit 5 (+32) of
`gridRes.w`, above ADR-1201's channel bits (the channel decode now masks to its three bits); the radius is
`axisRadius.w`, already packed for every field. `gridCoord` in `shaders/fields.wgsl` and the CPU grid branches
(`scalarAt`, `vectorAt`) call the same unroll, so every reader (effectors, deformers, programs, compounds) sees it.

## Consequences

- One reader per channel serves the whole sphere. Phase Hall reads phase and activity with two.
- The poles are singular, as any azimuth is: a pattern read there converges on the pole. A scene lays its panels where
  that does not show (Phase Hall's columns end in a ragged fringe short of the poles).
- The simulation itself is unchanged: the grid steps on its plane, and its own fields (injection, frequency) are
  still sampled in the grid's coordinates.
- Test: `A sphere-projected grid field agrees on the CPU and the GPU` (`[gpu][simulation][fields]`: CPU against GPU
  for the default reading and a channel, the same cell at every radius, not the plane reading, JSON and hash,
  validation).
