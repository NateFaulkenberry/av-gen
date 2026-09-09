# Bioluminescence: light that comes from somewhere

`examples/world/world.json` is the five-element scene the ecosystem brief asks for before anything
scales: a dark environment, one glowing organism, volumetric fog, foreground vegetation, and
distant silhouettes for scale. Everything in it is the `tube` primitive.

## The organism

A hero organism went through four states in four renders, and the sequence is the lesson.

**1. Emissive 9.0 on a solid dome.** Five per cent of the frame clipped. A bright ellipse with no
form, reading as a lamp rather than as tissue. *Emission is not light; it is a surface that is
already at its maximum.*

**2. Emissive 2.1.** Nothing clips, the dome has form, and there is a bright rim underneath. Better,
and still a lamp.

**3. Gills added under the cap.** Nearly invisible: they were inside the hanging glow dome, which
occluded them. A structural idea that the geometry hides is worth nothing.

**4. The glow dome flattened to a shallow ceiling and the gills lengthened, curved and varied.**
Now the light comes *from* sixty-four hanging filaments of uneven length. This is the version that
reads as an organism.

The general rule: **give the light a structure to come out of.** A glowing surface reads as paint.
A glowing structure reads as biology. Structure costs sixty-four thin tubes, which is nothing.

## Atmospheric perspective needs the fog to match the sky

The distant silhouettes first rendered *lighter* than the sky and read as clouds. Distance fog
pulls a surface towards `fogColor`, so if `fogColor` is brighter than `background` then everything
far away gets brighter with distance -- the opposite of a silhouette.

`fogColor` was `[0.02, 0.05, 0.075]` against a background of `[0.004, 0.007, 0.013]`. Bringing it
to `[0.010, 0.022, 0.038]` -- close to the sky, slightly bluer -- made the distant forms recede
into darkness instead of emerging from it.

**The visible haze comes from the volumetrics, not from the distance fog.** They do different
jobs: the volume carries the organism's light through the air, and the distance fog decides what
far things fade *towards*. Setting the second to a pretty colour breaks the first.

## Palette discipline

| Role | Colour | Where |
|---|---|---|
| Environment | indigo-black | sky, ground, silhouettes |
| Primary life | cyan / turquoise | the hero and its kin |
| Rare accent | amber | exactly one small organism, low in frame |

One warm object in an entirely cool frame does more than a dozen. The moment a second warm source
appears, the first stops being special.

## What this scene still gets wrong

- **The cap top is a featureless teal dome.** It is the largest object in frame and has no surface
  detail at all -- no mottling, no translucency at the edge, no wetness.
- **The foreground is one-sided.** Fronds pass on the right and the left edge is empty, so the
  framing is lopsided and the parallax will be too.
- **The ground is a flat plane with a visible horizon** running across the middle of the frame.
- **The kin do not share the hero's language.** They are domes with a bright slot; they should have
  gills of their own, smaller.
- **Nothing moves yet.** The camera is static and the parallax the brief asks for is untested.
